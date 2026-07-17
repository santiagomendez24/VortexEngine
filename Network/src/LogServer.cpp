#include "../include/LogServer.h"
#include <print>
#include <intrin.h>
#include <bit>

namespace Network
{
	LogServer::LogServer(const uint16_t port) noexcept 
		: acceptor_(asio::ip::tcp::acceptor(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))), work_guard_(asio::make_work_guard(io_context_)), accept_strand_(asio::make_strand(io_context_))
	{
		for (size_t i = 0; i < 2; ++i)
		{
			auto ctx = std::make_unique<asio::io_context>();
			work_guard_vec.push_back(asio::make_work_guard(*ctx));
			context_vec.push_back(std::move(ctx));
		}
	}

	void LogServer::start(const std::vector<Core::LogQueue*>& ptr)
	{
		ptr_queue = ptr;
		network_pool.reserve(context_vec.size());

		for (size_t i = 0; i < context_vec.size(); ++i)
		{
			work_guard_vec.emplace_back(asio::make_work_guard(*context_vec[i]));
		}

		for (size_t i = 0; i < context_vec.size(); ++i)
		{
			asio::io_context* raw_ctx = context_vec[i].get();
			network_pool.emplace_back([raw_ctx]()
			{
				raw_ctx->run();
			});
		}

		start_accept();
		io_context_.run();
	}

	void LogServer::start_accept()
	{
		size_t worker_index = next_worker_index++ % context_vec.size();
		asio::io_context& worker_context = *(context_vec[worker_index]);
		auto worker_socket = std::make_shared<asio::ip::tcp::socket>(worker_context);

		acceptor_.async_accept(*worker_socket, asio::bind_executor(accept_strand_, [this, worker_socket, worker_index](const asio::error_code& ec) mutable
		{
			if (!ec)
			{
				Core::LogQueue* assigned_ptr = ptr_queue[worker_index];
				std::shared_ptr<NetworkSession> session = std::make_shared<NetworkSession>(std::move(*worker_socket), 10, assigned_ptr);
				session->start();
			}
			else
			{
				std::print("Error al aceptar conexion: {}\n", ec.message());
			}

			start_accept();
		}));
	}

	NetworkSession::NetworkSession(asio::ip::tcp::socket socket, size_t max_size, Core::LogQueue* ptr) noexcept : socket_(std::move(socket)),
		CurrentParser(GetBestParser()), assigned_ptr(ptr)
	{
		max_log_size = std::bit_floor(max_size * 1024 * 1024) - 32;
		stash_buffer = std::make_unique<MagicRingBuffer>(std::bit_floor(max_size * 1024 * 1024));
	}

	void NetworkSession::read_header()
	{
		auto self = shared_from_this();

		size_t space = stash_buffer->get_write_space();
		if (space < 1024)
		{
			process_stash();

			space = stash_buffer->get_write_space();

			if (space < 1024)
			{
				disconnect("El paquete se pasa de tamaño");
				return;
			}
		}

		socket_.async_read_some(asio::buffer(stash_buffer->get_write_ptr(), space), [this, self](const asio::error_code& ec, size_t bytes_transferred)
		{
			if (!ec)
			{
				stash_buffer->commit_write(bytes_transferred);
				process_stash();
				read_header();
			}
			else
			{
				handle_error(ec);
			}
		});
	}

	void NetworkSession::process_stash()
	{
		constexpr uint32_t MAGIC = 0xDEADBEEF;
		constexpr uint32_t NETWORKMAGIC = (std::endian::native == std::endian::little) ? std::byteswap(MAGIC) : MAGIC;

		while (stash_buffer->get_bytes_available() >= sizeof(uint32_t))
		{
			uint32_t signature = stash_buffer->peek();

			if (signature == NETWORKMAGIC)
			{
				if (stash_buffer->get_bytes_available() < 8) break;

				uint32_t log_len = ntohl(stash_buffer->peek(4));

				if (log_len > max_log_size)
				{
					disconnect("El paquete se pasa del tamaño establecido");
					return;
				}

				if (stash_buffer->get_bytes_available() < 8 + log_len) break;

				auto full_view = stash_buffer->get_view(8 + log_len);
				std::string_view log_data(full_view.data() + 8, log_len);

				CurrentParser(log_data, this);

				stash_buffer->consume(8 + log_len);
			}
			else
			{
				auto full_view = stash_buffer->get_view(stash_buffer->get_bytes_available());
				const char* start = full_view.data();
				const char* found = static_cast<const char*>(std::memchr(start, '\n', stash_buffer->get_bytes_available()));

				if (!found)
				{
					if (stash_buffer->get_bytes_available() > max_log_size)
					{
						disconnect("Error: Text log exceeds max_log_size without delimiter");
						return;
					}

					break;
				}

				size_t len = found - start;

				std::string_view view(start, len);
				CurrentParser(view, this);

				stash_buffer->consume(len + 1);
			}
		}
	}

	void NetworkSession::parse_and_push(std::string_view raw_data, Network::NetworkSession* session)
	{
		auto part_1 = raw_data.find(' ');
		if (part_1 == std::string_view::npos) return;

		std::string_view time_view = raw_data.substr(0, part_1);
		uint64_t time = 0;

		auto [ptr1, ec1] = std::from_chars(time_view.data(), time_view.data() + time_view.size(), time);
		if (!CheckLogEntry::validate_fromcharts(ec1)) return;

		auto part_2 = raw_data.find(' ', part_1 + 1);
		if (part_2 == std::string_view::npos) return;

		std::string_view level_view = raw_data.substr(part_1 + 1, part_2 - part_1 - 1);
		size_t level = 0;
		auto [ptr2, ec2] = std::from_chars(level_view.data(), level_view.data() + level_view.size(), level);
		if (!CheckLogEntry::validate_fromcharts(ec2)) return;
		Core::LogLevel log_level = static_cast<Core::LogLevel>(level);

		auto part_3 = raw_data.find(' ', part_2 + 1);
		if (part_3 == std::string_view::npos) return;

		std::string_view id_view = raw_data.substr(part_2 + 1, part_3 - part_2 - 1);
		uint32_t id = 0;
		auto [ptr3, ec3] = std::from_chars(id_view.data(), id_view.data() + id_view.size(), id);
		if (!CheckLogEntry::validate_fromcharts(ec3)) return;

		std::string_view message = raw_data.substr(part_3 + 1);

		Core::LogQueue* assigned_ptr = session->get_private_queue();

		uint32_t slab_offset;
		auto* slab = assigned_ptr->slab_pool->get_next_slab(message, slab_offset);
		if (!slab) return;

		std::memcpy(slab->data.data() + slab_offset, message.data(), message.size());

		Core::LogEntry log_entry;
		log_entry.level = log_level;
		log_entry.log_id = id;
		log_entry.message_offset = slab_offset;
		log_entry.timestamp = time;
		log_entry.slab_ptr = reinterpret_cast<uintptr_t>(slab);
		log_entry.message_lenght = static_cast<uint32_t>(message.size());

		assigned_ptr->push(std::move(log_entry));
	}

	void NetworkSession::parse_and_push_simd(std::string_view raw_data, Network::NetworkSession* session)
	{
		if (raw_data.size() < 32)
		{
			parse_and_push(raw_data, session);
			return;
		}

		uintptr_t address = reinterpret_cast<uintptr_t>(raw_data.data());
		uintptr_t page_offset = address & 0xFFF;
		if (page_offset + 32 > 4096)
		{
			parse_and_push(raw_data, session);
			return;
		}

		__m256i header_block = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(raw_data.data()));

		__m256i spaces = _mm256_set1_epi8(' ');
		__m256i comparition = _mm256_cmpeq_epi8(header_block, spaces);
		uint32_t mask = _mm256_movemask_epi8(comparition);

		uint32_t first_space_index = _tzcnt_u32(mask);
		uint32_t mask_without_first = mask & (mask - 1);
		uint32_t second_space_index = _tzcnt_u32(mask_without_first);
		uint32_t mask_without_second = mask_without_first & (mask_without_first - 1);
		uint32_t third_space_index = _tzcnt_u32(mask_without_second);

		if ((mask_without_second == 0) || (third_space_index >= 32))
		{
			parse_and_push(raw_data, session);
			return;
		}

		std::string_view time_view = raw_data.substr(0, first_space_index);
		uint64_t time = 0;
		auto [ptr1, ec1] = std::from_chars(time_view.data(), time_view.data() + time_view.size(), time);
		if (!CheckLogEntry::validate_fromcharts(ec1)) return;

		std::string_view level_view = raw_data.substr(first_space_index + 1, second_space_index - first_space_index - 1);
		size_t level = 0;
		auto [ptr2, ec2] = std::from_chars(level_view.data(), level_view.data() + level_view.size(), level);
		if (!CheckLogEntry::validate_fromcharts(ec2)) return;
		Core::LogLevel log_level = static_cast<Core::LogLevel>(level);

		std::string_view id_view = raw_data.substr(second_space_index + 1, third_space_index - second_space_index - 1);
		uint32_t id = 0;
		auto [ptr3, ec3] = std::from_chars(id_view.data(), id_view.data() + id_view.size(), id);
		if (!CheckLogEntry::validate_fromcharts(ec3)) return;

		std::string_view message = raw_data.substr(third_space_index + 1);

		Core::LogQueue* assigned_ptr = session->get_private_queue();

		uint32_t slab_offset;
		auto* slab = assigned_ptr->slab_pool->get_next_slab(message, slab_offset);
		if (!slab) return;

		std::memcpy(slab->data.data() + slab_offset, message.data(), message.size());

		Core::LogEntry log_entry;
		log_entry.level = log_level;
		log_entry.log_id = id;
		log_entry.message_offset = slab_offset;
		log_entry.timestamp = time;
		log_entry.slab_ptr = reinterpret_cast<uintptr_t>(slab);
		log_entry.message_lenght = static_cast<uint32_t>(message.size());

		assigned_ptr->push(std::move(log_entry));
	}

	inline void NetworkSession::handle_error(const asio::error_code& ec)
	{
		bool esperado = false;
		if (!is_closing.compare_exchange_strong(esperado, true))
		{
			return;
		}

		fprintf(stderr, "Asio Error: %s (Val: %d)\n", ec.message().c_str(), ec.value());
		fflush(stderr);

		asio::post(socket_.get_executor(), [self = shared_from_this()]()
		{
			asio::error_code error_ignorado;
			self->socket_.close(error_ignorado);
		});
	}
}
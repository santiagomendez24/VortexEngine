#include "../include/LogServer.h"
#include <print>
#include <intrin.h>

namespace Network
{
	LogServer::LogServer(const uint16_t port) noexcept 
		: acceptor_(asio::ip::tcp::acceptor(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))), work_guard_(asio::make_work_guard(io_context_)), accept_strand_(asio::make_strand(io_context_))
	{ }

	void LogServer::start(Core::LogQueue* ptr)
	{
		ptr_queue.push_back(ptr);
		io_context_.run();
	}

	void LogServer::start_accept()
	{
		acceptor_.async_accept(asio::bind_executor(accept_strand_, [this](const asio::error_code& ec, asio::ip::tcp::socket&& move_socket) mutable
		{
			if (!ec)
			{
				size_t index = next_queue_index.fetch_add(1, std::memory_order_relaxed) & (ptr_queue.size() - 1);
				Core::LogQueue* assigned_ptr = ptr_queue[index];
				std::make_shared<NetworkSession>(std::move(move_socket))->start(assigned_ptr);
			}
			else
			{
				std::print("Error al aceptar conexion: {}\n", ec.message());
			}

			start_accept();
		}));
	}

	NetworkSession::NetworkSession(asio::ip::tcp::socket socket) noexcept : socket_(std::move(socket)), CurrentParser(GetBestParser())
	{ }

	void NetworkSession::read_header()
	{
		auto self = this->shared_from_this();

		asio::async_read(socket_, asio::buffer(&body_length_buffer_, sizeof(body_length_buffer_)), [this, self](const asio::error_code& ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				uint32_t body_length = ntohl(body_length_buffer_);

				const uint32_t MAX_LOG_SIZE = 10 * 1024 * 1024;
				if (body_length > MAX_LOG_SIZE)
				{
					handle_error(asio::error::message_size);
					return;
				}

				auto data = asio::buffer(read_buffer_, body_length);
				read_body(static_cast<uint32_t>(data.size()));
			}
			else
			{
				handle_error(ec);
			}
		});
	}

	void NetworkSession::read_body(uint32_t lenght)
	{
		auto self = this->shared_from_this();
		auto data = asio::buffer(read_buffer_, lenght);
		
		asio::async_read(socket_, data, [this, self](const asio::error_code& ec, std::size_t lenght)
		{
			if (!ec)
			{
				std::string_view view(read_buffer_.data(), lenght);
				CurrentParser(view, this);
				read_header();
			}
			else
			{
				handle_error(ec);
			}
		});
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

		size_t offset = 0;
		size_t total_size = message.size();

		while (offset < total_size)
		{
			Core::LogEntry log_entry;
			log_entry.level = log_level;
			log_entry.log_id = id;
			log_entry.timestamp = time;

			size_t secure_bytes = std::min(total_size - offset, log_entry.raw_log.size());

			std::copy_n(message.data() + offset, secure_bytes, log_entry.raw_log.data());
			log_entry.message_lenght = static_cast<uint8_t>(secure_bytes);

			offset += secure_bytes;

			log_entry.is_continued = (offset < total_size);

			if (!CheckLogEntry::validate(log_entry)) return;

			size_t index = session->next_queue_index.fetch_add(1, std::memory_order_relaxed) & (session->assigned_ptr.size() - 1);
			Core::LogQueue* assigned_ptr = session->assigned_ptr[index];
			assigned_ptr->push(std::move(log_entry));
		}
	}

	void NetworkSession::parse_and_push_simd(std::string_view raw_data, Network::NetworkSession* session)
	{
		if (raw_data.size() < 32)
		{
			parse_and_push(raw_data, session);
			return;
		}

		__m256i header_block = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(raw_data.data()));

		__m256i spaces = _mm256_set1_epi8(' ');
		__m256i comparition = _mm256_cmpeq_epi8(header_block, spaces);
		uint32_t mask = _mm256_movemask_epi8(comparition);

		uint32_t space_count = 0;
		uint32_t first_space_index = 0;
		uint32_t second_space_index = 0;
		uint32_t third_space_index = 0;
		uint32_t copy_mask = mask;

		while (copy_mask != 0)
		{
			uint32_t index = count_trailing_zeros(copy_mask);
			space_count++;

			if (space_count == 1)
			{
				first_space_index = index;
			}

			if (space_count == 2)
			{
				second_space_index = index;
			}

			if (space_count == 3)
			{
				third_space_index = index;
				break;
			}
			copy_mask &= (copy_mask - 1);
		}

		if (space_count < 3)
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

		size_t offset = 0;
		size_t total_size = message.size();

		while (offset < total_size)
		{
			Core::LogEntry log_entry;
			log_entry.level = log_level;
			log_entry.log_id = id;
			log_entry.timestamp = time;

			size_t secure_bytes = std::min(total_size - offset, log_entry.raw_log.size());

			std::copy_n(message.data() + offset, secure_bytes, log_entry.raw_log.data());
			log_entry.message_lenght = static_cast<uint8_t>(secure_bytes);

			offset += secure_bytes;

			log_entry.is_continued = (offset < total_size);

			if (!CheckLogEntry::validate(log_entry)) return;

			size_t index = session->next_queue_index.fetch_add(1, std::memory_order_relaxed) & (session->assigned_ptr.size() - 1);
			Core::LogQueue* assigned_ptr = session->assigned_ptr[index];
			assigned_ptr->push(std::move(log_entry));
		}
	}

	inline void NetworkSession::handle_error(const asio::error_code& ec)
	{
		std::print("{}\n", ec.message());
		socket_.close();
	}
}
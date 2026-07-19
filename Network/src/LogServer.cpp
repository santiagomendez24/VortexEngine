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

	void LogServer::stop() noexcept
	{
		asio::error_code ec;
		acceptor_.close(ec);
		work_guard_.reset();
		io_context_.stop();

		for (auto& guard : work_guard_vec)
		{
			guard.reset();
		}

		for (auto& th : network_pool)
		{
			if (th.joinable())
			{
				th.join();
			}
		}
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

				if (log_len == 0 || log_len > max_log_size)
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
		const char* ptr = raw_data.data();
		const char* end = ptr + raw_data.size();

		uint64_t time = 0;
		while (ptr < end && *ptr >= '0' && *ptr <= '9')
		{
			time = time * 10 + (*ptr++ - '0');
		}

		if (ptr >= end || *ptr++ != ' ') return session->disconnect("Malformed Time");
		if (!CheckLogEntry::validateTimestamp(time)) return session->disconnect("Invalid Time");

		size_t level = 0;
		while (ptr < end && *ptr >= '0' && *ptr <= '9')
		{
			level = level * 10 + (*ptr++ - '0');
		}

		if (ptr >= end || *ptr++ != ' ') return session->disconnect("Malformed Level");
		if (!CheckLogEntry::validate_level(level)) return session->disconnect("Invalid level");

		uint32_t id = 0;
		while (ptr < end && *ptr >= '0' && *ptr <= '9')
		{
			id = id * 10 + (*ptr++ - '0');
		}

		if (ptr >= end || *ptr++ != ' ') return session->disconnect("Malformed ID");
		if (!CheckLogEntry::validateID(id)) return session->disconnect("Invalid ID");

		std::string_view message(ptr, end - ptr);

		Core::LogQueue* assigned_ptr = session->get_private_queue();

		uint32_t slab_offset;
		auto* slab = assigned_ptr->slab_pool->get_next_slab(message, slab_offset);
		if (!slab) return;

		std::memcpy(slab->data.data() + slab_offset, message.data(), message.size());

		Core::LogEntry log_entry;
		log_entry.level = static_cast<Core::LogLevel>(level);
		log_entry.log_id = id;
		log_entry.message_offset = slab_offset;
		log_entry.timestamp = time;
		log_entry.slab_ptr = reinterpret_cast<uintptr_t>(slab);
		log_entry.message_lenght = static_cast<uint32_t>(message.size());

		assigned_ptr->push(std::move(log_entry));
	}

	void NetworkSession::parse_and_push_simd(std::string_view raw_data, Network::NetworkSession* session)
	{
		const char* data = raw_data.data();
		const char* end = data + raw_data.size();

		auto fast_parse = [](const char*& ptr, const char* end_ptr) noexcept -> uint64_t
		{
			uint64_t val = 0;
			while (ptr < end_ptr && *ptr >= '0' && *ptr <= '9')
			{
				val = (val * 10) + (*ptr++ - '0');
			}
			return val;
		};

		if (raw_data.size() < 32 || ((reinterpret_cast<uintptr_t>(data) & 0xFFF) + 32 > 4096))
		{
			return parse_and_push(raw_data, session);
		}

		__m256i header = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data));
		uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(header, _mm256_set1_epi8(' ')));

		if (__builtin_popcount(mask) < 3)
		{
			return parse_and_push(raw_data, session);
		}

		uint32_t p1 = _tzcnt_u32(mask);
		uint32_t p2 = _tzcnt_u32(mask & (mask - 1));
		uint32_t p3 = _tzcnt_u32(mask & (mask - 1) & (mask - 2));

		const char* ptr = data;

		uint64_t time = fast_parse(ptr, data + p1);
		ptr = data + p1 + 1;
		if (!CheckLogEntry::validateTimestamp(time)) return session->disconnect("Invalid Time");

		size_t level = fast_parse(ptr, data + p2);
		ptr = data + p2 + 1;
		if (!CheckLogEntry::validate_level(level)) return session->disconnect("Invalid Level");

		uint32_t id = static_cast<uint32_t>(fast_parse(ptr, data + p3));
		ptr = data + p3 + 1;
		if (!CheckLogEntry::validateID(id)) return session->disconnect("Invalid ID");

		std::string_view message(ptr, end - ptr);

		Core::LogQueue* assigned_ptr = session->get_private_queue();

		uint32_t slab_offset;
		auto* slab = assigned_ptr->slab_pool->get_next_slab(message, slab_offset);
		if (!slab) return;

		std::memcpy(slab->data.data() + slab_offset, message.data(), message.size());

		Core::LogEntry log_entry;
		log_entry.level = static_cast<Core::LogLevel>(level);
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
			asio::error_code ignored_error;
			self->socket_.close(ignored_error);
		});
	}

	void NetworkSession::disconnect(const std::string& reason)
	{
		bool esperado = false;
		if (!is_closing.compare_exchange_strong(esperado, true))
		{
			return;
		}

		std::cerr << "[VORTEX] Disconnecting session: " << reason << std::endl;

		asio::post(socket_.get_executor(), [self = shared_from_this()]()
		{
			asio::error_code ec;
			self->socket_.cancel(ec);
			self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
			self->socket_.close(ec);
		});
	}

	inline uint32_t NetworkSession::count_trailing_zeros(uint32_t mask)
	{
		unsigned long index;
		if (_BitScanForward(&index, mask))
		{
			return static_cast<uint32_t>(index);
		}
		return 0;
	}
	
	MagicRingBuffer::MagicRingBuffer(size_t physical_size) : size(physical_size), base_ptr(nullptr), hMapFile(NULL),
		viewA(nullptr), viewB(nullptr), head(0), tail(0)
	{
		if (size == 0 || (size % (64 * 1024)) != 0)
		{
			throw std::invalid_argument("El tamaño del buffer debe ser múltiplo de 64 KB.");
		}

		start_portal();
	}

	MagicRingBuffer::MagicRingBuffer(MagicRingBuffer&& other) noexcept : size(other.size), base_ptr(other.base_ptr), hMapFile(other.hMapFile),
		viewA(other.viewA), viewB(other.viewB), head(other.head), tail(other.tail)
	{
		other.base_ptr = nullptr;
		other.hMapFile = NULL;
		other.viewA = nullptr;
		other.viewB = nullptr;
		other.head = 0;
		other.tail = 0;
	}

	uint8_t* MagicRingBuffer::get_write_ptr() const noexcept
	{
		return base_ptr + (head & (size - 1));
	}

	size_t MagicRingBuffer::get_write_space() const noexcept
	{
		return size - (head - tail);
	}

	size_t MagicRingBuffer::get_bytes_available() const noexcept
	{
		return head - tail;
	}

	void MagicRingBuffer::commit_write(size_t bytes_written) noexcept
	{
		head += bytes_written;
	}

	void MagicRingBuffer::consume(size_t bytes_consumed) noexcept
	{
		tail += bytes_consumed;
	}

	std::string_view MagicRingBuffer::get_view(size_t len) const noexcept
	{
		return std::string_view(reinterpret_cast<const char*>(base_ptr + (tail & (size - 1))), len);
	}

	void MagicRingBuffer::reset() noexcept
	{
		head = 0;
		tail = 0;
	}

	uint32_t MagicRingBuffer::peek(uint32_t usize) noexcept
	{
		return *reinterpret_cast<const uint32_t*>(base_ptr + ((tail + usize) & (this->size - 1)));
	}

	void MagicRingBuffer::start_portal()
	{
		base_ptr = reinterpret_cast<uint8_t*>(VirtualAlloc2(GetCurrentProcess(), nullptr, size * 2, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));

		if (!base_ptr)
		{
			throw std::runtime_error("No se pudo reservar el direccionamiento virtual contiguo.");
		}

		if (!VirtualFreeEx(GetCurrentProcess(), base_ptr, size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
		{
			VirtualFreeEx(GetCurrentProcess(), base_ptr, 0, MEM_RELEASE);
			throw std::runtime_error("Fallo crítico: No se pudo subdividir la región de memoria virtual.");
		}

		hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(size), nullptr);

		if (!hMapFile)
		{
			throw std::runtime_error("Fallo al crear File Mapping de la sección física.");
		}

		viewA = MapViewOfFile3(hMapFile, GetCurrentProcess(), base_ptr, 0, size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);

		if (!viewA)
		{
			CloseHandle(hMapFile);
			throw std::runtime_error("Fallo al mapear la Región Espejo A.");
		}

		viewB = MapViewOfFile3(hMapFile, GetCurrentProcess(), base_ptr + size, 0, size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);

		if (!viewB)
		{
			UnmapViewOfFile2(GetCurrentProcess(), viewA, 0);
			CloseHandle(hMapFile);
			throw std::runtime_error("Fallo al mapear la Región Espejo B.");
		}
	}

	void MagicRingBuffer::free_portal() noexcept
	{
		if (viewB) UnmapViewOfFile2(GetCurrentProcess(), viewB, 0);
		if (viewA) UnmapViewOfFile2(GetCurrentProcess(), viewA, 0);
		if (hMapFile) CloseHandle(hMapFile);
	}

}
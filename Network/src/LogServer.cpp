#include "../include/LogServer.h"
#include "../time/include/Time.h"
#include <print>

namespace Network
{
	template<typename SecurityCheck>
	LogServer<SecurityCheck>::LogServer(const uint16_t port, Core::LogQueue& log_queue) noexcept 
		: acceptor_(asio::ip::tcp::acceptor(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))), log_queue_(log_queue), work_guard_(asio::make_work_guard(io_context_)), accept_strand_(asio::make_strand(io_context_))
	{ }

	template<typename SecurityCheck>
	void LogServer<SecurityCheck>::start()
	{
		io_context_.run();
	}

	template<typename SecurityCheck>
	void LogServer<SecurityCheck>::start_accept()
	{
		acceptor_.async_accept(asio::bind_executor(accept_strand_, [this](const asio::error_code& ec, asio::ip::tcp::socket&& move_socket) mutable
		{
			if (!ec)
			{
				std::make_shared<NetworkSession<SecurityCheck>>(std::move(move_socket), log_queue_)->start();
			}
			else
			{
				std::print("Error al aceptar conexion: {}\n", ec.message());
			}

			start_accept();
		}));
	}

	template<typename SecurityCheck>
	NetworkSession<SecurityCheck>::NetworkSession(asio::ip::tcp::socket socket, Core::LogQueue& queue) noexcept : socket_(std::move(socket)), log_queue_(queue), CurrentParser(GetBestParser())
	{ }

	template<typename SecurityCheck>
	void NetworkSession<SecurityCheck>::read_header()
	{
		auto self = this->shared_from_this();

		asio::async_read(socket_, asio::buffer(&body_length_buffer_, sizeof(body_length_buffer_)), [this, self](const asio::error_code& ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				uint32_t body_length = ntohl(body_length_buffer_);

				const uint32_t MAX_LOG_SIZE = 1024 * 64;
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

	template<typename SecurityCheck>
	void NetworkSession<SecurityCheck>::read_body(uint32_t lenght)
	{
		auto self = this->shared_from_this();
		auto data = asio::buffer(read_buffer_, lenght);
		
		asio::async_read(socket_, data, [this, self](const asio::error_code& ec, std::size_t lenght)
		{
			if (!ec)
			{
				std::string_view view(read_buffer_.data(), lenght);
				CurrentParser(view, log_queue_);
				read_header();
			}
			else
			{
				handle_error(ec);
			}
		});
	}

	template<typename SecurityCheck>
	void NetworkSession<SecurityCheck>::parse_and_push(std::string_view raw_data, Core::LogQueue& log_queue)
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

		Core::LogEntry log_entry;

		log_entry.timestamp = time;
		log_entry.log_id = id;
		log_entry.level = log_level;
		log_entry.raw_log = message;

		if (!CheckLogEntry::validate(log_entry)) return;

		log_queue.push(std::move(log_entry));
	}

	template<typename SecurityCheck>
	void NetworkSession<SecurityCheck>::parse_and_push_simd(std::string_view raw_data, Core::LogQueue& log_queue)
	{
		if (raw_data.size() < 32)
		{
			parse_and_push(raw_data, log_queue);
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
			uint32_t index = __builtin_ctz(copy_mask);
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
			parse_and_push(raw_data, log_queue);
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

		Core::LogEntry log_entry;

		log_entry.level = log_level;
		log_entry.log_id = id;
		log_entry.raw_log = message;
		log_entry.timestamp = time;

		if (!CheckLogEntry::validate(log_entry)) return;

		log_queue.push(std::move(log_entry));
	}

	template<typename SecurityCheck>
	inline void NetworkSession<SecurityCheck>::handle_error(const asio::error_code& ec)
	{
		std::print("{}", ec.message());
		socket_.close();
	}
}
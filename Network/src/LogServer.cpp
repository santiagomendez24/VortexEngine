#include "../include/LogServer.h"
#include "../time/include/Time.h"
#include <print>

namespace Network
{
	template<typename SecurityCheck>
	LogServer<SecurityCheck>::LogServer(const uint16_t port, Core::LogQueue& log_queue) noexcept 
		: acceptor_(asio::ip::tcp::acceptor(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))), log_queue_(log_queue), pool_size_(std::thread::hardware_concurrency())
	{ }

	template<typename SecurityCheck>
	LogServer<SecurityCheck>::~LogServer() noexcept
	{
		stop();
	}

	template<typename SecurityCheck>
	void LogServer<SecurityCheck>::start()
	{
		start_accept();
		thread_pool_.reserve(pool_size_);
		
		for (size_t i = 0; i < pool_size_; ++i)
		{
			thread_pool_.emplace_back([this]()
			{
				io_context_.run();
			});
		}
	}

	template<typename SecurityCheck>
	void LogServer<SecurityCheck>::stop()
	{
		io_context_.stop();
		for (auto& thread : thread_pool_)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
	}

	template<typename SecurityCheck>
	void LogServer<SecurityCheck>::start_accept()
	{
		acceptor_.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket&& move_socket) mutable
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
		});
	}

	template<typename SecurityCheck>
	NetworkSession<SecurityCheck>::NetworkSession(asio::ip::tcp::socket socket, Core::LogQueue& queue) noexcept : socket_(std::move(socket)), log_queue_(queue)
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
				std::string_view raw_data(read_buffer_.data(), lenght);
				parse_and_push(raw_data);
				read_header();
			}
			else
			{
				handle_error(ec);
			}
		});
	}

	template<typename SecurityCheck>
	void NetworkSession<SecurityCheck>::parse_and_push(const std::string_view& raw_data)
	{
		std::string_view view = raw_data;

		std::array<std::string_view, 6> parts;
		size_t part_index = 0;

		size_t pos = 0;
		while ((pos = view.find('|')) != std::string_view::npos && part_index < 6)
		{
			parts[part_index++] = view.substr(0, pos);
			view.remove_prefix(pos + 1);       
		}

		if (!view.empty() && part_index < 6)
		{
			parts[part_index++] = view;
		}

		Core::LogEntry log_entry;

		auto convert_numeric = [](std::string_view token, auto& destination) -> bool
		{
			auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), destination);
			return ec == std::errc{}; 
		};

		if (part_index == 6)
		{
			if (!convert_numeric(parts[0], log_entry.timestamp))
			{
				return;
			}

			uint8_t raw_level = 0;
			if (!convert_numeric(parts[1], raw_level))
			{
				return;
			}
			log_entry.level = static_cast<Core::LogLevel>(raw_level);

			if (!convert_numeric(parts[2], log_entry.component_id))
			{
				return;
			}

			if (!SecurityCheck::validate(log_entry))
			{
				return;
			}

			if (parts[3].empty() || parts[4].empty() || parts[5].empty()) return;

			size_t location_size = std::min(parts[3].size(), log_entry.location.size() - 1);
			std::copy_n(parts[3].data(), location_size, log_entry.location.begin());
			log_entry.location[location_size] = '\0';

			size_t message_size = std::min(parts[4].size(), log_entry.message.size() - 1);
			std::copy_n(parts[4].data(), message_size, log_entry.message.begin());
			log_entry.message[message_size] = '\0';

			size_t context_size = std::min(parts[5].size(), log_entry.context.size() - 1);
			std::copy_n(parts[5].data(), context_size, log_entry.context.begin());
			log_entry.context[context_size] = '\0';

			log_queue_.push(std::move(log_entry));
		}
		else
		{
			handle_error(asio::error::invalid_argument);
		}
	}

	template<typename SecurityCheck>
	inline void NetworkSession<SecurityCheck>::handle_error(const asio::error_code& ec)
	{
		std::print("{}", ec.message());
		socket_.close();
	}
}
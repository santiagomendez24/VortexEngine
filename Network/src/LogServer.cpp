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

	}

	template<typename SecurityCheck>
	inline void NetworkSession<SecurityCheck>::handle_error(const asio::error_code& ec)
	{
		std::print("{}", ec.message());
		socket_.close();
	}
}
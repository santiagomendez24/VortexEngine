#include "../include/LogServer.h"

namespace Network
{
	LogServer::start_accept()
	{
		acceptor_.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket)
		{
			if (!ec)
			{
				std::make_shared<NetworkSession>(std::move(socket), log_queue_)->start();
			}
			else
			{
				//Manejar error
			}

			start_accept();
		});
	}

	NetworkSession::read_body(uint32_t lenght)
	{
		auto self(shared_from_this());
		read_buffer_.resize(lenght);

		asio::async_read(socket_, asio::buffer(read_buffer_), [this, self](asio::error_code ec, std::size_t /*lenght*/)
		{
			if (!ec)
			{
				std::string_view raw_data(read_buffer_.data(), read_buffer_.size());
				parse_and_push(raw_data);
				read_header();
			}
			else
			{
				handle_error(ec);
			}
		});
	}

	NetworkSession::parse_and_push(const std::string_view& raw_data)
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
	}

}
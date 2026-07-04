#pragma once

#include <asio.hpp>
#include <memory>
#include <vector>
#include <thread>

namespace Network
{

    class NetworkSession : public std::enable_shared_from_this<NetworkSession>
    {
    private:

        asio::ip::tcp::socket socket_;
        Core::LogQueue& log_queue_;

        uint32_t body_length_buffer_ = 0;
        std::vector<char> read_buffer_;

    public:

        explicit NetworkSession(asio::ip::tcp::socket socket, Core::LogQueue& queue);
        void start();

    private:

        void read_header();
        void read_body(uint32_t length);
        void parse_and_push(const std::string_view& raw_data);
        void handle_error(const asio::error_code& ec);
    };

    class LogServer
    {
    private:

        asio::io_context io_context_;
        asio::ip::tcp::acceptor acceptor_;
        Core::LogQueue& log_queue_;

        std::vector<std::thread> thread_pool_;
        size_t pool_size_;

    public:

        LogServer(Core::LogQueue& queue, uint16_t port, size_t pool_size);
        ~LogServer();

        LogServer(const LogServer&) = delete;
        LogServer& operator=(const LogServer&) = delete;

        void start();
        void stop();

    private:

        void start_accept();
    };
}
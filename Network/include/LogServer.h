#pragma once

#include <asio.hpp>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <string_view>
#include <array>
#include <charconv>
#include <algorithm>
#include <winsock2.h>

namespace Network
{
    struct CheckLogEntry
    {
        [[nodiscard]] static bool validate(const Core::LogEntry& log) noexcept
        {
            if (!validateTimestamp(log.timestamp))
            {
                return false;
            }

            if (!validateID(log.component_id))
            {
                return false;
            }

            return true;
        }

        static bool validateTimestamp(uint64_t timestamp) noexcept
        {
            uint64_t CurrentTime = Network::Time::GetTime();

            constexpr uint64_t MinAllowedTime = 1780000000; // 01/06/2026 
            constexpr uint64_t MaxAllowedTime = 5; // Tolerate 5 seconds into the future

            if (timestamp < MinAllowedTime || timestamp >(CurrentTime + MaxAllowedTime))
            {
                return false;
            }

            return true;
        }

        static bool validateID(uint32_t id) noexcept
        {
            if (id < 1 || id > 250)
            {
                return false;
            }
            return true;
        }
    };

    template <typename SecurityCheck>
    class NetworkSession : public std::enable_shared_from_this<NetworkSession<SecurityCheck>>
    {
    private:

        asio::ip::tcp::socket socket_;
        Core::LogQueue& log_queue_;

        uint32_t body_length_buffer_ = 0;
        std::array<char, 1024 * 64> read_buffer_;

    public:

        explicit NetworkSession(asio::ip::tcp::socket socket, Core::LogQueue& queue) noexcept;
        void start() { read_header(); }

    private:

        void read_header();
        void read_body(uint32_t length);
        void parse_and_push(const std::string_view& raw_data);
        inline void handle_error(const asio::error_code& ec);
    };

    template <typename SecurityCheck>
    class LogServer
    {
    private:

        asio::io_context io_context_;
        asio::ip::tcp::acceptor acceptor_;
        Core::LogQueue& log_queue_;

        std::vector<std::thread> thread_pool_;
        size_t pool_size_;

    public:

        explicit LogServer(const uint16_t port, Core::LogQueue& queue) noexcept;
        ~LogServer() noexcept;

        LogServer(const LogServer&) = delete;
        LogServer& operator=(const LogServer&) = delete;

        void start();
        void stop();

    private:

        void start_accept();
    };
}
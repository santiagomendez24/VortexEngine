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
#include "../time/include/Time.h"

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

            if (!validateID(log.log_id))
            {
                return false;
            }

            return true;
        }

        static bool validateTimestamp(uint64_t timestamp) noexcept
        {
            uint64_t CurrentTime = ::Network::Tools::Time::GetTime();

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
    
        static bool validate_fromcharts(const std::errc ec)
        {
            if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range)
            {
                return false;
            }

            return true;
        }
    };

    class NetworkSession : public std::enable_shared_from_this<NetworkSession>
    {
    private:

        asio::ip::tcp::socket socket_;

        uint32_t body_length_buffer_ = 0;
        alignas(32) std::array<char, (10 * 1024 * 1024) + 1024> read_buffer_;

        std::vector<Core::LogQueue*> assigned_ptr;
        std::atomic<size_t> next_queue_index{ 0 };

    public:

        explicit NetworkSession(asio::ip::tcp::socket socket) noexcept;
        ~NetworkSession() noexcept {}
        void start(Core::LogQueue* ptr) noexcept { this->assigned_ptr.push_back(ptr); read_header(); }

    private:

        void read_header();
        void read_body(uint32_t length);

        static void parse_and_push(std::string_view raw_data, Network::NetworkSession* session);
        static void parse_and_push_simd(std::string_view raw_data, Network::NetworkSession* session);

        inline void handle_error(const asio::error_code& ec);

        using ParserFunc = void(*)(std::string_view raw_data, Network::NetworkSession* session);

        static ParserFunc GetBestParser()
        {
            if (IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0)
            {
                return parse_and_push_simd;
            }

            return parse_and_push;
        }

        inline static uint32_t count_trailing_zeros(uint32_t mask)
        {
            unsigned long index;
            if (_BitScanForward(&index, mask))
            {
                return static_cast<uint32_t>(index);
            }
            return 0;
        }

        ParserFunc CurrentParser;
    };

    class LogServer
    {
    private:

        std::vector<Core::LogQueue*> ptr_queue;
        std::atomic<size_t> next_queue_index{ 0 };

        asio::io_context io_context_;
        asio::ip::tcp::acceptor acceptor_;

        asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
        asio::strand<asio::io_context::executor_type> accept_strand_;

    public:

        explicit LogServer(const uint16_t port) noexcept;
        ~LogServer() noexcept {}

        LogServer(const LogServer&) = delete;
        LogServer& operator=(const LogServer&) = delete;

        void start(Core::LogQueue* ptr);
        void stop() noexcept { work_guard_.reset();  io_context_.stop(); }
        void start_accept();
    };
}
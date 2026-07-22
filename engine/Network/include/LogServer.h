#pragma once

#include <asio.hpp>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <string_view>
#include <print>
#include <array>
#include <charconv>
#include <algorithm>
#include <winsock2.h>
#include <Windows.h>
#include "../time/include/Time.h"

#pragma comment(lib, "onecore.lib")

namespace Network
{
    struct CheckLogEntry
    {
        static bool validateTimestamp(uint64_t timestamp) noexcept
        {
            uint64_t CurrentTime = ::Network::Tools::Time::GetTime();

            uint64_t MinAllowedTime = CurrentTime - 5;
            constexpr uint64_t MaxAllowedTime = 5;

            if (timestamp < MinAllowedTime || timestamp >(CurrentTime + MaxAllowedTime))
            {
                return false;
            }

            return true;
        }

        static bool validateID(uint32_t id) noexcept
        {
            if (id < 1 || id > 4294967295U)
            {
                return false;
            }
            return true;
        }

        static bool validate_level(const size_t level)
        {
            if (level > static_cast<size_t>(Core::LogLevel::Critical) || level < static_cast<size_t>(Core::LogLevel::Debug))
            {
                return false;
            }
            
            return true;
        }
    };

#pragma pack(push, 1)
    struct Slab
    {
        std::unique_ptr<char[]> data;
        uint8_t tail_byte = 0;
    };
#pragma pack(pop)

    class SlabPool
    {
    private:

        std::unique_ptr<Slab[]> slab_pool;
        size_t write_index = 0;
        size_t current_offset = 0;
        size_t slab_size = 0;
        size_t max_slabs = 0;

    public:

        SlabPool(size_t max, size_t size) : slab_size(size * 1024 * 1024), max_slabs(max), slab_pool(std::make_unique<Slab[]>(max)) 
        {  
            for (size_t i = 0; i < max; ++i)
            {
                slab_pool[i].data = std::make_unique<char[]>(size * 1024 * 1024);
                slab_pool[i].tail_byte = 0;
            }
        }

        char* get_next_slab(std::string_view message, uint32_t& out_offset, Slab*& out_slab)
        {
            if (message.size() > slab_size) return nullptr;

            // Verificar si el mensaje cabe en el espacio restante del slab actual
            if (current_offset + message.size() > slab_size)
            {
                write_index = (write_index + 1) & (max_slabs - 1);
                current_offset = 0;
                slab_pool[write_index].tail_byte = 0x00;
            }

            out_offset = static_cast<uint32_t>(current_offset);
            out_slab = &slab_pool[write_index];

            current_offset += message.size();
            return out_slab->data.get() + out_offset;
        }
    };

    class NetworkSession : public std::enable_shared_from_this<NetworkSession>
    {
    private:

        asio::ip::tcp::socket socket_;

        std::unique_ptr<class MagicRingBuffer> stash_buffer;
        size_t max_log_size;

        Core::LogQueue* assigned_ptr;
        std::atomic<size_t> next_queue_index{ 0 };
        std::atomic<bool> is_closing{ false };

        void disconnect(const std::string& reason);

    public:

        explicit NetworkSession(asio::ip::tcp::socket socket, size_t max_size, Core::LogQueue* ptr) noexcept;
        ~NetworkSession() noexcept {}
        void start() noexcept { read_header(); }
        [[nodiscard]] Core::LogQueue* get_private_queue() const noexcept { return assigned_ptr; }

    private:

        void read_header();
        void process_stash();

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

        inline static uint32_t count_trailing_zeros(uint32_t mask);

        ParserFunc CurrentParser;
    };

    class LogServer
    {
    private:

        std::vector<std::thread> network_pool;
        std::vector<Core::LogQueue*> ptr_queue;

        std::atomic<size_t> next_queue_index{ 0 };
        std::atomic<size_t> next_context{ 0 };
        std::atomic<size_t> next_worker_index{ 0 };

        asio::io_context io_context_;
        std::vector<std::unique_ptr<asio::io_context>> context_vec;

        asio::ip::tcp::acceptor acceptor_;
        asio::executor_work_guard<asio::io_context::executor_type> work_guard_;

        std::vector<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_vec;
        asio::strand<asio::io_context::executor_type> accept_strand_;

        size_t log_size = 0;

    public:

        explicit LogServer(const uint16_t port, size_t max_size) noexcept;
        ~LogServer() noexcept {}

        LogServer(const LogServer&) = delete;
        LogServer& operator=(const LogServer&) = delete;

        void start(const std::vector<Core::LogQueue*>& ptr);

        void stop() noexcept;

        void start_accept();
    };

    class MagicRingBuffer
    {
    private:

        HANDLE hMapFile;
        void* viewA;
        void* viewB;

        uint8_t* base_ptr;    // Puntero base virtual contiguo de tamaño size * 2
        size_t size;
        size_t head;          // Índice de escritura (acumulativo)
        size_t tail;

    public:

        explicit MagicRingBuffer(size_t physical_size);

        ~MagicRingBuffer() noexcept { free_portal(); }

        MagicRingBuffer(const MagicRingBuffer&) = delete;
        MagicRingBuffer& operator=(const MagicRingBuffer&) = delete;

        MagicRingBuffer(MagicRingBuffer&& other) noexcept;

        uint8_t* get_write_ptr() const noexcept;

        size_t get_write_space() const noexcept;

        size_t get_bytes_available() const noexcept;

        void commit_write(size_t bytes_written) noexcept;

        void consume(size_t bytes_consumed) noexcept;

        std::string_view get_view(size_t len) const noexcept;

        void reset() noexcept;

        uint32_t peek(uint32_t usize = 0) noexcept;

    private:

        void start_portal();

        void free_portal() noexcept;
    };
}
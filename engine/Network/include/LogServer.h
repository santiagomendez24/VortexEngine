#pragma once

#ifndef LOGSERVER_H
#define LOGSERVER_H

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
#include "../../../shared/shared_memory_protocol.h"

#pragma comment(lib, "onecore.lib")

struct SharedMemoryControl;

namespace Network
{
    struct CheckLogEntry
    {
        static bool validateTimestamp(uint64_t timestamp) noexcept
        {
            uint64_t CurrentTime = ::Network::Tools::Time::GetTime();

            uint64_t MinAllowedTime = CurrentTime - 5;
            constexpr uint64_t MaxAllowedTime = 5;

            if (timestamp < MinAllowedTime || timestamp > (CurrentTime + MaxAllowedTime))
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

    class SlabPool
    {
    private:

        size_t write_index = 0;
        size_t current_offset = 0;
        size_t slab_size = 0;
        size_t max_slabs = 0;

        HANDLE hMapFile = nullptr;

    public:

        void* shared_memory_base_ptr = nullptr;

        SlabPool(size_t max, size_t size) : slab_size(size * 1024 * 1024), max_slabs(max) {}

        SlabPool(const SlabPool&) = delete;
        SlabPool& operator=(const SlabPool&) = delete;

        SlabPool(SlabPool&& other) noexcept : write_index(other.write_index), current_offset(other.current_offset),
            slab_size(other.slab_size), max_slabs(other.max_slabs),
            hMapFile(other.hMapFile), shared_memory_base_ptr(other.shared_memory_base_ptr)
        {
            other.hMapFile = nullptr;
            other.shared_memory_base_ptr = nullptr;
        }

        ~SlabPool() noexcept
        {
            if (shared_memory_base_ptr)
            {
                UnmapViewOfFile(shared_memory_base_ptr);
                shared_memory_base_ptr = nullptr;
            }

            if (hMapFile)
            {
                CloseHandle(hMapFile);
                hMapFile = nullptr;
            }
        }

        char* get_next_slab(std::string_view message, uint32_t& out_offset)
        {
            size_t total_needed = sizeof(SharedLogEntryHeader) + message.size();
            if (total_needed > slab_size) return nullptr;

            if (current_offset + total_needed > slab_size)
            {
                write_index = (write_index + 1) & (max_slabs - 1);
                current_offset = 0;
            }

            size_t relative_bytes = (write_index * slab_size) + current_offset;
            out_offset = static_cast<uint32_t>(relative_bytes);

            current_offset += total_needed;

            char* data_region_start = static_cast<char*>(shared_memory_base_ptr) + sizeof(SharedMemoryControl);
            return data_region_start + relative_bytes;
        }

        bool InitializeSharedMemory(uint32_t channel_id)
        {
            size_t total_bytes = sizeof(SharedMemoryControl) + (slab_size * max_slabs);

            wchar_t name_buffer[64];
            swprintf_s(name_buffer, 64, L"Local\\Vortex_Channel_%u", channel_id);

            DWORD high_size = static_cast<DWORD>(total_bytes >> 32);
            DWORD low_size = static_cast<DWORD>(total_bytes & 0xFFFFFFFF);

            hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, high_size, low_size, name_buffer);
            if (!hMapFile || hMapFile == INVALID_HANDLE_VALUE) return false;

            shared_memory_base_ptr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, total_bytes);
            if (!shared_memory_base_ptr)
            {
                CloseHandle(hMapFile);
                hMapFile = nullptr;
                return false;
            }

            auto* header = static_cast<SharedMemoryControl*>(shared_memory_base_ptr);
            header->magic_number = 0x564F5254; // 'VORT'
            header->slab_size = static_cast<uint32_t>(slab_size);
            header->max_slabs = static_cast<uint32_t>(max_slabs);
            header->header_size = sizeof(SharedMemoryControl);
            header->write_offset.store(0, std::memory_order_relaxed);
            header->read_offset.store(0, std::memory_order_relaxed);

            return true;
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

#endif
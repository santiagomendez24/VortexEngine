#pragma once

#ifndef LOG_QUEUE_H
#define LOG_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <new>
#include <cstdint>
#include <array>

namespace Core
{
    enum class LogLevel
    {
        Info,
        Warning,
        Error,
        Critical
    };

    struct LogEntry
    {
        uint64_t timestamp;     
        LogLevel level;          
        uint32_t component_id;   

        std::array<char, 64> location;
        std::array<char, 256> message;
        std::array<char, 128> context;
    };

    class LogQueue
    {
    private:

        std::queue<LogEntry> raw_queue;
        std::mutex queue_mutex;

        alignas(std::hardware_destructive_interference_size) std::condition_variable cv;
        bool is_running = true;

        /* "inline static" works for MaxCapacity being the same for every open instance of LogQueue, if you put 1MB in the first instance
        and 100MB in the second it will erase the megabyte and will be 100MB for both */
        // Erase "inline static" if you want that MaxCapacity is unique for each instance of LogQueue, but it will consume the choosen ram for each instance
        inline static size_t MaxCapacity;

        inline static void GetMaxRamUsage(size_t usable_ram) noexcept
        {
            constexpr size_t MaxLogEntrySize = sizeof(LogEntry);
            size_t MaxRamUsage = usable_ram * 1024 * 1024;
            MaxCapacity = MaxRamUsage / MaxLogEntrySize;
        }

    public:

        explicit LogQueue(size_t ram_usage) noexcept { GetMaxRamUsage(ram_usage); }
        explicit LogQueue(const LogQueue&) noexcept = delete;
        LogQueue& operator=(const LogQueue&) = delete;

        void push(LogEntry&& log_line);
        bool pop(LogEntry& out_log);
        void set_finished();
    };

} // namespace Core

#endif
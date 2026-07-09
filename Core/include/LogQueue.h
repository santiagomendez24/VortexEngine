#pragma once

#ifndef LOG_QUEUE_H
#define LOG_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <new>
#include <cstdint>
#include <atomic>
#include <array>

namespace Core
{
    enum class LogLevel : uint8_t
    {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    enum class OverflowProfile : uint8_t
    {
        Block,
        DropAll,
        DropNonCritical,
        Spillover
    };

    struct LogEntry
    {
        uint64_t timestamp;    
        std::string raw_log;
        uint32_t log_id;
        LogLevel level;

        [[nodiscard]] size_t GetMemory() const noexcept
        {
            return sizeof(*this) + raw_log.capacity();
        }
    };

    class LogQueue
    {
    private:

        std::queue<LogEntry> raw_queue;
        std::mutex queue_mutex;
        std::condition_variable cv_push;
        std::condition_variable cv_pop;
        bool is_running = true;

        size_t MaxCapacity;
        size_t HighWatermark;
        size_t LowWatermark;
        size_t current_queue_bytes{ 0 };

        size_t waiting_threads{ 0 };

        void GetMaxRamUsage(size_t usable_ram) noexcept;

        bool high_watermark_tripped{ false };
        Telemetry::Telemetry& telemetry_;

        OverflowProfile overflow;

    public:

        explicit LogQueue(size_t ram_usage, Telemetry::Telemetry& telemetry, OverflowProfile& over) noexcept : telemetry_(telemetry), overflow(over) { GetMaxRamUsage(ram_usage); }
        ~LogQueue() noexcept = default;

        explicit LogQueue(const LogQueue&) noexcept = delete;
        LogQueue& operator=(const LogQueue&) = delete;

        void push(LogEntry&& log_line);
        bool pop(LogEntry& out_log);
        void set_finished();
    };

} // namespace Core

#endif
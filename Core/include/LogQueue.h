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

    struct LogEntry
    {
        uint64_t timestamp;    
        std::string raw_log;
        uint16_t offset_level;
        uint16_t offset_msg;
        LogLevel level;

        [[nodiscard]] size_t GetMemory() const noexcept
        {
            return sizeof(*this) + raw_log.capacity();
        }
    };

    class LogQueue
    {
    private:

        alignas(std::hardware_destructive_interference_size) std::queue<LogEntry> raw_queue;
        alignas(std::hardware_destructive_interference_size) std::mutex queue_mutex;
        alignas(std::hardware_destructive_interference_size) std::condition_variable cv_push;
        alignas(std::hardware_destructive_interference_size) std::condition_variable cv_pop;
        bool is_running = true;

        size_t MaxCapacity;
        size_t HighWatermark;
        size_t LowWatermark;
        std::atomic<size_t> current_queue_bytes{ 0 };

        size_t waiting_threads{ 0 };

        void GetMaxRamUsage(size_t usable_ram) noexcept;

        bool high_watermark_tripped{ false };
        Telemetry::Telemetry& telemetry_;

    public:

        explicit LogQueue(size_t ram_usage, Telemetry::Telemetry& telemetry) noexcept : telemetry_(telemetry) { GetMaxRamUsage(ram_usage); }
        ~LogQueue() noexcept = default;

        explicit LogQueue(const LogQueue&) noexcept = delete;
        LogQueue& operator=(const LogQueue&) = delete;

        void push(LogEntry&& log_line);
        bool pop(LogEntry& out_log);
        void set_finished();
    };

} // namespace Core

#endif
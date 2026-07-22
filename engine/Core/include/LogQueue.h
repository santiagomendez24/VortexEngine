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
#include "../../Telemetry/include/Telemetry.h"

struct MainConfig;

namespace Network
{
    class SlabPool;
}

namespace Core
{
    class LogQueue;

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
        PassCriticalDropElse
    };

#pragma pack(push, 1)
    struct LogEntry
    {
        uint64_t timestamp;    
        uint32_t log_id;
        uint32_t message_lenght;
        uint32_t message_offset;
        LogLevel level;

        uint64_t relative_offset;

        [[nodiscard]] size_t GetMemory() const noexcept
        {
            return sizeof(*this);
        }
    };
#pragma pack(pop)

    class LogQueue
    {
    private:

        alignas(64) std::unique_ptr<LogEntry[]> log_array;
        alignas(64) std::atomic<size_t> tail{ 0 };

        std::atomic<bool> is_running = true;

        size_t MaxCapacity;

        std::atomic<size_t> HighWatermark;
        std::atomic<size_t> LowWatermark;

        size_t CapacityMask;

        void GetMaxRamUsage(size_t usable_ram) noexcept;

        std::atomic<bool> high_watermark_tripped{ false };
        Telemetry::Telemetry& telemetry_;

        OverflowProfile overflow;

    public:

        alignas(64) std::atomic<size_t> head{ 0 };

        std::unique_ptr<Network::SlabPool> slab_pool;

        explicit LogQueue(const MainConfig mainconfig, Telemetry::Telemetry& telemetry) noexcept;

        ~LogQueue() noexcept = default;

        explicit LogQueue(const LogQueue&) noexcept = delete;
        LogQueue& operator=(const LogQueue&) = delete;

        void push(LogEntry&& log_line);
        bool pop(LogEntry& out_log);

        void set_finished() noexcept;
        inline bool func_is_running() noexcept { return is_running.load(std::memory_order_relaxed); }
    };

} // namespace Core

#endif
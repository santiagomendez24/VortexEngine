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

namespace Network
{
    template<size_t max_slabs, size_t slab_size> class SlabPool;
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
        Spillover
    };

#pragma pack(push, 1)
    struct LogEntry
    {
        uint64_t timestamp;    
        uint32_t log_id;
        uint32_t message_lenght;
        uint32_t message_offset;
        LogLevel level;

        uintptr_t slab_ptr;

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
        alignas(64) std::atomic<size_t> head{ 0 };
        alignas(64) std::atomic<size_t> tail{ 0 };

        std::atomic<bool> is_running = true;

        size_t MaxCapacity;

        std::atomic<size_t> HighWatermark;
        std::atomic<size_t> LowWatermark;

        std::atomic<size_t> waiting_threads{ 0 };

        size_t CapacityMask;

        void GetMaxRamUsage(size_t usable_ram) noexcept;

        std::atomic<bool> high_watermark_tripped{ false };
        Telemetry::Telemetry& telemetry_;

        OverflowProfile overflow;

    public:

        std::unique_ptr<Network::SlabPool<10, 10>> slab_pool;

        explicit LogQueue(size_t ram_usage, Telemetry::Telemetry& telemetry, const OverflowProfile& over) noexcept : telemetry_(telemetry), overflow(over), 
            slab_pool(std::make_unique<Network::SlabPool<10, 10>>())
        { 
            GetMaxRamUsage(ram_usage);
            size_t raw_slots = MaxCapacity / sizeof(LogEntry);

            if (raw_slots < 2)
            {
                std::cerr << "[ERROR] Memoria RAM insuficiente para inicializar el buffer.\n";
                return;
            }

            size_t optimized_capacity = std::bit_ceil(raw_slots);
            CapacityMask = optimized_capacity - 1;
            log_array = std::make_unique<LogEntry[]>(optimized_capacity);

            HighWatermark.store((optimized_capacity * 8) / 10, std::memory_order_relaxed);
            LowWatermark.store((optimized_capacity * 2) / 10, std::memory_order_relaxed);
        }
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
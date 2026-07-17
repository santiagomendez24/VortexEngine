#include "../include/LogQueue.h"
#include "../../Network/include/LogServer.h"
#include <utility>
#include <memory>
#include <immintrin.h>
#include <print>

namespace Core
{
    void LogQueue::push(LogEntry&& log_line)
    {
        if (!log_array) return;
        if (!is_running) return;

        size_t current_head = head.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) & CapacityMask;

        if (next_head == tail.load(std::memory_order_acquire))
        {
            switch (overflow)
            {
                case OverflowProfile::Block:
                    while (current_head == tail.load(std::memory_order_acquire))
                    {
                        _mm_pause();
                    }
                    break;

                case OverflowProfile::DropAll:
                    return;

                case OverflowProfile::DropNonCritical:
                    if (log_line.level != LogLevel::Critical) return;
                    while (current_head == tail.load(std::memory_order_acquire))
                    {
                        _mm_pause();
                    }
                    break;
            }
        }

        if (!is_running) return;

        log_array[current_head] = log_line;

        head.store(next_head, std::memory_order_release);

        telemetry_.RegisterPushed();

        size_t occupied_slots = (next_head - tail.load(std::memory_order_acquire)) & CapacityMask; 

        if (occupied_slots >= HighWatermark.load(std::memory_order_relaxed))
        {
            high_watermark_tripped.exchange(true, std::memory_order_relaxed);

            size_t total_slots = CapacityMask + 1;
            size_t percentage = (occupied_slots * 100) / total_slots;

            //std::print("[VORTEX] Alerta: Cola al {}% de capacidad. Slots ocupados: {} de {}\n", percentage, occupied_slots, total_slots);
        }
    }

    void LogQueue::set_finished() noexcept
    {
        is_running = false;
    }

    bool LogQueue::pop(LogEntry& out_log)
    {
        if (!is_running) return false;

        size_t current_tail = tail.load(std::memory_order_relaxed);

        if (current_tail == head.load(std::memory_order_acquire))
        {
            return false;
        }

        if (!is_running) return false;

        out_log = std::move(log_array[current_tail]);

        tail.store((current_tail + 1) & CapacityMask, std::memory_order_release);

        telemetry_.RegisterEliminated();

        size_t total_slots = CapacityMask + 1;
        size_t occupied_slots = (head.load(std::memory_order_acquire) - current_tail) & CapacityMask;
        size_t freed_slots = total_slots - occupied_slots;

        if (freed_slots <= LowWatermark.load(std::memory_order_relaxed))
        {
            if (high_watermark_tripped.exchange(false, std::memory_order_relaxed))
            {
                //std::print("[VORTEX] Sistema estabilizado de nuevo. Slots libres: {}\n", freed_slots);
            }
        }

        return true;
    }

    void LogQueue::GetMaxRamUsage(size_t usable_ram) noexcept
    {
        MaxCapacity = usable_ram * 1024 * 1024;
    }
} 
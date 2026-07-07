#include "../include/LogQueue.h"
#include <utility>
#include <memory>

namespace Core
{
    void LogQueue::push(LogEntry&& log_line)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!is_running) return;

        cv.wait(lock, [this] { return raw_queue.size() < MaxCapacity || !is_running; });

        if (!is_running) return;

        raw_queue.push(std::move(log_line));

        telemetry_.RegisterPushed();

        if (raw_queue.size() >= HighWatermark)
        {
            if (!high_watermark_tripped.exchange(true, std::memory_order_relaxed))
            {
                //Tirar reporte
            }
        }

        cv.notify_one();
    }

    void LogQueue::set_finished()
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        is_running = false;

        cv.notify_all();
    }

    bool LogQueue::pop(LogEntry& out_log)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv.wait(lock, [this] { return !raw_queue.empty() || !is_running; });

        if (raw_queue.empty() && !is_running)
        {
            return false;
        }

        out_log = std::move(raw_queue.front());
        raw_queue.pop();

        if (raw_queue.size() <= LowWatermark)
        {
            if (high_watermark_tripped.exchange(false, std::memory_order_relaxed))
            {
                //Tirar reporte
            }
        }

        if (raw_queue.size() < MaxCapacity)
        {
            cv.notify_one();
        }

        return true;
    }

    void LogQueue::GetMaxRamUsage(size_t usable_ram) noexcept
    {
        constexpr size_t MaxLogEntrySize = sizeof(LogEntry);
        size_t MaxRamUsage = usable_ram * 1024 * 1024;
        MaxCapacity = MaxRamUsage / MaxLogEntrySize;

        HighWatermark = (MaxCapacity * 8) / 10;
        LowWatermark = (MaxCapacity * 2) / 10;
    }
} 
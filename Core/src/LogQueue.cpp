#include "../include/LogQueue.h"
#include <utility>
#include <memory>

namespace Core
{
    void LogQueue::push(LogEntry&& log_line)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!is_running) return;

        waiting_threads++;
        cv_push.wait(lock, [this, &log_line] { return !is_running || current_queue_bytes == 0 || current_queue_bytes + log_line.GetMemory() < MaxCapacity; });
        waiting_threads--;

        if (!is_running) return;

        current_queue_bytes += log_line.GetMemory();
        raw_queue.push(std::move(log_line));

        telemetry_.RegisterPushed();

        if (current_queue_bytes >= HighWatermark)
        {
            if (!high_watermark_tripped)
            {
                high_watermark_tripped = true;
                //Tirar reporte
            }
        }

        cv_pop.notify_one();
    }

    void LogQueue::set_finished()
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        is_running = false;

        cv_push.notify_all();
        cv_pop.notify_all();
    }

    bool LogQueue::pop(LogEntry& out_log)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv_pop.wait(lock, [this] { return !raw_queue.empty() || !is_running; });

        if (raw_queue.empty() && !is_running)
        {
            return false;
        }

        current_queue_bytes -= raw_queue.front().GetMemory();
        out_log = std::move(raw_queue.front());
        raw_queue.pop();

        if (current_queue_bytes <= LowWatermark)
        {
            if (high_watermark_tripped)
            {
                high_watermark_tripped = false;
                //Tirar reporte
            }
        }

        if (current_queue_bytes < MaxCapacity && waiting_threads > 0)
        {
            cv_push.notify_all();
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
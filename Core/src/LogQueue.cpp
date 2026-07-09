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

        if (current_queue_bytes + log_line.GetMemory() > MaxCapacity)
        {
            switch (overflow)
            {
                case Core::OverflowProfile::Block:

                    cv_push.wait(lock, [this, &log_line] { return !is_running || current_queue_bytes == 0 || current_queue_bytes + log_line.GetMemory() < MaxCapacity; });
                    break;

                case Core::OverflowProfile::DropAll:

                    waiting_threads--;
                    return;

                case Core::OverflowProfile::DropNonCritical:

                    if (log_line.level < LogLevel::Critical)
                    {
                        waiting_threads--;
                        return;
                    }
                    cv_push.wait(lock, [this, &log_line] { return !is_running || current_queue_bytes == 0 || current_queue_bytes + log_line.GetMemory() < MaxCapacity; });
                    break;

                case Core::OverflowProfile::Spillover:

                    //Levar al disco
                    break;
            }
        }

        waiting_threads--;

        if (!is_running) return;

        current_queue_bytes += log_line.GetMemory();
        raw_queue.push(std::move(log_line));

        telemetry_.RegisterPushed();
        telemetry_.UpdateQueueBytes(current_queue_bytes);

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

        telemetry_.RegisterEliminated();
        telemetry_.UpdateQueueBytes(current_queue_bytes);

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
        MaxCapacity = usable_ram * 1024 * 1024;
        HighWatermark = (MaxCapacity * 8) / 10;
        LowWatermark = (MaxCapacity * 2) / 10;
    }
} 
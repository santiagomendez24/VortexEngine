#include "../include/LogQueue.h"
#include <utility>

namespace Core
{

    void LogQueue::push(LogEntry&& log_line)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!is_running) return;

        cv.wait(lock, [this] { return raw_queue.size() < MaxCapacity || !is_running; });

        if (!is_running) return;

        raw_queue.push(std::move(log_line));
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
        return true;
    }

} 
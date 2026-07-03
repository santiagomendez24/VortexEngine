#pragma once

#ifndef LOG_QUEUE_H
#define LOG_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

namespace Core
{

    class LogQueue
    {
    private:
        std::queue<std::string> raw_queue;
        std::mutex queue_mutex;
        std::condition_variable cv;
        bool is_running = true;

    public:
        LogQueue() = default;
        LogQueue(const LogQueue&) = delete;
        LogQueue& operator=(const LogQueue&) = delete;

        void push(std::string&& log_line);
        bool pop(std::string& out_log);
        void set_finished();
    };

} // namespace Core

#endif
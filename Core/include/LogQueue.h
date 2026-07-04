#pragma once

#ifndef LOG_QUEUE_H
#define LOG_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <new>
#include <cstdint>

namespace Core
{
    enum class LogLevel
    {
        Info,
        Warning,
        Error,
        Critical
    };

    struct LogEntry
    {
        uint64_t timestamp;     
        LogLevel level;          
        uint32_t component_id;   

        std::array<char, 64> location; 
        std::array<char, 256> message;
        std::array<char, 128> context;
    };

    class LogQueue
    {
    private:

        std::queue<LogEntry> raw_queue;
        std::mutex queue_mutex;

        alignas(std::hardware_destructive_interference_size) std::condition_variable cv;
        bool is_running = true;

    public:

        LogQueue() = default;
        LogQueue(const LogQueue&) = delete;
        LogQueue& operator=(const LogQueue&) = delete;

        void push(LogEntry&& log_line);
        bool pop(LogEntry& out_log);
        void set_finished();
    };

} // namespace Core

#endif
#pragma once

#ifndef SHARED_MEMORY_PROTOCOL_H
#define SHARED_MEMORY_PROTOCOL_H

#include <cstdint>
#include <atomic>

#pragma pack(push, 1)

struct SharedLogEntryHeader
{
    uint64_t timestamp;
    uint32_t log_id;
    uint32_t message_len;
    uint8_t  level;
};

struct SharedMemoryControl
{
    alignas(64) std::atomic<uint64_t> write_offset{ 0 };
    alignas(64) std::atomic<uint64_t> read_offset{ 0 }; 
    uint64_t total_capacity_bytes;                     
};
#pragma pack(pop)

#endif
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
    uint32_t message_offset;
    uint64_t relative_offset;
    uint8_t  level;
};

struct SharedMemoryControl
{
    uint32_t magic_number;  
    uint32_t slab_size;      
    uint32_t max_slabs;       
    uint32_t header_size;

    alignas(64) std::atomic<uint64_t> write_offset{ 0 };
    alignas(64) std::atomic<uint64_t> read_offset{ 0 }; 
    
    uint8_t padding[28];
};
#pragma pack(pop)

#endif
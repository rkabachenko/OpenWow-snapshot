
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace openwow::core {

class SCompPool {
public:
    static SCompPool& Instance();

    void* Alloc(uint32_t elem_size, uint32_t count, void* context);

    void Free(void* ptr, void* context);

    int Shutdown();

private:
    SCompPool() = default;

    void* pool_buffer_ = nullptr;
};

struct SCompArenaContext {
    uint8_t* current;
    uint32_t total_size;
    uint8_t* base;
};

}

#pragma once

#include <cstdint>

namespace openwow::core {

struct BlizzardHashMapAllocator {
    using AllocFn   = void* (*)(std::uint32_t size);
    using ReallocFn = void* (*)(void* ptr, std::uint32_t size);
    using FreeFn    = void  (*)(void* ptr);

    AllocFn   alloc{nullptr};
    ReallocFn realloc{nullptr};
    FreeFn    free{nullptr};
};

struct BlizzardHashMapBucketArray {
    void**                      data{nullptr};
    std::uint32_t               capacity{0};
    std::uint32_t               used_count{0};
    std::uint32_t               load_thresh{0};
    BlizzardHashMapAllocator*   allocator{nullptr};

    void DestroyBucketArray();
};

}

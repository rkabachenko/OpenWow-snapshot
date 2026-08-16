#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::core {

inline constexpr uint32_t kLmemPoolSizeClasses[] = {
    16, 24, 32, 40, 64, 80, 128, 160, 256
};
inline constexpr uint32_t kLmemPoolNumClasses = 9;
inline constexpr uint32_t kLmemPoolElementsPerBlock = 1024;

struct MemBlock {
    uint8_t*  baseAddress  = nullptr;
    void*     freeListHead = nullptr;
    uint32_t  totalSize    = 0;
    uint32_t  elementSize  = 0;
    uint32_t  freeCount    = 0;

    void Init(uint32_t elemSize, uint32_t allocSize);
};

struct MemChunk {
    uint32_t   numBlocks       = 0;
    MemBlock** blocks          = nullptr;
    uint32_t   blockAllocSize  = 0;
    uint32_t   elementSize     = 0;
    uint32_t   elementsPerBlock = 0;

    bool ReleaseBlock(void* ptr);

    void* AllocBlock();

    void DestroyAll();

    void ResizeBlocks(uint32_t newCount);
};

using PoolSet = MemChunk*[kLmemPoolNumClasses];

void lmemPool_CreatePoolSet(PoolSet& out);

void lmemPool_DestroyPoolSet(PoolSet& set);

void* lmemPool_Realloc(PoolSet& set, void* ptr, size_t oldSize, size_t newSize);

int lmemPool_FindSizeClass(size_t size);

}

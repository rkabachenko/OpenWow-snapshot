
#include "lmem_pool.h"
#include "storm_string.h"

#include <algorithm>
#include <cstring>

namespace openwow::core {

void MemBlock::Init(uint32_t elemSize, uint32_t allocSize) {
    elementSize = elemSize;
    freeCount   = allocSize / elemSize;
    totalSize   = elemSize * freeCount;

    baseAddress  = static_cast<uint8_t*>(SMemAlloc(totalSize, __FILE__, __LINE__, 0));
    freeListHead = baseAddress;

    uint8_t* cur = baseAddress;
    uint8_t* end = baseAddress + totalSize - elemSize;
    while (cur < end) {
        *reinterpret_cast<uint8_t**>(cur) = cur + elemSize;
        cur += elemSize;
    }
    *reinterpret_cast<void**>(cur) = nullptr;
}

bool MemChunk::ReleaseBlock(void* ptr) {
    if (numBlocks == 0)
        return false;

    auto ptrAddr = reinterpret_cast<uintptr_t>(ptr);

    for (uint32_t i = 0; i < numBlocks; ++i) {
        MemBlock* blk = blocks[i];
        auto base = reinterpret_cast<uintptr_t>(blk->baseAddress);
        if (ptrAddr >= base && ptrAddr < base + blk->totalSize) {

            *reinterpret_cast<void**>(ptr) = blk->freeListHead;
            ++blk->freeCount;
            blk->freeListHead = ptr;
            return true;
        }
    }
    return false;
}

void MemChunk::ResizeBlocks(uint32_t newCount) {
    MemBlock** oldBlocks = blocks;

    blocks = static_cast<MemBlock**>(
        SMemReAlloc(oldBlocks, newCount * sizeof(MemBlock*), __FILE__, __LINE__, 16));

    if (!blocks) {

        blocks = static_cast<MemBlock**>(
            SMemAlloc(newCount * sizeof(MemBlock*), __FILE__, __LINE__, 0));

        if (oldBlocks) {
            uint32_t copyCount = std::min(newCount, numBlocks);
            for (uint32_t i = 0; i < copyCount; ++i)
                blocks[i] = oldBlocks[i];
            SMemFree(oldBlocks, __FILE__, __LINE__, 0);
        }
    }
}

void* MemChunk::AllocBlock() {

    for (uint32_t i = 0; i < numBlocks; ++i) {
        MemBlock* blk = blocks[i];
        if (blk->freeListHead) {
            void* result = blk->freeListHead;
            blk->freeListHead = *reinterpret_cast<void**>(result);
            --blk->freeCount;
            return result;
        }
    }

    uint32_t newIndex = numBlocks;

    if (newIndex == static_cast<uint32_t>(-1)) {

        if (blocks)
            SMemFree(blocks, __FILE__, __LINE__, 0);
        numBlocks = 0;
        blocks    = nullptr;
        newIndex  = 0;
    }

    ResizeBlocks(newIndex + 1);
    numBlocks = newIndex + 1;

    auto* newBlock = static_cast<MemBlock*>(SMemAlloc(sizeof(MemBlock), __FILE__, __LINE__, 0));
    if (newBlock) {
        newBlock->baseAddress  = nullptr;
        newBlock->freeListHead = nullptr;
        newBlock->totalSize    = 0;
        newBlock->elementSize  = 0;
        newBlock->freeCount    = 0;
        newBlock->Init(elementSize, blockAllocSize);
    }
    blocks[newIndex] = newBlock;

    if (newBlock && newBlock->freeListHead) {
        void* result = newBlock->freeListHead;
        newBlock->freeListHead = *reinterpret_cast<void**>(result);
        --newBlock->freeCount;
        return result;
    }

    return nullptr;
}

void MemChunk::DestroyAll() {

    for (int i = static_cast<int>(numBlocks); i > 0; --i) {
        MemBlock* blk = blocks[i - 1];
        if (blk) {
            SMemFree(blk->baseAddress, __FILE__, __LINE__, 0);
            SMemFree(blk, "delete", -1, 0);
        }
    }

    if (blocks)
        SMemFree(blocks, __FILE__, __LINE__, 0);

    numBlocks = 0;
    blocks    = nullptr;
}

void lmemPool_CreatePoolSet(PoolSet& out) {
    for (uint32_t i = 0; i < kLmemPoolNumClasses; ++i) {
        auto* chunk = static_cast<MemChunk*>(SMemAlloc(sizeof(MemChunk), __FILE__, __LINE__, 0));
        if (chunk) {
            chunk->numBlocks       = 0;
            chunk->blocks          = nullptr;
            chunk->elementSize     = kLmemPoolSizeClasses[i];
            chunk->elementsPerBlock = kLmemPoolElementsPerBlock;
            chunk->blockAllocSize  = chunk->elementSize * chunk->elementsPerBlock;
        }
        out[i] = chunk;
    }
}

void lmemPool_DestroyPoolSet(PoolSet& set) {
    for (uint32_t i = 0; i < kLmemPoolNumClasses; ++i) {
        if (set[i]) {
            set[i]->DestroyAll();
            SMemFree(set[i], "delete", -1, 0);
            set[i] = nullptr;
        }
    }
}

int lmemPool_FindSizeClass(size_t size) {
    for (uint32_t i = 0; i < kLmemPoolNumClasses; ++i) {
        if (size <= kLmemPoolSizeClasses[i])
            return static_cast<int>(i);
    }
    return -1;
}

void* lmemPool_Realloc(PoolSet& set, void* ptr, size_t oldSize, size_t newSize) {
    int oldClass = ptr ? lmemPool_FindSizeClass(oldSize) : -1;
    int newClass = -1;

    if (newSize == 0) {
        if (ptr) {
            if (oldClass >= 0) {
                set[oldClass]->ReleaseBlock(ptr);
            } else {
                SMemFree(ptr, __FILE__, __LINE__, 0);
            }
        }
        return nullptr;
    }

    newClass = lmemPool_FindSizeClass(newSize);

    if (ptr && oldClass == newClass) {

        if (oldClass < 0) {

            return SMemReAlloc(ptr, newSize, __FILE__, __LINE__, 0);
        }
        return ptr;
    }

    void* newPtr;
    if (newClass < 0) {
        newPtr = SMemAlloc(newSize, __FILE__, __LINE__, 0);
    } else {
        newPtr = set[newClass]->AllocBlock();
    }

    if (ptr) {
        size_t copySize = std::min(oldSize, newSize);
        std::memcpy(newPtr, ptr, copySize);

        if (oldClass >= 0) {
            set[oldClass]->ReleaseBlock(ptr);
        } else {
            SMemFree(ptr, __FILE__, __LINE__, 0);
        }
    }

    return newPtr;
}

}

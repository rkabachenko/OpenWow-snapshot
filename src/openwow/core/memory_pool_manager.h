#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::core {

struct PoolStats {
    std::string poolName;
    size_t      objectSize         = 0;
    uint32_t    allocatedCount     = 0;
    uint32_t    freeCount          = 0;
    uint32_t    highWaterMark      = 0;
    uint64_t    totalAllocations   = 0;
    uint64_t    totalDeallocations = 0;
};

class MemoryPoolManager {
public:
    static constexpr uint32_t kMaxPools = 32;

    MemoryPoolManager();
    ~MemoryPoolManager();

    MemoryPoolManager(const MemoryPoolManager&) = delete;
    MemoryPoolManager& operator=(const MemoryPoolManager&) = delete;

    uint32_t CreatePool(const std::string& name, size_t objectSize, uint32_t initialCapacity);

    void DestroyPool(uint32_t poolId);

    void* Allocate(uint32_t poolId);

    void Deallocate(uint32_t poolId, void* ptr);

    [[nodiscard]] PoolStats GetPoolStats(uint32_t poolId) const;

    [[nodiscard]] std::vector<PoolStats> GetAllPoolStats() const;

    [[nodiscard]] size_t GetTotalAllocated() const;

    [[nodiscard]] uint32_t GetTotalFreeBlocks() const;

    [[nodiscard]] uint32_t GetPoolCount() const;

    void ResetPool(uint32_t poolId);

    void ShrinkPool(uint32_t poolId);

    void SetGrowthFactor(float factor);

private:
    struct FreeNode {
        FreeNode* next;
    };

    struct Page {
        uint8_t* memory   = nullptr;
        uint32_t capacity = 0;
    };

    struct Pool {
        bool                active     = false;
        std::string         name;
        size_t              objectSize = 0;
        uint32_t            capacity   = 0;
        FreeNode*           freeList   = nullptr;
        uint32_t            freeCount  = 0;
        uint32_t            allocatedCount = 0;
        uint32_t            highWaterMark  = 0;
        uint64_t            totalAllocations   = 0;
        uint64_t            totalDeallocations = 0;
        std::vector<Page>   pages;
    };

    void GrowPool(Pool& pool);
    void InitPage(Pool& pool, Page& page);

    Pool        m_pools[kMaxPools]{};
    uint32_t    m_poolCount  = 0;
    float       m_growthFactor = 2.0f;
    mutable std::mutex m_mutex;
};

}

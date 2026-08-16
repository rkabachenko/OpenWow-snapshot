
#include "memory_pool_manager.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace openwow::core {

MemoryPoolManager::MemoryPoolManager() = default;

MemoryPoolManager::~MemoryPoolManager() {
    for (uint32_t i = 0; i < kMaxPools; ++i) {
        if (m_pools[i].active) {
            for (auto& page : m_pools[i].pages)
                delete[] page.memory;
            m_pools[i].pages.clear();
            m_pools[i].active = false;
        }
    }
}

uint32_t MemoryPoolManager::CreatePool(const std::string& name, size_t objectSize, uint32_t initialCapacity) {
    std::lock_guard lock(m_mutex);

    if (objectSize < sizeof(FreeNode))
        objectSize = sizeof(FreeNode);

    uint32_t slot = kMaxPools;
    for (uint32_t i = 0; i < kMaxPools; ++i) {
        if (!m_pools[i].active) { slot = i; break; }
    }
    if (slot == kMaxPools)
        throw std::runtime_error("MemoryPoolManager: max pool count reached (32)");

    Pool& pool      = m_pools[slot];
    pool.active     = true;
    pool.name       = name;
    pool.objectSize = objectSize;
    pool.capacity   = 0;
    pool.freeList   = nullptr;
    pool.freeCount  = 0;
    pool.allocatedCount     = 0;
    pool.highWaterMark      = 0;
    pool.totalAllocations   = 0;
    pool.totalDeallocations = 0;

    if (initialCapacity > 0) {
        Page page;
        page.capacity = initialCapacity;
        page.memory   = new uint8_t[objectSize * initialCapacity];
        InitPage(pool, page);
        pool.pages.push_back(page);
        pool.capacity += initialCapacity;
    }

    ++m_poolCount;
    return slot;
}

void MemoryPoolManager::InitPage(Pool& pool, Page& page) {

    for (uint32_t i = 0; i < page.capacity; ++i) {
        auto* node  = reinterpret_cast<FreeNode*>(page.memory + i * pool.objectSize);
        node->next  = pool.freeList;
        pool.freeList = node;
        ++pool.freeCount;
    }
}

void MemoryPoolManager::GrowPool(Pool& pool) {
    uint32_t newCap = std::max(1u, static_cast<uint32_t>(pool.capacity * m_growthFactor));
    if (newCap <= pool.capacity) newCap = pool.capacity + 1;

    Page page;
    page.capacity = newCap;
    page.memory   = new uint8_t[pool.objectSize * newCap];
    InitPage(pool, page);
    pool.pages.push_back(page);
    pool.capacity += newCap;
}

void MemoryPoolManager::DestroyPool(uint32_t poolId) {
    std::lock_guard lock(m_mutex);
    if (poolId >= kMaxPools || !m_pools[poolId].active)
        throw std::runtime_error("MemoryPoolManager::DestroyPool: invalid pool id");

    Pool& pool = m_pools[poolId];
    for (auto& page : pool.pages)
        delete[] page.memory;
    pool.pages.clear();
    pool.active = false;
    pool.freeList = nullptr;
    pool.freeCount = 0;
    pool.allocatedCount = 0;
    pool.capacity = 0;
    --m_poolCount;
}

void* MemoryPoolManager::Allocate(uint32_t poolId) {
    std::lock_guard lock(m_mutex);
    if (poolId >= kMaxPools || !m_pools[poolId].active)
        throw std::runtime_error("MemoryPoolManager::Allocate: invalid pool id");

    Pool& pool = m_pools[poolId];
    if (!pool.freeList)
        GrowPool(pool);

    FreeNode* node = pool.freeList;
    pool.freeList  = node->next;
    --pool.freeCount;
    ++pool.allocatedCount;
    ++pool.totalAllocations;

    if (pool.allocatedCount > pool.highWaterMark)
        pool.highWaterMark = pool.allocatedCount;

    std::memset(node, 0, pool.objectSize);
    return static_cast<void*>(node);
}

void MemoryPoolManager::Deallocate(uint32_t poolId, void* ptr) {
    std::lock_guard lock(m_mutex);
    if (poolId >= kMaxPools || !m_pools[poolId].active)
        throw std::runtime_error("MemoryPoolManager::Deallocate: invalid pool id");
    if (!ptr) return;

    Pool& pool = m_pools[poolId];
    auto* node = static_cast<FreeNode*>(ptr);
    node->next = pool.freeList;
    pool.freeList = node;
    ++pool.freeCount;
    --pool.allocatedCount;
    ++pool.totalDeallocations;
}

PoolStats MemoryPoolManager::GetPoolStats(uint32_t poolId) const {
    std::lock_guard lock(m_mutex);
    if (poolId >= kMaxPools || !m_pools[poolId].active)
        throw std::runtime_error("MemoryPoolManager::GetPoolStats: invalid pool id");

    const Pool& pool = m_pools[poolId];
    PoolStats s;
    s.poolName           = pool.name;
    s.objectSize         = pool.objectSize;
    s.allocatedCount     = pool.allocatedCount;
    s.freeCount          = pool.freeCount;
    s.highWaterMark      = pool.highWaterMark;
    s.totalAllocations   = pool.totalAllocations;
    s.totalDeallocations = pool.totalDeallocations;
    return s;
}

std::vector<PoolStats> MemoryPoolManager::GetAllPoolStats() const {
    std::lock_guard lock(m_mutex);
    std::vector<PoolStats> result;
    for (uint32_t i = 0; i < kMaxPools; ++i) {
        if (m_pools[i].active) {
            const Pool& pool = m_pools[i];
            PoolStats s;
            s.poolName           = pool.name;
            s.objectSize         = pool.objectSize;
            s.allocatedCount     = pool.allocatedCount;
            s.freeCount          = pool.freeCount;
            s.highWaterMark      = pool.highWaterMark;
            s.totalAllocations   = pool.totalAllocations;
            s.totalDeallocations = pool.totalDeallocations;
            result.push_back(s);
        }
    }
    return result;
}

size_t MemoryPoolManager::GetTotalAllocated() const {
    std::lock_guard lock(m_mutex);
    size_t total = 0;
    for (uint32_t i = 0; i < kMaxPools; ++i) {
        if (m_pools[i].active)
            total += m_pools[i].allocatedCount * m_pools[i].objectSize;
    }
    return total;
}

uint32_t MemoryPoolManager::GetTotalFreeBlocks() const {
    std::lock_guard lock(m_mutex);
    uint32_t total = 0;
    for (uint32_t i = 0; i < kMaxPools; ++i) {
        if (m_pools[i].active)
            total += m_pools[i].freeCount;
    }
    return total;
}

uint32_t MemoryPoolManager::GetPoolCount() const {
    std::lock_guard lock(m_mutex);
    return m_poolCount;
}

void MemoryPoolManager::ResetPool(uint32_t poolId) {
    std::lock_guard lock(m_mutex);
    if (poolId >= kMaxPools || !m_pools[poolId].active)
        throw std::runtime_error("MemoryPoolManager::ResetPool: invalid pool id");

    Pool& pool = m_pools[poolId];
    pool.freeList = nullptr;
    pool.freeCount = 0;
    pool.allocatedCount = 0;

    for (auto& page : pool.pages)
        InitPage(pool, page);
}

void MemoryPoolManager::ShrinkPool(uint32_t poolId) {
    std::lock_guard lock(m_mutex);
    if (poolId >= kMaxPools || !m_pools[poolId].active)
        throw std::runtime_error("MemoryPoolManager::ShrinkPool: invalid pool id");

    Pool& pool = m_pools[poolId];

    if (pool.allocatedCount == 0 && pool.pages.size() > 1) {

        for (size_t i = 1; i < pool.pages.size(); ++i)
            delete[] pool.pages[i].memory;

        uint32_t firstCap = pool.pages[0].capacity;
        pool.pages.resize(1);
        pool.capacity = firstCap;

        pool.freeList = nullptr;
        pool.freeCount = 0;
        InitPage(pool, pool.pages[0]);
    }
}

void MemoryPoolManager::SetGrowthFactor(float factor) {
    std::lock_guard lock(m_mutex);
    if (factor < 1.0f) factor = 1.0f;
    m_growthFactor = factor;
}

}

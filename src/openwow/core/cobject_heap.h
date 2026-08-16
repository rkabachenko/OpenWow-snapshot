
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace openwow::core {

struct PoolBlock {
    void* data_ptr = nullptr;
    std::byte* freelist_ptr = nullptr;
    std::uint32_t alloc_count = 0;
};

class CObjectHeap {
 public:
    CObjectHeap() = default;
    ~CObjectHeap();

    CObjectHeap(const CObjectHeap&) = delete;
    CObjectHeap& operator=(const CObjectHeap&) = delete;
    CObjectHeap(CObjectHeap&&) noexcept;
    CObjectHeap& operator=(CObjectHeap&&) noexcept;

    bool Allocate(std::uint32_t* out_index, void** out_ptr, bool zero_init);

    void ReleaseHandle(std::uint32_t object_index);

    void GarbageCollect();

    [[nodiscard]] std::uint32_t CountAllocatedObjects() const;

    [[nodiscard]] std::uint32_t CountActiveBlocks() const;

    [[nodiscard]] std::uint32_t GetUtilization() const;

    void Resize(std::uint32_t new_block_count);

    void Reset(std::uint32_t new_block_count);

    [[nodiscard]] void* GetObjectDataPtr(std::uint32_t object_index) const;

    void SetName(const char* name);
    [[nodiscard]] const char* GetName() const { return name_; }
    [[nodiscard]] std::uint32_t GetObjSize() const { return obj_size_; }
    [[nodiscard]] std::uint32_t GetObjsPerBlock() const { return objs_per_block_; }
    [[nodiscard]] std::uint32_t GetBlockCapacity() const { return block_capacity_; }
    [[nodiscard]] std::uint32_t GetBlockCount() const { return block_count_; }
    [[nodiscard]] std::uint64_t GetTotalAllocs() const { return total_allocs_; }
    [[nodiscard]] bool AllowsReleaseGarbageCollection() const {
        return release_gc_enabled_;
    }

    void SetObjSize(std::uint32_t size) { obj_size_ = size; }
    void SetObjsPerBlock(std::uint32_t count) { objs_per_block_ = count; }
    void SetReleaseGarbageCollectionEnabled(bool enabled) {
        release_gc_enabled_ = enabled;
    }

 private:
    static void FreeBlockStorage(PoolBlock& block);

    static bool PoolInit(PoolBlock& block, std::uint32_t obj_size,
                         std::uint32_t count, const char* alloc_tag);

    static bool PoolAlloc(PoolBlock& block, std::uint32_t obj_size,
                          std::uint32_t count, const char* alloc_tag,
                          std::uint32_t* out_local_index, void** out_ptr,
                          bool zero_init);
    static std::uint32_t ReadFreelistIndex(const PoolBlock& block,
                                           std::uint32_t slot);
    static void WriteFreelistIndex(PoolBlock& block, std::uint32_t slot,
                                   std::uint32_t value);

    void DestroyDescriptorStorage();
    void MoveConstructBlockDescriptorsFrom(CObjectHeap& source);
    bool ReallocateBlockSlots(std::uint32_t new_capacity);
    bool ResetDescriptorSlots(std::uint32_t new_capacity);
    void RecalculateFullBlockCount();
    std::uint32_t ResolveAutoGrowthQuantum(std::uint32_t requested_count);
    PoolBlock* AppendBlockSlot();

    PoolBlock* FindAllocatableBlock();

    PoolBlock* blocks_ = nullptr;
    std::uint32_t block_capacity_ = 0;
    std::uint32_t block_count_ = 0;

    std::uint32_t block_storage_count_ = 0;
    std::uint32_t growth_quantum_ = 0;
    std::uint32_t obj_size_ = 0;
    std::uint32_t objs_per_block_ = 128;
    std::uint32_t full_block_count_ = 0;
    bool has_empty_blocks_ = false;
    std::uint32_t gc_pending_counter_ = 0;
    std::uint32_t last_alloc_block_index_ = 0;
    char name_[80] = {};
    std::uint64_t total_allocs_ = 0;
    bool release_gc_enabled_ = true;
};

class CObjectHeapList {
 public:
    static CObjectHeapList& Instance();

    CObjectHeap* Grow();

    std::uint32_t RegisterType(std::uint32_t obj_size,
                               std::uint32_t objs_per_block,
                               const char* name,
                               bool release_gc_enabled);

    void Shutdown();

    void Lock() { mutex_.lock(); }
    void Unlock() { mutex_.unlock(); }

    [[nodiscard]] std::uint32_t Count() const { return count_; }
    [[nodiscard]] CObjectHeap* GetHeap(std::uint32_t index);
    [[nodiscard]] const CObjectHeap* GetHeap(std::uint32_t index) const;

 private:

    void DumpUsageToDebugTrace() const;

    void SetCapacity(std::uint32_t new_capacity);
    std::uint32_t ResolveAutoGrowthQuantum(std::uint32_t requested_count);

    void DestroyStorageUnlocked();

    CObjectHeapList() = default;
    mutable std::recursive_mutex mutex_;
    CObjectHeap* heaps_ = nullptr;
    std::uint32_t capacity_ = 0;
    std::uint32_t count_ = 0;
    std::uint32_t growth_step_ = 0;
};

void HeapUsage_UnregisterConsoleCmd();

void MemoryStorm_RegisterConsoleCommands();

bool CVar_HeapAllocTracking_Handler(const char* new_value);

void ConsoleCmd_HeapUsage();

void ObjectMgr_InitTypeRegistry();

void ObjectMgr_ReleaseTypeHandle(std::uint32_t heap_index,
                                 std::uint32_t object_index);

bool ObjectMgr_AllocateTypeHandle(std::uint32_t heap_index,
                                  std::uint32_t* out_object_index,
                                  void** out_ptr, bool zero_init);

void* GetObjectDataPtr(std::uint32_t heap_index, std::uint32_t object_index);

}

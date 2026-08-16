
#include "openwow/core/cobject_heap.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/core/object_type_fields.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/ui/game/cvar_system.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <string_view>
#include <utility>

namespace openwow::core {

namespace {
void ResetObjectMgrTypeRegistryState();
}

CObjectHeap::~CObjectHeap() {
    DestroyDescriptorStorage();
}

CObjectHeap::CObjectHeap(CObjectHeap&& other) noexcept {
    MoveConstructBlockDescriptorsFrom(other);
    growth_quantum_ = other.growth_quantum_;
    obj_size_ = other.obj_size_;
    objs_per_block_ = other.objs_per_block_;
    full_block_count_ = other.full_block_count_;
    has_empty_blocks_ = other.has_empty_blocks_;
    gc_pending_counter_ = other.gc_pending_counter_;
    last_alloc_block_index_ = other.last_alloc_block_index_;
    total_allocs_ = other.total_allocs_;
    release_gc_enabled_ = other.release_gc_enabled_;
    std::memcpy(name_, other.name_, sizeof(name_));
    other.block_capacity_ = 0;
    other.block_count_ = 0;
    other.block_storage_count_ = 0;
    other.growth_quantum_ = 0;
    other.obj_size_ = 0;
    other.objs_per_block_ = 0;
    other.full_block_count_ = 0;
    other.has_empty_blocks_ = false;
    other.gc_pending_counter_ = 0;
    other.last_alloc_block_index_ = 0;
    other.total_allocs_ = 0;
    other.release_gc_enabled_ = true;
    std::memset(other.name_, 0, sizeof(other.name_));
}

CObjectHeap& CObjectHeap::operator=(CObjectHeap&& other) noexcept {
    if (this != &other) {
        DestroyDescriptorStorage();
        MoveConstructBlockDescriptorsFrom(other);
        growth_quantum_ = other.growth_quantum_;
        obj_size_ = other.obj_size_;
        objs_per_block_ = other.objs_per_block_;
        full_block_count_ = other.full_block_count_;
        has_empty_blocks_ = other.has_empty_blocks_;
        gc_pending_counter_ = other.gc_pending_counter_;
        last_alloc_block_index_ = other.last_alloc_block_index_;
        release_gc_enabled_ = other.release_gc_enabled_;
        total_allocs_ = other.total_allocs_;
        std::memcpy(name_, other.name_, sizeof(name_));
        other.block_capacity_ = 0;
        other.block_count_ = 0;
        other.block_storage_count_ = 0;
        other.growth_quantum_ = 0;
        other.obj_size_ = 0;
        other.objs_per_block_ = 0;
        other.full_block_count_ = 0;
        other.has_empty_blocks_ = false;
        other.gc_pending_counter_ = 0;
        other.last_alloc_block_index_ = 0;
        other.total_allocs_ = 0;
        other.release_gc_enabled_ = true;
        std::memset(other.name_, 0, sizeof(other.name_));
    }
    return *this;
}

void CObjectHeap::FreeBlockStorage(PoolBlock& block) {
    std::free(block.data_ptr);
    block.data_ptr = nullptr;
    block.freelist_ptr = nullptr;
    block.alloc_count = 0;
}

void CObjectHeap::DestroyDescriptorStorage() {
    for (std::uint32_t i = 0; i < block_count_; ++i) {
        FreeBlockStorage(blocks_[i]);
    }
    delete[] blocks_;
    blocks_ = nullptr;
    block_capacity_ = 0;
    block_count_ = 0;
    block_storage_count_ = 0;
    full_block_count_ = 0;
    has_empty_blocks_ = false;
    gc_pending_counter_ = 0;
    last_alloc_block_index_ = 0;
}

void CObjectHeap::MoveConstructBlockDescriptorsFrom(CObjectHeap& source) {

    Reset(source.block_count_);
    if (source.block_count_ == 0) {
        return;
    }

    if (blocks_ == nullptr) {
        std::abort();
    }

    for (std::uint32_t i = 0; i < source.block_count_; ++i) {
        blocks_[i] = source.blocks_[i];
        source.blocks_[i].data_ptr = nullptr;
        source.blocks_[i].freelist_ptr = nullptr;
    }
    block_count_ = source.block_count_;
}

bool CObjectHeap::PoolInit(PoolBlock& block, std::uint32_t obj_size,
                           std::uint32_t count, const char* alloc_tag) {
    const std::size_t data_bytes = static_cast<std::size_t>(count) * obj_size;
    const std::size_t total_bytes =
        static_cast<std::size_t>(count) *
        (obj_size + sizeof(std::uint32_t));
    block.data_ptr = std::malloc(total_bytes);
    if (!block.data_ptr) {
        return false;
    }

    block.freelist_ptr = reinterpret_cast<std::byte*>(
        static_cast<char*>(block.data_ptr) + data_bytes);

    for (std::uint32_t i = 0; i < count; ++i) {
        WriteFreelistIndex(block, i, i);
    }
    block.alloc_count = 0;
    static_cast<void>(alloc_tag);
    return true;
}

std::uint32_t CObjectHeap::ReadFreelistIndex(const PoolBlock& block,
                                             std::uint32_t slot) {
    std::uint32_t value = 0;
    std::memcpy(&value, block.freelist_ptr + slot * sizeof(value),
                sizeof(value));
    return value;
}

void CObjectHeap::WriteFreelistIndex(PoolBlock& block, std::uint32_t slot,
                                     std::uint32_t value) {
    std::memcpy(block.freelist_ptr + slot * sizeof(value), &value,
                sizeof(value));
}

bool CObjectHeap::PoolAlloc(PoolBlock& block, std::uint32_t obj_size,
                            std::uint32_t count, const char* alloc_tag,
                            std::uint32_t* out_local_index, void** out_ptr,
                            bool zero_init) {
    if (block.alloc_count >= count) {
        return false;
    }
    if (!block.data_ptr && !PoolInit(block, obj_size, count, alloc_tag)) {
        return false;
    }

    const std::uint32_t local_index =
        ReadFreelistIndex(block, block.alloc_count++);
    if (out_local_index) {
        *out_local_index = local_index;
    }

    auto* object_ptr = static_cast<char*>(block.data_ptr) +
                       static_cast<std::size_t>(obj_size) * local_index;
    if (zero_init) {
        std::memset(object_ptr, 0, obj_size);
    }
    if (out_ptr) {
        *out_ptr = object_ptr;
    }
    return true;
}

void CObjectHeap::RecalculateFullBlockCount() {
    full_block_count_ = 0;
    for (std::uint32_t i = 0; i < block_count_; ++i) {
        if (blocks_[i].alloc_count == objs_per_block_) {
            ++full_block_count_;
        }
    }
}

bool CObjectHeap::ReallocateBlockSlots(std::uint32_t new_capacity) {
    PoolBlock* new_blocks = nullptr;
    if (new_capacity != 0) {
        new_blocks = new (std::nothrow) PoolBlock[new_capacity]();
        if (!new_blocks) {
            return false;
        }
    }

    const std::uint32_t preserved_count = std::min(block_count_, new_capacity);
    for (std::uint32_t i = 0; i < preserved_count; ++i) {
        new_blocks[i] = blocks_[i];
    }

    delete[] blocks_;
    blocks_ = new_blocks;
    block_storage_count_ = new_capacity;
    block_capacity_ = new_capacity;
    block_count_ = preserved_count;
    if (last_alloc_block_index_ >= block_count_) {
        last_alloc_block_index_ = 0;
    }
    RecalculateFullBlockCount();
    return true;
}

bool CObjectHeap::ResetDescriptorSlots(std::uint32_t new_capacity) {
    const std::uint32_t storage_count = std::max(block_count_, new_capacity);

    PoolBlock* new_blocks = nullptr;
    if (storage_count != 0) {
        new_blocks = new (std::nothrow) PoolBlock[storage_count]();
        if (!new_blocks) {
            return false;
        }
    }

    delete[] blocks_;
    blocks_ = new_blocks;
    block_storage_count_ = storage_count;
    block_capacity_ = new_capacity;
    return true;
}

std::uint32_t CObjectHeap::ResolveAutoGrowthQuantum(
    std::uint32_t requested_count) {
    if (requested_count >= 0x15) {
        growth_quantum_ = 0x15;
        return 0x15;
    }

    std::uint32_t quantum = requested_count;
    for (std::uint32_t reduced = requested_count & (requested_count - 1);
         reduced != 0; reduced &= reduced - 1) {
        quantum = reduced;
    }
    return quantum == 0 ? 1 : quantum;
}

PoolBlock* CObjectHeap::AppendBlockSlot() {
    const std::uint32_t requested_count = block_count_ + 1;
    if (requested_count > block_capacity_ ||
        requested_count > block_storage_count_) {
        std::uint32_t growth_quantum = growth_quantum_;
        if (growth_quantum == 0) {
            growth_quantum = ResolveAutoGrowthQuantum(requested_count);
        }

        std::uint32_t new_capacity = requested_count;
        const std::uint32_t remainder = requested_count % growth_quantum;
        if (remainder != 0) {
            new_capacity += growth_quantum - remainder;
        }
        if (!ReallocateBlockSlots(new_capacity)) {
            return nullptr;
        }
    }

    PoolBlock* const block = &blocks_[block_count_++];
    *block = {};
    return block;
}

PoolBlock* CObjectHeap::FindAllocatableBlock() {
    PoolBlock* block = nullptr;
    if (last_alloc_block_index_ < block_count_) {
        PoolBlock& candidate = blocks_[last_alloc_block_index_];
        if (candidate.alloc_count != objs_per_block_ && candidate.data_ptr) {
            return &candidate;
        }
    }

    if (block_count_ == full_block_count_) {
        last_alloc_block_index_ = block_count_;
        return AppendBlockSlot();
    }

    for (std::uint32_t i = 0; i < block_count_; ++i) {
        PoolBlock& candidate = blocks_[i];
        if (candidate.alloc_count == objs_per_block_) {
            continue;
        }
        last_alloc_block_index_ = i;
        block = &candidate;
        if (candidate.data_ptr) {
            break;
        }
    }
    return block;
}

bool CObjectHeap::Allocate(std::uint32_t* out_index, void** out_ptr,
                           bool zero_init) {
    PoolBlock* const block = FindAllocatableBlock();
    if (!block) {
        return false;
    }

    std::uint32_t local_index = 0;
    if (!PoolAlloc(*block, obj_size_, objs_per_block_, name_, &local_index,
                   out_ptr, zero_init)) {
        return false;
    }

    if (out_index) {
        *out_index = local_index + last_alloc_block_index_ * objs_per_block_;
    }
    if (block->alloc_count == objs_per_block_) {
        ++full_block_count_;
    }
    ++total_allocs_;
    return true;
}

void CObjectHeap::ReleaseHandle(std::uint32_t object_index) {
    if (objs_per_block_ == 0 || block_count_ == 0 || blocks_ == nullptr) {
        return;
    }

    const std::uint32_t local_index = object_index % objs_per_block_;
    const std::uint32_t block_index = object_index / objs_per_block_;
    if (block_index >= block_count_) {
        return;
    }

    PoolBlock& block = blocks_[block_index];
    if (block.alloc_count == objs_per_block_) {
        --full_block_count_;
    }

    const std::uint32_t alloc_count = block.alloc_count;
    if (alloc_count != 0 && block.freelist_ptr != nullptr) {
        const std::uint32_t next_alloc_count = alloc_count - 1;
        block.alloc_count = next_alloc_count;
        WriteFreelistIndex(block, next_alloc_count, local_index);
    }

    if (!release_gc_enabled_) {
        return;
    }

    if (block.data_ptr != nullptr && block.alloc_count == 0) {
        has_empty_blocks_ = true;
    }
    if (!has_empty_blocks_) {
        return;
    }

    const std::uint32_t gc_interval_mask = (objs_per_block_ - 1u) >> 2u;
    const std::uint32_t gc_counter = gc_pending_counter_;
    const bool should_collect = (gc_interval_mask & gc_counter) == 0;
    gc_pending_counter_ = gc_counter + 1u;
    if (should_collect) {
        GarbageCollect();
    }
}

void CObjectHeap::GarbageCollect() {
    if (block_count_ == 0) {
        has_empty_blocks_ = false;
        return;
    }

    std::uint32_t total_capacity = 0;
    std::uint32_t total_used = 0;
    std::uint32_t empty_count = 0;

    for (std::uint32_t i = 0; i < block_count_; ++i) {
        const PoolBlock& block = blocks_[i];
        if (!block.data_ptr) continue;
        total_capacity += objs_per_block_;
        if (block.alloc_count > 0) {
            total_used += block.alloc_count;
        } else {
            empty_count++;
        }
    }

    if (empty_count == 0) {
        has_empty_blocks_ = false;
        return;
    }

    std::uint32_t threshold = total_used + (3 * objs_per_block_) / 2;
    if (threshold >= total_capacity) {
        has_empty_blocks_ = (empty_count > 0);
        return;
    }

    for (std::uint32_t i = block_count_; i != 0 && empty_count > 0; --i) {
        if (threshold >= total_capacity) break;
        PoolBlock& block = blocks_[i - 1];
        if (!block.data_ptr || block.alloc_count > 0) continue;

        FreeBlockStorage(block);
        total_capacity -= objs_per_block_;
        empty_count--;
        gc_pending_counter_ = 0;
    }

    has_empty_blocks_ = (empty_count > 0);
}

std::uint32_t CObjectHeap::CountAllocatedObjects() const {
    std::uint32_t total = 0;
    for (std::uint32_t i = 0; i < block_count_; ++i) {
        total += blocks_[i].alloc_count;
    }
    return total;
}

std::uint32_t CObjectHeap::CountActiveBlocks() const {
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < block_count_; ++i) {
        if (blocks_[i].data_ptr) {
            ++count;
        }
    }
    return count;
}

std::uint32_t CObjectHeap::GetUtilization() const {
    std::uint32_t capacity = 0;
    std::uint32_t allocated = 0;
    for (std::uint32_t i = 0; i < block_count_; ++i) {
        const PoolBlock& block = blocks_[i];
        if (block.data_ptr) {
            capacity += objs_per_block_;
            allocated += block.alloc_count;
        }
    }
    if (capacity == 0) return 0;
    return 100 * allocated / capacity;
}

void CObjectHeap::Resize(std::uint32_t new_block_count) {
    if (new_block_count < block_count_) {
        for (std::uint32_t i = new_block_count; i < block_count_; ++i) {
            FreeBlockStorage(blocks_[i]);
        }
        block_count_ = new_block_count;
        RecalculateFullBlockCount();
    }
    (void)ReallocateBlockSlots(new_block_count);
    has_empty_blocks_ = false;
    gc_pending_counter_ = 0;
}

void CObjectHeap::Reset(std::uint32_t new_block_count) {
    for (std::uint32_t i = 0; i < block_count_; ++i) {
        FreeBlockStorage(blocks_[i]);
    }

    if (new_block_count != block_count_) {
        (void)ResetDescriptorSlots(new_block_count);
    }
}

void* CObjectHeap::GetObjectDataPtr(std::uint32_t object_index) const {

    std::uint32_t block_idx = object_index / objs_per_block_;
    std::uint32_t local_idx = object_index % objs_per_block_;

    if (block_idx >= block_count_ || block_idx >= block_storage_count_ ||
        !blocks_[block_idx].data_ptr)
        return nullptr;

    return static_cast<char*>(blocks_[block_idx].data_ptr) +
           static_cast<std::size_t>(local_idx) * obj_size_;
}

void CObjectHeap::SetName(const char* n) {
    std::strncpy(name_, n, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

CObjectHeapList& CObjectHeapList::Instance() {
    static CObjectHeapList instance;
    return instance;
}

std::uint32_t CObjectHeapList::ResolveAutoGrowthQuantum(
    std::uint32_t requested_count) {
    if (requested_count >= 8) {
        growth_step_ = 8;
        return 8;
    }

    std::uint32_t quantum = requested_count;
    while ((quantum & (quantum - 1)) != 0) {
        quantum &= (quantum - 1);
    }
    return quantum == 0 ? 1 : quantum;
}

void CObjectHeapList::SetCapacity(std::uint32_t new_capacity) {
    if (new_capacity == capacity_) {
        return;
    }

    CObjectHeap* new_storage = nullptr;
    if (new_capacity != 0) {
        new_storage = static_cast<CObjectHeap*>(
            ::operator new[](sizeof(CObjectHeap) *
                             static_cast<std::size_t>(new_capacity)));
    }

    const std::uint32_t preserved_count = std::min(count_, new_capacity);
    for (std::uint32_t i = 0; i < preserved_count; ++i) {
        new (&new_storage[i]) CObjectHeap(std::move(heaps_[i]));
    }

    for (std::uint32_t i = 0; i < count_; ++i) {
        heaps_[i].~CObjectHeap();
    }

    ::operator delete[](heaps_);
    heaps_ = new_storage;
    capacity_ = new_capacity;
    count_ = preserved_count;
}

CObjectHeap* CObjectHeapList::Grow() {
    const std::uint32_t requested_count = count_ + 1;
    if (requested_count > capacity_) {
        std::uint32_t growth_quantum = growth_step_;
        if (growth_quantum == 0) {
            growth_quantum = ResolveAutoGrowthQuantum(requested_count);
        }

        std::uint32_t new_capacity = requested_count;
        const std::uint32_t remainder = requested_count % growth_quantum;
        if (remainder != 0) {
            new_capacity += growth_quantum - remainder;
        }

        SetCapacity(new_capacity);
    }

    auto* heap = &heaps_[count_];
    new (heap) CObjectHeap();
    ++count_;
    return heap;
}

void CObjectHeapList::DestroyStorageUnlocked() {
    for (std::uint32_t i = 0; i < count_; ++i) {
        heaps_[i].~CObjectHeap();
    }
    ::operator delete[](heaps_);
    heaps_ = nullptr;
}

std::uint32_t CObjectHeapList::RegisterType(std::uint32_t obj_size,
                                            std::uint32_t objs_per_block,
                                            const char* name,
                                            bool release_gc_enabled) {
    if (obj_size == 0) {
        SErrSetLastError(87);
        return 0;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::uint32_t index = count_;
    auto* heap = Grow();
    heap->SetObjSize(obj_size);
    heap->SetObjsPerBlock(objs_per_block);
    heap->SetName(name);
    heap->SetReleaseGarbageCollectionEnabled(release_gc_enabled);
    return index;
}

void CObjectHeapList::Shutdown() {
    DumpUsageToDebugTrace();
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    DestroyStorageUnlocked();
    capacity_ = 0;
    count_ = 0;

    ResetObjectMgrTypeRegistryState();
}

CObjectHeap* CObjectHeapList::GetHeap(std::uint32_t index) {
    if (index >= count_) return nullptr;
    return &heaps_[index];
}

const CObjectHeap* CObjectHeapList::GetHeap(std::uint32_t index) const {
    if (index >= count_) return nullptr;
    return &heaps_[index];
}

void CObjectHeapList::DumpUsageToDebugTrace() const {

}

bool CVar_HeapAllocTracking_Handler(const char* new_value) {
    const bool enabled =
        ParseSignedDecimalLikeSub76F0D0(new_value) != 0u;
    SetLogFlags(enabled ? 0x20u : 0u, 0x20u);
    ida::ConsoleAddLine(enabled ? "Heap tracking switched ON"
                                : "Heap tracking switched OFF",
                        ida::COLOR_DEFAULT);
    return true;
}

void ConsoleCmd_HeapUsage() {
    auto& list = CObjectHeapList::Instance();
    list.Lock();

    std::uint32_t count = list.Count();
    std::uint32_t total_obj_bytes = 0;
    std::uint32_t total_heap_bytes = 0;

    std::ostringstream oss;
    oss << count << " Heaps in use:\n";

    for (std::uint32_t i = 0; i < count; ++i) {
        auto* heap = list.GetHeap(i);
        if (!heap) continue;

        std::uint32_t blocks = heap->CountActiveBlocks();
        std::uint32_t objs = heap->CountAllocatedObjects();
        std::uint32_t util = heap->GetUtilization();
        std::uint32_t obj_size = heap->GetObjSize();
        std::uint32_t opb = heap->GetObjsPerBlock();
        std::uint32_t obj_bytes = objs * obj_size;
        std::uint32_t heap_bytes = blocks * opb * obj_size;
        std::uint64_t total_allocs = heap->GetTotalAllocs();
        const int display_total_allocs =
            total_allocs >= 0xFFFFFFFFull
                ? -1
                : static_cast<int>(static_cast<std::uint32_t>(total_allocs));

        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "    \"%24s\" %3u blocks, %5u objs per block (%6u bytes per obj): "
            "%6u objs allocated (%3u%%) - Total bytes: %8.3fMb (%8.3fMb), "
            "Total Allocs: %d\n",
            heap->GetName(), blocks, opb, obj_size, objs, util,
            static_cast<double>(obj_bytes) * 0.00000095367432,
            static_cast<double>(heap_bytes) * 0.00000095367432,
            display_total_allocs);
        oss << buf;

        total_obj_bytes += obj_bytes;
        total_heap_bytes += heap_bytes;
    }

    char summary[256];
    std::snprintf(summary, sizeof(summary),
        "%4.3fMb total object bytes used, %4.3fMb bytes allocated for heaps",
        static_cast<double>(total_obj_bytes) * 0.00000095367432,
        static_cast<double>(total_heap_bytes) * 0.00000095367432);
    oss << summary;

    list.Unlock();

    std::string output = oss.str();
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty()) {
            ida::ConsoleAddLine(line, ida::COLOR_DEFAULT);
        }
    }
}

void HeapUsage_UnregisterConsoleCmd() {
    openwow::debug::DebugConsole::Get().UnregisterCommand("HeapUsage");
}

void MemoryStorm_RegisterConsoleCommands() {
    auto& console = openwow::debug::DebugConsole::Get();
    console.RegisterRawCommand(
        "HeapUsage", {}, [](std::string_view) -> std::string {
            ConsoleCmd_HeapUsage();
            return {};
        }, {}, 4);
    console.RegisterRawCommand(
        "MemUsage", {}, [](std::string_view) -> std::string {
            return {};
        }, {}, 4);
    console.RegisterRawCommand(
        "HeapUsage2", {}, [](std::string_view) -> std::string {
            return {};
        }, {}, 4);

    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    cvars.RegisterNativeCVar(
        "heapAllocTracking", "1", openwow::ui::game::CVarFlags::None,
        "Enables/disables allocation tracking & dumping in SMemMalloc",
        [](const std::string&, const std::string&, const std::string& new_value) {
            return CVar_HeapAllocTracking_Handler(new_value.c_str());
        }, 0.0f, 0.0f, 4);
}

void* GetObjectDataPtr(std::uint32_t heap_index, std::uint32_t object_index) {
    auto& list = CObjectHeapList::Instance();
    list.Lock();
    auto* heap = list.GetHeap(heap_index);
    void* result = nullptr;
    if (heap) {
        result = heap->GetObjectDataPtr(object_index);
    }
    list.Unlock();
    return result;
}

void ObjectMgr_ReleaseTypeHandle(std::uint32_t heap_index,
                                 std::uint32_t object_index) {
    auto& list = CObjectHeapList::Instance();
    list.Lock();
    if (auto* heap = list.GetHeap(heap_index); heap != nullptr) {
        heap->ReleaseHandle(object_index);
    }
    list.Unlock();
}

bool ObjectMgr_AllocateTypeHandle(std::uint32_t heap_index,
                                  std::uint32_t* out_object_index,
                                  void** out_ptr, bool zero_init) {
    if (out_object_index == nullptr) {
        SErrSetLastError(87);
        return false;
    }

    *out_object_index = 0;

    auto& list = CObjectHeapList::Instance();
    list.Lock();
    auto* heap = list.GetHeap(heap_index);
    if (heap == nullptr) {
        list.Unlock();
        SErrSetLastError(87);
        return false;
    }

    std::uint32_t object_index = 0;
    void* object_ptr = nullptr;
    if (!heap->Allocate(&object_index, &object_ptr, zero_init)) {
        list.Unlock();
        return false;
    }

    if (out_ptr != nullptr) {
        *out_ptr = object_ptr;
    }
    list.Unlock();

    *out_object_index = object_index;
    return true;
}

namespace {
struct ObjectTypeInfo {
    std::uint32_t obj_size;
    std::uint32_t objs_per_block;
    const char* name;
};

constexpr std::array<ObjectTypeInfo, 8> kObjectTypes{{
    {0u, 0u, ""},
    {0x5A8u, 0x200u, "CGItem_C"},
    {0xB88u, 0x20u, "CGContainer_C"},
    {0x1450u, 0x40u, "CGUnit_C"},
    {0x2314u, 0x40u, "CGPlayer_C"},
    {0x27Cu, 0x40u, "CGGameObject_C"},
    {0x1ACu, 0x20u, "CGDynamicObject_C"},
    {0x338u, 0x20u, "CGCorpse_C"},
}};

constexpr std::uint32_t kInvalidTypeHeapIndex =
    std::numeric_limits<std::uint32_t>::max();

std::array<std::uint32_t, 8> g_type_heap_indices = [] {
    std::array<std::uint32_t, 8> heap_indices{};
    heap_indices.fill(kInvalidTypeHeapIndex);
    return heap_indices;
}();
bool g_type_registry_initialized = false;

void ResetObjectMgrTypeRegistryState() {
    for (auto& heap_index : g_type_heap_indices) {
        heap_index = kInvalidTypeHeapIndex;
    }
    g_type_registry_initialized = false;
}

void ObjUsage_PrintObjectManagerListStatus() {
    const auto status = GetRetailDebugObjectManagerStatus();
    if (!status.has_value()) {
        return;
    }

    const std::uint32_t pending_free_count = status->pending_free_count;
    const std::uint32_t tracked_count = status->tracked_count;
    const std::uint32_t active_count =
        tracked_count >= pending_free_count ? tracked_count - pending_free_count : 0u;

    const std::uint32_t visible_count = active_count;

    ida::ConsoleAddLine("Object manager list status:", 7);
    ida::ConsoleLogColored(
        "    Active objects:              %u objects (%u visible)", 7,
        active_count, visible_count);
    ida::ConsoleLogColored(
        "    Objects waiting to be freed: %u objects", 7,
        pending_free_count);
}

void RegisterObjectManagerConsoleCommands() {
    auto& console = openwow::debug::DebugConsole::Get();
    console.RegisterRawCommand(
        "ObjUsage", {},
        [](std::string_view) -> std::string {
            ObjUsage_PrintObjectManagerListStatus();
            return {};
        });
}

}

void ObjectMgr_InitTypeRegistry() {
    if (!g_type_registry_initialized) {
        auto& list = CObjectHeapList::Instance();
        for (std::uint32_t i = 1; i < kObjectTypes.size(); ++i) {
            g_type_heap_indices[i] = list.RegisterType(
                kObjectTypes[i].obj_size,
                kObjectTypes[i].objs_per_block,
                kObjectTypes[i].name,
                true);
        }
        g_type_registry_initialized = true;
    }

    ObjectMgr_InitFieldDescriptorTables();
    RegisterObjectManagerConsoleCommands();
}

}

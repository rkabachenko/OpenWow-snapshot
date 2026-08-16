#include "openwow/net/serialization/wdatastore.h"

#include "openwow/core/storm_string.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace openwow::net {
namespace {

constexpr char kWDataStoreSourceFile[] = ".\\WDataStore.cpp";
constexpr char kCDataStoreSourceFile[] =
    "d:\\buildserver\\wow\\1\\work\\wow-code\\branches\\wow-patch-3_3_5_a-bnet\\engine\\source\\base\\CDataStore.h";
constexpr int kCDataStoreAllocLine = 0xF6;
constexpr int kCDataStoreReallocLine = 0x118;
constexpr int kWDataStoreHeapAllocLine = 0xBA;
constexpr int kWDataStoreHeapFreeLine = 0xCD;
constexpr int kWDataStoreGrowHeapReallocLine = 0x93;
constexpr int kWDataStoreGrowPoolToHeapLine = 0x97;

struct PoolBlockHeader {
  std::uint32_t token = 0;
  std::uint8_t reserved[kWDataStorePooledHeaderSize -
                        kWDataStorePooledHandleBytes]{};
};

static_assert(sizeof(PoolBlockHeader) == kWDataStorePooledHeaderSize);

struct PoolConfig {
  WDataStoreBufferTier tier;
  std::size_t payload_capacity;
  std::size_t initial_count;
};

constexpr std::array<PoolConfig, 3> kPoolConfigs{{
    {WDataStoreBufferTier::SmallPool, kWDataStoreSmallBufferCapacity, 0x100},
    {WDataStoreBufferTier::LargePool, kWDataStoreLargeBufferCapacity, 0x10},
    {WDataStoreBufferTier::SuperPool, kWDataStoreSuperBufferCapacity, 0x4},
}};

class PoolRegistry {
public:
  void Init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
      return;
    }

    for (std::size_t i = 0; i < kPoolConfigs.size(); ++i) {
      pools_[i].Init(kPoolConfigs[i].tier, kPoolConfigs[i].payload_capacity,
                     kPoolConfigs[i].initial_count);
    }
    initialized_ = true;
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
      return;
    }

    for (auto &pool : pools_) {
      pool.Clear();
    }
    initialized_ = false;
  }

  [[nodiscard]] BufferPool *ForTier(WDataStoreBufferTier tier) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
      return nullptr;
    }

    switch (tier) {
    case WDataStoreBufferTier::SmallPool:
      return &pools_[0];
    case WDataStoreBufferTier::LargePool:
      return &pools_[1];
    case WDataStoreBufferTier::SuperPool:
      return &pools_[2];
    case WDataStoreBufferTier::Heap:
      return nullptr;
    }
    return nullptr;
  }

  [[nodiscard]] std::size_t GetPoolFreeCount(WDataStoreBufferTier tier) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
      return 0;
    }

    switch (tier) {
    case WDataStoreBufferTier::SmallPool:
      return pools_[0].GetFreeCount();
    case WDataStoreBufferTier::LargePool:
      return pools_[1].GetFreeCount();
    case WDataStoreBufferTier::SuperPool:
      return pools_[2].GetFreeCount();
    case WDataStoreBufferTier::Heap:
      return 0;
    }

    return 0;
  }

private:
  std::array<BufferPool, 3> pools_{};
  mutable std::mutex mutex_;
  bool initialized_{false};
};

[[nodiscard]] PoolRegistry &GetPoolRegistry() {
  static PoolRegistry registry;
  return registry;
}

void EnsurePoolsInitialized() {
  GetPoolRegistry().Init();
}

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) {
  if (alignment == 0) {
    return value;
  }

  const auto remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }
  return value + alignment - remainder;
}

}

void *WDataStore_AllocHelper(std::size_t size, const char *source_file,
                             int source_line) {
  if (size == 0) {
    return nullptr;
  }

  const char *resolved_source =
      source_file != nullptr ? source_file : kCDataStoreSourceFile;
  const int resolved_line =
      source_file != nullptr ? source_line : kCDataStoreAllocLine;
  return core::SMemAlloc(size, resolved_source, resolved_line, 0);
}

void *WDataStore_ReallocHelper(void *block, std::size_t new_size,
                               const char *source_file, int source_line) {
  const char *resolved_source =
      source_file != nullptr ? source_file : kCDataStoreSourceFile;
  const int resolved_line =
      source_file != nullptr ? source_line : kCDataStoreReallocLine;
  return core::SMemReAlloc(block, new_size, resolved_source, resolved_line, 0);
}

BufferPool::~BufferPool() {
  Clear();
}

void BufferPool::Init(WDataStoreBufferTier tier, std::size_t payload_capacity,
                      std::size_t initial_count) {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearUnlocked();
  tier_ = tier;
  payload_capacity_ = payload_capacity;
  block_size_ = payload_capacity + kWDataStorePooledHeaderSize;
  free_list_.reserve(initial_count);
  for (std::size_t i = free_list_.size(); i < initial_count; ++i) {
    auto *raw_block = AllocateRawBlock();
    if (!raw_block) {
      break;
    }
    free_list_.push_back(raw_block);
  }
}

void BufferPool::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  ClearUnlocked();
}

std::uint8_t *BufferPool::AcquirePayload() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::uint8_t *raw_block = nullptr;
  if (!free_list_.empty()) {
    raw_block = free_list_.back();
    free_list_.pop_back();
  } else {
    raw_block = AllocateRawBlock();
  }

  if (!raw_block) {
    return nullptr;
  }

  return raw_block + kWDataStorePooledHeaderSize;
}

void BufferPool::ReleasePayload(std::uint8_t *payload) {
  if (!payload) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  free_list_.push_back(payload - kWDataStorePooledHeaderSize);
}

std::size_t BufferPool::GetFreeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return free_list_.size();
}

std::uint8_t *BufferPool::AllocateRawBlock() const {
  auto *raw_block =
      static_cast<std::uint8_t *>(core::SMemAlloc(block_size_, kWDataStoreSourceFile, -2, 0));
  if (!raw_block) {
    return nullptr;
  }

  auto *header = reinterpret_cast<PoolBlockHeader *>(raw_block);
  header->token = 0;
  return raw_block;
}

void BufferPool::ClearUnlocked() {
  for (auto *raw_block : free_list_) {
    core::SMemFree(raw_block, kWDataStoreSourceFile, -2, 0);
  }
  free_list_.clear();
}

WDataStoreBufferTier WDataStore_ClassifyBufferSize(std::size_t size) {
  if (size <= kWDataStoreSmallBufferCapacity) {
    return WDataStoreBufferTier::SmallPool;
  }
  if (size <= kWDataStoreLargeBufferCapacity) {
    return WDataStoreBufferTier::LargePool;
  }
  if (size <= kWDataStoreSuperBufferCapacity) {
    return WDataStoreBufferTier::SuperPool;
  }
  return WDataStoreBufferTier::Heap;
}

std::size_t WDataStore_ResolveGrowthCapacity(std::size_t current_capacity,
                                             std::size_t required_size,
                                             std::size_t growth_quantum) {
  if (required_size < current_capacity) {
    return current_capacity;
  }

  switch (WDataStore_ClassifyBufferSize(required_size)) {
  case WDataStoreBufferTier::SmallPool:
    return kWDataStoreSmallBufferCapacity;
  case WDataStoreBufferTier::LargePool:
    return kWDataStoreLargeBufferCapacity;
  case WDataStoreBufferTier::SuperPool:
    return kWDataStoreSuperBufferCapacity;
  case WDataStoreBufferTier::Heap:
    return AlignUp(required_size, growth_quantum);
  }

  return required_size;
}

void WDataStore_InitPools() {
  GetPoolRegistry().Init();
}

void WDataStore_ShutdownPools() {
  GetPoolRegistry().Shutdown();
}

std::size_t WDataStore_GetPoolFreeCount(WDataStoreBufferTier tier) {
  return GetPoolRegistry().GetPoolFreeCount(tier);
}

std::uint8_t *WDataStore_AllocBuffer(std::size_t size) {
  const auto tier = WDataStore_ClassifyBufferSize(size);
  if (tier == WDataStoreBufferTier::Heap) {
    return static_cast<std::uint8_t *>(core::SMemAlloc(
        size, kWDataStoreSourceFile, kWDataStoreHeapAllocLine, 0));
  }

  if (auto *pool = GetPoolRegistry().ForTier(tier)) {
    return pool->AcquirePayload();
  }
  return nullptr;
}

void WDataStore_FreeBuffer(std::uint8_t *ptr, std::size_t size) {
  if (!ptr) {
    return;
  }

  const auto tier = WDataStore_ClassifyBufferSize(size);
  if (tier == WDataStoreBufferTier::Heap) {
    core::SMemFree(ptr, kWDataStoreSourceFile, kWDataStoreHeapFreeLine, 0);
    return;
  }

  if (auto *pool = GetPoolRegistry().ForTier(tier)) {
    pool->ReleasePayload(ptr);
  }
}

std::uint8_t *WDataStore_GrowBuffer(std::uint8_t *old_ptr, std::size_t current_capacity,
                                    std::size_t required_size, std::size_t preserved_size,
                                    std::size_t growth_quantum) {
  EnsurePoolsInitialized();

  const auto target_capacity =
      WDataStore_ResolveGrowthCapacity(current_capacity, required_size, growth_quantum);

  if (!old_ptr) {
    if (WDataStore_ClassifyBufferSize(target_capacity) == WDataStoreBufferTier::Heap) {
      return static_cast<std::uint8_t *>(WDataStore_ReallocHelper(
          nullptr, target_capacity, kWDataStoreSourceFile,
          kWDataStoreGrowHeapReallocLine));
    }
    return WDataStore_AllocBuffer(target_capacity);
  }

  const auto current_tier = WDataStore_ClassifyBufferSize(current_capacity);
  const auto target_tier = WDataStore_ClassifyBufferSize(target_capacity);

  if (current_tier == WDataStoreBufferTier::Heap && target_tier == WDataStoreBufferTier::Heap) {
    return static_cast<std::uint8_t *>(WDataStore_ReallocHelper(
        old_ptr, target_capacity, kWDataStoreSourceFile,
        kWDataStoreGrowHeapReallocLine));
  }

  std::uint8_t *new_ptr = nullptr;
  if (target_tier == WDataStoreBufferTier::Heap) {
    new_ptr = static_cast<std::uint8_t *>(WDataStore_AllocHelper(
        target_capacity, kWDataStoreSourceFile, kWDataStoreGrowPoolToHeapLine));
  } else {
    new_ptr = WDataStore_AllocBuffer(target_capacity);
  }
  if (!new_ptr) {
    return nullptr;
  }

  if (preserved_size != 0) {
    const auto bytes_to_copy = std::min(preserved_size, current_capacity);
    std::memcpy(new_ptr, old_ptr, bytes_to_copy);
  }

  WDataStore_FreeBuffer(old_ptr, current_capacity);
  return new_ptr;
}

}

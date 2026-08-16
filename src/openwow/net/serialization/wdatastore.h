#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace openwow::net {

inline constexpr std::size_t kWDataStorePooledHeaderSize = 0x20;
inline constexpr std::size_t kWDataStorePooledHandleBytes = sizeof(std::uint32_t);
inline constexpr std::size_t kWDataStorePooledPrefixWritableBytes =
    kWDataStorePooledHeaderSize - kWDataStorePooledHandleBytes;
inline constexpr std::size_t kWDataStoreSmallBufferCapacity = 0x300;
inline constexpr std::size_t kWDataStoreLargeBufferCapacity = 0x4000;
inline constexpr std::size_t kWDataStoreSuperBufferCapacity = 0x40040;
inline constexpr std::size_t kWDataStoreDefaultGrowthQuantum = 0x100;

enum class WDataStoreBufferTier : std::uint8_t {
  SmallPool,
  LargePool,
  SuperPool,
  Heap,
};

class BufferPool {
public:
  BufferPool() = default;
  ~BufferPool();

  BufferPool(const BufferPool &) = delete;
  BufferPool &operator=(const BufferPool &) = delete;
  BufferPool(BufferPool &&) = delete;
  BufferPool &operator=(BufferPool &&) = delete;

  void Init(WDataStoreBufferTier tier, std::size_t payload_capacity, std::size_t initial_count);
  void Clear();
  [[nodiscard]] std::uint8_t *AcquirePayload();
  void ReleasePayload(std::uint8_t *payload);
  [[nodiscard]] std::size_t GetFreeCount() const;

private:
  [[nodiscard]] std::uint8_t *AllocateRawBlock() const;
  void ClearUnlocked();

  WDataStoreBufferTier tier_{WDataStoreBufferTier::SmallPool};
  std::size_t payload_capacity_{0};
  std::size_t block_size_{0};
  std::vector<std::uint8_t *> free_list_;
  mutable std::mutex mutex_;
};

[[nodiscard]] WDataStoreBufferTier WDataStore_ClassifyBufferSize(std::size_t size);

[[nodiscard]] std::size_t
WDataStore_ResolveGrowthCapacity(std::size_t current_capacity, std::size_t required_size,
                                 std::size_t growth_quantum = kWDataStoreDefaultGrowthQuantum);

[[nodiscard]] void *WDataStore_AllocHelper(std::size_t size,
                                           const char *source_file = nullptr,
                                           int source_line = 0);
[[nodiscard]] void *WDataStore_ReallocHelper(void *block, std::size_t new_size,
                                             const char *source_file = nullptr,
                                             int source_line = 0);

void WDataStore_InitPools();
void WDataStore_ShutdownPools();

[[nodiscard]] std::size_t WDataStore_GetPoolFreeCount(WDataStoreBufferTier tier);

[[nodiscard]] std::uint8_t *WDataStore_AllocBuffer(std::size_t size);
void WDataStore_FreeBuffer(std::uint8_t *ptr, std::size_t size);

[[nodiscard]] std::uint8_t *
WDataStore_GrowBuffer(std::uint8_t *old_ptr, std::size_t current_capacity,
                      std::size_t required_size, std::size_t preserved_size,
                      std::size_t growth_quantum = kWDataStoreDefaultGrowthQuantum);

}

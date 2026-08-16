#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::core {

class CMapTable {
 public:
  using BucketChain = std::vector<std::uint32_t>;

  CMapTable() = default;

  void InitWithBuckets();

  void Reset();

  void Clear();

  [[nodiscard]] bool GrowAndRehash(std::uint32_t bucket_index);

  void Rehash(std::uint32_t new_bucket_count);

  [[nodiscard]] bool InsertHashedKey(std::uint32_t key);

  [[nodiscard]] bool EraseHashedKey(std::uint32_t key);

  [[nodiscard]] bool initialized() const {
    return bucket_mask_ != kUninitializedMask;
  }

  [[nodiscard]] std::uint32_t bucket_mask() const { return bucket_mask_; }
  [[nodiscard]] std::uint32_t bucket_slot_count() const {
    return initialized() ? static_cast<std::uint32_t>(buckets_.size()) : 0u;
  }
  [[nodiscard]] std::uint32_t probe_counter() const { return probe_counter_; }
  [[nodiscard]] std::size_t entry_count() const { return entry_count_; }
  [[nodiscard]] std::size_t bucket_chain_length(
      const std::uint32_t bucket_index) const;
  [[nodiscard]] const BucketChain& bucket_chain(
      const std::uint32_t bucket_index) const;

  static constexpr std::uint32_t kUninitializedMask = 0xFFFFFFFFu;
  static constexpr std::uint32_t kInitialBucketMask = 3u;
  static constexpr std::uint32_t kInitialBucketCount = 4u;
  static constexpr std::uint32_t kMaxBucketMask = 0x1FFFu;
  static constexpr std::uint32_t kRehashProbeThreshold = 13u;

 private:
  std::vector<BucketChain> buckets_;
  std::uint32_t bucket_mask_ = kUninitializedMask;
  std::uint32_t probe_counter_ = 0;
  std::size_t entry_count_ = 0;
};

}

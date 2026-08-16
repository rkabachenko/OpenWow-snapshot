
#include "openwow/core/cmap_hashtable.h"

#include <algorithm>
#include <utility>

namespace openwow::core {

namespace {

void InsertBucketHead(CMapTable::BucketChain& bucket, const std::uint32_t key) {
  bucket.insert(bucket.begin(), key);
}

}

void CMapTable::InitWithBuckets() {

  buckets_.assign(kInitialBucketCount, {});
  bucket_mask_ = kInitialBucketMask;
  probe_counter_ = 0;
  entry_count_ = 0;
}

void CMapTable::Reset() {

  std::vector<BucketChain>().swap(buckets_);
  bucket_mask_ = kUninitializedMask;
  probe_counter_ = 0;
  entry_count_ = 0;
}

void CMapTable::Clear() {
  for (BucketChain& bucket : buckets_) {
    bucket.clear();
  }

  probe_counter_ = 0;
  entry_count_ = 0;
}

bool CMapTable::GrowAndRehash(const std::uint32_t bucket_index) {
  if (!initialized() || bucket_index >= buckets_.size()) {
    return false;
  }

  if (bucket_mask_ >= kMaxBucketMask) {
    return false;
  }

  if (probe_counter_ <= 3u) {
    probe_counter_ = 0;
  } else {
    probe_counter_ -= 3u;
  }

  for (const std::uint32_t key : buckets_[bucket_index]) {
    (void)key;
    ++probe_counter_;
    if (probe_counter_ > kRehashProbeThreshold) {
      const std::uint32_t new_bucket_count = bucket_mask_ * 2u + 2u;
      probe_counter_ = 0;
      Rehash(new_bucket_count);
      return true;
    }
  }

  return false;
}

void CMapTable::Rehash(const std::uint32_t new_bucket_count) {
  std::vector<BucketChain> old_buckets = std::move(buckets_);
  buckets_.assign(new_bucket_count, {});
  bucket_mask_ = new_bucket_count - 1u;

  for (const BucketChain& bucket : old_buckets) {
    for (const std::uint32_t key : bucket) {
      buckets_[key & bucket_mask_].push_back(key);
    }
  }
}

bool CMapTable::InsertHashedKey(const std::uint32_t key) {
  if (!initialized()) {
    InitWithBuckets();
  }

  std::uint32_t bucket_index = key & bucket_mask_;
  const bool rehashed = GrowAndRehash(bucket_index);
  if (rehashed) {
    bucket_index = key & bucket_mask_;
  }

  InsertBucketHead(buckets_[bucket_index], key);
  ++entry_count_;
  return rehashed;
}

bool CMapTable::EraseHashedKey(const std::uint32_t key) {
  if (!initialized()) {
    return false;
  }

  BucketChain &bucket = buckets_[key & bucket_mask_];
  const auto it = std::find(bucket.begin(), bucket.end(), key);
  if (it == bucket.end()) {
    return false;
  }

  bucket.erase(it);
  --entry_count_;
  return true;
}

std::size_t CMapTable::bucket_chain_length(
    const std::uint32_t bucket_index) const {
  if (!initialized() || bucket_index >= buckets_.size()) {
    return 0;
  }
  return buckets_[bucket_index].size();
}

const CMapTable::BucketChain& CMapTable::bucket_chain(
    const std::uint32_t bucket_index) const {
  static const BucketChain kEmptyBucket;
  if (!initialized() || bucket_index >= buckets_.size()) {
    return kEmptyBucket;
  }
  return buckets_[bucket_index];
}

}

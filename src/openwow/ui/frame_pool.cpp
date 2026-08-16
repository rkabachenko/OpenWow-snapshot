
#include "openwow/ui/frame_pool.h"

#include <algorithm>

namespace openwow::ui {

void FramePool::Create(const std::string& poolName, std::uint32_t initialSize,
                       const std::string& frameType,
                       const std::string& parent) {
  auto& pool = pools_[poolName];
  pool.frameType = frameType;
  pool.parent = parent;
  pool.nextId = 0;
  pool.entries.clear();
  pool.entries.reserve(initialSize);

  for (std::uint32_t i = 0; i < initialSize; ++i) {
    FramePoolEntry entry;
    entry.frameName = poolName + "_Frame" + std::to_string(pool.nextId++);
    entry.frameType = frameType;
    entry.parent = parent;
    entry.isActive = false;
    entry.createTime = 0.0f;
    pool.entries.push_back(std::move(entry));
  }
}

std::optional<FramePoolEntry> FramePool::Acquire(const std::string& poolName) {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return std::nullopt;

  auto& pool = it->second;

  for (auto& entry : pool.entries) {
    if (!entry.isActive) {
      entry.isActive = true;
      return entry;
    }
  }

  if (pool.maxSize > 0 &&
      static_cast<std::uint32_t>(pool.entries.size()) >= pool.maxSize) {
    return std::nullopt;
  }

  FramePoolEntry entry;
  entry.frameName = poolName + "_Frame" + std::to_string(pool.nextId++);
  entry.frameType = pool.frameType;
  entry.parent = pool.parent;
  entry.isActive = true;
  entry.createTime = 0.0f;
  pool.entries.push_back(entry);
  return entry;
}

void FramePool::Release(const std::string& poolName,
                        const std::string& frameName) {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return;

  for (auto& entry : it->second.entries) {
    if (entry.frameName == frameName) {
      entry.isActive = false;
      return;
    }
  }
}

void FramePool::ReleaseAll(const std::string& poolName) {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return;

  for (auto& entry : it->second.entries) {
    entry.isActive = false;
  }
}

std::uint32_t FramePool::GetActiveCount(const std::string& poolName) const {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return 0;

  std::uint32_t count = 0;
  for (const auto& e : it->second.entries) {
    if (e.isActive) ++count;
  }
  return count;
}

std::uint32_t FramePool::GetInactiveCount(const std::string& poolName) const {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return 0;

  std::uint32_t count = 0;
  for (const auto& e : it->second.entries) {
    if (!e.isActive) ++count;
  }
  return count;
}

std::uint32_t FramePool::GetTotalCount(const std::string& poolName) const {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return 0;
  return static_cast<std::uint32_t>(it->second.entries.size());
}

bool FramePool::HasPool(const std::string& poolName) const {
  return pools_.count(poolName) != 0;
}

std::vector<std::string> FramePool::GetPoolNames() const {
  std::vector<std::string> names;
  names.reserve(pools_.size());
  for (const auto& [name, _] : pools_) {
    names.push_back(name);
  }
  return names;
}

std::uint32_t FramePool::GetPoolCount() const {
  return static_cast<std::uint32_t>(pools_.size());
}

void FramePool::SetMaxSize(const std::string& poolName, std::uint32_t max) {
  auto it = pools_.find(poolName);
  if (it != pools_.end()) {
    it->second.maxSize = max;
  }
}

std::uint32_t FramePool::GetMaxSize(const std::string& poolName) const {
  auto it = pools_.find(poolName);
  if (it == pools_.end()) return 0;
  return it->second.maxSize;
}

void FramePool::DestroyPool(const std::string& poolName) {
  pools_.erase(poolName);
}

std::uint32_t FramePool::GetTotalActiveFrames() const {
  std::uint32_t total = 0;
  for (const auto& [_, pool] : pools_) {
    for (const auto& e : pool.entries) {
      if (e.isActive) ++total;
    }
  }
  return total;
}

std::uint32_t FramePool::GetTotalFrames() const {
  std::uint32_t total = 0;
  for (const auto& [_, pool] : pools_) {
    total += static_cast<std::uint32_t>(pool.entries.size());
  }
  return total;
}

void FramePool::Reset() { pools_.clear(); }

}

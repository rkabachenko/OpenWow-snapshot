#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

struct FramePoolEntry {
  std::string frameName;
  std::string frameType;
  std::string parent;
  bool isActive{false};
  float createTime{0.0f};
};

class FramePool {
 public:
  FramePool() = default;
  ~FramePool() = default;

  void Create(const std::string& poolName, std::uint32_t initialSize,
              const std::string& frameType, const std::string& parent);

  std::optional<FramePoolEntry> Acquire(const std::string& poolName);

  void Release(const std::string& poolName, const std::string& frameName);

  void ReleaseAll(const std::string& poolName);

  [[nodiscard]] std::uint32_t GetActiveCount(const std::string& poolName) const;
  [[nodiscard]] std::uint32_t GetInactiveCount(const std::string& poolName) const;
  [[nodiscard]] std::uint32_t GetTotalCount(const std::string& poolName) const;

  [[nodiscard]] bool HasPool(const std::string& poolName) const;
  [[nodiscard]] std::vector<std::string> GetPoolNames() const;
  [[nodiscard]] std::uint32_t GetPoolCount() const;

  void SetMaxSize(const std::string& poolName, std::uint32_t max);
  [[nodiscard]] std::uint32_t GetMaxSize(const std::string& poolName) const;

  void DestroyPool(const std::string& poolName);

  [[nodiscard]] std::uint32_t GetTotalActiveFrames() const;
  [[nodiscard]] std::uint32_t GetTotalFrames() const;

  void Reset();

 private:
  struct PoolData {
    std::string frameType;
    std::string parent;
    std::uint32_t maxSize{0};
    std::uint32_t nextId{0};
    std::vector<FramePoolEntry> entries;
  };

  std::unordered_map<std::string, PoolData> pools_;
};

}

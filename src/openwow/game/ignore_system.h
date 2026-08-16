#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct IgnoreEntry {
  std::uint64_t guid{0};
  std::string name;
  std::string reason;
  std::uint64_t timestamp{0};
};

class IgnoreSystem {
 public:
  static IgnoreSystem& Get();

  static constexpr std::uint32_t kMaxIgnores = 50;

  bool AddIgnore(std::uint64_t guid, const std::string& name);

  bool RemoveIgnore(std::uint64_t guid);

  [[nodiscard]] bool IsIgnored(std::uint64_t guid) const;

  [[nodiscard]] bool IsIgnoredByName(const std::string& name) const;

  [[nodiscard]] std::vector<IgnoreEntry> GetIgnoreList() const;
  [[nodiscard]] std::uint32_t GetIgnoreCount() const;
  [[nodiscard]] std::uint32_t GetMaxIgnores() const { return kMaxIgnores; }
  [[nodiscard]] bool IsAtCap() const;

  void SetReason(std::uint64_t guid, const std::string& reason);

  using IgnoreCallback = std::function<void(const IgnoreEntry&)>;
  void ForEach(const IgnoreCallback& callback) const;

  void MuteInChannel(std::uint64_t guid, const std::string& channel_name);
  void UnmuteInChannel(std::uint64_t guid, const std::string& channel_name);
  [[nodiscard]] bool IsMutedInChannel(std::uint64_t guid,
                                      const std::string& channel_name) const;

  void ClearAll();

  void Reset();

 private:
  IgnoreSystem() = default;

  static std::string NormalizeName(const std::string& name);

  IgnoreEntry* FindEntry(std::uint64_t guid);
  const IgnoreEntry* FindEntry(std::uint64_t guid) const;

  std::vector<IgnoreEntry> entries_;

  std::unordered_map<std::uint64_t, std::unordered_set<std::string>>
      channel_mutes_;

  mutable std::mutex mutex_;
};

}

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

struct QuestQueryResult {
  std::uint32_t questId{0};
  std::string title;
  std::string description;
  std::string objectives;
  std::uint32_t level{0};
  std::uint32_t requiredLevel{0};
  std::uint32_t rewardXP{0};
  std::uint32_t rewardMoney{0};
  bool isDaily{false};
  bool isComplete{false};
  bool isTracked{false};

  std::vector<std::pair<std::string, std::pair<std::uint32_t, std::uint32_t>>>
      objectiveProgress;
};

class QuestQueryBridge {
 public:

  static QuestQueryBridge& Get();

  [[nodiscard]] std::optional<QuestQueryResult> Query(std::uint32_t questId) const;

  [[nodiscard]] std::string GetQuestName(std::uint32_t questId) const;
  [[nodiscard]] std::uint32_t GetQuestLevel(std::uint32_t questId) const;
  [[nodiscard]] bool IsQuestComplete(std::uint32_t questId) const;

  [[nodiscard]] std::uint32_t GetNumQuestLogEntries() const;

  [[nodiscard]] std::optional<QuestQueryResult> GetQuestLogEntry(
      std::uint32_t index) const;

  [[nodiscard]] bool IsQuestTracked(std::uint32_t questId) const;

  void SetQuestData(std::uint32_t questId, QuestQueryResult data);

  void Reset();

 private:
  QuestQueryBridge() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, QuestQueryResult> cache_;
};

}

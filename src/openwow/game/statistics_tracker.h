#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class StatCategory : std::uint8_t {
  Character  = 0,
  Combat     = 1,
  Deaths     = 2,
  Quests     = 3,
  Dungeons   = 4,
  Skills     = 5,
  Travel     = 6,
  Social     = 7,
  PvP        = 8,
};

inline constexpr std::uint32_t kStatCategoryCount = 9;

struct StatisticEntry {
  std::uint32_t stat_id{0};
  std::string   name;
  std::uint64_t value{0};
  StatCategory  category{StatCategory::Character};
  bool          is_hidden{false};
};

class StatisticsTracker {
 public:

  void SetStatistic(std::uint32_t stat_id, const std::string& name,
                    std::uint64_t value, StatCategory cat);

  [[nodiscard]] std::optional<StatisticEntry> GetStatistic(
      std::uint32_t stat_id) const;

  [[nodiscard]] std::uint64_t GetValue(std::uint32_t stat_id) const;

  void IncrementStatistic(std::uint32_t stat_id, std::uint64_t amount = 1);

  [[nodiscard]] std::vector<StatisticEntry> GetStatisticsByCategory(
      StatCategory cat) const;

  [[nodiscard]] std::vector<StatisticEntry> GetAllStatistics() const;

  [[nodiscard]] static std::string GetCategoryName(StatCategory cat);

  [[nodiscard]] static std::uint32_t GetCategoryCount();

  [[nodiscard]] std::uint32_t GetStatCount() const;

  void SetHidden(std::uint32_t stat_id, bool hidden);

  [[nodiscard]] bool IsHidden(std::uint32_t stat_id) const;

  [[nodiscard]] std::vector<StatisticEntry> GetVisibleStatistics() const;

  void Reset();

 private:
  std::unordered_map<std::uint32_t, StatisticEntry> stats_;
};

}

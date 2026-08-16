
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

struct HKCategory {
  uint32_t todayHK = 0;
  uint32_t todayHonor = 0;
  uint32_t yesterdayHK = 0;
  uint32_t yesterdayHonor = 0;
  uint32_t lifetimeHK = 0;
  uint32_t lifetimeDK = 0;
};

class PvPInfo {
 public:
  static PvPInfo& Get();

  PvPInfo(const PvPInfo&) = delete;
  PvPInfo& operator=(const PvPInfo&) = delete;

  void SetHonorPoints(uint32_t points);
  [[nodiscard]] uint32_t GetHonorPoints() const;

  void SetArenaPoints(uint32_t points);
  [[nodiscard]] uint32_t GetArenaPoints() const;

  void SetTodayHK(uint32_t kills);
  [[nodiscard]] uint32_t GetTodayHK() const;

  void SetTodayHonor(uint32_t honor);
  [[nodiscard]] uint32_t GetTodayHonor() const;

  void SetYesterdayHK(uint32_t kills);
  [[nodiscard]] uint32_t GetYesterdayHK() const;

  void SetYesterdayHonor(uint32_t honor);
  [[nodiscard]] uint32_t GetYesterdayHonor() const;

  void SetLifetimeHK(uint32_t kills);
  [[nodiscard]] uint32_t GetLifetimeHK() const;

  void SetLifetimeDK(uint32_t deaths);
  [[nodiscard]] uint32_t GetLifetimeDK() const;

  [[nodiscard]] float GetKDRatio() const;

  void SetHighestRank(uint32_t rank);
  [[nodiscard]] uint32_t GetHighestRank() const;

  [[nodiscard]] std::string GetRankName(uint32_t rank) const;

  void SetFaction(uint32_t faction);
  [[nodiscard]] uint32_t GetFaction() const;

  [[nodiscard]] std::vector<std::string> GetRankNames() const;

  [[nodiscard]] static std::uint8_t ResolveLegacyRankBadgeIndexForTitleTemplate(
      std::string_view male_title, std::string_view female_title);

  [[nodiscard]] HKCategory GetHKCategory() const;

  void Reset();

 private:
  PvPInfo() = default;

  uint32_t honorPoints_ = 0;
  uint32_t arenaPoints_ = 0;
  uint32_t todayHK_ = 0;
  uint32_t todayHonor_ = 0;
  uint32_t yesterdayHK_ = 0;
  uint32_t yesterdayHonor_ = 0;
  uint32_t lifetimeHK_ = 0;
  uint32_t lifetimeDK_ = 0;
  uint32_t highestRank_ = 0;
  uint32_t faction_ = 0;

  mutable std::mutex mutex_;
};

}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class XPSourceType : std::uint8_t {
  Quest,
  Kill,
  Exploration,
  Battleground,
  Dungeon,
  Gathering,
};

struct XPSource {
  std::uint32_t amount{0};
  std::string source;
  XPSourceType type{XPSourceType::Kill};
};

class XPTracker {
 public:
  void SetCurrentXP(std::uint32_t xp);
  [[nodiscard]] std::uint32_t GetCurrentXP() const;

  void SetMaxXP(std::uint32_t xp);
  [[nodiscard]] std::uint32_t GetMaxXP() const;

  void SetLevel(std::uint32_t level);
  [[nodiscard]] std::uint32_t GetLevel() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] std::string GetProgressText() const;

  [[nodiscard]] std::uint32_t GetXPToLevel() const;

  void AddXPGain(std::uint32_t amount, std::string source,
                 XPSourceType type = XPSourceType::Kill);

  [[nodiscard]] const std::vector<XPSource>& GetRecentGains() const;

  [[nodiscard]] std::uint32_t GetSessionXP() const;

  [[nodiscard]] float GetSessionTime() const;

  [[nodiscard]] float GetXPPerHour() const;

  [[nodiscard]] float GetEstimatedTimeToLevel() const;

  void SetRestedXP(std::uint32_t xp);
  [[nodiscard]] std::uint32_t GetRestedXP() const;

  [[nodiscard]] float GetRestedPercent() const;

  [[nodiscard]] bool IsRested() const;

  void SetMaxLevel(bool maxLevel);
  [[nodiscard]] bool IsMaxLevel() const;

  [[nodiscard]] std::uint32_t GetKillsToLevel() const;

  [[nodiscard]] std::uint32_t GetQuestsToLevel() const;

  void Update(float dt);

  void Reset();

 private:
  std::uint32_t currentXP_{0};
  std::uint32_t maxXP_{1};
  std::uint32_t level_{1};
  std::uint32_t restedXP_{0};
  bool maxLevel_{false};

  std::vector<XPSource> recentGains_;
  std::uint32_t sessionXP_{0};
  float sessionTime_{0.0f};

  std::uint32_t killXPTotal_{0};
  std::uint32_t killCount_{0};
  std::uint32_t questXPTotal_{0};
  std::uint32_t questCount_{0};
};

}

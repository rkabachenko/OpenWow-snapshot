
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace openwow::game {

struct HeroicDisplayInfo {
  bool isHeroicDungeon = false;
  bool isHeroicRaid = false;
  uint32_t instanceId = 0;
  std::string instanceName;
  uint8_t playerCount = 5;
  std::string difficultyLabel;
};

class HeroicDisplay {
 public:

  void SetHeroicInfo(const HeroicDisplayInfo& info);
  [[nodiscard]] std::optional<HeroicDisplayInfo> GetHeroicInfo() const;

  [[nodiscard]] bool IsHeroic() const;

  [[nodiscard]] std::string GetDifficultyLabel() const;

  [[nodiscard]] std::string GetPlayerCountLabel() const;

  [[nodiscard]] std::string GetFullLabel() const;

  void SetInInstance(bool inInstance);
  [[nodiscard]] bool IsInInstance() const;

  [[nodiscard]] bool ShowDifficultyFrame() const;

  [[nodiscard]] uint8_t GetDifficultyId() const;

  [[nodiscard]] bool IsRaid() const;

  [[nodiscard]] bool IsDungeon() const;

  [[nodiscard]] uint32_t GetInstanceId() const;

  [[nodiscard]] std::string GetInstanceName() const;

  [[nodiscard]] uint8_t GetPlayerCount() const;

  [[nodiscard]] bool IsNormalDungeon() const;

  [[nodiscard]] static std::string GetTexturePath(bool isHeroic);

  void SetLocked(bool locked);
  [[nodiscard]] bool IsLocked() const;

  void SetResetTime(uint32_t seconds);
  [[nodiscard]] uint32_t GetResetTime() const;

  [[nodiscard]] static std::string FormatResetTime(uint32_t seconds);

  void Reset();

 private:
  std::optional<HeroicDisplayInfo> info_;
  bool inInstance_ = false;
  bool locked_     = false;
  uint32_t resetTime_ = 0;
  mutable std::mutex mutex_;
};

}

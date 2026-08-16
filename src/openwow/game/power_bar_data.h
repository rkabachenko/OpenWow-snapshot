#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class PowerDisplayType : std::uint8_t {
  Mana = 0,
  Rage = 1,
  Focus = 2,
  Energy = 3,
  Happiness = 4,
  Rune = 5,
  RunicPower = 6,
  Health = 7,
};

struct PowerBarColor {
  float r{0.0f};
  float g{0.0f};
  float b{0.0f};
  float a{1.0f};
};

struct PowerBarEntry {
  PowerDisplayType powerType{PowerDisplayType::Mana};
  std::uint32_t current{0};
  std::uint32_t maximum{0};
  std::string displayName;
};

struct PowerBarAnimation {
  float displayedPercent{0.0f};
  float targetPercent{0.0f};
  float speed{4.0f};
  bool  active{false};
};

class PowerBarData {
 public:
  void SetPower(ObjectGuid unitGuid, PowerDisplayType type,
                std::uint32_t current, std::uint32_t max);
  [[nodiscard]] std::optional<PowerBarEntry> GetPower(
      ObjectGuid unitGuid, PowerDisplayType type) const;
  [[nodiscard]] float GetPowerPercent(ObjectGuid unitGuid,
                                      PowerDisplayType type) const;

  [[nodiscard]] std::optional<PowerBarEntry> GetPrimaryPower(
      ObjectGuid unitGuid) const;
  void SetPrimaryPowerType(ObjectGuid unitGuid, PowerDisplayType type);

  [[nodiscard]] static PowerBarColor GetColor(PowerDisplayType type);
  [[nodiscard]] static std::string GetDisplayName(PowerDisplayType type);

  [[nodiscard]] std::vector<PowerBarEntry> GetAllPowers(
      ObjectGuid unitGuid) const;
  void ClearUnit(ObjectGuid unitGuid);
  void Reset();

  void UpdateAnimation(ObjectGuid unitGuid, PowerDisplayType type, float dt);

  [[nodiscard]] float GetAnimatedPercent(ObjectGuid unitGuid,
                                         PowerDisplayType type) const;

  void SnapAnimation(ObjectGuid unitGuid, PowerDisplayType type);

  void SetAnimationSpeed(ObjectGuid unitGuid, PowerDisplayType type,
                         float speed);

  [[nodiscard]] std::uint32_t GetPowerDeficit(ObjectGuid unitGuid,
                                              PowerDisplayType type) const;

  [[nodiscard]] bool HasPower(ObjectGuid unitGuid,
                              PowerDisplayType type) const;

  [[nodiscard]] std::size_t GetPowerTypeCount(ObjectGuid unitGuid) const;

  [[nodiscard]] std::size_t GetTrackedUnitCount() const;

  void SanitizeAll();

 private:
  struct UnitPowers {
    std::unordered_map<std::uint8_t, PowerBarEntry> powers;
    std::unordered_map<std::uint8_t, PowerBarAnimation> animations;
    PowerDisplayType primaryType{PowerDisplayType::Mana};
  };
  std::unordered_map<std::uint64_t, UnitPowers> units_;
};

}

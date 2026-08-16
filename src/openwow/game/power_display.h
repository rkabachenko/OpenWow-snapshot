#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

#include "openwow/game/unit_defines.h"

namespace openwow::game {

[[nodiscard]] std::uint32_t GetPowerDisplayValueDivisor(std::int32_t power_type);
[[nodiscard]] std::uint32_t NormalizePowerDisplayValue(
    std::uint32_t raw_value, std::int32_t power_type);

struct PowerBarConfig {
  PowerType type = PowerType::kMana;
  std::string name;
  std::uint32_t color = 0;
  bool smooth_update = false;
  float update_rate = 0.0f;
  bool show_numeric = true;
  bool hide_when_empty = false;
  bool hide_when_full = false;

  static std::uint32_t GetClassColor(std::uint8_t class_id);
  static std::string GetClassName(std::uint8_t class_id);
};

class PowerDisplay {
 public:
  static PowerDisplay& Get();

  [[nodiscard]] PowerBarConfig GetPowerConfig(PowerType type) const;
  [[nodiscard]] std::uint32_t GetPowerColor(PowerType type) const;
  [[nodiscard]] std::string GetPowerName(PowerType type) const;

  static std::uint32_t GetClassColor(std::uint8_t class_id);
  static std::string GetClassName(std::uint8_t class_id);

  static std::uint32_t GetReactionColor(std::uint8_t reaction);

  static std::uint32_t GetDifficultyColor(std::int32_t level_diff);

  struct RuneState {
    bool ready[6] = {};
    float cooldown[6] = {};
    std::uint8_t type[6] = {};
  };

  [[nodiscard]] RuneState GetRuneState() const;
  void SetRuneState(const RuneState& state);
  void SetRuneReady(std::uint8_t index, bool ready);
  void SetRuneCooldown(std::uint8_t index, float progress);
  void SetRuneType(std::uint8_t index, std::uint8_t type);

  [[nodiscard]] std::uint8_t GetComboPoints() const;
  void SetComboPoints(std::uint8_t points);

  void Reset();

 private:
  PowerDisplay() = default;

  RuneState rune_state_;
  std::uint8_t combo_points_ = 0;
  mutable std::mutex mutex_;
};

}

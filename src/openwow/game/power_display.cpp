#include "openwow/game/power_display.h"

#include <algorithm>

namespace openwow::game {

std::uint32_t GetPowerDisplayValueDivisor(const std::int32_t power_type) {
  switch (power_type) {
    case 1:
    case 6:
      return 10;
    case 4:
      return 1000;
    default:
      return 1;
  }
}

std::uint32_t NormalizePowerDisplayValue(const std::uint32_t raw_value,
                                         const std::int32_t power_type) {
  return raw_value / GetPowerDisplayValueDivisor(power_type);
}

static constexpr std::uint32_t kClassColors[] = {
    0x00000000,
    0xFFC79C6E,
    0xFFF58CBA,
    0xFFABD473,
    0xFFFFF569,
    0xFFFFFFFF,
    0xFFC41F3B,
    0xFF0070DE,
    0xFF69CCF0,
    0xFF9482C9,
    0x00000000,
    0xFFFF7D0A,
};

static constexpr const char* kClassNames[] = {
    "Unknown",
    "Warrior",
    "Paladin",
    "Hunter",
    "Rogue",
    "Priest",
    "Death Knight",
    "Shaman",
    "Mage",
    "Warlock",
    "",
    "Druid",
};

static constexpr std::size_t kNumClasses = 12;

struct PowerInfo {
  std::uint32_t color;
  const char* name;
  bool smooth;
  float rate;
  bool hide_empty;
  bool hide_full;
};

static constexpr PowerInfo kPowerInfos[] = {
    {0xFF0000FF, "Mana",        true,  10.0f, false, false},
    {0xFFFF0000, "Rage",        false, 0.0f,  true,  false},
    {0xFFFF8040, "Focus",       false, 0.0f,  false, false},
    {0xFFFFFF00, "Energy",      false, 0.0f,  false, false},
    {0xFF00FF00, "Happiness",   false, 0.0f,  false, false},
    {0xFF808080, "Runes",       false, 0.0f,  false, false},
    {0xFF00D1FF, "Runic Power", false, 0.0f,  true,  false},
};

static constexpr std::size_t kNumPowerTypes = 7;

PowerDisplay& PowerDisplay::Get() {
  static PowerDisplay instance;
  return instance;
}

PowerBarConfig PowerDisplay::GetPowerConfig(PowerType type) const {
  auto idx = static_cast<std::size_t>(type);
  PowerBarConfig cfg;
  cfg.type = type;
  if (idx < kNumPowerTypes) {
    cfg.name = kPowerInfos[idx].name;
    cfg.color = kPowerInfos[idx].color;
    cfg.smooth_update = kPowerInfos[idx].smooth;
    cfg.update_rate = kPowerInfos[idx].rate;
    cfg.hide_when_empty = kPowerInfos[idx].hide_empty;
    cfg.hide_when_full = kPowerInfos[idx].hide_full;
  } else {
    cfg.name = "Unknown";
    cfg.color = 0xFF808080;
  }
  return cfg;
}

std::uint32_t PowerDisplay::GetPowerColor(PowerType type) const {
  auto idx = static_cast<std::size_t>(type);
  if (idx < kNumPowerTypes) return kPowerInfos[idx].color;
  return 0xFF808080;
}

std::string PowerDisplay::GetPowerName(PowerType type) const {
  auto idx = static_cast<std::size_t>(type);
  if (idx < kNumPowerTypes) return kPowerInfos[idx].name;
  return "Unknown";
}

std::uint32_t PowerDisplay::GetClassColor(std::uint8_t class_id) {
  if (class_id < kNumClasses) return kClassColors[class_id];
  return 0xFF808080;
}

std::string PowerDisplay::GetClassName(std::uint8_t class_id) {
  if (class_id < kNumClasses) return kClassNames[class_id];
  return "Unknown";
}

std::uint32_t PowerBarConfig::GetClassColor(std::uint8_t class_id) {
  return PowerDisplay::GetClassColor(class_id);
}

std::string PowerBarConfig::GetClassName(std::uint8_t class_id) {
  return PowerDisplay::GetClassName(class_id);
}

std::uint32_t PowerDisplay::GetReactionColor(std::uint8_t reaction) {
  switch (reaction) {
    case 0:
      return 0xFFFF0000;
    case 1:
      return 0xFFFFFF00;
    case 2:
      return 0xFF00FF00;
    default:
      return 0xFF00FF00;
  }
}

std::uint32_t PowerDisplay::GetDifficultyColor(std::int32_t level_diff) {
  if (level_diff <= -10) return 0xFF808080;
  if (level_diff <= -5) return 0xFF00FF00;
  if (level_diff <= 2) return 0xFFFFFF00;
  if (level_diff <= 4) return 0xFFFF8000;
  return 0xFFFF0000;
}

PowerDisplay::RuneState PowerDisplay::GetRuneState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rune_state_;
}

void PowerDisplay::SetRuneState(const RuneState& state) {
  std::lock_guard<std::mutex> lock(mutex_);
  rune_state_ = state;
}

void PowerDisplay::SetRuneReady(std::uint8_t index, bool ready) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 6) rune_state_.ready[index] = ready;
}

void PowerDisplay::SetRuneCooldown(std::uint8_t index, float progress) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 6)
    rune_state_.cooldown[index] = std::clamp(progress, 0.0f, 1.0f);
}

void PowerDisplay::SetRuneType(std::uint8_t index, std::uint8_t type) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < 6) rune_state_.type[index] = type;
}

std::uint8_t PowerDisplay::GetComboPoints() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return combo_points_;
}

void PowerDisplay::SetComboPoints(std::uint8_t points) {
  std::lock_guard<std::mutex> lock(mutex_);
  combo_points_ = std::min(points, static_cast<std::uint8_t>(5));
}

void PowerDisplay::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  rune_state_ = RuneState{};
  combo_points_ = 0;
}

}

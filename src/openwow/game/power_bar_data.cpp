
#include "openwow/game/power_bar_data.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void PowerBarData::SetPower(ObjectGuid unitGuid, PowerDisplayType type,
                             std::uint32_t current, std::uint32_t max) {
  auto& up = units_[unitGuid.GetRawValue()];
  auto key = static_cast<std::uint8_t>(type);
  auto& entry = up.powers[key];
  entry.powerType = type;
  entry.current = (current <= max) ? current : max;
  entry.maximum = max;
  entry.displayName = GetDisplayName(type);

  auto& anim = up.animations[key];
  float newTarget = (max > 0) ? static_cast<float>(entry.current) /
                                    static_cast<float>(max)
                               : 0.0f;
  if (!anim.active) {

    anim.displayedPercent = newTarget;
  }
  anim.targetPercent = newTarget;
  anim.active = (std::fabs(anim.displayedPercent - anim.targetPercent) > 1e-5f);
}

std::optional<PowerBarEntry> PowerBarData::GetPower(
    ObjectGuid unitGuid, PowerDisplayType type) const {
  auto it = units_.find(unitGuid.GetRawValue());
  if (it == units_.end()) return std::nullopt;
  auto pit = it->second.powers.find(static_cast<std::uint8_t>(type));
  if (pit == it->second.powers.end()) return std::nullopt;
  return pit->second;
}

float PowerBarData::GetPowerPercent(ObjectGuid unitGuid,
                                     PowerDisplayType type) const {
  auto entry = GetPower(unitGuid, type);
  if (!entry || entry->maximum == 0) return 0.0f;
  return static_cast<float>(entry->current) /
         static_cast<float>(entry->maximum);
}

std::optional<PowerBarEntry> PowerBarData::GetPrimaryPower(
    ObjectGuid unitGuid) const {
  auto it = units_.find(unitGuid.GetRawValue());
  if (it == units_.end()) return std::nullopt;
  return GetPower(unitGuid, it->second.primaryType);
}

void PowerBarData::SetPrimaryPowerType(ObjectGuid unitGuid,
                                        PowerDisplayType type) {
  units_[unitGuid.GetRawValue()].primaryType = type;
}

PowerBarColor PowerBarData::GetColor(PowerDisplayType type) {
  switch (type) {
    case PowerDisplayType::Mana:       return {0.0f, 0.0f, 1.0f, 1.0f};
    case PowerDisplayType::Rage:       return {1.0f, 0.0f, 0.0f, 1.0f};
    case PowerDisplayType::Focus:      return {1.0f, 0.5f, 0.25f, 1.0f};
    case PowerDisplayType::Energy:     return {1.0f, 1.0f, 0.0f, 1.0f};
    case PowerDisplayType::Happiness:  return {0.0f, 1.0f, 1.0f, 1.0f};
    case PowerDisplayType::Rune:       return {0.5f, 0.5f, 0.5f, 1.0f};
    case PowerDisplayType::RunicPower: return {0.0f, 0.82f, 1.0f, 1.0f};
    case PowerDisplayType::Health:     return {0.0f, 1.0f, 0.0f, 1.0f};
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

std::string PowerBarData::GetDisplayName(PowerDisplayType type) {
  switch (type) {
    case PowerDisplayType::Mana:       return "Mana";
    case PowerDisplayType::Rage:       return "Rage";
    case PowerDisplayType::Focus:      return "Focus";
    case PowerDisplayType::Energy:     return "Energy";
    case PowerDisplayType::Happiness:  return "Happiness";
    case PowerDisplayType::Rune:       return "Rune";
    case PowerDisplayType::RunicPower: return "Runic Power";
    case PowerDisplayType::Health:     return "Health";
  }
  return "Unknown";
}

std::vector<PowerBarEntry> PowerBarData::GetAllPowers(
    ObjectGuid unitGuid) const {
  auto it = units_.find(unitGuid.GetRawValue());
  if (it == units_.end()) return {};
  std::vector<PowerBarEntry> out;
  out.reserve(it->second.powers.size());
  for (const auto& [_, entry] : it->second.powers)
    out.push_back(entry);
  return out;
}

void PowerBarData::ClearUnit(ObjectGuid unitGuid) {
  units_.erase(unitGuid.GetRawValue());
}

void PowerBarData::Reset() { units_.clear(); }

void PowerBarData::UpdateAnimation(ObjectGuid unitGuid, PowerDisplayType type,
                                    float dt) {
  auto uit = units_.find(unitGuid.GetRawValue());
  if (uit == units_.end()) return;
  auto key = static_cast<std::uint8_t>(type);
  auto ait = uit->second.animations.find(key);
  if (ait == uit->second.animations.end()) return;

  auto& anim = ait->second;
  if (!anim.active || dt <= 0.0f) return;

  float diff = anim.targetPercent - anim.displayedPercent;
  float step = anim.speed * dt;
  if (std::fabs(diff) <= step) {
    anim.displayedPercent = anim.targetPercent;
    anim.active = false;
  } else {
    anim.displayedPercent += (diff > 0.0f ? step : -step);
  }

  anim.displayedPercent = std::clamp(anim.displayedPercent, 0.0f, 1.0f);
}

float PowerBarData::GetAnimatedPercent(ObjectGuid unitGuid,
                                        PowerDisplayType type) const {
  auto uit = units_.find(unitGuid.GetRawValue());
  if (uit == units_.end()) return 0.0f;
  auto key = static_cast<std::uint8_t>(type);
  auto ait = uit->second.animations.find(key);
  if (ait == uit->second.animations.end()) {

    return GetPowerPercent(unitGuid, type);
  }
  return ait->second.displayedPercent;
}

void PowerBarData::SnapAnimation(ObjectGuid unitGuid, PowerDisplayType type) {
  auto uit = units_.find(unitGuid.GetRawValue());
  if (uit == units_.end()) return;
  auto key = static_cast<std::uint8_t>(type);
  auto& anim = uit->second.animations[key];
  float pct = GetPowerPercent(unitGuid, type);
  anim.displayedPercent = pct;
  anim.targetPercent = pct;
  anim.active = false;
}

void PowerBarData::SetAnimationSpeed(ObjectGuid unitGuid,
                                      PowerDisplayType type, float speed) {
  auto& up = units_[unitGuid.GetRawValue()];
  auto key = static_cast<std::uint8_t>(type);
  up.animations[key].speed = (speed > 0.0f) ? speed : 1.0f;
}

std::uint32_t PowerBarData::GetPowerDeficit(ObjectGuid unitGuid,
                                             PowerDisplayType type) const {
  auto entry = GetPower(unitGuid, type);
  if (!entry) return 0;
  return entry->maximum - entry->current;
}

bool PowerBarData::HasPower(ObjectGuid unitGuid, PowerDisplayType type) const {
  auto it = units_.find(unitGuid.GetRawValue());
  if (it == units_.end()) return false;
  return it->second.powers.count(static_cast<std::uint8_t>(type)) > 0;
}

std::size_t PowerBarData::GetPowerTypeCount(ObjectGuid unitGuid) const {
  auto it = units_.find(unitGuid.GetRawValue());
  if (it == units_.end()) return 0;
  return it->second.powers.size();
}

std::size_t PowerBarData::GetTrackedUnitCount() const {
  return units_.size();
}

void PowerBarData::SanitizeAll() {
  for (auto& [_, up] : units_) {
    for (auto& [key, entry] : up.powers) {
      if (entry.current > entry.maximum) {
        entry.current = entry.maximum;
      }
    }
  }
}

}

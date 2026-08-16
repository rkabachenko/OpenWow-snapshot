#include "openwow/ui/unit_frame_data.h"

namespace openwow::ui {

void UnitFrameProvider::SetUnitData(const std::string& unitId,
                                     const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  units_[unitId] = data;
}

std::optional<UnitFrameData> UnitFrameProvider::GetUnitData(
    const std::string& unitId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) return it->second;
  return std::nullopt;
}

bool UnitFrameProvider::HasUnit(const std::string& unitId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return units_.count(unitId) > 0;
}

void UnitFrameProvider::RemoveUnit(const std::string& unitId) {
  std::lock_guard<std::mutex> lock(mutex_);
  units_.erase(unitId);
}

std::vector<std::string> UnitFrameProvider::GetActiveUnits() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(units_.size());
  for (const auto& [key, _] : units_) result.push_back(key);
  return result;
}

std::uint32_t UnitFrameProvider::GetUnitCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(units_.size());
}

void UnitFrameProvider::SetHealth(const std::string& unitId,
                                   std::uint32_t current,
                                   std::uint32_t max) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) {
    it->second.healthCurrent = current;
    it->second.healthMax = max;
  }
}

void UnitFrameProvider::SetPower(const std::string& unitId,
                                  std::uint32_t current, std::uint32_t max,
                                  PowerType type) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) {
    it->second.powerCurrent = current;
    it->second.powerMax = max;
    it->second.powerType = type;
  }
}

void UnitFrameProvider::SetLevel(const std::string& unitId,
                                  std::uint32_t level) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) it->second.level = level;
}

void UnitFrameProvider::SetInCombat(const std::string& unitId,
                                     bool inCombat) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) it->second.isInCombat = inCombat;
}

void UnitFrameProvider::SetRaidIcon(const std::string& unitId,
                                     std::uint32_t icon) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end())
    it->second.raidIcon = static_cast<std::uint8_t>(icon > 8 ? 0 : icon);
}

void UnitFrameProvider::SetAbsorb(const std::string& unitId,
                                   std::uint32_t amount) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) it->second.absorbAmount = amount;
}

void UnitFrameProvider::SetIncomingHeals(const std::string& unitId,
                                          std::uint32_t amount) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it != units_.end()) it->second.incomingHeals = amount;
}

float UnitFrameProvider::GetHealthPercent(const std::string& unitId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it == units_.end()) return 0.0f;
  const auto& d = it->second;
  if (d.healthMax == 0) return 0.0f;
  return static_cast<float>(d.healthCurrent) /
         static_cast<float>(d.healthMax) * 100.0f;
}

float UnitFrameProvider::GetPowerPercent(const std::string& unitId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = units_.find(unitId);
  if (it == units_.end()) return 0.0f;
  const auto& d = it->second;
  if (d.powerMax == 0) return 0.0f;
  return static_cast<float>(d.powerCurrent) /
         static_cast<float>(d.powerMax) * 100.0f;
}

std::uint32_t UnitFrameProvider::GetPowerColor(PowerType type) {

  switch (type) {
    case PowerType::Mana:       return 0xFF0000FF;
    case PowerType::Rage:       return 0xFFFF0000;
    case PowerType::Focus:      return 0xFFFF8040;
    case PowerType::Energy:     return 0xFFFFFF00;
    case PowerType::Happiness:  return 0xFF00FF80;
    case PowerType::Runes:      return 0xFF808080;
    case PowerType::RunicPower: return 0xFF00D1FF;
    case PowerType::Health:     return 0xFF00FF00;
  }
  return 0xFFFFFFFF;
}

void UnitFrameProvider::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  units_.clear();
}

void UnitFrameProvider::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  units_.clear();
}

}


#include "openwow/game/stance_bar.h"

#include <algorithm>

namespace openwow::game {

void StanceBar::SetSlots(const std::vector<StanceSlot>& slots) {
  slots_ = slots;

  for (auto& s : slots_) s.isActive = (s.stanceId == activeStanceId_);
}

const std::vector<StanceSlot>& StanceBar::GetSlots() const { return slots_; }

uint32_t StanceBar::GetSlotCount() const {
  return static_cast<uint32_t>(slots_.size());
}

std::optional<StanceSlot> StanceBar::GetSlot(uint32_t index) const {
  if (index >= slots_.size()) return std::nullopt;
  return slots_[index];
}

void StanceBar::SetActive(uint32_t stanceId) {
  activeStanceId_ = stanceId;
  for (auto& s : slots_) s.isActive = (s.stanceId == stanceId);
}

uint32_t StanceBar::GetActive() const { return activeStanceId_; }

bool StanceBar::IsInStance() const {
  return activeStanceId_ != StanceId::None;
}

int32_t StanceBar::GetActiveSlotIndex() const {
  for (size_t i = 0; i < slots_.size(); ++i)
    if (slots_[i].stanceId == activeStanceId_) return static_cast<int32_t>(i);
  return -1;
}

void StanceBar::SetCooldown(uint32_t slotIndex, float remaining) {
  if (slotIndex < slots_.size()) slots_[slotIndex].cooldown = remaining;
}

float StanceBar::GetCooldown(uint32_t slotIndex) const {
  if (slotIndex >= slots_.size()) return 0.0f;
  return slots_[slotIndex].cooldown;
}

bool StanceBar::IsUsable(uint32_t slotIndex) const {
  if (slotIndex >= slots_.size()) return false;
  return slots_[slotIndex].isUsable;
}

void StanceBar::SetUsable(uint32_t slotIndex, bool usable) {
  if (slotIndex < slots_.size()) slots_[slotIndex].isUsable = usable;
}

std::string StanceBar::GetStanceName(uint32_t stanceId) {
  switch (stanceId) {
    case StanceId::BattleStance:    return "Battle Stance";
    case StanceId::DefensiveStance: return "Defensive Stance";
    case StanceId::BerserkerStance: return "Berserker Stance";
    case StanceId::BearForm:        return "Bear Form";
    case StanceId::AquaticForm:     return "Aquatic Form";
    case StanceId::CatForm:         return "Cat Form";
    case StanceId::TravelForm:      return "Travel Form";
    case StanceId::TreeOfLife:       return "Tree of Life";
    case StanceId::MoonkinForm:     return "Moonkin Form";
    case StanceId::Stealth:         return "Stealth";
    case StanceId::FlightForm:      return "Flight Form";
    case StanceId::ShadowForm:      return "Shadowform";
    case StanceId::SwiftFlightForm: return "Swift Flight Form";
    default:                        return "Unknown";
  }
}

void StanceBar::Update(float dt) {
  for (auto& s : slots_) {
    if (s.cooldown > 0.0f) {
      s.cooldown -= dt;
      if (s.cooldown < 0.0f) s.cooldown = 0.0f;
    }
  }
}

bool StanceBar::IsVisible() const { return visible_; }
void StanceBar::SetVisible(bool visible) { visible_ = visible; }

void StanceBar::Reset() {
  slots_.clear();
  activeStanceId_ = StanceId::None;
  visible_ = false;
}

}

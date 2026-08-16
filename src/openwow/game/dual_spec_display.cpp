
#include "openwow/game/dual_spec_display.h"

namespace openwow::game {

void DualSpecDisplay::SetSpec(uint8_t index, SpecDisplaySlot spec) {
  if (index >= GetMaxSpecs()) return;

  spec.specIndex = index;

  spec.isActive  = (index == active_spec_);
  specs_[index]  = std::move(spec);
  has_spec_[index] = true;
}

std::optional<SpecDisplaySlot> DualSpecDisplay::GetSpec(uint8_t index) const {
  if (index >= GetMaxSpecs() || !has_spec_[index]) return std::nullopt;
  return specs_[index];
}

std::vector<SpecDisplaySlot> DualSpecDisplay::GetSpecs() const {
  std::vector<SpecDisplaySlot> result;
  result.reserve(GetMaxSpecs());
  for (uint8_t i = 0; i < GetMaxSpecs(); ++i) {
    if (has_spec_[i]) result.push_back(specs_[i]);
  }
  return result;
}

uint8_t DualSpecDisplay::GetActiveSpec() const {
  return active_spec_;
}

void DualSpecDisplay::SetActiveSpec(uint8_t index) {
  if (index >= GetMaxSpecs()) return;

  if (has_spec_[active_spec_]) {
    specs_[active_spec_].isActive = false;
  }
  active_spec_ = index;
  if (has_spec_[active_spec_]) {
    specs_[active_spec_].isActive = true;
  }
}

bool DualSpecDisplay::IsUnlocked() const {
  return unlocked_;
}

void DualSpecDisplay::SetUnlocked(bool unlocked) {
  unlocked_ = unlocked;
}

uint32_t DualSpecDisplay::GetUnlockCost() {

  return 100000;
}

uint8_t DualSpecDisplay::GetMaxSpecs() {
  return 2;
}

std::string DualSpecDisplay::GetSpecName(uint8_t index) const {
  if (index >= GetMaxSpecs() || !has_spec_[index]) return {};
  return specs_[index].name;
}

bool DualSpecDisplay::CanSwitch() const {
  if (!unlocked_)   return false;
  if (in_combat_)   return false;
  if (dead_)        return false;
  return true;
}

void DualSpecDisplay::SetInCombat(bool inCombat) {
  in_combat_ = inCombat;
}

void DualSpecDisplay::SetDead(bool dead) {
  dead_ = dead;
}

void DualSpecDisplay::Reset() {
  for (uint8_t i = 0; i < GetMaxSpecs(); ++i) {
    specs_[i]     = {};
    has_spec_[i]  = false;
  }
  active_spec_ = 0;
  unlocked_    = false;
  in_combat_   = false;
  dead_        = false;
}

}

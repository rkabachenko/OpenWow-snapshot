
#include "openwow/game/inventory/loot/auto_loot.h"

namespace openwow::game {

void AutoLootConfig::SetMode(AutoLootMode mode) {
  mode_ = mode;
}

AutoLootMode AutoLootConfig::GetMode() const {
  return mode_;
}

void AutoLootConfig::SetModifierKey(std::uint8_t key) {
  modifierKey_ = key;
}

std::uint8_t AutoLootConfig::GetModifierKey() const {
  return modifierKey_;
}

bool AutoLootConfig::ShouldAutoLoot(bool modifierPressed) const {
  switch (mode_) {
    case AutoLootMode::Always:
      return true;
    case AutoLootMode::KeyModified:
      return modifierPressed;
    case AutoLootMode::Disabled:
    default:
      return false;
  }
}

void AutoLootConfig::SetAutoLootCorpse(bool v) {
  autoCorpse_ = v;
}

bool AutoLootConfig::GetAutoLootCorpse() const {
  return autoCorpse_;
}

void AutoLootConfig::SetAutoLootQuest(bool v) {
  autoQuest_ = v;
}

bool AutoLootConfig::GetAutoLootQuest() const {
  return autoQuest_;
}

void AutoLootConfig::SetAutoLootGather(bool v) {
  autoGather_ = v;
}

bool AutoLootConfig::GetAutoLootGather() const {
  return autoGather_;
}

void AutoLootConfig::SetAutoLootFishing(bool v) {
  autoFishing_ = v;
}

bool AutoLootConfig::GetAutoLootFishing() const {
  return autoFishing_;
}

void AutoLootConfig::SetSkipJunk(bool v) {
  skipJunk_ = v;
}

bool AutoLootConfig::GetSkipJunk() const {
  return skipJunk_;
}

void AutoLootConfig::SetMinQuality(std::uint8_t quality) {
  minQuality_ = quality;
}

std::uint8_t AutoLootConfig::GetMinQuality() const {
  return minQuality_;
}

bool AutoLootConfig::IsEnabled() const {
  return mode_ != AutoLootMode::Disabled;
}

void AutoLootConfig::Reset() {
  mode_ = AutoLootMode::Disabled;
  modifierKey_ = 1;
  autoCorpse_ = false;
  autoQuest_ = false;
  autoGather_ = false;
  autoFishing_ = false;
  skipJunk_ = false;
  minQuality_ = 0;
}

}

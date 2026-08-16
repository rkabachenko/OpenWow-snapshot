
#include "openwow/game/spell_flyout.h"

#include <algorithm>

namespace openwow::game {

uint32_t SpellFlyout::CreateFlyout(uint32_t flyoutId,
                                   const std::vector<FlyoutSlot>& slots,
                                   FlyoutDirection dir) {
  flyouts_[flyoutId] = FlyoutEntry{slots, dir};
  return flyoutId;
}

void SpellFlyout::RemoveFlyout(uint32_t flyoutId) {
  flyouts_.erase(flyoutId);
  if (openFlyoutId_ && *openFlyoutId_ == flyoutId) openFlyoutId_.reset();
}

std::optional<FlyoutData> SpellFlyout::GetFlyout(uint32_t flyoutId) const {
  auto it = flyouts_.find(flyoutId);
  if (it == flyouts_.end()) return std::nullopt;
  return FlyoutData{it->second.slots, it->second.direction};
}

uint32_t SpellFlyout::GetFlyoutCount() const {
  return static_cast<uint32_t>(flyouts_.size());
}

void SpellFlyout::SetSlots(uint32_t flyoutId, const std::vector<FlyoutSlot>& slots) {
  auto it = flyouts_.find(flyoutId);
  if (it != flyouts_.end()) it->second.slots = slots;
}

std::vector<FlyoutSlot> SpellFlyout::GetSlots(uint32_t flyoutId) const {
  auto it = flyouts_.find(flyoutId);
  if (it == flyouts_.end()) return {};
  return it->second.slots;
}

uint32_t SpellFlyout::GetSlotCount(uint32_t flyoutId) const {
  auto it = flyouts_.find(flyoutId);
  if (it == flyouts_.end()) return 0;
  return static_cast<uint32_t>(it->second.slots.size());
}

void SpellFlyout::SetDirection(uint32_t flyoutId, FlyoutDirection dir) {
  auto it = flyouts_.find(flyoutId);
  if (it != flyouts_.end()) it->second.direction = dir;
}

FlyoutDirection SpellFlyout::GetDirection(uint32_t flyoutId) const {
  auto it = flyouts_.find(flyoutId);
  if (it == flyouts_.end()) return FlyoutDirection::Up;
  return it->second.direction;
}

void SpellFlyout::SetCooldown(uint32_t flyoutId, uint32_t slotIndex, float remaining) {
  auto it = flyouts_.find(flyoutId);
  if (it == flyouts_.end()) return;
  if (slotIndex < it->second.slots.size())
    it->second.slots[slotIndex].cooldownRemaining = remaining;
}

bool SpellFlyout::IsOpen(uint32_t flyoutId) const {
  return openFlyoutId_.has_value() && *openFlyoutId_ == flyoutId;
}

void SpellFlyout::OpenFlyout(uint32_t flyoutId) {
  if (flyouts_.contains(flyoutId)) openFlyoutId_ = flyoutId;
}

void SpellFlyout::CloseAllFlyouts() { openFlyoutId_.reset(); }

std::optional<uint32_t> SpellFlyout::GetOpenFlyout() const {
  return openFlyoutId_;
}

void SpellFlyout::Update(float dt) {
  for (auto& [id, entry] : flyouts_) {
    for (auto& slot : entry.slots) {
      if (slot.cooldownRemaining > 0.0f) {
        slot.cooldownRemaining -= dt;
        if (slot.cooldownRemaining < 0.0f) slot.cooldownRemaining = 0.0f;
      }
    }
  }
}

void SpellFlyout::Reset() {
  flyouts_.clear();
  openFlyoutId_.reset();
}

}

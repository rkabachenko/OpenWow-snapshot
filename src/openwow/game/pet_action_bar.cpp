
#include "openwow/game/pet_action_bar.h"

#include <algorithm>

namespace openwow::game {

void PetActionBar::SetSlots(const std::vector<PetActionSlot>& slots) {
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        if (i < slots.size()) {
            slots_[i] = slots[i];
            slots_[i].slotIndex = i;
        } else {
            slots_[i] = PetActionSlot{};
            slots_[i].slotIndex = i;
        }
    }
}

std::vector<PetActionSlot> PetActionBar::GetSlots() const {
    return {std::begin(slots_), std::end(slots_)};
}

std::optional<PetActionSlot> PetActionBar::GetSlot(uint32_t index) const {
    if (index >= kSlotCount) return std::nullopt;
    return slots_[index];
}

void PetActionBar::SetAutocast(uint32_t slotIndex, bool enabled) {
    if (slotIndex >= kSlotCount) return;
    slots_[slotIndex].isAutoCastEnabled = enabled;
}

bool PetActionBar::IsAutocast(uint32_t slotIndex) const {
    if (slotIndex >= kSlotCount) return false;
    return slots_[slotIndex].isAutoCastEnabled;
}

void PetActionBar::SetUsable(uint32_t slotIndex, bool usable) {
    if (slotIndex >= kSlotCount) return;
    slots_[slotIndex].isUsable = usable;
}

void PetActionBar::SetCooldown(uint32_t slotIndex, float remaining) {
    if (slotIndex >= kSlotCount) return;
    slots_[slotIndex].cooldown = remaining;
}

float PetActionBar::GetCooldown(uint32_t slotIndex) const {
    if (slotIndex >= kSlotCount) return 0.0f;
    return slots_[slotIndex].cooldown;
}

bool PetActionBar::IsOnCooldown(uint32_t slotIndex) const {
    if (slotIndex >= kSlotCount) return false;
    return slots_[slotIndex].cooldown > 0.0f;
}

void PetActionBar::SetMode(PetMode mode) { mode_ = mode; }
PetMode PetActionBar::GetMode() const { return mode_; }

void PetActionBar::SetAction(PetActionEnum action) { action_ = action; }
PetActionEnum PetActionBar::GetAction() const { return action_; }

bool PetActionBar::IsVisible() const { return visible_; }
void PetActionBar::SetVisible(bool visible) { visible_ = visible; }

void PetActionBar::Update(float dt) {
    for (auto& slot : slots_) {
        if (slot.cooldown > 0.0f) {
            slot.cooldown = std::max(0.0f, slot.cooldown - dt);
        }
    }
}

void PetActionBar::Reset() {
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        slots_[i] = PetActionSlot{};
        slots_[i].slotIndex = i;
    }
    mode_ = PetMode::Defensive;
    action_ = PetActionEnum::Follow;
    visible_ = false;
}

void PetActionBar::ToggleAutocastBySpellId(uint32_t spellId, int32_t state) {
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        if (slots_[i].actionType != PetActionType::Spell) continue;
        if (slots_[i].actionId != spellId) continue;

        bool newState;
        if (state < 0) {
            newState = !slots_[i].isAutoCastEnabled;
        } else {
            newState = (state != 0);
        }
        slots_[i].isAutoCastEnabled = newState;
    }
}

int32_t PetActionBar::SwapSlot(uint32_t srcIndex, uint32_t dstIndex) {
    if (srcIndex >= kSlotCount || dstIndex >= kSlotCount) return -1;
    if (srcIndex == dstIndex) return -1;
    std::swap(slots_[srcIndex], slots_[dstIndex]);
    slots_[srcIndex].slotIndex = srcIndex;
    slots_[dstIndex].slotIndex = dstIndex;
    return static_cast<int32_t>(srcIndex);
}

}

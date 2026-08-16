
#include "openwow/game/stance_bar_display.h"

#include <algorithm>

namespace openwow::game {

void StanceBarDisplay::SetSlots(const std::vector<StanceBarSlotInfo>& slots) {

    if (slots.size() <= kMaxSlots) {
        slots_ = slots;
    } else {
        slots_.assign(slots.begin(), slots.begin() + kMaxSlots);
    }
}

uint8_t StanceBarDisplay::GetSlotCount() const {
    return static_cast<uint8_t>(slots_.size());
}

std::optional<StanceBarSlotInfo> StanceBarDisplay::GetSlot(
    uint8_t index) const {
    if (index >= slots_.size()) return std::nullopt;
    return slots_[index];
}

StanceFormType StanceBarDisplay::GetActiveForm() const {
    for (const auto& s : slots_) {
        if (s.isActive) return s.formType;
    }
    return StanceFormType::None;
}

void StanceBarDisplay::ActivateSlot(uint8_t index) {
    for (auto& s : slots_) {
        s.isActive = (s.slotIndex == index);
    }
}

void StanceBarDisplay::Deactivate() {
    for (auto& s : slots_) {
        s.isActive = false;
    }
}

std::optional<uint8_t> StanceBarDisplay::GetActiveSlotIndex() const {
    for (const auto& s : slots_) {
        if (s.isActive) return s.slotIndex;
    }
    return std::nullopt;
}

void StanceBarDisplay::SetSlotCooldown(uint8_t index, float remainingSec) {
    if (index < slots_.size()) {
        slots_[index].cooldownRemaining = remainingSec;
    }
}

void StanceBarDisplay::SetSlotUsable(uint8_t index, bool usable) {
    if (index < slots_.size()) {
        slots_[index].isUsable = usable;
    }
}

bool StanceBarDisplay::IsVisible() const {
    return !slots_.empty();
}

void StanceBarDisplay::Clear() {
    slots_.clear();
}

void StanceBarDisplay::UpdateCooldowns(float dt) {
    for (auto& s : slots_) {
        if (s.cooldownRemaining > 0.0f) {
            s.cooldownRemaining -= dt;
            if (s.cooldownRemaining < 0.0f) {
                s.cooldownRemaining = 0.0f;
            }
        }
    }
}

std::string StanceBarDisplay::GetFormLabel(StanceFormType form) {
    switch (form) {
        case StanceFormType::None:              return "None";
        case StanceFormType::Cat:               return "Cat Form";
        case StanceFormType::Tree:              return "Tree of Life";
        case StanceFormType::Travel:            return "Travel Form";
        case StanceFormType::Aquatic:           return "Aquatic Form";
        case StanceFormType::Bear:              return "Bear Form";
        case StanceFormType::DireBear:          return "Dire Bear Form";
        case StanceFormType::Moonkin:           return "Moonkin Form";
        case StanceFormType::Flight:            return "Flight Form";
        case StanceFormType::FlightEpic:        return "Swift Flight Form";
        case StanceFormType::BattleStance:      return "Battle Stance";
        case StanceFormType::DefensiveStance:   return "Defensive Stance";
        case StanceFormType::BerserkerStance:   return "Berserker Stance";
        case StanceFormType::Stealth:           return "Stealth";
        case StanceFormType::StealthShadow:     return "Shadow Stealth";
        case StanceFormType::Shadow:            return "Shadowform";
        case StanceFormType::GhostWolf:         return "Ghost Wolf";
        case StanceFormType::Metamorphosis:     return "Metamorphosis";
        case StanceFormType::SpiritOfRedemption: return "Spirit of Redemption";
        default:                                return "Unknown";
    }
}

std::optional<uint8_t> StanceBarDisplay::FindSlotByFormType(
    StanceFormType form) const {
    for (uint8_t i = 0; i < static_cast<uint8_t>(slots_.size()); ++i) {
        if (slots_[i].formType == form) return i;
    }
    return std::nullopt;
}

std::optional<uint8_t> StanceBarDisplay::FindSlotBySpellId(
    uint32_t spellId) const {
    for (uint8_t i = 0; i < static_cast<uint8_t>(slots_.size()); ++i) {
        if (slots_[i].spellId == spellId) return i;
    }
    return std::nullopt;
}

bool StanceBarDisplay::CanSwitchTo(uint8_t index) const {
    if (index >= slots_.size()) return false;
    const auto& s = slots_[index];

    return s.isUsable && s.cooldownRemaining <= 0.0f && !s.isActive;
}

bool StanceBarDisplay::HasActiveForm() const {
    for (const auto& s : slots_) {
        if (s.isActive) return true;
    }
    return false;
}

}

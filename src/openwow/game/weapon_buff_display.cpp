
#include "openwow/game/weapon_buff_display.h"

namespace openwow::game {

std::optional<WeaponBuffDisplayInfo> const& WeaponBuffDisplay::SlotRef(WeaponBuffSlotType s) const {
    switch (s) {
        case WeaponBuffSlotType::MainHandBuff: return mainHand_;
        case WeaponBuffSlotType::OffHandBuff:  return offHand_;
        case WeaponBuffSlotType::RangedBuff:   return ranged_;
    }
    return mainHand_;
}

std::optional<WeaponBuffDisplayInfo>& WeaponBuffDisplay::SlotRef(WeaponBuffSlotType s) {
    switch (s) {
        case WeaponBuffSlotType::MainHandBuff: return mainHand_;
        case WeaponBuffSlotType::OffHandBuff:  return offHand_;
        case WeaponBuffSlotType::RangedBuff:   return ranged_;
    }
    return mainHand_;
}

void WeaponBuffDisplay::SetBuff(WeaponBuffDisplayInfo info) {
    SlotRef(info.slot) = std::move(info);
}

void WeaponBuffDisplay::RemoveBuff(WeaponBuffSlotType slot) {
    SlotRef(slot).reset();
}

void WeaponBuffDisplay::ClearAll() {
    mainHand_.reset();
    offHand_.reset();
    ranged_.reset();
}

std::optional<WeaponBuffDisplayInfo> WeaponBuffDisplay::GetBuff(WeaponBuffSlotType slot) const {
    return SlotRef(slot);
}

bool WeaponBuffDisplay::HasBuff(WeaponBuffSlotType slot) const {
    return SlotRef(slot).has_value();
}

bool WeaponBuffDisplay::HasAnyBuff() const {
    return mainHand_.has_value() || offHand_.has_value() || ranged_.has_value();
}

uint32_t WeaponBuffDisplay::GetBuffCount() const {
    uint32_t n = 0;
    if (mainHand_) ++n;
    if (offHand_)  ++n;
    if (ranged_)   ++n;
    return n;
}

std::vector<WeaponBuffDisplayInfo> WeaponBuffDisplay::GetActiveBuffs() const {
    std::vector<WeaponBuffDisplayInfo> out;
    if (mainHand_) out.push_back(*mainHand_);
    if (offHand_)  out.push_back(*offHand_);
    if (ranged_)   out.push_back(*ranged_);
    return out;
}

void WeaponBuffDisplay::Update(float deltaTime) {
    auto tick = [&](std::optional<WeaponBuffDisplayInfo>& opt) {
        if (!opt) return;
        opt->remainingDuration -= deltaTime;
        if (opt->remainingDuration < 0.0f) opt->remainingDuration = 0.0f;
    };
    tick(mainHand_);
    tick(offHand_);
    tick(ranged_);
}

float WeaponBuffDisplay::GetDurationPercent(WeaponBuffSlotType slot) const {
    auto const& opt = SlotRef(slot);
    if (!opt) return 0.0f;
    if (opt->totalDuration <= 0.0f) return 0.0f;
    return opt->remainingDuration / opt->totalDuration;
}

bool WeaponBuffDisplay::IsExpiring(WeaponBuffSlotType slot) const {
    auto const& opt = SlotRef(slot);
    if (!opt) return false;
    return opt->remainingDuration < 120.0f;
}

std::string WeaponBuffDisplay::GetSlotName(WeaponBuffSlotType slot) {
    switch (slot) {
        case WeaponBuffSlotType::MainHandBuff: return "Main Hand";
        case WeaponBuffSlotType::OffHandBuff:  return "Off Hand";
        case WeaponBuffSlotType::RangedBuff:   return "Ranged";
    }
    return "Unknown";
}

}

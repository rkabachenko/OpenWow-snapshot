
#include "openwow/game/paper_doll.h"

#include <cstdio>

namespace openwow::game {

void PaperDollStats::SetStat(const std::string& statName,
                             float baseValue, float totalValue,
                             float positiveBonus, float negativeBonus) {
    std::lock_guard lock(mutex_);
    auto& sv     = stats_[statName];
    sv.base      = baseValue;
    sv.total     = totalValue;
    sv.posBonus  = positiveBonus;
    sv.negBonus  = negativeBonus;
}

StatValue PaperDollStats::GetStat(const std::string& statName) const {
    std::lock_guard lock(mutex_);
    auto it = stats_.find(statName);
    if (it != stats_.end()) return it->second;
    return {};
}

std::vector<StatLine> PaperDollStats::GetStatLines(StatCategory category) const {
    std::lock_guard lock(mutex_);
    std::vector<StatLine> result;

    auto inCategory = [&](const std::string& name) -> bool {
        switch (category) {
            case StatCategory::Attributes:
                return name == "Strength" || name == "Agility" ||
                       name == "Stamina" || name == "Intellect" || name == "Spirit";
            case StatCategory::MeleeAttack:
                return name == "MeleeDamage" || name == "MeleeSpeed" ||
                       name == "MeleePower" || name == "MeleeHit" ||
                       name == "MeleeCrit" || name == "Expertise";
            case StatCategory::RangedAttack:
                return name == "RangedDamage" || name == "RangedSpeed" ||
                       name == "RangedPower" || name == "RangedHit" ||
                       name == "RangedCrit";
            case StatCategory::SpellAttack:
                return name == "SpellPower" || name == "SpellHit" ||
                       name == "SpellCrit" || name == "SpellHaste" ||
                       name == "ManaRegen";
            case StatCategory::Defenses:
                return name == "Armor" || name == "Defense" || name == "Dodge" ||
                       name == "Parry" || name == "Block" || name == "Resilience";
            case StatCategory::Resistances:
                return name == "HolyResist" || name == "FireResist" ||
                       name == "NatureResist" || name == "FrostResist" ||
                       name == "ShadowResist" || name == "ArcaneResist";
        }
        return false;
    };

    for (const auto& [name, sv] : stats_) {
        if (!inCategory(name)) continue;

        char valueBuf[64];
        std::snprintf(valueBuf, sizeof(valueBuf), "%.0f", sv.total);

        char tooltipBuf[256];
        std::snprintf(tooltipBuf, sizeof(tooltipBuf),
                      "Base: %.0f\nGreen bonus: +%.0f\nRed penalty: %.0f",
                      sv.base, sv.posBonus, sv.negBonus);

        StatLine sl;
        sl.label   = name;
        sl.value   = valueBuf;
        sl.tooltip = tooltipBuf;
        result.push_back(std::move(sl));
    }
    return result;
}

void PaperDollStats::SetEquippedItem(uint32_t slot, uint32_t itemId,
                                     uint32_t displayId, uint32_t enchantId) {
    std::lock_guard lock(mutex_);
    EquippedItem ei;
    ei.itemId    = itemId;
    ei.displayId = displayId;
    ei.enchantId = enchantId;
    equipment_[slot] = ei;
}

EquippedItem PaperDollStats::GetEquippedItem(uint32_t slot) const {
    std::lock_guard lock(mutex_);
    auto it = equipment_.find(slot);
    if (it != equipment_.end()) return it->second;
    return {};
}

void PaperDollStats::ClearSlot(uint32_t slot) {
    std::lock_guard lock(mutex_);
    equipment_.erase(slot);
    durability_.erase(slot);
}

float PaperDollStats::GetItemLevel() const {
    std::lock_guard lock(mutex_);
    if (equipment_.empty()) return 0.0f;

    uint32_t total = 0;
    uint32_t count = 0;
    for (const auto& [_, ei] : equipment_) {
        if (ei.itemId != 0) {
            total += ei.itemLevel;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(total) / static_cast<float>(count) : 0.0f;
}

uint32_t PaperDollStats::GetEquippedSlotCount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& [_, ei] : equipment_) {
        if (ei.itemId != 0) ++count;
    }
    return count;
}

void PaperDollStats::SetDurability(uint32_t slot, uint32_t current, uint32_t maximum) {
    std::lock_guard lock(mutex_);
    durability_[slot] = {current, maximum};
}

std::pair<uint32_t, uint32_t> PaperDollStats::GetDurability(uint32_t slot) const {
    std::lock_guard lock(mutex_);
    auto it = durability_.find(slot);
    if (it != durability_.end()) return {it->second.current, it->second.maximum};
    return {0, 0};
}

bool PaperDollStats::HasBrokenItems() const {
    std::lock_guard lock(mutex_);
    for (const auto& [_, d] : durability_) {
        if (d.maximum > 0 && d.current == 0) return true;
    }
    return false;
}

uint32_t PaperDollStats::GetRepairCost() const {
    std::lock_guard lock(mutex_);

    uint32_t cost = 0;
    for (const auto& [_, d] : durability_) {
        if (d.maximum > d.current) {
            cost += (d.maximum - d.current) * 10;
        }
    }
    return cost;
}

void PaperDollStats::Clear() {
    std::lock_guard lock(mutex_);
    stats_.clear();
    equipment_.clear();
    durability_.clear();
}

void PaperDollStats::Reset() {
    Clear();
}

void PaperDoll::SetSlot(PaperDollSlotId id, PaperDollSlot slot) {
    std::lock_guard lock(mutex_);
    slot.id      = id;
    slot.isEmpty = (slot.itemId == 0);
    slots_[static_cast<uint8_t>(id)] = std::move(slot);
}

void PaperDoll::ClearSlot(PaperDollSlotId id) {
    std::lock_guard lock(mutex_);
    slots_.erase(static_cast<uint8_t>(id));
}

std::optional<PaperDollSlot> PaperDoll::GetSlot(PaperDollSlotId id) const {
    std::lock_guard lock(mutex_);
    auto it = slots_.find(static_cast<uint8_t>(id));
    if (it != slots_.end()) return it->second;
    return std::nullopt;
}

std::vector<PaperDollSlot> PaperDoll::GetAllSlots() const {
    std::lock_guard lock(mutex_);
    std::vector<PaperDollSlot> result;
    result.reserve(slots_.size());
    for (const auto& [_, s] : slots_) {
        result.push_back(s);
    }
    return result;
}

std::vector<PaperDollSlot> PaperDoll::GetEquippedSlots() const {
    std::lock_guard lock(mutex_);
    std::vector<PaperDollSlot> result;
    for (const auto& [_, s] : slots_) {
        if (!s.isEmpty) result.push_back(s);
    }
    return result;
}

std::vector<PaperDollSlotId> PaperDoll::GetEmptySlots() const {
    std::lock_guard lock(mutex_);
    std::vector<PaperDollSlotId> result;
    for (uint8_t i = 0; i < kPaperDollTotalSlots; ++i) {
        auto it = slots_.find(i);
        if (it == slots_.end() || it->second.isEmpty) {
            result.push_back(static_cast<PaperDollSlotId>(i));
        }
    }
    return result;
}

size_t PaperDoll::GetEquippedCount() const {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& [_, s] : slots_) {
        if (!s.isEmpty) ++count;
    }
    return count;
}

float PaperDoll::GetAverageItemLevel() const {
    std::lock_guard lock(mutex_);

    uint32_t total = 0;
    uint32_t count = 0;
    for (const auto& [_, s] : slots_) {
        if (!s.isEmpty) {
            total += s.iconId;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(total) / static_cast<float>(count)
                     : 0.0f;
}

bool PaperDoll::HasWeapon() const {
    std::lock_guard lock(mutex_);
    auto mh = slots_.find(static_cast<uint8_t>(PaperDollSlotId::MainHand));
    if (mh != slots_.end() && !mh->second.isEmpty) return true;
    auto oh = slots_.find(static_cast<uint8_t>(PaperDollSlotId::OffHand));
    if (oh != slots_.end() && !oh->second.isEmpty) return true;
    auto rng = slots_.find(static_cast<uint8_t>(PaperDollSlotId::Ranged));
    if (rng != slots_.end() && !rng->second.isEmpty) return true;
    return false;
}

bool PaperDoll::IsDualWielding() const {
    std::lock_guard lock(mutex_);
    auto mh = slots_.find(static_cast<uint8_t>(PaperDollSlotId::MainHand));
    auto oh = slots_.find(static_cast<uint8_t>(PaperDollSlotId::OffHand));
    return (mh != slots_.end() && !mh->second.isEmpty) &&
           (oh != slots_.end() && !oh->second.isEmpty);
}

std::string PaperDoll::GetSlotName(PaperDollSlotId id) {
    switch (id) {
        case PaperDollSlotId::Head:     return "Head";
        case PaperDollSlotId::Neck:     return "Neck";
        case PaperDollSlotId::Shoulder: return "Shoulder";
        case PaperDollSlotId::Shirt:    return "Shirt";
        case PaperDollSlotId::Chest:    return "Chest";
        case PaperDollSlotId::Waist:    return "Waist";
        case PaperDollSlotId::Legs:     return "Legs";
        case PaperDollSlotId::Feet:     return "Feet";
        case PaperDollSlotId::Wrist:    return "Wrist";
        case PaperDollSlotId::Hands:    return "Hands";
        case PaperDollSlotId::Ring1:    return "Ring1";
        case PaperDollSlotId::Ring2:    return "Ring2";
        case PaperDollSlotId::Trinket1: return "Trinket1";
        case PaperDollSlotId::Trinket2: return "Trinket2";
        case PaperDollSlotId::Back:     return "Back";
        case PaperDollSlotId::MainHand: return "MainHand";
        case PaperDollSlotId::OffHand:  return "OffHand";
        case PaperDollSlotId::Ranged:   return "Ranged";
        case PaperDollSlotId::Tabard:   return "Tabard";
    }
    return "Unknown";
}

void PaperDoll::Reset() {
    std::lock_guard lock(mutex_);
    slots_.clear();
}

}

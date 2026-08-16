
#include "openwow/game/debuff_type_display.h"

#include <algorithm>

namespace openwow::game {

static void SortDebuffs(std::vector<DebuffTypeInfo>& v) {
    std::sort(v.begin(), v.end(), [](auto const& a, auto const& b) {

        if (a.isBossDebuff != b.isBossDebuff) return a.isBossDebuff;

        return a.priority > b.priority;
    });
}

void DebuffTypeDisplay::SetDebuffs(std::vector<DebuffTypeInfo> debuffs) {
    debuffs_ = std::move(debuffs);
    SortDebuffs(debuffs_);
}

std::vector<DebuffTypeInfo> DebuffTypeDisplay::GetDebuffs() const {
    return debuffs_;
}

std::vector<DebuffTypeInfo> DebuffTypeDisplay::GetDebuffsByType(DebuffDispelType type) const {
    std::vector<DebuffTypeInfo> out;
    for (auto const& d : debuffs_) {
        if (d.dispelType == type) out.push_back(d);
    }
    return out;
}

DebuffBorderColor DebuffTypeDisplay::GetBorderColor(DebuffDispelType type) {
    switch (type) {
        case DebuffDispelType::Magic:   return {0.2f, 0.6f, 1.0f, 1.0f};
        case DebuffDispelType::Curse:   return {0.6f, 0.0f, 1.0f, 1.0f};
        case DebuffDispelType::Disease: return {0.6f, 0.4f, 0.0f, 1.0f};
        case DebuffDispelType::Poison:  return {0.0f, 0.6f, 0.0f, 1.0f};
        case DebuffDispelType::Enrage:  return {1.0f, 0.2f, 0.2f, 1.0f};
        case DebuffDispelType::None:
        default:                        return {0.8f, 0.0f, 0.0f, 1.0f};
    }
}

std::vector<DebuffTypeInfo> DebuffTypeDisplay::GetBossDebuffs() const {
    std::vector<DebuffTypeInfo> out;
    for (auto const& d : debuffs_) {
        if (d.isBossDebuff) out.push_back(d);
    }
    return out;
}

bool DebuffTypeDisplay::HasDispelType(DebuffDispelType type) const {
    return std::any_of(debuffs_.begin(), debuffs_.end(),
        [type](auto const& d) { return d.dispelType == type; });
}

uint32_t DebuffTypeDisplay::GetCountByType(DebuffDispelType type) const {
    return static_cast<uint32_t>(std::count_if(debuffs_.begin(), debuffs_.end(),
        [type](auto const& d) { return d.dispelType == type; }));
}

uint32_t DebuffTypeDisplay::GetTotalCount() const {
    return static_cast<uint32_t>(debuffs_.size());
}

void DebuffTypeDisplay::RemoveDebuff(uint32_t spellId) {
    debuffs_.erase(
        std::remove_if(debuffs_.begin(), debuffs_.end(),
            [spellId](auto const& d) { return d.spellId == spellId; }),
        debuffs_.end());
}

void DebuffTypeDisplay::ClearAll() { debuffs_.clear(); }

bool DebuffTypeDisplay::CanDispel(DebuffDispelType type,
                                   std::string const& playerClass) {

    if (playerClass == "Mage")    return type == DebuffDispelType::Curse;
    if (playerClass == "Priest")  return type == DebuffDispelType::Magic ||
                                         type == DebuffDispelType::Disease;
    if (playerClass == "Paladin") return type == DebuffDispelType::Magic  ||
                                         type == DebuffDispelType::Poison ||
                                         type == DebuffDispelType::Disease;
    if (playerClass == "Druid")   return type == DebuffDispelType::Curse  ||
                                         type == DebuffDispelType::Poison;
    if (playerClass == "Shaman")  return type == DebuffDispelType::Disease;
    return false;
}

std::string DebuffTypeDisplay::GetDispelTypeName(DebuffDispelType type) {
    switch (type) {
        case DebuffDispelType::Magic:   return "Magic";
        case DebuffDispelType::Curse:   return "Curse";
        case DebuffDispelType::Disease: return "Disease";
        case DebuffDispelType::Poison:  return "Poison";
        case DebuffDispelType::Enrage:  return "Enrage";
        case DebuffDispelType::None:
        default:                        return "None";
    }
}

}

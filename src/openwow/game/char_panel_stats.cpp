
#include "openwow/game/char_panel_stats.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void CharPanelStats::SetStat(CharStatCategory cat,
                             const std::string& label,
                             const std::string& value,
                             const std::string& tooltip) {
    std::lock_guard lock(mutex_);
    auto& vec = catStats_[static_cast<uint8_t>(cat)];

    for (auto& sl : vec) {
        if (sl.label == label) {
            sl.value   = value;
            sl.tooltip = tooltip;
            return;
        }
    }
    CharStatLine sl;
    sl.label   = label;
    sl.value   = value;
    sl.tooltip = tooltip;
    vec.push_back(std::move(sl));
}

std::vector<CharStatLine> CharPanelStats::GetStats(CharStatCategory cat) const {
    std::lock_guard lock(mutex_);
    auto it = catStats_.find(static_cast<uint8_t>(cat));
    if (it != catStats_.end()) return it->second;
    return {};
}

std::vector<CharStatCategory> CharPanelStats::GetAllCategories() const {
    std::lock_guard lock(mutex_);
    std::vector<CharStatCategory> result;
    for (const auto& [key, _] : catStats_) {
        result.push_back(static_cast<CharStatCategory>(key));
    }

    std::sort(result.begin(), result.end());
    return result;
}

size_t CharPanelStats::GetStatCount(CharStatCategory cat) const {
    std::lock_guard lock(mutex_);
    auto it = catStats_.find(static_cast<uint8_t>(cat));
    if (it != catStats_.end()) return it->second.size();
    return 0;
}

void CharPanelStats::SetStrength(int32_t base, int32_t bonus) {
    std::lock_guard lock(mutex_);
    strength_ = {base, bonus};
}

std::pair<int32_t, int32_t> CharPanelStats::GetStrength() const {
    std::lock_guard lock(mutex_);
    return {strength_.base, strength_.base + strength_.bonus};
}

void CharPanelStats::SetAgility(int32_t base, int32_t bonus) {
    std::lock_guard lock(mutex_);
    agility_ = {base, bonus};
}

std::pair<int32_t, int32_t> CharPanelStats::GetAgility() const {
    std::lock_guard lock(mutex_);
    return {agility_.base, agility_.base + agility_.bonus};
}

void CharPanelStats::SetStamina(int32_t base, int32_t bonus) {
    std::lock_guard lock(mutex_);
    stamina_ = {base, bonus};
}

std::pair<int32_t, int32_t> CharPanelStats::GetStamina() const {
    std::lock_guard lock(mutex_);
    return {stamina_.base, stamina_.base + stamina_.bonus};
}

void CharPanelStats::SetIntellect(int32_t base, int32_t bonus) {
    std::lock_guard lock(mutex_);
    intellect_ = {base, bonus};
}

std::pair<int32_t, int32_t> CharPanelStats::GetIntellect() const {
    std::lock_guard lock(mutex_);
    return {intellect_.base, intellect_.base + intellect_.bonus};
}

void CharPanelStats::SetSpirit(int32_t base, int32_t bonus) {
    std::lock_guard lock(mutex_);
    spirit_ = {base, bonus};
}

std::pair<int32_t, int32_t> CharPanelStats::GetSpirit() const {
    std::lock_guard lock(mutex_);
    return {spirit_.base, spirit_.base + spirit_.bonus};
}

void CharPanelStats::SetArmor(int32_t value) {
    std::lock_guard lock(mutex_);
    armor_ = value;
}

int32_t CharPanelStats::GetArmor() const {
    std::lock_guard lock(mutex_);
    return armor_;
}

float CharPanelStats::GetDamageReduction(int32_t armor, uint8_t attackerLevel) {
    if (armor <= 0 || attackerLevel == 0) return 0.0f;

    float C;
    if (attackerLevel < 60) {
        C = 400.0f + 85.0f * static_cast<float>(attackerLevel);
    } else {
        C = 467.5f * static_cast<float>(attackerLevel) - 22167.5f;
    }

    if (C <= 0.0f) return 0.0f;

    float reduction = static_cast<float>(armor) /
                      (static_cast<float>(armor) + C);

    return std::clamp(reduction, 0.0f, 0.75f);
}

void CharPanelStats::SetSelectedCategory(CharStatCategory cat) {
    std::lock_guard lock(mutex_);
    selectedCat_ = cat;
}

CharStatCategory CharPanelStats::GetSelectedCategory() const {
    std::lock_guard lock(mutex_);
    return selectedCat_;
}

void CharPanelStats::Reset() {
    std::lock_guard lock(mutex_);
    catStats_.clear();
    strength_  = {};
    agility_   = {};
    stamina_   = {};
    intellect_ = {};
    spirit_    = {};
    armor_     = 0;
    selectedCat_ = CharStatCategory::Attributes;
}

}

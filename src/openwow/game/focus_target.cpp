
#include "openwow/game/focus_target.h"

#include <algorithm>

namespace openwow::game {

void FocusTargetSystem::SetFocus(ObjectGuid guid) { guid_ = guid; }
ObjectGuid FocusTargetSystem::GetFocus() const { return guid_; }
bool FocusTargetSystem::HasFocus() const { return !guid_.IsEmpty(); }
void FocusTargetSystem::ClearFocus() { Reset(); }

void FocusTargetSystem::SetFocusName(const std::string& name) { name_ = name; }
const std::string& FocusTargetSystem::GetFocusName() const { return name_; }

void FocusTargetSystem::SetFocusHealth(std::uint32_t current, std::uint32_t max) {
    healthCurrent_ = current;
    healthMax_     = max;
}

float FocusTargetSystem::GetFocusHealthPercent() const {
    if (healthMax_ == 0) return 0.0f;
    return static_cast<float>(healthCurrent_) / static_cast<float>(healthMax_) * 100.0f;
}

void FocusTargetSystem::SetFocusMana(std::uint32_t current, std::uint32_t max) {
    manaCurrent_ = current;
    manaMax_     = max;
}

float FocusTargetSystem::GetFocusManaPercent() const {
    if (manaMax_ == 0) return 0.0f;
    return static_cast<float>(manaCurrent_) / static_cast<float>(manaMax_) * 100.0f;
}

void FocusTargetSystem::SetFocusLevel(std::uint32_t level) { level_ = level; }
std::uint32_t FocusTargetSystem::GetFocusLevel() const { return level_; }

void FocusTargetSystem::SetFocusCasting(std::uint32_t spellId,
                                        const std::string& name,
                                        float progress) {
    castSpellId_  = spellId;
    castName_     = name;
    castProgress_ = progress;
}

std::uint32_t FocusTargetSystem::GetFocusCastSpellId() const { return castSpellId_; }

bool FocusTargetSystem::IsFocusCasting() const { return castSpellId_ != 0; }

void FocusTargetSystem::SetFocusAuras(const std::vector<std::uint32_t>& spellIds) {
    auras_ = spellIds;
}

const std::vector<std::uint32_t>& FocusTargetSystem::GetFocusAuras() const {
    return auras_;
}

bool FocusTargetSystem::HasFocusAura(std::uint32_t spellId) const {
    return std::find(auras_.begin(), auras_.end(), spellId) != auras_.end();
}

bool FocusTargetSystem::IsFocusFriendly() const { return reaction_ == 2; }
void FocusTargetSystem::SetFocusFriendly(bool friendly) {
    reaction_ = friendly ? 2 : 0;
}

void FocusTargetSystem::SetFocusReaction(std::uint32_t reaction) { reaction_ = reaction; }
std::uint32_t FocusTargetSystem::GetFocusReaction() const { return reaction_; }

void FocusTargetSystem::Reset() {
    guid_          = ObjectGuid{};
    name_.clear();
    healthCurrent_ = 0;
    healthMax_     = 0;
    manaCurrent_   = 0;
    manaMax_       = 0;
    level_         = 0;
    castSpellId_   = 0;
    castName_.clear();
    castProgress_  = 0.0f;
    auras_.clear();
    reaction_      = 0;
}

}

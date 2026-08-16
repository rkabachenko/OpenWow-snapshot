
#include "openwow/game/player_unit_frame_data.h"

#include <algorithm>

namespace openwow::game {

void PlayerUnitFrameData::SetName(const std::string& name) { name_ = name; }
const std::string& PlayerUnitFrameData::GetName() const { return name_; }

void PlayerUnitFrameData::SetLevel(std::uint8_t level) { level_ = level; }
std::uint8_t PlayerUnitFrameData::GetLevel() const { return level_; }

void PlayerUnitFrameData::SetClass(std::uint8_t classId) { classId_ = classId; }
std::uint8_t PlayerUnitFrameData::GetClass() const { return classId_; }

void PlayerUnitFrameData::SetHealth(std::uint32_t current, std::uint32_t max) {
    healthCurrent_ = current;
    healthMax_     = max;
}

float PlayerUnitFrameData::GetHealthPercent() const {
    if (healthMax_ == 0) return 0.0f;
    return static_cast<float>(healthCurrent_) / static_cast<float>(healthMax_) * 100.0f;
}

void PlayerUnitFrameData::SetPower(UnitFramePowerType type,
                                   std::uint32_t current,
                                   std::uint32_t max) {
    powerType_    = type;
    powerCurrent_ = current;
    powerMax_     = max;
}

float PlayerUnitFrameData::GetPowerPercent() const {
    if (powerMax_ == 0) return 0.0f;
    return static_cast<float>(powerCurrent_) / static_cast<float>(powerMax_) * 100.0f;
}

UnitFramePowerType PlayerUnitFrameData::GetPowerType() const {
    return powerType_;
}

UnitFramePowerColor PlayerUnitFrameData::GetPowerColor() const {
    return GetDefaultPowerColor(powerType_);
}

void PlayerUnitFrameData::SetPortraitDisplayId(std::uint32_t displayId) {
    portraitDisplayId_ = displayId;
}

std::uint32_t PlayerUnitFrameData::GetPortraitDisplayId() const {
    return portraitDisplayId_;
}

void PlayerUnitFrameData::SetCombatState(bool combat) { inCombat_ = combat; }
bool PlayerUnitFrameData::IsCombatState() const { return inCombat_; }

void PlayerUnitFrameData::SetResting(bool resting) { resting_ = resting; }
bool PlayerUnitFrameData::IsResting() const { return resting_; }

void PlayerUnitFrameData::SetAFK(bool afk) { afk_ = afk; }
bool PlayerUnitFrameData::IsAFK() const { return afk_; }

void PlayerUnitFrameData::SetDND(bool dnd) { dnd_ = dnd; }
bool PlayerUnitFrameData::IsDND() const { return dnd_; }

void PlayerUnitFrameData::SetGroupRole(const std::string& role) {
    groupRole_ = role;
}

const std::string& PlayerUnitFrameData::GetGroupRole() const {
    return groupRole_;
}

void PlayerUnitFrameData::SetAuras(const std::vector<UnitFrameAuraIcon>& auras) {
    auras_ = auras;
}

const std::vector<UnitFrameAuraIcon>& PlayerUnitFrameData::GetAuras() const {
    return auras_;
}

std::uint32_t PlayerUnitFrameData::GetBuffCount() const {
    return static_cast<std::uint32_t>(
        std::count_if(auras_.begin(), auras_.end(),
                      [](const UnitFrameAuraIcon& a) { return !a.isDebuff; }));
}

std::uint32_t PlayerUnitFrameData::GetDebuffCount() const {
    return static_cast<std::uint32_t>(
        std::count_if(auras_.begin(), auras_.end(),
                      [](const UnitFrameAuraIcon& a) { return a.isDebuff; }));
}

bool PlayerUnitFrameData::HasAura(std::uint32_t spellId) const {
    return std::any_of(auras_.begin(), auras_.end(),
                       [spellId](const UnitFrameAuraIcon& a) {
                           return a.spellId == spellId;
                       });
}

UnitFramePowerColor PlayerUnitFrameData::GetDefaultPowerColor(
    UnitFramePowerType type) {
    switch (type) {
        case UnitFramePowerType::Mana:       return {0.0f, 0.0f, 1.0f};
        case UnitFramePowerType::Rage:       return {1.0f, 0.0f, 0.0f};
        case UnitFramePowerType::Focus:      return {1.0f, 0.5f, 0.25f};
        case UnitFramePowerType::Energy:     return {1.0f, 1.0f, 0.0f};
        case UnitFramePowerType::Happiness:  return {0.0f, 1.0f, 1.0f};
        case UnitFramePowerType::Runic:      return {0.0f, 0.2f, 0.6f};
        case UnitFramePowerType::RunicPower: return {0.0f, 0.82f, 1.0f};
        default:                             return {0.0f, 0.0f, 1.0f};
    }
}

}

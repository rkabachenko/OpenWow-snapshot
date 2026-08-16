
#include "openwow/game/aggro_range_display.h"

#include <algorithm>

namespace openwow::game {

static constexpr float kBaseAggroRange = 20.0f;
static constexpr float kMinAggroRange  = 5.0f;
static constexpr float kMaxAggroRange  = 45.0f;
static constexpr float kEliteBonus     = 5.0f;
static constexpr float kAlertRangeMul  = 1.5f;

float AggroRangeDisplay::CalculateAggroRange(std::uint8_t playerLevel,
                                             std::uint8_t creatureLevel,
                                             bool isElite) {
    float range =
        kBaseAggroRange -
        (static_cast<float>(playerLevel) - static_cast<float>(creatureLevel));

    if (isElite) {
        range += kEliteBonus;
    }

    return std::clamp(range, kMinAggroRange, kMaxAggroRange);
}

float AggroRangeDisplay::CalculateDetectionRange(float baseRange,
                                                 bool stealth,
                                                 float stealthLevel) {
    if (!stealth || stealthLevel <= 0.0f) {
        return baseRange;
    }

    float reduced = baseRange - (baseRange * stealthLevel);
    return std::max(0.0f, reduced);
}

void AggroRangeDisplay::SetPlayerLevel(std::uint8_t level) {
    playerLevel_ = level;
}

std::uint8_t AggroRangeDisplay::GetPlayerLevel() const {
    return playerLevel_;
}

bool AggroRangeDisplay::IsInAggroRange(float distance,
                                       std::uint8_t creatureLevel,
                                       bool isElite) const {
    float range = CalculateAggroRange(playerLevel_, creatureLevel, isElite);
    return distance <= range;
}

AggroRangeInfo AggroRangeDisplay::GetAggroInfo(std::uint8_t creatureLevel,
                                               bool isElite, bool stealth,
                                               float stealthLevel) const {
    AggroRangeInfo info{};
    info.creatureLevel = creatureLevel;
    info.playerLevel = playerLevel_;
    info.isElite = isElite;
    info.baseRange =
        CalculateAggroRange(playerLevel_, creatureLevel, isElite);
    info.detectionRange =
        CalculateDetectionRange(info.baseRange, stealth, stealthLevel);
    info.alertRange = info.baseRange * kAlertRangeMul;

    info.isAggressive = (info.baseRange > kMinAggroRange);
    return info;
}

void AggroRangeDisplay::SetShowAggroRadius(bool show) {
    showAggroRadius_ = show;
}

bool AggroRangeDisplay::IsShowingAggroRadius() const {
    return showAggroRadius_;
}

void AggroRangeDisplay::Reset() {
    playerLevel_ = 1;
    showAggroRadius_ = false;
}

}

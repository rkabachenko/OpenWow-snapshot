
#include "openwow/game/threat_display.h"

#include <algorithm>
#include <cstdio>

namespace openwow::game {

ThreatDisplay& ThreatDisplay::Get() {
    static ThreatDisplay instance;
    return instance;
}

void ThreatDisplay::SetMyThreat(const ObjectGuid& target,
                                std::uint32_t threat, float percent) {
    target_ = target;
    my_threat_value_ = threat;
    my_threat_percent_ = percent;
}

float ThreatDisplay::GetMyThreatPercent() const {
    return my_threat_percent_;
}

ThreatDisplayStatus ThreatDisplay::GetMyThreatStatus() const {
    return ClassifyThreat(my_threat_percent_);
}

void ThreatDisplay::SetThreatList(const ObjectGuid& target,
                                  std::vector<ThreatDisplayEntry> list) {
    target_ = target;
    threat_list_ = std::move(list);
}

const std::vector<ThreatDisplayEntry>& ThreatDisplay::GetThreatList() const {
    return threat_list_;
}

ObjectGuid ThreatDisplay::GetThreatTarget() const {
    return target_;
}

bool ThreatDisplay::IsTanking() const {

    return my_threat_percent_ >= 100.0f;
}

float ThreatDisplay::GetAggroThreshold() const {
    return is_melee_ ? 110.0f : 130.0f;
}

void ThreatDisplay::SetIsMelee(bool melee) {
    is_melee_ = melee;
}

bool ThreatDisplay::IsMelee() const {
    return is_melee_;
}

float ThreatDisplay::GetThreatPct() const {
    return my_threat_percent_;
}

bool ThreatDisplay::IsOnThreatList() const {
    return my_threat_percent_ > 0.0f || !target_.IsEmpty();
}

std::size_t ThreatDisplay::GetListSize() const {
    return threat_list_.size();
}

void ThreatDisplay::Reset() {
    target_ = ObjectGuid{};
    my_threat_value_ = 0;
    my_threat_percent_ = 0.0f;
    threat_list_.clear();
    is_melee_ = true;
}

ThreatDisplayStatus ThreatDisplay::ClassifyThreat(float pct) const {
    const float threshold = GetAggroThreshold();
    if (pct >= 100.0f) return ThreatDisplayStatus::ThreatHasAggro;
    if (pct >= threshold - 10.0f) return ThreatDisplayStatus::ThreatPullingAggro;
    if (pct >= 80.0f) return ThreatDisplayStatus::ThreatHigh;
    if (pct >= 50.0f) return ThreatDisplayStatus::ThreatWarning;
    return ThreatDisplayStatus::ThreatSafe;
}

std::optional<ThreatDisplayEntry> ThreatDisplay::GetTopThreatEntry() const {
    if (threat_list_.empty()) return std::nullopt;
    auto it = std::max_element(
        threat_list_.begin(), threat_list_.end(),
        [](const ThreatDisplayEntry& a, const ThreatDisplayEntry& b) {
            return a.threatValue < b.threatValue;
        });
    return *it;
}

void ThreatDisplay::SortByThreat() {
    std::sort(threat_list_.begin(), threat_list_.end(),
              [](const ThreatDisplayEntry& a, const ThreatDisplayEntry& b) {
                  return a.threatValue > b.threatValue;
              });
}

std::size_t ThreatDisplay::GetMyRank() const {
    if (threat_list_.empty() || my_threat_percent_ <= 0.0f) return 0;

    auto sorted = threat_list_;
    std::sort(sorted.begin(), sorted.end(),
              [](const ThreatDisplayEntry& a, const ThreatDisplayEntry& b) {
                  return a.threatValue > b.threatValue;
              });

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i].threatValue == my_threat_value_) {
            return i + 1;
        }
    }
    return 0;
}

std::uint32_t ThreatDisplay::GetColorForStatus(ThreatDisplayStatus status) {

    switch (status) {
        case ThreatDisplayStatus::ThreatSafe:
            return 0xFF00FF00;
        case ThreatDisplayStatus::ThreatWarning:
            return 0xFFFFFF00;
        case ThreatDisplayStatus::ThreatHigh:
            return 0xFFFF8800;
        case ThreatDisplayStatus::ThreatPullingAggro:
            return 0xFFFF4400;
        case ThreatDisplayStatus::ThreatHasAggro:
            return 0xFFFF0000;
        default:
            return 0xFFFFFFFF;
    }
}

std::string ThreatDisplay::GetStatusLabel(ThreatDisplayStatus status) {
    switch (status) {
        case ThreatDisplayStatus::ThreatSafe:         return "Safe";
        case ThreatDisplayStatus::ThreatWarning:      return "Warning";
        case ThreatDisplayStatus::ThreatHigh:         return "High";
        case ThreatDisplayStatus::ThreatPullingAggro: return "Pulling Aggro";
        case ThreatDisplayStatus::ThreatHasAggro:     return "Has Aggro";
        default:                                      return "Unknown";
    }
}

std::string ThreatDisplay::FormatThreatValue(std::uint32_t threat) {
    char buf[32];
    if (threat >= 1'000'000) {
        std::snprintf(buf, sizeof(buf), "%.1fM",
                      static_cast<float>(threat) / 1'000'000.0f);
    } else if (threat >= 1'000) {
        std::snprintf(buf, sizeof(buf), "%.1fk",
                      static_cast<float>(threat) / 1'000.0f);
    } else {
        std::snprintf(buf, sizeof(buf), "%u", threat);
    }
    return std::string(buf);
}

}

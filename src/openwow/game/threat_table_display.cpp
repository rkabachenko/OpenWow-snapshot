
#include "openwow/game/threat_table_display.h"

#include <algorithm>

namespace openwow::game {

void ThreatTableDisplay::SetTarget(ObjectGuid target) {
    target_ = target;
}

ObjectGuid ThreatTableDisplay::GetTarget() const {
    return target_;
}

void ThreatTableDisplay::SetEntries(std::vector<ThreatTableEntry> entries) {
    entries_ = std::move(entries);
}

const std::vector<ThreatTableEntry>& ThreatTableDisplay::GetEntries() const {
    return entries_;
}

std::optional<ThreatTableEntry> ThreatTableDisplay::GetTopThreat() const {
    if (entries_.empty()) {
        return std::nullopt;
    }

    auto it = std::max_element(
        entries_.begin(), entries_.end(),
        [](const ThreatTableEntry& a, const ThreatTableEntry& b) {
            return a.threatAmount < b.threatAmount;
        });
    return *it;
}

std::optional<ThreatTableEntry> ThreatTableDisplay::GetPlayerThreat(
    ObjectGuid playerGuid) const {
    for (const auto& entry : entries_) {
        if (entry.unitGuid.GetRawValue() == playerGuid.GetRawValue()) {
            return entry;
        }
    }
    return std::nullopt;
}

std::size_t ThreatTableDisplay::GetEntryCount() const {
    return entries_.size();
}

std::uint32_t ThreatTableDisplay::GetPlayerRank(
    ObjectGuid playerGuid) const {
    for (const auto& entry : entries_) {
        if (entry.unitGuid.GetRawValue() == playerGuid.GetRawValue()) {
            return entry.rank;
        }
    }
    return 0;
}

float ThreatTableDisplay::GetPlayerThreatPercent(
    ObjectGuid playerGuid) const {
    for (const auto& entry : entries_) {
        if (entry.unitGuid.GetRawValue() == playerGuid.GetRawValue()) {
            return entry.threatPercent;
        }
    }
    return 0.0f;
}

bool ThreatTableDisplay::IsPlayerTanking(ObjectGuid playerGuid) const {
    for (const auto& entry : entries_) {
        if (entry.unitGuid.GetRawValue() == playerGuid.GetRawValue()) {
            return entry.isTanking;
        }
    }
    return false;
}

void ThreatTableDisplay::SetUpdateRate(float hz) {
    updateRateHz_ = hz;
}

float ThreatTableDisplay::GetUpdateRate() const {
    return updateRateHz_;
}

bool ThreatTableDisplay::ShouldUpdate(float dt) {
    if (updateRateHz_ <= 0.0f) {
        return false;
    }
    updateAccumulator_ += dt;
    float interval = 1.0f / updateRateHz_;
    if (updateAccumulator_ >= interval) {
        updateAccumulator_ -= interval;
        return true;
    }
    return false;
}

void ThreatTableDisplay::Sort() {
    std::sort(entries_.begin(), entries_.end(),
              [](const ThreatTableEntry& a, const ThreatTableEntry& b) {
                  return a.threatAmount > b.threatAmount;
              });

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].rank = static_cast<std::uint32_t>(i + 1);
    }
}

void ThreatTableDisplay::Clear() {
    target_ = ObjectGuid{};
    entries_.clear();
    updateAccumulator_ = 0.0f;
}

}

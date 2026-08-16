
#include "openwow/game/diminishing_returns.h"

#include <algorithm>

namespace openwow::game {

DRLevel DiminishingReturnsTracker::ApplyDR(ObjectGuid target,
                                           DRCategory cat) {
    TargetKey key{target.GetRawValue(), cat};
    auto it = entries_.find(key);

    if (it == entries_.end()) {

        DREntry entry{};
        entry.category = cat;
        entry.level = DRLevel::Half;
        entry.resetTimer = DREntry::kResetTime;
        entries_[key] = entry;
        return DRLevel::Full;
    }

    auto& entry = it->second;

    DRLevel current = entry.level;

    entry.resetTimer = DREntry::kResetTime;

    if (current == DRLevel::Half) {
        entry.level = DRLevel::Quarter;
    } else if (current == DRLevel::Quarter) {
        entry.level = DRLevel::Immune;
    }

    return current;
}

DRLevel DiminishingReturnsTracker::GetDRLevel(ObjectGuid target,
                                              DRCategory cat) const {
    TargetKey key{target.GetRawValue(), cat};
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return DRLevel::Full;
    }
    return it->second.level;
}

float DiminishingReturnsTracker::GetTimeUntilReset(ObjectGuid target,
                                                   DRCategory cat) const {
    TargetKey key{target.GetRawValue(), cat};
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return 0.0f;
    }
    return it->second.resetTimer;
}

float DiminishingReturnsTracker::GetDurationMultiplier(DRLevel level) {
    switch (level) {
        case DRLevel::Full:    return 1.0f;
        case DRLevel::Half:    return 0.5f;
        case DRLevel::Quarter: return 0.25f;
        case DRLevel::Immune:  return 0.0f;
    }
    return 0.0f;
}

std::vector<DREntry> DiminishingReturnsTracker::GetAllDR(
    ObjectGuid target) const {
    std::vector<DREntry> result;
    auto guid = target.GetRawValue();
    for (const auto& [key, entry] : entries_) {
        if (key.guid == guid) {
            result.push_back(entry);
        }
    }
    return result;
}

bool DiminishingReturnsTracker::IsImmune(ObjectGuid target,
                                         DRCategory cat) const {
    return GetDRLevel(target, cat) == DRLevel::Immune;
}

void DiminishingReturnsTracker::Update(float dt) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        it->second.resetTimer -= dt;
        if (it->second.resetTimer <= 0.0f) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t DiminishingReturnsTracker::GetTrackedTargetCount() const {

    std::unordered_map<std::uint64_t, bool> seen;
    for (const auto& [key, entry] : entries_) {
        seen[key.guid] = true;
    }
    return seen.size();
}

std::string DiminishingReturnsTracker::GetCategoryName(DRCategory cat) {
    switch (cat) {
        case DRCategory::Stun:         return "Stun";
        case DRCategory::Fear:         return "Fear";
        case DRCategory::Root:         return "Root";
        case DRCategory::Incapacitate: return "Incapacitate";
        case DRCategory::Silence:      return "Silence";
        case DRCategory::Disarm:       return "Disarm";
        case DRCategory::Horror:       return "Horror";
        case DRCategory::Cyclone:      return "Cyclone";
        case DRCategory::Banish:       return "Banish";
        case DRCategory::Disorient:    return "Disorient";
        case DRCategory::FrostShock:   return "FrostShock";
    }
    return "Unknown";
}

void DiminishingReturnsTracker::Clear() {
    entries_.clear();
}

}

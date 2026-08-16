
#include "openwow/game/reputation_detail.h"

namespace openwow::game {

void RepDetailSystem::SetFaction(const RepDetailEntry& entry) {
    std::lock_guard lock(mutex_);
    factions_[entry.factionId] = entry;
}

std::optional<RepDetailEntry> RepDetailSystem::GetFaction(uint32_t factionId) const {
    std::lock_guard lock(mutex_);
    auto it = factions_.find(factionId);
    if (it == factions_.end()) return std::nullopt;
    return it->second;
}

std::vector<RepDetailEntry> RepDetailSystem::GetAllFactions() const {
    std::lock_guard lock(mutex_);
    std::vector<RepDetailEntry> result;
    result.reserve(factions_.size());
    for (const auto& [id, entry] : factions_) {
        result.push_back(entry);
    }
    return result;
}

std::vector<RepDetailEntry> RepDetailSystem::GetFactionsByHeader(
    uint32_t headerIndex) const {
    std::lock_guard lock(mutex_);
    std::vector<RepDetailEntry> result;
    for (const auto& [id, entry] : factions_) {
        if (entry.headerIndex == headerIndex) {
            result.push_back(entry);
        }
    }
    return result;
}

void RepDetailSystem::SetWatched(uint32_t factionId) {
    std::lock_guard lock(mutex_);

    if (watched_faction_) {
        auto it = factions_.find(*watched_faction_);
        if (it != factions_.end()) {
            it->second.watched = false;
        }
    }
    watched_faction_ = factionId;
    auto it = factions_.find(factionId);
    if (it != factions_.end()) {
        it->second.watched = true;
    }
}

std::optional<RepDetailEntry> RepDetailSystem::GetWatched() const {
    std::lock_guard lock(mutex_);
    if (!watched_faction_) return std::nullopt;
    auto it = factions_.find(*watched_faction_);
    if (it == factions_.end()) return std::nullopt;
    return it->second;
}

void RepDetailSystem::ToggleAtWar(uint32_t factionId) {
    std::lock_guard lock(mutex_);
    auto it = factions_.find(factionId);
    if (it != factions_.end()) {
        it->second.atWar = !it->second.atWar;
    }
}

void RepDetailSystem::ToggleInactive(uint32_t factionId) {
    std::lock_guard lock(mutex_);
    auto it = factions_.find(factionId);
    if (it != factions_.end()) {
        it->second.inactive = !it->second.inactive;
    }
}

std::string RepDetailSystem::GetStandingLabel(RepStandingLevel level) {
    switch (level) {
        case RepStandingLevel::Hated:      return "Hated";
        case RepStandingLevel::Hostile:    return "Hostile";
        case RepStandingLevel::Unfriendly: return "Unfriendly";
        case RepStandingLevel::Neutral:    return "Neutral";
        case RepStandingLevel::Friendly:   return "Friendly";
        case RepStandingLevel::Honored:    return "Honored";
        case RepStandingLevel::Revered:    return "Revered";
        case RepStandingLevel::Exalted:    return "Exalted";
    }
    return "Unknown";
}

float RepDetailSystem::GetProgressPercent(uint32_t factionId) const {
    std::lock_guard lock(mutex_);
    auto it = factions_.find(factionId);
    if (it == factions_.end()) return 0.0f;
    const auto& e = it->second;
    if (e.max <= 0) return 0.0f;
    float pct = static_cast<float>(e.current) / static_cast<float>(e.max);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    return pct;
}

size_t RepDetailSystem::GetFactionCount() const {
    std::lock_guard lock(mutex_);
    return factions_.size();
}

size_t RepDetailSystem::GetExaltedCount() const {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& [id, entry] : factions_) {
        if (entry.level == RepStandingLevel::Exalted) {
            ++count;
        }
    }
    return count;
}

void RepDetailSystem::Reset() {
    std::lock_guard lock(mutex_);
    factions_.clear();
    watched_faction_.reset();
}

}

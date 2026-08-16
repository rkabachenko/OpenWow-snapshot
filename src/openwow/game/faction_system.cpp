
#include "openwow/game/faction_system.h"

namespace openwow::game {

FactionSystem& FactionSystem::Get() {
    static FactionSystem instance;
    return instance;
}

void FactionSystem::SetFactions(const std::vector<FactionInfo>& factions) {
    std::lock_guard lock(mutex_);
    factions_ = factions;
}

size_t FactionSystem::GetNumFactions() const {
    std::lock_guard lock(mutex_);
    return factions_.size();
}

const FactionInfo* FactionSystem::GetFaction(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= factions_.size()) return nullptr;
    return &factions_[index];
}

const FactionInfo* FactionSystem::GetFactionById(uint32_t factionId) const {
    std::lock_guard lock(mutex_);
    for (const auto& f : factions_) {
        if (f.faction_id == factionId) return &f;
    }
    return nullptr;
}

std::string FactionSystem::StandingText(FactionStanding standing) {
    switch (standing) {
        case FactionStanding::Hated:      return "Hated";
        case FactionStanding::Hostile:    return "Hostile";
        case FactionStanding::Unfriendly: return "Unfriendly";
        case FactionStanding::Neutral:    return "Neutral";
        case FactionStanding::Friendly:   return "Friendly";
        case FactionStanding::Honored:    return "Honored";
        case FactionStanding::Revered:    return "Revered";
        case FactionStanding::Exalted:    return "Exalted";
    }
    return "Unknown";
}

FactionStanding FactionSystem::StandingFromRep(int32_t rep) {

    if (rep >= 42000) return FactionStanding::Exalted;
    if (rep >= 21000) return FactionStanding::Revered;
    if (rep >=  9000) return FactionStanding::Honored;
    if (rep >=  3000) return FactionStanding::Friendly;
    if (rep >=     0) return FactionStanding::Neutral;
    if (rep >= -3000) return FactionStanding::Unfriendly;
    if (rep >= -6000) return FactionStanding::Hostile;
    return FactionStanding::Hated;
}

void FactionSystem::SetWatchedFaction(uint32_t factionId) {
    std::lock_guard lock(mutex_);
    watched_faction_ = factionId;
}

uint32_t FactionSystem::GetWatchedFaction() const {
    std::lock_guard lock(mutex_);
    return watched_faction_;
}

void FactionSystem::SetSelectedFactionByIndex(size_t index) {
    std::lock_guard lock(mutex_);
    if (index >= factions_.size()) {
        selected_faction_id_.reset();
        return;
    }
    selected_faction_id_ = factions_[index].faction_id;
}

void FactionSystem::ClearSelectedFaction() {
    std::lock_guard lock(mutex_);
    selected_faction_id_.reset();
}

int FactionSystem::GetSelectedFactionIndex() const {
    std::lock_guard lock(mutex_);
    if (factions_.empty() || !selected_faction_id_) {
        return -1;
    }

    for (size_t i = 0; i < factions_.size(); ++i) {
        if (factions_[i].faction_id == *selected_faction_id_) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

void FactionSystem::SetCollapsed(size_t index, bool collapsed) {
    std::lock_guard lock(mutex_);
    if (index < factions_.size()) {
        factions_[index].is_collapsed = collapsed;
    }
}

void FactionSystem::SetAtWar(size_t index, bool atWar) {
    std::lock_guard lock(mutex_);
    if (index < factions_.size()) {
        factions_[index].at_war = atWar;
    }
}

void FactionSystem::SetInactive(size_t index, bool inactive) {
    std::lock_guard lock(mutex_);
    if (index < factions_.size()) {
        factions_[index].is_inactive = inactive;
    }
}

void FactionSystem::PushRepChange(const RepChange& change) {
    std::lock_guard lock(mutex_);
    rep_changes_.push(change);
}

bool FactionSystem::PopRepChange(RepChange& out) {
    std::lock_guard lock(mutex_);
    if (rep_changes_.empty()) return false;
    out = rep_changes_.front();
    rep_changes_.pop();
    return true;
}

void FactionSystem::Reset() {
    std::lock_guard lock(mutex_);
    factions_.clear();
    watched_faction_ = 0;
    selected_faction_id_.reset();
    rep_changes_ = {};
}

}

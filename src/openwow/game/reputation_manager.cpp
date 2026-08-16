
#include "openwow/game/reputation_manager.h"

#include <algorithm>

namespace openwow::game {

ReputationManager& ReputationManager::Get() {
    static ReputationManager instance;
    return instance;
}

BarPosition ReputationManager::ComputeBarPosition(int32_t raw_rep) {
    BarPosition bp;

    for (size_t i = 0; i < kStandingThresholds.size(); ++i) {
        const auto& th = kStandingThresholds[i];
        int32_t tier_max = th.min_rep + th.tier_size;

        if (raw_rep < tier_max || i == kStandingThresholds.size() - 1) {
            bp.standing = th.standing;
            bp.current  = raw_rep - th.min_rep;
            bp.maximum  = th.tier_size;

            if (bp.current < 0) bp.current = 0;
            if (bp.current > bp.maximum) bp.current = bp.maximum;
            break;
        }
    }
    return bp;
}

std::string ReputationManager::GetStandingName(FactionStanding s) {
    return FactionSystem::StandingText(s);
}

StandingColor ReputationManager::GetStandingColor(FactionStanding s) {

    switch (s) {
        case FactionStanding::Hated:      return {0xCC, 0x20, 0x22, 0xFF};
        case FactionStanding::Hostile:    return {0xFF, 0x00, 0x00, 0xFF};
        case FactionStanding::Unfriendly: return {0xEE, 0x6C, 0x00, 0xFF};
        case FactionStanding::Neutral:    return {0xE7, 0xB2, 0x00, 0xFF};
        case FactionStanding::Friendly:   return {0x00, 0xFF, 0x00, 0xFF};
        case FactionStanding::Honored:    return {0x00, 0xCC, 0x44, 0xFF};
        case FactionStanding::Revered:    return {0x00, 0x88, 0xCC, 0xFF};
        case FactionStanding::Exalted:    return {0x00, 0xCC, 0xCC, 0xFF};
    }
    return {0xFF, 0xFF, 0xFF, 0xFF};
}

void ReputationManager::ForEach(const Visitor& fn) const {
    const auto& fs = FactionSystem::Get();
    size_t n = fs.GetNumFactions();
    for (size_t i = 0; i < n; ++i) {
        const auto* f = fs.GetFaction(i);
        if (f) fn(*f);
    }
}

void ReputationManager::ForEachAtWar(const Visitor& fn) const {
    ForEach([&](const FactionInfo& f) {
        if (f.at_war && !f.is_header) fn(f);
    });
}

void ReputationManager::ForEachByExpansion(ReputationExpansion exp,
                                           const Visitor& fn) const {
    ForEach([&](const FactionInfo& f) {
        if (!f.is_header && ClassifyExpansion(f.faction_id) == exp) fn(f);
    });
}

void ReputationManager::ForEachNonHeader(const Visitor& fn) const {
    ForEach([&](const FactionInfo& f) {
        if (!f.is_header) fn(f);
    });
}

uint32_t ReputationManager::GetWatchedFactionId() const {
    return FactionSystem::Get().GetWatchedFaction();
}

const FactionInfo* ReputationManager::GetWatchedFaction() const {
    uint32_t id = GetWatchedFactionId();
    if (id == 0) return nullptr;
    return FactionSystem::Get().GetFactionById(id);
}

bool ReputationManager::CanToggleAtWar(uint32_t faction_id) const {
    const auto* f = FactionSystem::Get().GetFactionById(faction_id);
    if (!f) return false;
    return f->can_toggle_at_war;
}

size_t ReputationManager::GetExaltedCount() const {
    size_t count = 0;
    ForEachNonHeader([&](const FactionInfo& f) {
        if (f.standing == FactionStanding::Exalted) ++count;
    });
    return count;
}

size_t ReputationManager::GetTotalFactionCount() const {
    return FactionSystem::Get().GetNumFactions();
}

size_t ReputationManager::GetVisibleFactionCount() const {
    size_t count = 0;
    ForEach([&](const FactionInfo& f) {
        if (!f.is_header && !f.is_inactive) ++count;
    });
    return count;
}

std::vector<const FactionInfo*> ReputationManager::GetSortedFactions() const {
    const auto& fs = FactionSystem::Get();
    size_t n = fs.GetNumFactions();

    std::vector<const FactionInfo*> result;
    result.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        const auto* f = fs.GetFaction(i);
        if (f) result.push_back(f);
    }

    std::stable_sort(result.begin(), result.end(),
        [](const FactionInfo* a, const FactionInfo* b) {
            if (a->header_index != b->header_index)
                return a->header_index < b->header_index;

            if (a->is_header != b->is_header)
                return a->is_header;
            return a->faction_id < b->faction_id;
        });

    return result;
}

ReputationExpansion ReputationManager::ClassifyExpansion(uint32_t faction_id) {

    if (faction_id < 980)  return ReputationExpansion::Classic;
    if (faction_id < 1038) return ReputationExpansion::TBC;
    if (faction_id < 1200) return ReputationExpansion::WotLK;
    return ReputationExpansion::Other;
}

}

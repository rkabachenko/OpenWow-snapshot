
#include "openwow/game/dungeon_finder_data.h"

#include <algorithm>

namespace openwow::game {

void DungeonFinderData::SetDungeonList(
    const std::vector<DungeonListEntry>& list) {
    std::lock_guard lock(mutex_);
    dungeons_ = list;
}

std::vector<DungeonListEntry> DungeonFinderData::GetDungeonList() const {
    std::lock_guard lock(mutex_);
    return dungeons_;
}

std::optional<DungeonListEntry> DungeonFinderData::GetDungeon(
    uint32_t dungeonId) const {
    std::lock_guard lock(mutex_);
    for (const auto& d : dungeons_) {
        if (d.dungeonId == dungeonId) return d;
    }
    return std::nullopt;
}

std::vector<DungeonListEntry> DungeonFinderData::GetDungeonsByCategory(
    DungeonCategory cat) const {
    std::lock_guard lock(mutex_);
    std::vector<DungeonListEntry> result;
    for (const auto& d : dungeons_) {
        if (d.category == cat) result.push_back(d);
    }
    return result;
}

std::vector<DungeonListEntry> DungeonFinderData::GetAvailableDungeons() const {
    std::lock_guard lock(mutex_);
    std::vector<DungeonListEntry> result;
    for (const auto& d : dungeons_) {
        if (d.isAvailable) result.push_back(d);
    }
    return result;
}

std::vector<DungeonListEntry> DungeonFinderData::GetEligibleDungeons(
    uint32_t playerLevel) const {
    std::lock_guard lock(mutex_);
    std::vector<DungeonListEntry> result;
    for (const auto& d : dungeons_) {
        if (d.isAvailable && playerLevel >= d.minLevel &&
            playerLevel <= d.maxLevel) {
            result.push_back(d);
        }
    }
    return result;
}

std::optional<DungeonListEntry>
DungeonFinderData::GetRandomDungeonEntry() const {
    std::lock_guard lock(mutex_);
    for (const auto& d : dungeons_) {
        if (d.isRandom) return d;
    }
    return std::nullopt;
}

void DungeonFinderData::SetProposal(const LFGProposalEntry& proposal) {
    std::lock_guard lock(mutex_);
    proposal_ = proposal;
}

std::optional<LFGProposalEntry> DungeonFinderData::GetProposal() const {
    std::lock_guard lock(mutex_);
    return proposal_;
}

bool DungeonFinderData::HasProposal() const {
    std::lock_guard lock(mutex_);
    return proposal_.has_value();
}

void DungeonFinderData::AcceptProposal() {
    std::lock_guard lock(mutex_);
    if (proposal_) {
        for (auto& p : proposal_->players) {
            if (p.isMe) {
                p.accepted = true;
                break;
            }
        }
    }
}

void DungeonFinderData::DeclineProposal() {
    std::lock_guard lock(mutex_);
    if (proposal_) {
        for (auto& p : proposal_->players) {
            if (p.isMe) {
                p.accepted = false;
                break;
            }
        }
    }
}

void DungeonFinderData::ClearProposal() {
    std::lock_guard lock(mutex_);
    proposal_.reset();
}

void DungeonFinderData::SetProposalAccepted(ObjectGuid guid, bool accepted) {
    std::lock_guard lock(mutex_);
    if (!proposal_) return;
    for (auto& p : proposal_->players) {
        if (p.guid.GetRawValue() == guid.GetRawValue()) {
            p.accepted = accepted;
            break;
        }
    }
}

uint32_t DungeonFinderData::GetAcceptedCount() const {
    std::lock_guard lock(mutex_);
    if (!proposal_) return 0;
    uint32_t count = 0;
    for (const auto& p : proposal_->players) {
        if (p.accepted) ++count;
    }
    return count;
}

uint32_t DungeonFinderData::GetDeclinedCount() const {
    std::lock_guard lock(mutex_);
    if (!proposal_) return 0;
    uint32_t total = static_cast<uint32_t>(proposal_->players.size());
    uint32_t accepted = 0;
    for (const auto& p : proposal_->players) {
        if (p.accepted) ++accepted;
    }
    return total - accepted;
}

bool DungeonFinderData::AllAccepted() const {
    std::lock_guard lock(mutex_);
    if (!proposal_ || proposal_->players.empty()) return false;
    for (const auto& p : proposal_->players) {
        if (!p.accepted) return false;
    }
    return true;
}

void DungeonFinderData::Reset() {
    std::lock_guard lock(mutex_);
    dungeons_.clear();
    proposal_.reset();
}

}

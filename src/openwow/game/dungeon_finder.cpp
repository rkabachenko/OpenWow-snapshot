
#include "openwow/game/dungeon_finder.h"

#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

DungeonFinder& DungeonFinder::Get() {
  static DungeonFinder instance;
  return instance;
}

void DungeonFinder::AddDungeon(const LFGDungeonInfo& info) {
  std::lock_guard lock(mutex_);

  for (auto& d : dungeons_) {
    if (d.id == info.id) {
      d = info;
      return;
    }
  }
  dungeons_.push_back(info);
}

void DungeonFinder::ClearDungeons() {
  std::lock_guard lock(mutex_);
  dungeons_.clear();
}

const LFGDungeonInfo* DungeonFinder::GetDungeon(std::uint32_t id) const {
  std::lock_guard lock(mutex_);
  for (const auto& d : dungeons_) {
    if (d.id == id) return &d;
  }
  return nullptr;
}

std::size_t DungeonFinder::GetDungeonCount() const {
  std::lock_guard lock(mutex_);
  return dungeons_.size();
}

std::vector<const LFGDungeonInfo*> DungeonFinder::GetDungeonsByLevel(
    std::uint32_t player_level) const {
  std::lock_guard lock(mutex_);
  std::vector<const LFGDungeonInfo*> result;
  for (const auto& d : dungeons_) {
    if (player_level >= d.min_level && player_level <= d.max_level) {
      result.push_back(&d);
    }
  }
  return result;
}

std::vector<const LFGDungeonInfo*> DungeonFinder::GetDungeonsByExpansion(
    std::uint32_t expansion) const {
  std::lock_guard lock(mutex_);
  std::vector<const LFGDungeonInfo*> result;
  for (const auto& d : dungeons_) {
    if (d.expansion == expansion) {
      result.push_back(&d);
    }
  }
  return result;
}

std::vector<const LFGDungeonInfo*> DungeonFinder::GetDungeonsByGroupType(
    DungeonGroupType type) const {
  std::lock_guard lock(mutex_);
  std::vector<const LFGDungeonInfo*> result;
  for (const auto& d : dungeons_) {
    if (d.group_type == type) {
      result.push_back(&d);
    }
  }
  return result;
}

std::vector<const LFGDungeonInfo*> DungeonFinder::GetEligibleDungeons() const {
  std::lock_guard lock(mutex_);
  std::vector<const LFGDungeonInfo*> result;
  for (const auto& d : dungeons_) {
    if (d.eligible) {
      result.push_back(&d);
    }
  }
  return result;
}

std::vector<const LFGDungeonInfo*> DungeonFinder::GetHolidayDungeons() const {
  std::lock_guard lock(mutex_);
  std::vector<const LFGDungeonInfo*> result;
  for (const auto& d : dungeons_) {
    if (d.holiday) {
      result.push_back(&d);
    }
  }
  return result;
}

void DungeonFinder::SetDungeonEligible(std::uint32_t id, bool eligible) {
  std::lock_guard lock(mutex_);
  for (auto& d : dungeons_) {
    if (d.id == id) {
      d.eligible = eligible;
      return;
    }
  }
}

void DungeonFinder::SetSelectedRoles(std::uint8_t role_mask) {
  std::lock_guard lock(mutex_);
  selected_roles_ = role_mask;
}

std::uint8_t DungeonFinder::GetSelectedRoles() const {
  std::lock_guard lock(mutex_);
  return selected_roles_;
}

bool DungeonFinder::IsTankSelected() const {
  std::lock_guard lock(mutex_);
  return (selected_roles_ & 0x02) != 0;
}

bool DungeonFinder::IsHealerSelected() const {
  std::lock_guard lock(mutex_);
  return (selected_roles_ & 0x04) != 0;
}

bool DungeonFinder::IsDpsSelected() const {
  std::lock_guard lock(mutex_);
  return (selected_roles_ & 0x08) != 0;
}

void DungeonFinder::SetQueued(bool queued) {
  std::lock_guard lock(mutex_);
  queued_ = queued;
}

bool DungeonFinder::IsQueued() const {
  std::lock_guard lock(mutex_);
  return queued_;
}

void DungeonFinder::SetQueueWaitInfo(const LFGQueueWaitInfo& info) {
  std::lock_guard lock(mutex_);
  queue_wait_ = info;
}

const LFGQueueWaitInfo& DungeonFinder::GetQueueWaitInfo() const {
  std::lock_guard lock(mutex_);
  return queue_wait_;
}

void DungeonFinder::SetProposal(const LFGProposalInfo& proposal) {
  std::lock_guard lock(mutex_);
  proposal_ = proposal;
  Log(LogLevel::kInfo,
      "DungeonFinder: proposal set id=" +
          std::to_string(proposal.proposal_id) +
          " dungeon=" + std::to_string(proposal.dungeon_id));
}

void DungeonFinder::ClearProposal() {
  std::lock_guard lock(mutex_);
  proposal_.reset();
}

bool DungeonFinder::HasProposal() const {
  std::lock_guard lock(mutex_);
  return proposal_.has_value();
}

const LFGProposalInfo* DungeonFinder::GetProposal() const {
  std::lock_guard lock(mutex_);
  if (proposal_.has_value()) return &proposal_.value();
  return nullptr;
}

void DungeonFinder::AcceptProposal() {
  std::lock_guard lock(mutex_);
  if (proposal_.has_value()) {
    proposal_->state = ProposalState::kAccepted;
  }
}

void DungeonFinder::DeclineProposal() {
  std::lock_guard lock(mutex_);
  if (proposal_.has_value()) {
    proposal_->state = ProposalState::kDeclined;
  }
}

void DungeonFinder::SetReward(const LFGRewardInfo& reward) {
  std::lock_guard lock(mutex_);

  for (auto& r : rewards_) {
    if (r.dungeon_id == reward.dungeon_id) {
      r = reward;
      return;
    }
  }
  rewards_.push_back(reward);
}

void DungeonFinder::ClearRewards() {
  std::lock_guard lock(mutex_);
  rewards_.clear();
}

const LFGRewardInfo* DungeonFinder::GetReward(
    std::uint32_t dungeon_id) const {
  std::lock_guard lock(mutex_);
  for (const auto& r : rewards_) {
    if (r.dungeon_id == dungeon_id) return &r;
  }
  return nullptr;
}

std::size_t DungeonFinder::GetRewardCount() const {
  std::lock_guard lock(mutex_);
  return rewards_.size();
}

void DungeonFinder::SetSpecificLoot(
    std::uint32_t dungeon_id,
    const std::vector<SpecificLootItem>& items) {
  std::lock_guard lock(mutex_);
  for (auto& [did, loot] : specific_loot_) {
    if (did == dungeon_id) {
      loot = items;
      return;
    }
  }
  specific_loot_.emplace_back(dungeon_id, items);
}

void DungeonFinder::ClearSpecificLoot() {
  std::lock_guard lock(mutex_);
  specific_loot_.clear();
}

const std::vector<SpecificLootItem>* DungeonFinder::GetSpecificLoot(
    std::uint32_t dungeon_id) const {
  std::lock_guard lock(mutex_);
  for (const auto& [did, loot] : specific_loot_) {
    if (did == dungeon_id) return &loot;
  }
  return nullptr;
}

void DungeonFinder::SetRandomDungeonBonus(std::uint32_t bonus_money,
                                          std::uint32_t bonus_item_id) {
  std::lock_guard lock(mutex_);
  random_bonus_money_ = bonus_money;
  random_bonus_item_id_ = bonus_item_id;
}

std::uint32_t DungeonFinder::GetRandomBonusMoney() const {
  std::lock_guard lock(mutex_);
  return random_bonus_money_;
}

std::uint32_t DungeonFinder::GetRandomBonusItemId() const {
  std::lock_guard lock(mutex_);
  return random_bonus_item_id_;
}

void DungeonFinder::SetDeserterCooldown(std::uint32_t seconds) {
  std::lock_guard lock(mutex_);
  deserter_cooldown_ = seconds;
}

std::uint32_t DungeonFinder::GetDeserterCooldown() const {
  std::lock_guard lock(mutex_);
  return deserter_cooldown_;
}

bool DungeonFinder::HasDeserterDebuff() const {
  std::lock_guard lock(mutex_);
  return deserter_cooldown_ > 0;
}

void DungeonFinder::Reset() {
  std::lock_guard lock(mutex_);
  dungeons_.clear();
  selected_roles_ = 0;
  queued_ = false;
  queue_wait_ = {};
  proposal_.reset();
  rewards_.clear();
  specific_loot_.clear();
  random_bonus_money_ = 0;
  random_bonus_item_id_ = 0;
  deserter_cooldown_ = 0;
}

}

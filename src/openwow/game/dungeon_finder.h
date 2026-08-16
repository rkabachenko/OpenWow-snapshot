
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class DungeonGroupType : std::uint8_t {
  kDungeon = 0,
  kHeroic = 1,
  kRaid = 2,
  kRaid25 = 3,
};

enum class ProposalState : std::uint8_t {
  kNone = 0,
  kActive = 1,
  kAccepted = 2,
  kDeclined = 3,
};

struct LFGDungeonInfo {
  std::uint32_t id = 0;
  std::string name;
  std::uint32_t min_level = 0;
  std::uint32_t max_level = 0;
  std::uint32_t map_id = 0;
  std::uint32_t difficulty = 0;
  std::uint32_t expansion = 0;
  DungeonGroupType group_type = DungeonGroupType::kDungeon;
  bool holiday = false;
  bool eligible = true;
};

struct LFGProposalMember {
  std::uint8_t role = 0;
  bool accepted = false;
  bool is_self = false;
};

struct LFGProposalInfo {
  std::uint32_t proposal_id = 0;
  ProposalState state = ProposalState::kNone;
  std::uint8_t assigned_role = 0;
  std::uint32_t dungeon_id = 0;
  std::uint32_t timeout_seconds = 0;
  std::vector<LFGProposalMember> members;
};

struct LFGRewardInfo {
  std::uint32_t dungeon_id = 0;
  bool first_of_day = false;
  std::uint32_t bonus_xp = 0;
  std::uint32_t bonus_money = 0;
  std::uint32_t bonus_item_id = 0;
  std::uint32_t subsequent_xp = 0;
  std::uint32_t subsequent_money = 0;
};

struct SpecificLootItem {
  std::uint32_t item_id = 0;
  std::string name;
  std::uint32_t quality = 0;
  std::uint32_t quantity = 1;
};

struct LFGQueueWaitInfo {
  std::int32_t estimated_wait = -1;
  std::int32_t actual_wait = 0;
  std::int32_t avg_wait_tank = -1;
  std::int32_t avg_wait_healer = -1;
  std::int32_t avg_wait_dps = -1;
  std::uint8_t tanks_needed = 0;
  std::uint8_t healers_needed = 0;
  std::uint8_t dps_needed = 0;
};

class DungeonFinder {
 public:
  static DungeonFinder& Get();

  void AddDungeon(const LFGDungeonInfo& info);
  void ClearDungeons();
  [[nodiscard]] const LFGDungeonInfo* GetDungeon(std::uint32_t id) const;
  [[nodiscard]] std::size_t GetDungeonCount() const;

  [[nodiscard]] std::vector<const LFGDungeonInfo*> GetDungeonsByLevel(
      std::uint32_t player_level) const;

  [[nodiscard]] std::vector<const LFGDungeonInfo*> GetDungeonsByExpansion(
      std::uint32_t expansion) const;

  [[nodiscard]] std::vector<const LFGDungeonInfo*> GetDungeonsByGroupType(
      DungeonGroupType type) const;

  [[nodiscard]] std::vector<const LFGDungeonInfo*> GetEligibleDungeons() const;

  [[nodiscard]] std::vector<const LFGDungeonInfo*> GetHolidayDungeons() const;

  void SetDungeonEligible(std::uint32_t id, bool eligible);

  void SetSelectedRoles(std::uint8_t role_mask);
  [[nodiscard]] std::uint8_t GetSelectedRoles() const;
  [[nodiscard]] bool IsTankSelected() const;
  [[nodiscard]] bool IsHealerSelected() const;
  [[nodiscard]] bool IsDpsSelected() const;

  void SetQueued(bool queued);
  [[nodiscard]] bool IsQueued() const;
  void SetQueueWaitInfo(const LFGQueueWaitInfo& info);
  [[nodiscard]] const LFGQueueWaitInfo& GetQueueWaitInfo() const;

  void SetProposal(const LFGProposalInfo& proposal);
  void ClearProposal();
  [[nodiscard]] bool HasProposal() const;
  [[nodiscard]] const LFGProposalInfo* GetProposal() const;
  void AcceptProposal();
  void DeclineProposal();

  void SetReward(const LFGRewardInfo& reward);
  void ClearRewards();
  [[nodiscard]] const LFGRewardInfo* GetReward(std::uint32_t dungeon_id) const;
  [[nodiscard]] std::size_t GetRewardCount() const;

  void SetSpecificLoot(std::uint32_t dungeon_id,
                       const std::vector<SpecificLootItem>& items);
  void ClearSpecificLoot();
  [[nodiscard]] const std::vector<SpecificLootItem>* GetSpecificLoot(
      std::uint32_t dungeon_id) const;

  void SetRandomDungeonBonus(std::uint32_t bonus_money,
                             std::uint32_t bonus_item_id);
  [[nodiscard]] std::uint32_t GetRandomBonusMoney() const;
  [[nodiscard]] std::uint32_t GetRandomBonusItemId() const;

  void SetDeserterCooldown(std::uint32_t seconds);
  [[nodiscard]] std::uint32_t GetDeserterCooldown() const;
  [[nodiscard]] bool HasDeserterDebuff() const;

  void Reset();

 private:
  DungeonFinder() = default;

  std::vector<LFGDungeonInfo> dungeons_;
  std::uint8_t selected_roles_ = 0;
  bool queued_ = false;
  LFGQueueWaitInfo queue_wait_{};
  std::optional<LFGProposalInfo> proposal_;
  std::vector<LFGRewardInfo> rewards_;
  std::vector<std::pair<std::uint32_t, std::vector<SpecificLootItem>>>
      specific_loot_;
  std::uint32_t random_bonus_money_ = 0;
  std::uint32_t random_bonus_item_id_ = 0;
  std::uint32_t deserter_cooldown_ = 0;
  mutable std::mutex mutex_;
};

}

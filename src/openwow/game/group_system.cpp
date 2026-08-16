
#include "openwow/game/group_system.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/group_manager.h"
#include "openwow/game/object_manager.h"

#include <algorithm>

namespace openwow::game {

GroupSystem& GroupSystem::Get() {
    static GroupSystem instance;
    return instance;
}

namespace {

constexpr std::size_t kTrackedPartySlotCount = 4;

const GroupSystemMember* FindTrackedMemberByGuid(
    const std::vector<GroupSystemMember>& members,
    std::uint64_t guid) {
  const auto it = std::find_if(
      members.begin(), members.end(),
      [guid](const GroupSystemMember& member) { return member.guid == guid; });
  return it == members.end() ? nullptr : &*it;
}

bool HasTrackedRaidOfficerRank(const std::vector<GroupSystemMember>& members,
                               const std::uint64_t leader_guid,
                               const bool is_raid,
                               const std::uint64_t guid) {
  if (!is_raid || guid == 0) {
    return false;
  }
  const auto* member = FindTrackedMemberByGuid(members, guid);
  if (member == nullptr) {
    return false;
  }
  if (guid == leader_guid) {
    return true;
  }
  return (member->flags & 0x01u) != 0;
}

std::uint64_t ResolveControlledUnitGuid(const ObjectManager& objects,
                                        std::uint64_t member_guid,
                                        std::uint64_t cached_pet_guid) {
  if (member_guid != 0) {
    if (const auto* unit = objects.GetUnit(ObjectGuid(member_guid))) {
      const auto controlled_guid =
          unit->State().GetPrimaryControlledUnitGUID().GetRawValue();
      if (controlled_guid != 0) {
        return controlled_guid;
      }
    }
  }

  return cached_pet_guid;
}

bool MatchesControlledUnitGuid(const ObjectManager& objects,
                               std::uint64_t member_guid,
                               std::uint64_t cached_pet_guid,
                               std::uint64_t guid) {
  const auto controlled_guid =
      ResolveControlledUnitGuid(objects, member_guid, cached_pet_guid);
  return controlled_guid != 0 && controlled_guid == guid;
}

std::uint8_t ResolveAssignmentRoleFlag(std::uint8_t role) {
  switch (role) {
    case 0:
      return 0x02;
    case 1:
      return 0x04;
    default:
      return 0;
  }
}

void PreserveCachedPetGuids(std::vector<GroupSystemMember>& members,
                            const std::vector<GroupSystemMember>& previous) {
  for (auto& member : members) {
    if (const auto* previous_member =
            FindTrackedMemberByGuid(previous, member.guid)) {
      member.pet_guid = previous_member->pet_guid;
    }
  }
}

bool IsReadyCheckDeadlineActive(const std::uint32_t now_tick,
                                const std::uint32_t deadline_tick) {
  return deadline_tick != 0 &&
         static_cast<std::int32_t>(now_tick - deadline_tick) < 0;
}

std::uint32_t ResolveReadyCheckNowTick(const std::uint32_t now_tick) {
  return now_tick != 0 ? now_tick : openwow::core::GameClock::GetTickCount32();
}

std::array<std::uint64_t, kTrackedPartySlotCount> BuildTrackedPartySlots(
    const std::vector<GroupSystemMember>& members,
    const std::uint8_t local_sub_group, const std::uint64_t active_player_guid) {
  std::array<std::uint64_t, kTrackedPartySlotCount> party_guids{};

  std::size_t party_index = 0;
  for (const auto& member : members) {
    if (party_index >= party_guids.size()) {
      break;
    }
    if (member.guid == 0) {
      continue;
    }
    if (active_player_guid != 0 && member.guid == active_player_guid) {
      continue;
    }
    if (member.group_index != local_sub_group) {
      continue;
    }
    party_guids[party_index++] = member.guid;
  }

  return party_guids;
}

std::array<int, kTrackedPartySlotCount> RemapTrackedPartyReadyResponses(
    const std::array<std::uint64_t, kTrackedPartySlotCount>& previous_guids,
    const std::array<int, kTrackedPartySlotCount>& previous_responses,
    const std::array<std::uint64_t, kTrackedPartySlotCount>& current_guids) {
  std::array<int, kTrackedPartySlotCount> current_responses{};

  for (std::size_t slot = 0; slot < current_guids.size(); ++slot) {
    const auto guid = current_guids[slot];
    if (guid == 0) {
      continue;
    }

    for (std::size_t previous_slot = 0; previous_slot < previous_guids.size();
         ++previous_slot) {
      if (previous_guids[previous_slot] != guid) {
        continue;
      }
      current_responses[slot] = previous_responses[previous_slot];
      break;
    }
  }

  return current_responses;
}

int FindTrackedPartySlot(
    const std::array<std::uint64_t, kTrackedPartySlotCount>& party_guids,
    const std::uint64_t guid) {
  if (guid == 0) {
    return -1;
  }

  for (std::size_t slot = 0; slot < party_guids.size(); ++slot) {
    if (party_guids[slot] == guid) {
      return static_cast<int>(slot);
    }
  }

  return -1;
}

std::uint8_t ResolveTrackedPartyAssignmentFlags(
    const std::uint64_t guid, const std::uint64_t active_player_guid,
    const std::array<std::uint64_t, kTrackedPartySlotCount>& party_guids,
    const std::uint8_t local_party_flags,
    const std::vector<GroupSystemMember>& members) {
  if (guid == 0) {
    return 0;
  }

  if (guid == active_player_guid && active_player_guid != 0) {
    return local_party_flags;
  }

  const auto tracked_it =
      std::find(party_guids.begin(), party_guids.end(), guid);
  if (tracked_it == party_guids.end()) {
    return 0;
  }

  const auto* member = FindTrackedMemberByGuid(members, guid);
  return member != nullptr ? member->flags : 0;
}

}

void GroupSystem::ResetReadyCheckStateUnlocked() {
  ready_check_ = false;
  ready_check_responses_.clear();
  ready_check_initiator_guid_ = 0;
  ready_check_end_time_ = 0;
  party_ready_responses_.fill(0);
  party_ready_waiting_.fill(0);
}

void GroupSystem::SetGroupType(GroupKind type) {
    std::lock_guard lock(mutex_);
    group_type_ = type;
    is_raid_ = (type == GroupKind::Raid);
}

GroupKind GroupSystem::GetGroupType() const {
    std::lock_guard lock(mutex_);
    return group_type_;
}

void GroupSystem::SetLeader(ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    leader_obj_ = guid;
    leader_guid_ = guid.GetRawValue();
    tracked_party_leader_slot_ =
        FindTrackedPartySlot(party_guids_, leader_guid_);

    leader_name_.clear();
    for (const auto& m : new_members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue()) {
            leader_name_ = m.name;
            break;
        }
    }
}

ObjectGuid GroupSystem::GetLeader() const {
    std::lock_guard lock(mutex_);
    return leader_obj_;
}

bool GroupSystem::IsLeader() const {
    std::lock_guard lock(mutex_);
    if (local_player_.IsEmpty()) return false;
    return local_player_.GetRawValue() == leader_obj_.GetRawValue();
}

void GroupSystem::SetLocalPlayerGuid(ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    local_player_ = guid;
}

ObjectGuid GroupSystem::GetLocalPlayerGuid() const {
    std::lock_guard lock(mutex_);
    return local_player_;
}

void GroupSystem::AddMember(const GroupMemberData& member) {
    std::lock_guard lock(mutex_);

    for (auto& m : new_members_) {
        if (m.guid.GetRawValue() == member.guid.GetRawValue()) {
            m = member;
            return;
        }
    }
    new_members_.push_back(member);
}

void GroupSystem::RemoveMember(ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    auto it = std::remove_if(new_members_.begin(), new_members_.end(),
                             [&](const GroupMemberData& m) {
                                 return m.guid.GetRawValue() == guid.GetRawValue();
                             });
    new_members_.erase(it, new_members_.end());
}

std::optional<GroupMemberData> GroupSystem::GetMember(ObjectGuid guid) const {
    std::lock_guard lock(mutex_);
    for (const auto& m : new_members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue()) return m;
    }
    return std::nullopt;
}

std::vector<GroupMemberData> GroupSystem::GetMembers() const {
    std::lock_guard lock(mutex_);
    return new_members_;
}

uint32_t GroupSystem::GetMemberCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(new_members_.size());
}

std::vector<GroupMemberData> GroupSystem::GetSubGroupMembers(uint32_t subGroup) const {
    std::lock_guard lock(mutex_);
    std::vector<GroupMemberData> result;
    for (const auto& m : new_members_) {
        if (m.subGroup == subGroup) result.push_back(m);
    }
    return result;
}

void GroupSystem::SetSubGroup(ObjectGuid guid, uint32_t subGroup) {
    std::lock_guard lock(mutex_);
    for (auto& m : new_members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue()) {
            m.subGroup = static_cast<uint8_t>(subGroup);
            return;
        }
    }
}

void GroupSystem::SetRole(ObjectGuid guid, GroupRole role) {
    std::lock_guard lock(mutex_);
    for (auto& m : new_members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue()) {
            m.role = role;
            return;
        }
    }
}

void GroupSystem::SetAssistant(ObjectGuid guid, bool assistant) {
    std::lock_guard lock(mutex_);
    for (auto& m : new_members_) {
        if (m.guid.GetRawValue() == guid.GetRawValue()) {
            m.isAssistant = assistant;
            return;
        }
    }
}

uint32_t GroupSystem::GetMaxMembers() const {
    std::lock_guard lock(mutex_);
    return (group_type_ == GroupKind::Raid) ? 40u : 5u;
}

bool GroupSystem::IsGroupFull() const {
    std::lock_guard lock(mutex_);
    uint32_t max = (group_type_ == GroupKind::Raid) ? 40u : 5u;
    return static_cast<uint32_t>(new_members_.size()) >= max;
}

void GroupSystem::SetLootMethod(uint32_t method) {
    std::lock_guard lock(mutex_);
    loot_method_new_ = method;
    loot_method_ = static_cast<uint8_t>(method);
}

uint32_t GroupSystem::GetLootMethod() const {
    std::lock_guard lock(mutex_);
    return loot_method_new_;
}

void GroupSystem::SetDungeonDifficulty(DungeonDifficulty diff) {
    std::lock_guard lock(mutex_);
    default_dungeon_diff_ = diff;
    party_dungeon_diff_ = diff;
}

void GroupSystem::SetDefaultDungeonDifficulty(DungeonDifficulty diff) {
    std::lock_guard lock(mutex_);
    default_dungeon_diff_ = diff;
}

DungeonDifficulty GroupSystem::GetDefaultDungeonDifficulty() const {
    std::lock_guard lock(mutex_);
    return default_dungeon_diff_;
}

void GroupSystem::SetPartyDungeonDifficulty(DungeonDifficulty diff) {
    std::lock_guard lock(mutex_);
    party_dungeon_diff_ = diff;
}

DungeonDifficulty GroupSystem::GetPartyDungeonDifficulty() const {
    std::lock_guard lock(mutex_);
    return party_dungeon_diff_;
}

void GroupSystem::ApplyDungeonDifficultyUpdate(DungeonDifficulty diff,
                                               const bool update_default,
                                               const bool update_group) {
    std::lock_guard lock(mutex_);
    if (update_default) {
        default_dungeon_diff_ = diff;
    }
    if (update_group) {
        party_dungeon_diff_ = diff;
    }
}

DungeonDifficulty GroupSystem::GetDungeonDifficulty() const {
    std::lock_guard lock(mutex_);
    const bool has_tracked_party_member =
        std::any_of(party_guids_.begin(), party_guids_.end(),
                    [](const uint64_t guid) { return guid != 0; });
    if (!has_tracked_party_member || real_raid_member_count_ != 0) {
        return default_dungeon_diff_;
    }
    return party_dungeon_diff_;
}

void GroupSystem::SetRaidDifficulty(RaidDifficulty diff) {
    std::lock_guard lock(mutex_);
    current_raid_diff_ = diff;
    default_raid_diff_ = diff;
}

void GroupSystem::SetCurrentRaidDifficulty(RaidDifficulty diff) {
    std::lock_guard lock(mutex_);
    current_raid_diff_ = diff;
}

RaidDifficulty GroupSystem::GetCurrentRaidDifficulty() const {
    std::lock_guard lock(mutex_);
    return current_raid_diff_;
}

void GroupSystem::SetDefaultRaidDifficulty(RaidDifficulty diff) {
    std::lock_guard lock(mutex_);
    default_raid_diff_ = diff;
}

RaidDifficulty GroupSystem::GetDefaultRaidDifficulty() const {
    std::lock_guard lock(mutex_);
    return default_raid_diff_;
}

RaidDifficulty GroupSystem::GetRaidDifficulty() const {
    std::lock_guard lock(mutex_);
    return GetActiveRaidDifficultyUnlocked();
}

void GroupSystem::SetPlayerDifficultyIndex(const std::uint8_t difficulty) {
    std::lock_guard lock(mutex_);
    player_difficulty_index_ = difficulty;
}

std::uint8_t GroupSystem::GetPlayerDifficultyIndex() const {
    std::lock_guard lock(mutex_);
    return player_difficulty_index_;
}

std::uint8_t GroupSystem::GetEffectiveRaidMapDifficultyIndex(
    const bool map_allows_player_difficulty) const {
    std::lock_guard lock(mutex_);
    const auto active =
        static_cast<std::uint8_t>(GetActiveRaidDifficultyUnlocked());
    if (!map_allows_player_difficulty) {
        return active;
    }

    return static_cast<std::uint8_t>(
        (active & 0x01u) + (static_cast<std::uint32_t>(player_difficulty_index_) << 1u));
}

bool GroupSystem::HasTrackedRaidRosterUnlocked() const {
  return real_raid_member_count_ != 0;
}

RaidDifficulty GroupSystem::GetActiveRaidDifficultyUnlocked() const {
  return HasTrackedRaidRosterUnlocked() ? current_raid_diff_
                                        : default_raid_diff_;
}

void GroupSystem::ConvertToRaid() {
    std::lock_guard lock(mutex_);
    if (group_type_ == GroupKind::Party) {
        group_type_ = GroupKind::Raid;
        is_raid_ = true;
    }
}

ObjectGuid GroupSystem::GetMainTank() const {
    std::lock_guard lock(mutex_);
    return main_tank_;
}

void GroupSystem::SetMainTank(ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    main_tank_ = guid;
}

ObjectGuid GroupSystem::GetMainAssist() const {
    std::lock_guard lock(mutex_);
    return main_assist_;
}

void GroupSystem::SetMainAssist(ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    main_assist_ = guid;
}

void GroupSystem::Clear() {
    std::lock_guard lock(mutex_);
    new_members_.clear();
    group_type_ = GroupKind::None;
    leader_obj_ = ObjectGuid{};
    local_player_ = ObjectGuid{};
    loot_method_new_ = kStartupLootMethod;
    default_dungeon_diff_ = DungeonDifficulty::Normal;
    party_dungeon_diff_ = DungeonDifficulty::Normal;
    current_raid_diff_ = RaidDifficulty::Normal10;
    default_raid_diff_ = RaidDifficulty::Normal10;
    player_difficulty_index_ = 0;
    main_tank_ = ObjectGuid{};
    main_assist_ = ObjectGuid{};

    members_.clear();
    leader_guid_ = 0;
    leader_name_.clear();
    is_raid_ = false;
    loot_method_ = kStartupLootMethod;
    master_looter_ = 0;
    loot_threshold_ = 2;
    ResetReadyCheckStateUnlocked();
    raid_targets_.fill(0);
    local_sub_group_ = 0;
    local_party_flags_ = 0;
    local_party_role_flags_ = 0;
    real_party_member_count_ = 0;
    real_raid_member_count_ = 0;
    is_battleground_group_ = false;
    real_leader_guid_ = 0;
    party_guids_.fill(0);
    tracked_party_leader_slot_ = -1;
}

void GroupSystem::SetGroupData(const std::vector<GroupSystemMember>& members,
                               uint64_t leader, uint8_t lootMethod,
                               uint64_t masterLooter,
                               uint8_t lootThreshold,
                               bool is_raid,
                               uint8_t local_sub_group,
                               uint64_t active_player_guid) {
  std::lock_guard lock(mutex_);
  const auto previous_members = members_;
  const auto previous_party_guids = party_guids_;
  const auto previous_party_ready_responses = party_ready_responses_;
  const auto previous_party_ready_waiting = party_ready_waiting_;
  members_ = members;
  PreserveCachedPetGuids(members_, previous_members);
  leader_guid_ = leader;
  local_player_ = ObjectGuid(active_player_guid);
  loot_method_ = lootMethod;
  loot_method_new_ = lootMethod;
  master_looter_ = masterLooter;
  loot_threshold_ = lootThreshold;
  is_raid_ = is_raid;
  local_sub_group_ = local_sub_group;
  group_type_ = is_raid ? GroupKind::Raid
                        : (members_.empty() ? GroupKind::None
                                           : GroupKind::Party);

  party_guids_ = BuildTrackedPartySlots(
      members_, local_sub_group_, active_player_guid);
  party_ready_responses_ = RemapTrackedPartyReadyResponses(
      previous_party_guids, previous_party_ready_responses, party_guids_);
  party_ready_waiting_ = RemapTrackedPartyReadyResponses(
      previous_party_guids, previous_party_ready_waiting, party_guids_);
  tracked_party_leader_slot_ =
      FindTrackedPartySlot(party_guids_, leader_guid_);

  leader_name_.clear();
  for (const auto& m : members_) {
    if (m.guid == leader) {
      leader_name_ = m.name;
      break;
    }
  }
}

void GroupSystem::ClearGroup() {
  std::lock_guard lock(mutex_);
  members_.clear();
  leader_guid_ = 0;
  leader_name_.clear();
  is_raid_ = false;
  group_type_ = GroupKind::None;
  main_tank_ = ObjectGuid{};
  main_assist_ = ObjectGuid{};
  loot_method_new_ = kStartupLootMethod;
  loot_method_ = kStartupLootMethod;
  master_looter_ = 0;
  loot_threshold_ = 2;
  ResetReadyCheckStateUnlocked();
  raid_targets_.fill(0);
  local_sub_group_ = 0;
  local_party_flags_ = 0;
  local_party_role_flags_ = 0;
  party_lfg_dungeon_id_ = 0;
  real_party_member_count_ = 0;
  real_raid_member_count_ = 0;
  is_battleground_group_ = false;
  has_lfg_restrictions_ = false;
  real_leader_guid_ = 0;
  party_guids_.fill(0);
  tracked_party_leader_slot_ = 0;
}

bool GroupSystem::IsInGroup() const {
  std::lock_guard lock(mutex_);
  return is_raid_ || !members_.empty() || !new_members_.empty() ||
         group_type_ != GroupKind::None;
}

bool GroupSystem::IsInRaid() const {
  std::lock_guard lock(mutex_);
  return is_raid_ || group_type_ == GroupKind::Raid;
}

void GroupSystem::SetIsRaid(bool raid) {
  std::lock_guard lock(mutex_);
  is_raid_ = raid;
  if (raid) group_type_ = GroupKind::Raid;
  else if (group_type_ == GroupKind::Raid) group_type_ = GroupKind::Party;
}

size_t GroupSystem::GetNumGroupMembers() const {
  std::lock_guard lock(mutex_);
  return members_.size();
}

const GroupSystemMember* GroupSystem::GetMember(size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= members_.size()) return nullptr;
  return &members_[index];
}

std::optional<GroupSystemMember> GroupSystem::GetMemberSnapshot(size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= members_.size()) {
    return std::nullopt;
  }
  return members_[index];
}

const GroupSystemMember* GroupSystem::GetMemberByGuid(uint64_t guid) const {
  std::lock_guard lock(mutex_);
  for (const auto& m : members_) {
    if (m.guid == guid) return &m;
  }
  return nullptr;
}

uint64_t GroupSystem::GetLeaderGuid() const {
    std::lock_guard lock(mutex_);
    return leader_guid_;
}

void GroupSystem::UpdateRealGroupStateFromGroupList(const bool is_raid,
                                                    const bool is_battleground,
                                                    const std::uint32_t roster_member_count,
                                                    const std::uint32_t local_party_member_count,
                                                    const std::uint64_t leader_guid) {
  std::lock_guard lock(mutex_);
  is_battleground_group_ = is_battleground;

  if (is_battleground) {
    return;
  }

  if (is_raid) {
    if (roster_member_count == 0) {
      real_raid_member_count_ = 0;
    } else if (roster_member_count < 40) {
      real_raid_member_count_ = roster_member_count + 1;
    }
  } else {
    real_raid_member_count_ = 0;
  }

  if (local_party_member_count <= 4) {
    real_party_member_count_ = local_party_member_count;
  }
  real_leader_guid_ = leader_guid;
}

void GroupSystem::ApplyRealGroupUpdate(const std::uint8_t group_flags,
                                       const std::uint32_t member_count,
                                       const std::uint64_t leader_guid) {
  std::lock_guard lock(mutex_);
  if ((group_flags & 0x02u) != 0) {
    if (member_count == 0) {
      real_raid_member_count_ = 0;
    } else if (member_count < 40) {
      real_raid_member_count_ = member_count + 1;
    }
    real_leader_guid_ = leader_guid;
    return;
  }

  if (real_raid_member_count_ != 0) {
    real_raid_member_count_ = 0;
  }
  if (member_count <= 4) {
    real_party_member_count_ = member_count;
  }
  real_leader_guid_ = leader_guid;
}

std::uint32_t GroupSystem::GetRealPartyMemberCount() const {
  std::lock_guard lock(mutex_);
  return real_party_member_count_;
}

std::uint32_t GroupSystem::GetRealRaidMemberCount() const {
  std::lock_guard lock(mutex_);
  return real_raid_member_count_;
}

bool GroupSystem::IsBattlegroundGroup() const {
  std::lock_guard lock(mutex_);
  return is_battleground_group_;
}

std::uint64_t GroupSystem::GetRealLeaderGuid() const {
  std::lock_guard lock(mutex_);
  return real_leader_guid_;
}

const std::string& GroupSystem::GetLeaderName() const {
    std::lock_guard lock(mutex_);
    return leader_name_;
}

bool GroupSystem::IsLeader(uint64_t guid) const {
    std::lock_guard lock(mutex_);
    return guid == leader_guid_;
}

bool GroupSystem::IsAssistant(uint64_t guid) const {
    std::lock_guard lock(mutex_);
    for (const auto& m : members_) {
        if (m.guid == guid) return (m.flags & 0x01) != 0;
    }
    return false;
}

bool GroupSystem::HasRaidOfficerRank(uint64_t guid) const {
  std::lock_guard lock(mutex_);
  return HasTrackedRaidOfficerRank(members_, leader_guid_, is_raid_, guid);
}

uint64_t GroupSystem::GetMasterLooter() const {
    std::lock_guard lock(mutex_);
    return master_looter_;
}

uint8_t GroupSystem::GetLootThreshold() const {
    std::lock_guard lock(mutex_);
    return loot_threshold_;
}

std::vector<const GroupSystemMember*> GroupSystem::GetSubgroup(
    uint8_t index) const {
    std::lock_guard lock(mutex_);
    std::vector<const GroupSystemMember*> result;
    for (const auto& m : members_) {
        if (m.group_index == index) result.push_back(&m);
    }
    return result;
}

void GroupSystem::StartReadyCheck(const uint64_t initiator,
                                  const std::uint32_t now_tick,
                                  const std::uint32_t duration_ms) {
  std::lock_guard lock(mutex_);
  ResetReadyCheckStateUnlocked();
  ready_check_ = true;
  ready_check_initiator_guid_ = initiator;
  ready_check_end_time_ = ResolveReadyCheckNowTick(now_tick) + duration_ms;

  if (initiator != 0) {
    ready_check_responses_[initiator] = true;
  }

  for (std::size_t slot = 0; slot < party_guids_.size(); ++slot) {
    const bool is_initiator =
        party_guids_[slot] != 0 && party_guids_[slot] == initiator;
    party_ready_responses_[slot] = is_initiator;
    party_ready_waiting_[slot] =
        party_guids_[slot] != 0 && !is_initiator ? 1 : 0;
  }
}

void GroupSystem::SetReadyCheckResponse(const uint64_t guid, const bool ready) {
  std::lock_guard lock(mutex_);
  if (!ready_check_ || guid == 0) {
    return;
  }

  ready_check_responses_[guid] = ready;

  for (std::size_t slot = 0; slot < party_guids_.size(); ++slot) {
    if (party_guids_[slot] == guid) {
      party_ready_responses_[slot] = ready ? 1 : 0;
      party_ready_waiting_[slot] = 0;

      break;
    }
  }
}

bool GroupSystem::IsReadyCheckInProgress() const {
  std::lock_guard lock(mutex_);
  return ready_check_ &&
         IsReadyCheckDeadlineActive(openwow::core::GameClock::GetTickCount32(),
                                    ready_check_end_time_);
}

ReadyCheckQueryResult GroupSystem::GetTrackedReadyCheckStatus(const uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (guid == 0 || !ready_check_) {
    return ReadyCheckQueryResult::None;
  }

  if (is_raid_) {
    if (FindTrackedMemberByGuid(members_, guid) == nullptr) {
      return ReadyCheckQueryResult::None;
    }
  } else if (std::find(party_guids_.begin(), party_guids_.end(), guid) ==
                 party_guids_.end() &&
             guid != ready_check_initiator_guid_) {
    return ReadyCheckQueryResult::None;
  }

  if (guid == ready_check_initiator_guid_) {
    return ReadyCheckQueryResult::Ready;
  }

  const auto response_it = ready_check_responses_.find(guid);
  if (response_it == ready_check_responses_.end()) {
    return ReadyCheckQueryResult::Waiting;
  }

  return response_it->second ? ReadyCheckQueryResult::Ready
                             : ReadyCheckQueryResult::NotReady;
}

ReadyCheckQueryResult GroupSystem::QueryReadyCheckStatus(const uint64_t guid) const {
  const auto active_player_guid = local_player_.GetRawValue();

  std::lock_guard lock(mutex_);
  if (guid == 0) {
    return ReadyCheckQueryResult::None;
  }

  if (active_player_guid == 0 ||
      !ready_check_ ||
      !IsReadyCheckDeadlineActive(openwow::core::GameClock::GetTickCount32(),
                                  ready_check_end_time_)) {
    return ReadyCheckQueryResult::None;
  }

  if (is_raid_) {
    if (!HasTrackedRaidOfficerRank(members_, leader_guid_, is_raid_,
                                   active_player_guid)) {
      return ReadyCheckQueryResult::None;
    }
  } else {
    const bool has_party_members =
        std::any_of(party_guids_.begin(), party_guids_.end(),
                    [](const std::uint64_t party_guid) { return party_guid != 0; });
    if (!has_party_members || active_player_guid != leader_guid_) {
      return ReadyCheckQueryResult::None;
    }
  }

  if (is_raid_) {
    if (FindTrackedMemberByGuid(members_, guid) == nullptr) {
      return ReadyCheckQueryResult::None;
    }
  } else if (std::find(party_guids_.begin(), party_guids_.end(), guid) ==
                 party_guids_.end() &&
             guid != ready_check_initiator_guid_) {
    return ReadyCheckQueryResult::None;
  }

  if (guid == ready_check_initiator_guid_) {
    return ReadyCheckQueryResult::Ready;
  }

  const auto response_it = ready_check_responses_.find(guid);
  if (response_it == ready_check_responses_.end()) {
    return ReadyCheckQueryResult::Waiting;
  }

  return response_it->second ? ReadyCheckQueryResult::Ready
                             : ReadyCheckQueryResult::NotReady;
}

bool GroupSystem::GetReadyCheckResponse(uint64_t guid, bool& ready) const {
    std::lock_guard lock(mutex_);
    auto it = ready_check_responses_.find(guid);
    if (it == ready_check_responses_.end()) return false;
    ready = it->second;
    return true;
}

uint64_t GroupSystem::GetReadyCheckEndTime() const {
  std::lock_guard lock(mutex_);
  if (!ready_check_ ||
      !IsReadyCheckDeadlineActive(openwow::core::GameClock::GetTickCount32(),
                                  ready_check_end_time_)) {
    return 0;
  }
  return ready_check_end_time_;
}

uint64_t GroupSystem::GetReadyCheckInitiatorGuid() const {
  std::lock_guard lock(mutex_);
  if (!ready_check_ ||
      !IsReadyCheckDeadlineActive(openwow::core::GameClock::GetTickCount32(),
                                  ready_check_end_time_)) {
    return 0;
  }
  return ready_check_initiator_guid_;
}

void GroupSystem::ExpireReadyCheck() {
  std::lock_guard lock(mutex_);
  if (!ready_check_) {
    return;
  }

  ready_check_end_time_ = 0;
}

void GroupSystem::ClearReadyCheck() {
  std::lock_guard lock(mutex_);
  ResetReadyCheckStateUnlocked();
}

void GroupSystem::SetMasterLooterGuid(uint64_t guid) {
    std::lock_guard lock(mutex_);
    master_looter_ = guid;
}

void GroupSystem::SetLootThresholdValue(uint8_t threshold) {
    std::lock_guard lock(mutex_);
    loot_threshold_ = threshold;
}

void GroupSystem::SetRaidTarget(uint64_t guid, uint8_t icon) {
    std::lock_guard lock(mutex_);
    if (icon < 8) {

        for (auto& t : raid_targets_) {
            if (t == guid) t = 0;
        }
        raid_targets_[icon] = guid;
    }
}

uint64_t GroupSystem::GetRaidTarget(uint8_t icon) const {
    std::lock_guard lock(mutex_);
    if (icon >= 8) return 0;
    return raid_targets_[icon];
}

uint8_t GroupSystem::GetRaidTargetIndex(uint64_t guid) const {
    std::lock_guard lock(mutex_);
    for (uint8_t i = 0; i < 8; ++i) {
        if (raid_targets_[i] == guid) return i;
    }
    return 0xFF;
}

void GroupSystem::SetMemberPetGuid(uint64_t member_guid, uint64_t pet_guid) {
    std::lock_guard lock(mutex_);
    if (member_guid == 0) {
        return;
    }

    for (auto& member : members_) {
        if (member.guid == member_guid) {
            member.pet_guid = pet_guid;
            break;
        }
    }
}

uint32_t GroupSystem::GetRoleFlags(uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (guid == 0) {
    return 0;
  }

  if (is_raid_) {
    if (const auto* raid_member = FindTrackedMemberByGuid(members_, guid);
        raid_member != nullptr) {
      return static_cast<std::uint32_t>(raid_member->role);
    }
  }

  const auto active_player_guid = local_player_.GetRawValue();
  if (guid == active_player_guid && active_player_guid != 0) {
    return local_party_role_flags_;
  }

  const auto tracked_it =
      std::find(party_guids_.begin(), party_guids_.end(), guid);
  if (tracked_it == party_guids_.end()) {
    return 0;
  }

  const auto* member = FindTrackedMemberByGuid(members_, guid);
  return member != nullptr ? static_cast<std::uint32_t>(member->role) : 0;
}

bool GroupSystem::SetRoleFlags(const uint64_t guid,
                               const std::uint8_t role_flags,
                               const uint64_t active_player_guid) {
  std::lock_guard lock(mutex_);
  if (guid == 0) {
    return false;
  }

  bool updated = false;
  if (active_player_guid != 0 && guid == active_player_guid) {
    local_party_role_flags_ = role_flags;
    updated = true;
  }

  for (auto& member : members_) {
    if (member.guid == guid) {
      member.role = role_flags;
      updated = true;
      break;
    }
  }

  const auto role = [role_flags]() {
    if ((role_flags & kGroupRoleFlagTank) != 0) {
      return GroupRole::Tank;
    }
    if ((role_flags & kGroupRoleFlagHealer) != 0) {
      return GroupRole::Healer;
    }
    if ((role_flags & kGroupRoleFlagDamager) != 0) {
      return GroupRole::Damage;
    }
    return GroupRole::None;
  }();

  for (auto& member : new_members_) {
    if (member.guid.GetRawValue() == guid) {
      member.role = role;
      updated = true;
      break;
    }
  }

  return updated;
}

void GroupSystem::SetLocalPlayerPartyFlags(std::uint8_t flags) {
  std::lock_guard lock(mutex_);
  local_party_flags_ = flags;
}

void GroupSystem::SetLocalPlayerRoleFlags(std::uint8_t flags) {
  std::lock_guard lock(mutex_);
  local_party_role_flags_ = flags;
}

std::uint8_t GroupSystem::GetTrackedPartyAssignmentFlags(
    uint64_t guid, uint64_t active_player_guid) const {
  std::lock_guard lock(mutex_);
  return ResolveTrackedPartyAssignmentFlags(
      guid, active_player_guid, party_guids_, local_party_flags_, members_);
}

void GroupSystem::ApplyTrackedPartyAssignment(std::uint8_t role, bool apply,
                                              uint64_t target_guid,
                                              uint64_t active_player_guid) {
  std::lock_guard lock(mutex_);
  const auto role_flag = ResolveAssignmentRoleFlag(role);
  if (role_flag == 0 || target_guid == 0) {
    return;
  }

  const auto update_flags = [role_flag, apply](std::uint8_t& flags) {
    if (apply) {
      flags |= role_flag;
    } else {
      flags &= static_cast<std::uint8_t>(~role_flag);
    }
  };

  if (target_guid == active_player_guid && active_player_guid != 0) {
    update_flags(local_party_flags_);
  }

  for (auto& member : members_) {
    if (member.guid == target_guid) {
      update_flags(member.flags);
      break;
    }
  }

  if (role == 0) {
    if (apply) {
      main_tank_ = ObjectGuid(target_guid);
    } else if (main_tank_.GetRawValue() == target_guid) {
      main_tank_ = ObjectGuid{};
    }
    return;
  }

  if (apply) {
    main_assist_ = ObjectGuid(target_guid);
  } else if (main_assist_.GetRawValue() == target_guid) {
    main_assist_ = ObjectGuid{};
  }
}

std::uint8_t GroupSystem::GetMemberFlags(const std::uint64_t guid) const {
  std::lock_guard lock(mutex_);
  for (const auto& member : members_) {
    if (member.guid == guid) {
      return member.flags;
    }
  }
  return 0;
}

bool GroupSystem::CanSendPartyAssignmentChange(
    const std::uint32_t now_tick) const {
  std::lock_guard lock(mutex_);
  if (last_party_assignment_change_tick_ == 0) {
    return true;
  }

  return static_cast<std::int32_t>(
             now_tick - last_party_assignment_change_tick_) >= 750;
}

void GroupSystem::MarkPartyAssignmentChangeSent(
    const std::uint32_t now_tick) {
  std::lock_guard lock(mutex_);
  last_party_assignment_change_tick_ = now_tick;
}

void GroupSystem::Reset() {
  std::lock_guard lock(mutex_);

  new_members_.clear();
  group_type_ = GroupKind::None;
  leader_obj_ = ObjectGuid{};
  local_player_ = ObjectGuid{};
  loot_method_new_ = kStartupLootMethod;
  default_dungeon_diff_ = DungeonDifficulty::Normal;
  party_dungeon_diff_ = DungeonDifficulty::Normal;
  current_raid_diff_ = RaidDifficulty::Normal10;
  default_raid_diff_ = RaidDifficulty::Normal10;
  player_difficulty_index_ = 0;
  main_tank_ = ObjectGuid{};
  main_assist_ = ObjectGuid{};

  members_.clear();
  leader_guid_ = 0;
  leader_name_.clear();
  is_raid_ = false;
  loot_method_ = kStartupLootMethod;
  master_looter_ = 0;
  loot_threshold_ = 2;
  ResetReadyCheckStateUnlocked();
  raid_targets_.fill(0);
  raid_roster_selection_ = 0;
  raid_initialized_ = false;
  local_sub_group_ = 0;
  local_party_flags_ = 0;
  local_party_role_flags_ = 0;
  party_lfg_dungeon_id_ = 0;
  real_party_member_count_ = 0;
  real_raid_member_count_ = 0;
  has_lfg_restrictions_ = false;
  real_leader_guid_ = 0;
  last_party_assignment_change_tick_ = 0;
  party_guids_.fill(0);
  tracked_party_leader_slot_ = -1;
}

bool GroupSystem::HasPartyMembers() const {
  std::lock_guard lock(mutex_);
  return party_guids_[0] != 0;
}

uint32_t GroupSystem::GetTrackedPartyMemberCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<uint32_t>(
      std::count_if(party_guids_.begin(), party_guids_.end(),
                    [](uint64_t guid) { return guid != 0; }));
}

uint64_t GroupSystem::GetTrackedPartyMemberGuid(uint32_t slot) const {
  std::lock_guard lock(mutex_);
  if (slot >= party_guids_.size()) {
    return 0;
  }
  return party_guids_[slot];
}

uint64_t GroupSystem::GetTrackedPartyControlledUnitGuid(const ObjectManager& objects,
                                                        uint32_t slot) const {
  std::lock_guard lock(mutex_);
  if (slot >= party_guids_.size()) {
    return 0;
  }

  const auto party_guid = party_guids_[slot];
  if (party_guid == 0) {
    return 0;
  }

  const auto* member = FindTrackedMemberByGuid(members_, party_guid);
  const auto cached_pet_guid = member != nullptr ? member->pet_guid : 0;
  return ResolveControlledUnitGuid(objects, party_guid, cached_pet_guid);
}

int GroupSystem::GetTrackedPartyLeaderIndex() const {
  std::lock_guard lock(mutex_);
  return tracked_party_leader_slot_ + 1;
}

GroupSystem::LootMethodMasterIndices GroupSystem::ResolveLootMethodMasterIndices(
    const uint64_t active_player_guid) const {
  std::lock_guard lock(mutex_);

  LootMethodMasterIndices indices;
  if (master_looter_ == 0) {
    return indices;
  }

  if (master_looter_ == active_player_guid) {
    indices.party_index = 0;
  } else {
    for (std::size_t slot = 0; slot < party_guids_.size(); ++slot) {
      if (party_guids_[slot] == master_looter_) {
        indices.party_index = static_cast<int>(slot + 1);
        break;
      }
    }
  }

  for (std::size_t roster_index = 0; roster_index < members_.size();
       ++roster_index) {
    if (members_[roster_index].guid == master_looter_) {
      indices.raid_index = static_cast<int>(roster_index + 1);
      break;
    }
  }

  return indices;
}

void GroupSystem::SetPartyLfgDungeonId(const std::uint32_t dungeon_id) {
  std::lock_guard lock(mutex_);
  party_lfg_dungeon_id_ = dungeon_id;
}

std::uint32_t GroupSystem::GetPartyLfgDungeonId() const {
  std::lock_guard lock(mutex_);
  return party_lfg_dungeon_id_;
}

bool GroupSystem::HasPartyLfgDungeon() const {
  std::lock_guard lock(mutex_);
  return party_lfg_dungeon_id_ != 0;
}

bool GroupSystem::CanPartyLfgBackfill() const {
  std::lock_guard lock(mutex_);
  return real_party_member_count_ != 0 &&
         real_party_member_count_ < party_guids_.size() &&
         party_lfg_dungeon_id_ != 0 &&
         (local_party_flags_ & static_cast<std::uint8_t>(GroupMemberFlag::kMainTank)) == 0;
}

void GroupSystem::SetPartyLfgStatusFlags(const std::uint32_t flags) {
  std::lock_guard lock(mutex_);
  party_lfg_status_flags_ = flags;
}

void GroupSystem::ReplaceRaidTargets(const std::array<std::uint64_t, 8>& icons) {
  std::lock_guard lock(mutex_);
  for (std::size_t i = 0; i < icons.size(); ++i) {
    SetRaidTarget(icons[i], static_cast<std::uint8_t>(i));
  }
}

bool GroupSystem::SetHasLfgRestrictions(bool restricted) {
  std::lock_guard lock(mutex_);
  if (has_lfg_restrictions_ == restricted)
    return false;
  has_lfg_restrictions_ = restricted;
  return true;
}

bool GroupSystem::HasLfgRestrictions() const {
  std::lock_guard lock(mutex_);
  return has_lfg_restrictions_;
}

int GroupSystem::FindPartySlotByGuid(uint64_t guid) const {
  std::lock_guard lock(mutex_);
  return FindTrackedPartySlot(party_guids_, guid);
}

int GroupSystem::GetPartyReadyCheckResponse(uint32_t slot) const {
  std::lock_guard lock(mutex_);
  if (slot < 4) return party_ready_responses_[slot];
  return 0;
}

int GroupSystem::IsPartyReadyCheckWaitingBySlot(uint32_t slot) const {
  std::lock_guard lock(mutex_);
  if (slot < 4) return party_ready_waiting_[slot];
  return 0;
}

int GroupSystem::FindRaidRosterIndexByGuid(uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (!is_raid_ || guid == 0) return -1;

  for (size_t i = 0; i < members_.size(); ++i) {
    if (members_[i].guid == guid) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int GroupSystem::FindRaidRosterSelection() const {
  std::lock_guard lock(mutex_);
  if (!is_raid_ || raid_roster_selection_ == 0) return -1;
  for (size_t i = 0; i < members_.size(); ++i) {
    if (members_[i].guid == raid_roster_selection_) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void GroupSystem::SetRaidTargetIcon(uint32_t icon_index,
                                   uint64_t target_guid) {
  std::lock_guard lock(mutex_);
  if (icon_index >= 8) return;
  raid_targets_[icon_index] = target_guid;
}

bool GroupSystem::FindPartyMemberByControlledUnitGuid(const ObjectManager& objects,
                                                      uint64_t unit_guid,
                                                      int* out_index) const {
  std::lock_guard lock(mutex_);
  if (unit_guid == 0) return false;

  for (size_t i = 0; i < party_guids_.size(); ++i) {
    const auto party_guid = party_guids_[i];
    if (party_guid == 0) {
      continue;
    }

    const auto* member = FindTrackedMemberByGuid(members_, party_guid);
    const auto cached_pet_guid = member != nullptr ? member->pet_guid : 0;
    if (MatchesControlledUnitGuid(objects, party_guid, cached_pet_guid, unit_guid)) {
      if (out_index) *out_index = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

bool GroupSystem::FindRaidMemberByControlledUnitGuid(const ObjectManager& objects,
                                                     uint64_t unit_guid,
                                                     int* out_index) const {
  std::lock_guard lock(mutex_);
  if (unit_guid == 0 || !is_raid_) return false;

  for (size_t i = 0; i < members_.size(); ++i) {
    const auto& member = members_[i];
    if (MatchesControlledUnitGuid(objects, member.guid, member.pet_guid, unit_guid)) {
      if (out_index) *out_index = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

bool GroupSystem::IsActivePlayerOrPartyUnitGuid(const ObjectManager& objects,
                                                uint64_t guid) const {
  if (guid == 0) {
    return false;
  }

  const auto active_player_guid = local_player_.GetRawValue();
  if (guid == active_player_guid) {
    return true;
  }

  if (const auto* active_player = objects.GetActivePlayer();
      active_player != nullptr &&
      active_player->State().GetPrimaryControlledUnitGUID().GetRawValue() == guid) {
    return true;
  }

  return IsPartyUnitGuid(objects, guid);
}

bool GroupSystem::IsActivePlayerPartyOrRaidUnitGuid(const ObjectManager& objects,
                                                    uint64_t guid) const {
  if (IsActivePlayerOrPartyUnitGuid(objects, guid)) {
    return true;
  }

  return IsRaidUnitGuid(objects, guid);
}

bool GroupSystem::IsGroupUnitGuid(const ObjectManager& objects,
                                  uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (guid == 0) return false;

  for (const auto& member : members_) {
    if (member.guid == guid ||
        MatchesControlledUnitGuid(objects, member.guid, member.pet_guid, guid)) {
      return true;
    }
  }
  return false;
}

bool GroupSystem::IsPartyUnitGuid(const ObjectManager& objects,
                                  uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (guid == 0) return false;

  for (const auto party_guid : party_guids_) {
    if (party_guid == 0) {
      continue;
    }

    if (party_guid == guid) {
      return true;
    }

    const auto* member = FindTrackedMemberByGuid(members_, party_guid);
    const auto cached_pet_guid = member != nullptr ? member->pet_guid : 0;
    if (MatchesControlledUnitGuid(objects, party_guid, cached_pet_guid, guid)) {
      return true;
    }
  }
  return false;
}

bool GroupSystem::IsRaidUnitGuid(const ObjectManager& objects,
                                 uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (guid == 0 || !is_raid_) return false;

  for (const auto& member : members_) {
    if (member.guid == guid ||
        MatchesControlledUnitGuid(objects, member.guid, member.pet_guid, guid)) {
      return true;
    }
  }
  return false;
}

bool GroupSystem::IsActivePlayerOrPartyMemberGuid(uint64_t guid) const {
  if (guid == 0) {
    return false;
  }

  const auto active_player_guid = local_player_.GetRawValue();
  if (guid == active_player_guid) {
    return true;
  }

  std::lock_guard lock(mutex_);
  for (const auto party_guid : party_guids_) {
    if (party_guid != 0 && party_guid == guid) {
      return true;
    }
  }
  return false;
}

bool GroupSystem::IsRaidMemberGuid(uint64_t guid) const {
  std::lock_guard lock(mutex_);
  if (guid == 0 || !is_raid_) return false;

  for (const auto& member : members_) {
    if (member.guid == guid) {
      return true;
    }
  }
  return false;
}

bool GroupSystem::IsActivePlayerPartyOrRaidMemberGuid(uint64_t guid) const {
  if (IsActivePlayerOrPartyMemberGuid(guid)) {
    return true;
  }
  return IsRaidMemberGuid(guid);
}

bool GroupSystem::DoReadyCheck(const ReadyCheckPacketSender& send_ready_check,
                               const ReadyCheckSystemMessageSink& system_message_sink) {
  if (!send_ready_check) {
    return false;
  }

  const auto active_player_guid = local_player_.GetRawValue();
  if (active_player_guid == 0) {
    return false;
  }

  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  bool is_authorized = false;
  bool show_throttle_message = false;
  {
    std::lock_guard lock(mutex_);
    is_authorized = active_player_guid == leader_guid_;
    if (!is_authorized && is_raid_) {
      is_authorized = HasTrackedRaidOfficerRank(members_, leader_guid_, is_raid_,
                                                active_player_guid);
    }

    if (is_authorized &&
        ready_check_ &&
        IsReadyCheckDeadlineActive(now_tick, ready_check_end_time_)) {
      show_throttle_message = true;
    }
  }

  if (!is_authorized) {
    return false;
  }

  if (show_throttle_message && system_message_sink) {
    system_message_sink(500);
    return false;
  }

  if (show_throttle_message) {
    return false;
  }

  send_ready_check();
  return true;
}

void GroupSystem::UpdateRaidRoster(
    const std::vector<uint64_t>& member_guids,
    const std::vector<uint8_t>& flags,
    const std::vector<uint8_t>& online_flags,
    uint8_t group_type_byte) {
  std::lock_guard lock(mutex_);

  if (member_guids.size() > 40) return;

  auto old_members = members_;
  members_.clear();
  is_raid_ = true;
  group_type_ = GroupKind::Raid;

  for (size_t i = 0; i < member_guids.size(); ++i) {
    GroupSystemMember m;
    m.guid = member_guids[i];
    m.is_online = (i < online_flags.size()) ? (online_flags[i] != 0) : true;
    m.flags = (i < flags.size()) ? flags[i] : 0;
    m.group_index = m.flags & 0x7;

    members_.push_back(m);
  }
  PreserveCachedPetGuids(members_, old_members);

  (void)group_type_byte;
}

void GroupSystem::InitializeRaid() {
  std::lock_guard lock(mutex_);
  if (raid_initialized_) return;

  members_.clear();
  members_.reserve(40);
  raid_targets_.fill(0);
  party_ready_responses_.fill(0);
  party_ready_waiting_.fill(0);
  party_guids_.fill(0);
  raid_roster_selection_ = 0;
  local_sub_group_ = 0;
  local_party_flags_ = 0;
  local_party_role_flags_ = 0;
  party_lfg_dungeon_id_ = 0;
  real_party_member_count_ = 0;
  real_raid_member_count_ = 0;
  real_leader_guid_ = 0;
  tracked_party_leader_slot_ = 0;
  raid_initialized_ = true;
}

void GroupSystem::ShutdownRaid() {
  std::lock_guard lock(mutex_);
  members_.clear();
  ResetReadyCheckStateUnlocked();
  raid_targets_.fill(0);
  party_guids_.fill(0);
  raid_roster_selection_ = 0;
  local_sub_group_ = 0;
  local_party_flags_ = 0;
  local_party_role_flags_ = 0;
  party_lfg_dungeon_id_ = 0;
  real_party_member_count_ = 0;
  real_raid_member_count_ = 0;
  real_leader_guid_ = 0;
  tracked_party_leader_slot_ = 0;
  raid_initialized_ = false;
}

void GroupSystem::ResetGroupWithNotification() {
  std::lock_guard lock(mutex_);
  members_.clear();
  is_raid_ = false;
  ResetReadyCheckStateUnlocked();
  raid_targets_.fill(0);
  local_party_flags_ = 0;
  local_party_role_flags_ = 0;
  party_lfg_dungeon_id_ = 0;
  has_lfg_restrictions_ = false;
  real_party_member_count_ = 0;
  real_raid_member_count_ = 0;
  real_leader_guid_ = 0;
  tracked_party_leader_slot_ = 0;
}

void GroupSystem::SetRaidRosterSelection(uint64_t guid) {
  std::lock_guard lock(mutex_);
  raid_roster_selection_ = guid;
}

uint64_t GroupSystem::GetRaidRosterSelection() const {
  std::lock_guard lock(mutex_);
  return raid_roster_selection_;
}

}

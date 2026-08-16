
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

class ObjectManager;

enum class GroupKind : uint8_t {
    None  = 0,
    Party = 1,
    Raid  = 2,
};

enum class GroupRole : uint8_t {
    None   = 0,
    Tank   = 1,
    Healer = 2,
    Damage = 4,
};

inline constexpr std::uint8_t kGroupRoleFlagDamager = 0x02u;
inline constexpr std::uint8_t kGroupRoleFlagTank = 0x04u;
inline constexpr std::uint8_t kGroupRoleFlagHealer = 0x08u;

enum class DungeonDifficulty : uint8_t {
    Normal = 0,
    Heroic = 1,
};

enum class RaidDifficulty : uint8_t {
    Normal10 = 0,
    Normal25 = 1,
    Heroic10 = 2,
    Heroic25 = 3,
};

enum class ReadyCheckQueryResult : uint8_t {
    None = 0,
    Waiting = 1,
    Ready = 2,
    NotReady = 3,
};

struct GroupSystemMember {
    uint64_t guid = 0;
    uint64_t pet_guid = 0;
    std::string name;
    uint8_t group_index = 0;
    uint8_t flags = 0;
    bool is_online = false;
    uint8_t online_status = 0;
    uint8_t class_id = 0;
    uint8_t role = 0;
};

struct GroupMemberData {
    ObjectGuid guid;
    std::string name;
    uint8_t classId = 0;
    uint8_t level = 0;
    bool online = false;
    bool dead = false;
    bool isAssistant = false;
    uint8_t subGroup = 0;
    GroupRole role = GroupRole::None;
};

class GroupSystem {
 public:
    static constexpr std::uint8_t kStartupLootMethod = 3;

    struct LootMethodMasterIndices {
        int party_index = -1;
        int raid_index = -1;
    };

    using ReadyCheckPacketSender = std::function<void()>;
    using ReadyCheckSystemMessageSink = std::function<void(int)>;

    static GroupSystem& Get();

    void SetGroupType(GroupKind type);
    [[nodiscard]] GroupKind GetGroupType() const;

    void SetLeader(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetLeader() const;

    [[nodiscard]] bool IsLeader() const;

    void SetLocalPlayerGuid(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetLocalPlayerGuid() const;

    void AddMember(const GroupMemberData& member);
    void RemoveMember(ObjectGuid guid);
    [[nodiscard]] std::optional<GroupMemberData> GetMember(ObjectGuid guid) const;
    [[nodiscard]] std::vector<GroupMemberData> GetMembers() const;
    [[nodiscard]] uint32_t GetMemberCount() const;

    [[nodiscard]] bool IsInGroup() const;
    [[nodiscard]] bool IsInRaid() const;
    [[nodiscard]] std::vector<GroupMemberData> GetSubGroupMembers(uint32_t subGroup) const;
    void SetSubGroup(ObjectGuid guid, uint32_t subGroup);
    void SetRole(ObjectGuid guid, GroupRole role);
    void SetAssistant(ObjectGuid guid, bool assistant);
    [[nodiscard]] uint32_t GetMaxMembers() const;
    [[nodiscard]] bool IsGroupFull() const;

    void SetLootMethod(uint32_t method);
    [[nodiscard]] uint32_t GetLootMethod() const;

    void SetDungeonDifficulty(DungeonDifficulty diff);
    void SetDefaultDungeonDifficulty(DungeonDifficulty diff);
    [[nodiscard]] DungeonDifficulty GetDefaultDungeonDifficulty() const;
    void SetPartyDungeonDifficulty(DungeonDifficulty diff);
    [[nodiscard]] DungeonDifficulty GetPartyDungeonDifficulty() const;
    void ApplyDungeonDifficultyUpdate(DungeonDifficulty diff,
                                      bool update_default,
                                      bool update_group);
    [[nodiscard]] DungeonDifficulty GetDungeonDifficulty() const;
    void SetRaidDifficulty(RaidDifficulty diff);
    void SetCurrentRaidDifficulty(RaidDifficulty diff);
    [[nodiscard]] RaidDifficulty GetCurrentRaidDifficulty() const;
    void SetDefaultRaidDifficulty(RaidDifficulty diff);
    [[nodiscard]] RaidDifficulty GetDefaultRaidDifficulty() const;
    [[nodiscard]] RaidDifficulty GetRaidDifficulty() const;
    void SetPlayerDifficultyIndex(std::uint8_t difficulty);
    [[nodiscard]] std::uint8_t GetPlayerDifficultyIndex() const;
    [[nodiscard]] std::uint8_t GetEffectiveRaidMapDifficultyIndex(
        bool map_allows_player_difficulty) const;

    void ConvertToRaid();

    [[nodiscard]] ObjectGuid GetMainTank() const;
    void SetMainTank(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetMainAssist() const;
    void SetMainAssist(ObjectGuid guid);

    void Clear();

    void SetGroupData(const std::vector<GroupSystemMember>& members,
                      uint64_t leader, uint8_t lootMethod,
                      uint64_t masterLooter, uint8_t lootThreshold,
                      bool is_raid = false, uint8_t local_sub_group = 0,
                      uint64_t active_player_guid = 0);
    void ClearGroup();
    void SetIsRaid(bool raid);

    [[nodiscard]] size_t GetNumGroupMembers() const;
    [[nodiscard]] const GroupSystemMember* GetMember(size_t index) const;
    [[nodiscard]] std::optional<GroupSystemMember> GetMemberSnapshot(size_t index) const;
    [[nodiscard]] const GroupSystemMember* GetMemberByGuid(uint64_t guid) const;

    [[nodiscard]] uint64_t GetLeaderGuid() const;
    [[nodiscard]] const std::string& GetLeaderName() const;
    [[nodiscard]] bool IsLeader(uint64_t guid) const;
    [[nodiscard]] bool IsAssistant(uint64_t guid) const;
    [[nodiscard]] bool HasRaidOfficerRank(uint64_t guid) const;

    [[nodiscard]] uint64_t GetMasterLooter() const;
    [[nodiscard]] uint8_t GetLootThreshold() const;

    [[nodiscard]] std::vector<const GroupSystemMember*> GetSubgroup(
        uint8_t index) const;

    void StartReadyCheck(uint64_t initiator, std::uint32_t now_tick = 0,
                         std::uint32_t duration_ms = 30000);
    void SetReadyCheckResponse(uint64_t guid, bool ready);
    [[nodiscard]] bool IsReadyCheckInProgress() const;
    [[nodiscard]] ReadyCheckQueryResult QueryReadyCheckStatus(uint64_t guid) const;
    [[nodiscard]] ReadyCheckQueryResult GetTrackedReadyCheckStatus(uint64_t guid) const;
    [[nodiscard]] bool GetReadyCheckResponse(uint64_t guid, bool& ready) const;
    [[nodiscard]] uint64_t GetReadyCheckEndTime() const;
    [[nodiscard]] uint64_t GetReadyCheckInitiatorGuid() const;
    void ExpireReadyCheck();
    void ClearReadyCheck();

    void SetMasterLooterGuid(uint64_t guid);
    void SetLootThresholdValue(uint8_t threshold);

    void SetRaidTarget(uint64_t guid, uint8_t icon);
    [[nodiscard]] uint64_t GetRaidTarget(uint8_t icon) const;
    [[nodiscard]] uint8_t GetRaidTargetIndex(uint64_t guid) const;
    void SetMemberPetGuid(uint64_t member_guid, uint64_t pet_guid);

    [[nodiscard]] uint32_t GetRoleFlags(uint64_t guid) const;
    bool SetRoleFlags(uint64_t guid, std::uint8_t role_flags,
                      uint64_t active_player_guid = 0);

    void SetLocalPlayerPartyFlags(std::uint8_t flags);
    void SetLocalPlayerRoleFlags(std::uint8_t flags);
    [[nodiscard]] std::uint8_t GetTrackedPartyAssignmentFlags(
        uint64_t guid, uint64_t active_player_guid) const;
    void ApplyTrackedPartyAssignment(std::uint8_t role, bool apply,
                                     uint64_t target_guid,
                                     uint64_t active_player_guid);
    [[nodiscard]] std::uint8_t GetMemberFlags(std::uint64_t guid) const;
    [[nodiscard]] bool CanSendPartyAssignmentChange(
        std::uint32_t now_tick) const;
    void MarkPartyAssignmentChangeSent(std::uint32_t now_tick);

    [[nodiscard]] bool HasPartyMembers() const;
    [[nodiscard]] uint32_t GetTrackedPartyMemberCount() const;
    [[nodiscard]] uint64_t GetTrackedPartyMemberGuid(uint32_t slot) const;
    [[nodiscard]] uint64_t GetTrackedPartyControlledUnitGuid(
        const ObjectManager& objects, uint32_t slot) const;
    [[nodiscard]] int GetTrackedPartyLeaderIndex() const;

    void UpdateRealGroupStateFromGroupList(bool is_raid, bool is_battleground,
                                           std::uint32_t roster_member_count,
                                           std::uint32_t local_party_member_count,
                                           std::uint64_t leader_guid);
    void ApplyRealGroupUpdate(std::uint8_t group_flags, std::uint32_t member_count,
                              std::uint64_t leader_guid);
    [[nodiscard]] std::uint32_t GetRealPartyMemberCount() const;
    [[nodiscard]] std::uint32_t GetRealRaidMemberCount() const;
    [[nodiscard]] bool IsBattlegroundGroup() const;
    [[nodiscard]] std::uint64_t GetRealLeaderGuid() const;

    [[nodiscard]] LootMethodMasterIndices ResolveLootMethodMasterIndices(
        uint64_t active_player_guid) const;

    void SetPartyLfgDungeonId(std::uint32_t dungeon_id);
    [[nodiscard]] std::uint32_t GetPartyLfgDungeonId() const;
    [[nodiscard]] bool HasPartyLfgDungeon() const;
    [[nodiscard]] bool CanPartyLfgBackfill() const;

    void SetPartyLfgStatusFlags(std::uint32_t flags);

    void ReplaceRaidTargets(const std::array<std::uint64_t, 8>& icons);

    bool SetHasLfgRestrictions(bool restricted);
    [[nodiscard]] bool HasLfgRestrictions() const;

    [[nodiscard]] int FindPartySlotByGuid(uint64_t guid) const;

    [[nodiscard]] int GetPartyReadyCheckResponse(uint32_t slot) const;

    [[nodiscard]] int IsPartyReadyCheckWaitingBySlot(uint32_t slot) const;

    [[nodiscard]] int FindRaidRosterIndexByGuid(uint64_t guid) const;

    [[nodiscard]] int FindRaidRosterSelection() const;

    void SetRaidTargetIcon(uint32_t icon_index, uint64_t target_guid);

    [[nodiscard]] bool FindPartyMemberByControlledUnitGuid(
        const ObjectManager& objects, uint64_t unit_guid, int* out_index) const;

    [[nodiscard]] bool FindRaidMemberByControlledUnitGuid(
        const ObjectManager& objects, uint64_t unit_guid, int* out_index) const;

    [[nodiscard]] bool IsActivePlayerOrPartyUnitGuid(
        const ObjectManager& objects, uint64_t guid) const;
    [[nodiscard]] bool IsActivePlayerPartyOrRaidUnitGuid(
        const ObjectManager& objects, uint64_t guid) const;

    [[nodiscard]] bool IsActivePlayerOrPartyMemberGuid(uint64_t guid) const;
    [[nodiscard]] bool IsActivePlayerPartyOrRaidMemberGuid(uint64_t guid) const;

    [[nodiscard]] bool IsGroupUnitGuid(const ObjectManager& objects,
                                       uint64_t guid) const;
    [[nodiscard]] bool IsPartyUnitGuid(const ObjectManager& objects,
                                       uint64_t guid) const;
    [[nodiscard]] bool IsRaidUnitGuid(const ObjectManager& objects,
                                      uint64_t guid) const;

    [[nodiscard]] bool IsRaidMemberGuid(uint64_t guid) const;

    bool DoReadyCheck(const ReadyCheckPacketSender& send_ready_check,
                      const ReadyCheckSystemMessageSink& system_message_sink = {});

    void UpdateRaidRoster(const std::vector<uint64_t>& member_guids,
                          const std::vector<uint8_t>& flags,
                          const std::vector<uint8_t>& online_flags,
                          uint8_t group_type_byte);

    void InitializeRaid();

    void ShutdownRaid();

    void ResetGroupWithNotification();

    void SetRaidRosterSelection(uint64_t guid);
    [[nodiscard]] uint64_t GetRaidRosterSelection() const;

    void Reset();

 private:
    GroupSystem() = default;
    void ResetReadyCheckStateUnlocked();
    [[nodiscard]] bool HasTrackedRaidRosterUnlocked() const;
    [[nodiscard]] RaidDifficulty GetActiveRaidDifficultyUnlocked() const;

    std::vector<GroupMemberData> new_members_;
    GroupKind group_type_ = GroupKind::None;
    ObjectGuid leader_obj_;
    ObjectGuid local_player_;
    uint32_t loot_method_new_ = kStartupLootMethod;
    DungeonDifficulty default_dungeon_diff_ = DungeonDifficulty::Normal;
    DungeonDifficulty party_dungeon_diff_ = DungeonDifficulty::Normal;
    RaidDifficulty current_raid_diff_ = RaidDifficulty::Normal10;
    RaidDifficulty default_raid_diff_ = RaidDifficulty::Normal10;
    std::uint8_t player_difficulty_index_ = 0;
    ObjectGuid main_tank_;
    ObjectGuid main_assist_;

    std::vector<GroupSystemMember> members_;
    uint64_t leader_guid_ = 0;
    std::string leader_name_;
    bool is_raid_ = false;
    uint8_t loot_method_ = kStartupLootMethod;
    uint64_t master_looter_ = 0;
    uint8_t loot_threshold_ = 2;
    bool ready_check_ = false;
    std::unordered_map<uint64_t, bool> ready_check_responses_;
    uint64_t ready_check_initiator_guid_ = 0;
    std::uint32_t ready_check_end_time_{0};
    std::array<uint64_t, 8> raid_targets_{};

    uint64_t raid_roster_selection_ = 0;
    bool raid_initialized_ = false;
    uint8_t local_sub_group_ = 0;
    uint8_t local_party_flags_ = 0;
    uint8_t local_party_role_flags_ = 0;
    std::uint32_t party_lfg_dungeon_id_ = 0;
    std::uint32_t party_lfg_status_flags_ = 0;
    std::uint32_t real_party_member_count_ = 0;
    std::uint32_t real_raid_member_count_ = 0;
    bool is_battleground_group_ = false;
    bool has_lfg_restrictions_ = false;

    std::uint64_t real_leader_guid_ = 0;
    std::uint32_t last_party_assignment_change_tick_ = 0;
    std::array<int, 4> party_ready_responses_{};

    std::array<int, 4> party_ready_waiting_{};

    std::array<uint64_t, 4> party_guids_{};

    int tracked_party_leader_slot_ = -1;

    mutable std::mutex mutex_;
};

}

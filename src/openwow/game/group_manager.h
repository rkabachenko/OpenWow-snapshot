#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class GroupType : std::uint8_t {
  kNormal    = 0x00,
  kBg        = 0x01,
  kRaid      = 0x02,
  kBgRaid    = 0x03,
  kLfgType1  = 0x08,
  kLfg       = 0x0C,
  kLeaveFlag = 0x10,
};

inline std::uint8_t GroupTypeRaw(GroupType t) {
  return static_cast<std::uint8_t>(t);
}

enum class GroupMemberOnline : std::uint8_t {
  kOffline         = 0x0000,
  kOnline          = 0x0001,
  kPvp             = 0x0002,
  kDead            = 0x0004,
  kGhost           = 0x0008,
  kPvpFfa          = 0x0010,
  kZoneSupport     = 0x0020,
  kAfk             = 0x0040,
  kDnd             = 0x0080,
};

enum class GroupMemberFlag : std::uint8_t {
  kNone          = 0x00,
  kAssistant     = 0x01,
  kMainTank      = 0x02,
  kMainAssist    = 0x04,
};

enum class PartyOperation : std::uint32_t {
  kInvite   = 0,
  kUninvite = 1,
  kLeave    = 2,
  kSwap     = 3,
};

enum class PartyResult : std::uint32_t {
  kOk                 = 0,
  kBadPlayerName      = 1,
  kTargetNotInGroup   = 2,
  kTargetNotInInstance = 3,
  kGroupFull          = 4,
  kAlreadyInGroup     = 5,
  kNotInGroup         = 6,
  kNotLeader          = 7,
  kPlayerWrongFaction = 8,
  kIgnoringYou        = 9,
  kLfgPending         = 12,
  kInviteRestricted   = 13,
};

struct GroupMember {
  std::string name;
  ObjectGuid guid;
  std::uint8_t online_status{0};
  std::uint8_t sub_group{0};
  std::uint8_t flags{0};
  std::uint8_t roles{0};
};

struct PartyCommandResult {
  PartyOperation operation{PartyOperation::kInvite};
  std::string member;
  PartyResult result{PartyResult::kOk};
  std::int32_t value{0};
};

struct GroupInvite {
  std::string inviter_name;
  bool pending{false};
};

struct RealGroupUpdate {
  std::uint8_t group_flags{0};
  std::uint32_t member_count{0};
  std::uint64_t leader_guid{0};
};

class GroupManager {
 public:
  GroupManager() = default;

  bool HandleGroupList(const std::uint8_t* data, std::size_t len);

  bool HandleGroupInvite(const std::uint8_t* data, std::size_t len);

  bool HandleGroupDecline(const std::uint8_t* data, std::size_t len,
                          std::string& decliner_name);

  bool HandleSetLeader(const std::uint8_t* data, std::size_t len);

  static bool ParsePartyCommandResult(const std::uint8_t* data, std::size_t len,
                                      PartyCommandResult& out);

  bool HandleGroupDestroyed();

  bool HandleGroupUninvite();

  bool HandleRealGroupUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleGroupActionThrottled();

  bool HandleGroupCancel();

  static net::wotlk::WorldPacket BuildGroupInvite(const std::string& name);
  static net::wotlk::WorldPacket BuildGroupAccept();
  static net::wotlk::WorldPacket BuildGroupAccept(std::uint32_t roles);
  static net::wotlk::WorldPacket BuildGroupDecline();
  static net::wotlk::WorldPacket BuildGroupDisband();
  static net::wotlk::WorldPacket BuildGroupUninvite(const std::string& name);

  static net::wotlk::WorldPacket BuildGroupUninviteByGuid(
      std::uint64_t target_guid, const std::string& reason = "");

  [[nodiscard]] bool IsInGroup() const { return !members_.empty() || group_type_ != 0; }
  [[nodiscard]] bool IsRaid() const { return is_raid_; }
  [[nodiscard]] std::uint8_t group_type() const { return group_type_; }
  [[nodiscard]] const std::vector<GroupMember>& members() const { return members_; }
  [[nodiscard]] std::size_t member_count() const { return members_.size(); }

  [[nodiscard]] const GroupMember* GetMember(const ObjectGuid& guid) const {
    for (const auto& m : members_) {
      if (m.guid == guid) return &m;
    }
    return nullptr;
  }
  [[nodiscard]] const ObjectGuid& leader_guid() const { return leader_guid_; }
  [[nodiscard]] const std::string& leader_name() const { return leader_name_; }
  [[nodiscard]] std::uint8_t my_sub_group() const { return my_sub_group_; }
  [[nodiscard]] std::uint8_t my_flags() const { return my_flags_; }
  [[nodiscard]] std::uint8_t my_roles() const { return my_roles_; }
  [[nodiscard]] std::uint8_t loot_method() const { return loot_method_; }
  [[nodiscard]] std::uint64_t master_looter_guid() const { return master_looter_guid_; }
  [[nodiscard]] std::uint8_t loot_threshold() const { return loot_threshold_; }
  [[nodiscard]] std::uint8_t dungeon_difficulty() const { return dungeon_difficulty_; }
  [[nodiscard]] std::uint8_t raid_difficulty() const { return raid_difficulty_; }
  [[nodiscard]] std::uint8_t player_difficulty_index() const {
    return player_difficulty_index_;
  }
  [[nodiscard]] bool IsBattlegroundGroup() const {
    return (group_type_ & static_cast<std::uint8_t>(GroupType::kBg)) != 0;
  }
  [[nodiscard]] std::uint32_t party_lfg_dungeon_id() const { return party_lfg_dungeon_id_; }
  [[nodiscard]] const GroupInvite& pending_invite() const { return pending_invite_; }
  [[nodiscard]] bool has_pending_invite() const { return pending_invite_.pending; }
  [[nodiscard]] bool group_destroyed() const { return group_destroyed_; }
  [[nodiscard]] bool group_uninvited() const { return group_uninvited_; }
  [[nodiscard]] bool group_action_throttled() const { return group_action_throttled_; }
  [[nodiscard]] bool group_cancelled() const { return group_cancelled_; }
  [[nodiscard]] const std::optional<RealGroupUpdate>& last_real_group_update() const {
    return last_real_group_update_;
  }

  void ClearInvite() { pending_invite_ = {}; }
  void Clear();

  [[nodiscard]] bool has_lfg_restrictions() const { return has_lfg_restrictions_; }
  [[nodiscard]] std::uint32_t party_lfg_status_flags() const { return party_lfg_status_flags_; }

 private:
  std::vector<GroupMember> members_;
  ObjectGuid leader_guid_;
  ObjectGuid group_guid_;
  std::string leader_name_;
  bool is_raid_{false};
  std::uint8_t my_sub_group_{0};
  std::uint8_t my_flags_{0};
  std::uint8_t my_roles_{0};
  std::uint8_t group_type_{0};
  std::uint8_t loot_method_{0};
  std::uint64_t master_looter_guid_{0};
  std::uint8_t loot_threshold_{2};
  std::uint8_t dungeon_difficulty_{0};
  std::uint8_t raid_difficulty_{0};
  std::uint8_t player_difficulty_index_{0};
  std::uint32_t party_lfg_dungeon_id_{0};
  std::uint32_t counter_{0};
  GroupInvite pending_invite_;
  bool group_destroyed_{false};
  bool group_uninvited_{false};
  bool group_action_throttled_{false};
  bool group_cancelled_{false};
  std::optional<RealGroupUpdate> last_real_group_update_;
  bool has_lfg_restrictions_{false};
  std::uint32_t party_lfg_status_flags_{0};
};

}


#include "openwow/game/group_manager.h"

namespace openwow::game {

namespace {

constexpr std::uint8_t kStartupLootMethod = 3;

}

bool GroupManager::HandleGroupList(const std::uint8_t* data, std::size_t len) {
  PacketReader reader(data, len);

  std::uint8_t group_type;
  if (!reader.ReadU8(group_type)) return false;

  std::uint8_t sub_group, flags, roles;
  if (!reader.ReadU8(sub_group)) return false;
  if (!reader.ReadU8(flags)) return false;
  if (!reader.ReadU8(roles)) return false;

  group_type_ = group_type;
  my_sub_group_ = sub_group;
  my_flags_ = flags;
  my_roles_ = roles;
  party_lfg_dungeon_id_ = 0;
  is_raid_ = (group_type & static_cast<std::uint8_t>(GroupType::kRaid)) != 0;

  if (group_type & 0x08) {
    std::uint8_t lfg_status;
    if (!reader.ReadU8(lfg_status)) return false;
    if (!reader.ReadU32(party_lfg_dungeon_id_)) return false;
  }

  std::uint64_t group_guid_raw;
  if (!reader.ReadU64(group_guid_raw)) return false;
  group_guid_ = ObjectGuid(group_guid_raw);

  if (!reader.ReadU32(counter_)) return false;

  std::uint32_t member_count;
  if (!reader.ReadU32(member_count)) return false;

  members_.clear();

  if (group_type & static_cast<std::uint8_t>(GroupType::kLeaveFlag)) {
    leader_guid_ = ObjectGuid();
    leader_name_.clear();
    my_sub_group_ = 0;
    my_flags_ = 0;
    my_roles_ = 0;
    group_type_ = 0;
    loot_method_ = 0;
    master_looter_guid_ = 0;
    loot_threshold_ = 2;
    dungeon_difficulty_ = 0;
    raid_difficulty_ = 0;
    player_difficulty_index_ = 0;
    party_lfg_dungeon_id_ = 0;
    return true;
  }

  members_.reserve(member_count);

  for (std::uint32_t i = 0; i < member_count; ++i) {
    GroupMember m;
    if (!reader.ReadCString(m.name)) return false;

    std::uint64_t guid_raw;
    if (!reader.ReadU64(guid_raw)) return false;
    m.guid = ObjectGuid(guid_raw);

    if (!reader.ReadU8(m.online_status)) return false;
    if (!reader.ReadU8(m.sub_group)) return false;
    if (!reader.ReadU8(m.flags)) return false;
    if (!reader.ReadU8(m.roles)) return false;

    members_.push_back(std::move(m));
  }

  std::uint64_t leader_raw;
  if (!reader.ReadU64(leader_raw)) return false;
  leader_guid_ = ObjectGuid(leader_raw);

  loot_method_ = kStartupLootMethod;
  master_looter_guid_ = 0;
  loot_threshold_ = 2;
  dungeon_difficulty_ = 0;
  raid_difficulty_ = 0;
  player_difficulty_index_ = 0;

  if (member_count > 0) {
    if (!reader.ReadU8(loot_method_)) return false;
    if (!reader.ReadU64(master_looter_guid_)) return false;
    if (!reader.ReadU8(loot_threshold_)) return false;
    if (!reader.ReadU8(dungeon_difficulty_)) return false;
    if (!reader.ReadU8(raid_difficulty_)) return false;
    if (!reader.ReadU8(player_difficulty_index_)) return false;
  }

  return true;
}

bool GroupManager::HandleGroupInvite(const std::uint8_t* data, std::size_t len) {
  PacketReader reader(data, len);

  std::uint8_t flag;
  if (!reader.ReadU8(flag)) return false;

  std::string inviter_name;
  if (!reader.ReadCString(inviter_name)) return false;

  std::uint32_t counter;
  if (!reader.ReadU32(counter)) return false;

  std::uint8_t realm_count;
  if (!reader.ReadU8(realm_count)) return false;
  for (std::uint8_t i = 0; i < realm_count; ++i) {
    std::uint32_t entry;
    if (!reader.ReadU32(entry)) return false;
  }

  std::uint32_t related_counter;
  if (!reader.ReadU32(related_counter)) return false;

  pending_invite_.inviter_name = std::move(inviter_name);
  pending_invite_.pending = (flag == 1);

  return true;
}

bool GroupManager::HandleGroupDecline(const std::uint8_t* data, std::size_t len,
                                      std::string& decliner_name) {
  PacketReader reader(data, len);
  return reader.ReadCString(decliner_name, 0x30u);
}

bool GroupManager::HandleSetLeader(const std::uint8_t* data, std::size_t len) {
  PacketReader reader(data, len);
  std::string leader_name;
  if (!reader.ReadCString(leader_name, 0x30u)) {
    return false;
  }
  leader_name_ = std::move(leader_name);
  return true;
}

bool GroupManager::ParsePartyCommandResult(const std::uint8_t* data, std::size_t len,
                                           PartyCommandResult& out) {
  PacketReader reader(data, len);

  std::uint32_t op;
  if (!reader.ReadU32(op)) return false;
  out.operation = static_cast<PartyOperation>(op);

  if (!reader.ReadCString(out.member, 0x30u)) return false;

  std::uint32_t result;
  if (!reader.ReadU32(result)) return false;
  out.result = static_cast<PartyResult>(result);

  if (!reader.ReadI32(out.value)) return false;

  return true;
}

bool GroupManager::HandleGroupDestroyed() {
  members_.clear();
  leader_guid_ = ObjectGuid();
  leader_name_.clear();
  is_raid_ = false;
  my_sub_group_ = 0;
  my_flags_ = 0;
  my_roles_ = 0;
  group_type_ = 0;
  loot_method_ = 0;
  master_looter_guid_ = 0;
  loot_threshold_ = 2;
  dungeon_difficulty_ = 0;
  raid_difficulty_ = 0;
  player_difficulty_index_ = 0;
  party_lfg_dungeon_id_ = 0;
  group_destroyed_ = true;
  return true;
}

bool GroupManager::HandleGroupUninvite() {
  group_uninvited_ = true;
  return true;
}

bool GroupManager::HandleRealGroupUpdate(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader reader(data, len);
  RealGroupUpdate rgu;
  if (!reader.ReadU8(rgu.group_flags) || !reader.ReadU32(rgu.member_count) ||
      !reader.ReadU64(rgu.leader_guid))
    return false;
  last_real_group_update_ = rgu;
  return true;
}

bool GroupManager::HandleGroupActionThrottled() {

  return true;
}

bool GroupManager::HandleGroupCancel() {
  group_cancelled_ = true;
  return true;
}

net::wotlk::WorldPacket GroupManager::BuildGroupInvite(const std::string& name) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_GROUP_INVITE);
  pkt.AppendString(name.c_str());
  pkt.AppendU32(0);
  return pkt;
}

net::wotlk::WorldPacket GroupManager::BuildGroupAccept() {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_GROUP_ACCEPT);
  pkt.AppendU32(0);
  return pkt;
}

net::wotlk::WorldPacket GroupManager::BuildGroupAccept(std::uint32_t roles) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_GROUP_ACCEPT);
  pkt.AppendU32(roles);
  return pkt;
}

net::wotlk::WorldPacket GroupManager::BuildGroupDecline() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_GROUP_DECLINE);
}

net::wotlk::WorldPacket GroupManager::BuildGroupDisband() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_GROUP_DISBAND);
}

net::wotlk::WorldPacket GroupManager::BuildGroupUninvite(const std::string& name) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_GROUP_UNINVITE);
  pkt.AppendString(name.c_str());
  return pkt;
}

net::wotlk::WorldPacket GroupManager::BuildGroupUninviteByGuid(
    std::uint64_t target_guid, const std::string& reason) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_GROUP_UNINVITE_GUID);
  pkt.AppendU64(target_guid);
  pkt.AppendString(reason.c_str());
  return pkt;
}

void GroupManager::Clear() {
  members_.clear();
  leader_guid_ = ObjectGuid();
  group_guid_ = ObjectGuid();
  leader_name_.clear();
  is_raid_ = false;
  my_sub_group_ = 0;
  my_flags_ = 0;
  my_roles_ = 0;
  group_type_ = 0;
  loot_method_ = 0;
  master_looter_guid_ = 0;
  loot_threshold_ = 2;
  dungeon_difficulty_ = 0;
  raid_difficulty_ = 0;
  player_difficulty_index_ = 0;
  party_lfg_dungeon_id_ = 0;
  counter_ = 0;
  pending_invite_ = {};
  group_destroyed_ = false;
  group_uninvited_ = false;
  group_action_throttled_ = false;
  group_cancelled_ = false;
  last_real_group_update_.reset();
}

}

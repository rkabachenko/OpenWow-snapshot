
#include "openwow/game/battlefield_mgr.h"

namespace openwow::game {

bool BattlefieldMgrHandler::HandleEntryInvite(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  BattlefieldEntryInvite ei;
  if (!r.ReadU32(ei.battle_id) || !r.ReadU8(ei.accept_flag))
    return false;
  last_entry_invite_ = ei;
  return true;
}

bool BattlefieldMgrHandler::HandleEntered(const std::uint8_t* data,
                                          std::size_t len) {
  PacketReader r(data, len);
  BattlefieldEntered be;
  if (!r.ReadU32(be.battle_id) || !r.ReadU32(be.area_id) ||
      !r.ReadU8(be.status_flag) || !r.ReadU8(be.secondary_flag) ||
      !r.ReadU8(be.cleared_afk))
    return false;
  last_entered_ = be;
  return true;
}

bool BattlefieldMgrHandler::HandleQueueInvite(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  BattlefieldQueueInvite qi;
  if (!r.ReadU32(qi.queue_id) || !r.ReadU8(qi.invite_flag) ||
      !r.ReadU8(qi.warmup) || !r.ReadU8(qi.cleared_afk))
    return false;
  last_queue_invite_ = qi;
  return true;
}

bool BattlefieldMgrHandler::HandleGroupJoinedBattleground(
    const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  GroupJoinedBattleground gj;
  if (!r.ReadI32(gj.result)) return false;
  if (gj.result == -12 || gj.result == -11) {
    if (!r.ReadU64(gj.player_guid)) return false;
    gj.has_guid = true;
  }
  last_group_joined_ = gj;
  return true;
}

void BattlefieldMgrHandler::Clear() {
  last_entry_invite_.reset();
  last_entered_.reset();
  last_queue_invite_.reset();
  last_group_joined_.reset();
  last_ejected_.reset();
  last_eject_pending_.reset();
  last_queue_response_.reset();
  last_state_change_.reset();
}

bool BattlefieldMgrHandler::HandleEjected(const std::uint8_t* data,
                                          std::size_t len) {
  PacketReader r(data, len);
  BattlefieldEjected ej;
  if (!r.ReadU32(ej.queue_id) || !r.ReadU32(ej.reason))
    return false;
  last_ejected_ = ej;
  return true;
}

bool BattlefieldMgrHandler::HandleEjectPending(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  BattlefieldEjectPending pending;
  if (!r.ReadU32(pending.queue_id) || !r.ReadU8(pending.reason) ||
      !r.ReadU8(pending.relocate_flag) ||
      !r.ReadU8(pending.battleground_flag))
    return false;
  last_eject_pending_ = pending;
  return true;
}

bool BattlefieldMgrHandler::HandleQueueRequestResponse(const std::uint8_t* data,
                                                       std::size_t len) {
  PacketReader r(data, len);
  BattlefieldQueueResponse qr;
  if (!r.ReadU32(qr.queue_id) || !r.ReadU8(qr.accepted))
    return false;
  last_queue_response_ = qr;
  return true;
}

bool BattlefieldMgrHandler::HandleStateChange(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  BattlefieldStateChange sc;
  if (!r.ReadU32(sc.battlefield_id) || !r.ReadU32(sc.area_id) ||
      !r.ReadU32(sc.expiry_time))
    return false;
  last_state_change_ = sc;
  return true;
}

net::wotlk::WorldPacket BattlefieldMgrHandler::BuildEntryInviteResponse(
    std::uint32_t battle_id, bool accepted) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_BATTLEFIELD_MGR_ENTRY_INVITE_RESPONSE);
  pkt.AppendU32(battle_id);
  pkt.AppendU8(accepted ? 1u : 0u);
  return pkt;
}

net::wotlk::WorldPacket BattlefieldMgrHandler::BuildQueueRequest(
    std::uint32_t battlefield_id) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_BATTLEFIELD_MGR_QUEUE_REQUEST);
  pkt.AppendU32(battlefield_id);
  return pkt;
}

net::wotlk::WorldPacket BattlefieldMgrHandler::BuildQueueInviteResponse(
    std::uint32_t battlefield_id, bool accepted) {
  net::wotlk::WorldPacket pkt(
      net::wotlk::Opcode::CMSG_BATTLEFIELD_MGR_QUEUE_INVITE_RESPONSE);
  pkt.AppendU32(battlefield_id);
  pkt.AppendU8(accepted ? 1u : 0u);
  return pkt;
}

net::wotlk::WorldPacket BattlefieldMgrHandler::BuildExitRequest(
    std::uint32_t battlefield_id) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_BATTLEFIELD_MGR_EXIT_REQUEST);
  pkt.AppendU32(battlefield_id);
  return pkt;
}

}


#include "openwow/game/inventory/adapters/protocol/inventory_messages.h"

namespace openwow::game {

bool InventoryMessageState::ParseItemPushResult(const std::uint8_t* data, std::size_t len,
                                           ItemPushResult& out) {
  if (len < 45) return false;
  PacketReader reader(data, len);

  std::uint64_t guid_raw;
  if (!reader.ReadU64(guid_raw)) return false;
  out.player_guid = ObjectGuid(guid_raw);

  if (!reader.ReadU32(out.pushed)) return false;
  if (!reader.ReadU32(out.created)) return false;
  if (!reader.ReadU32(out.display_in_chat)) return false;
  if (!reader.ReadU8(out.bag_slot)) return false;
  if (!reader.ReadU32(out.item_slot)) return false;
  if (!reader.ReadU32(out.item_entry)) return false;
  if (!reader.ReadU32(out.suffix_factor)) return false;

  std::int32_t rp;
  if (!reader.ReadI32(rp)) return false;
  out.random_property_id = rp;

  if (!reader.ReadU32(out.count)) return false;
  if (!reader.ReadU32(out.total_count)) return false;

  return true;
}

bool InventoryMessageState::ParseInventoryChangeFailure(
    const std::uint8_t* data, std::size_t len,
    InventoryChangeFailure& out) {
  PacketReader reader(data, len);

  std::uint8_t result_raw;
  if (!reader.ReadU8(result_raw)) return false;
  out.result = static_cast<InventoryResult>(result_raw);

  if (out.result != InventoryResult::kOk) {
    std::uint64_t item1, item2;
    if (!reader.ReadU64(item1)) return false;
    out.item1_guid = ObjectGuid(item1);
    if (!reader.ReadU64(item2)) return false;
    out.item2_guid = ObjectGuid(item2);
    if (!reader.ReadU8(out.bag_type_subclass)) return false;

    if (out.result == InventoryResult::kCantEquipLevel ||
        out.result == InventoryResult::kPurchaseLevelTooLow) {
      (void)reader.ReadU32(out.required_level);
    }
  }

  return true;
}

int InventoryResultCodeToSystemMessageId(const std::uint32_t result_code) {
  switch (result_code) {
  case 0:
  case 81:
  case 83:
    return 730;
  case 1:
    return 2;
  case 2:
    return 3;
  case 3:
    return 9;
  case 5:
    return 14;
  case 6:
    return 16;
  case 7:
    return 17;
  case 8:
    return 8;
  case 9:
  case 12:
  case 18:
    return 18;
  case 10:
  case 11:
    return 4;
  case 13:
    return 34;
  case 14:
    return 173;
  case 15:
  case 16:
    return 19;
  case 17:
    return 20;
  case 19:
  case 55:
    return 22;
  case 20:
    return 21;
  case 21:
    return 23;
  case 22:
    return 24;
  case 23:
  case 54:
    return 25;
  case 24:
    return 42;
  case 25:
    return 134;
  case 26:
    return 26;
  case 27:
    return 27;
  case 28:
    return 226;
  case 29:
    return 40;
  case 30:
    return 28;
  case 31:
    return 13;
  case 32:
    return 29;
  case 33:
    return 30;
  case 34:
    return 31;
  case 35:
    return 32;
  case 36:
    return 33;
  case 37:
    return 434;
  case 38:
    return 135;
  case 39:
    return 136;
  case 40:
  case 73:
    return 12;
  case 41:
    return 293;
  case 42:
    return 294;
  case 43:
    return 297;
  case 44:
    return 298;
  case 45:
    return 299;
  case 46:
    return 300;
  case 47:
    return 301;
  case 48:
    return 302;
  case 49:
    return 311;
  case 50:
    return 0;
  case 51:
    return 1;
  case 52:
  case 57:
    return 37;
  case 58:
    return 367;
  case 59:
    return 386;
  case 60:
    return 450;
  case 61:
    return 451;
  case 63:
    return 5;
  case 64:
    return 7;
  case 65:
    return 15;
  case 66:
    return 507;
  case 67:
    return 513;
  case 68:
    return 549;
  case 69:
    return 552;
  case 70:
    return 553;
  case 71:
    return 556;
  case 72:
    return 376;
  case 75:
    return 559;
  case 76:
    return 560;
  case 77:
    return 562;
  case 78:
    return 575;
  case 79:
    return 43;
  case 80:
    return 6;
  case 82:
    return 622;
  case 84:
    return 626;
  case 85:
    return 628;
  case 86:
    return 632;
  case 87:
    return 633;
  case 88:
    return 10;
  case 89:
    return 629;
  case 90:
    return 630;
  case 91:
    return 631;
  default:
    return 11;
  }
}

net::wotlk::WorldPacket InventoryMessageState::BuildAutoEquipItem(
    std::uint8_t src_bag, std::uint8_t src_slot) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_AUTOEQUIP_ITEM);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  return pkt;
}

net::wotlk::WorldPacket InventoryMessageState::BuildSwapItem(
    std::uint8_t dest_bag, std::uint8_t dest_slot,
    std::uint8_t src_bag, std::uint8_t src_slot) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_SWAP_ITEM);
  pkt.AppendU8(dest_bag);
  pkt.AppendU8(dest_slot);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  return pkt;
}

net::wotlk::WorldPacket InventoryMessageState::BuildSwapInvItem(
    std::uint8_t dest_slot, std::uint8_t src_slot) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_SWAP_INV_ITEM);
  pkt.AppendU8(dest_slot);
  pkt.AppendU8(src_slot);
  return pkt;
}

net::wotlk::WorldPacket InventoryMessageState::BuildSplitItem(
    std::uint8_t src_bag, std::uint8_t src_slot,
    std::uint8_t dest_bag, std::uint8_t dest_slot,
    std::uint32_t count) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_SPLIT_ITEM);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  pkt.AppendU8(dest_bag);
  pkt.AppendU8(dest_slot);
  pkt.AppendU32(count);
  return pkt;
}

void InventoryMessageState::OnItemPushResult(const ItemPushResult& result) {
  pending_push_results_.push_back(result);
}

void InventoryMessageState::OnInventoryChangeFailure(const InventoryChangeFailure& failure) {
  last_failure_ = failure;
}

}

#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"

namespace openwow::game::trade_protocol {
namespace {

class RetailDefaultingTailReader {
 public:
  explicit RetailDefaultingTailReader(PacketReader& reader) : reader_(reader) {}

  std::uint32_t ReadU32() {
    std::uint32_t value = 0;
    if (!failed_ && !reader_.ReadU32(value)) {
      Fail();
    }
    return value;
  }

  std::uint8_t ReadU8() {
    std::uint8_t value = 0;
    if (!failed_ && !reader_.ReadU8(value)) {
      Fail();
    }
    return value;
  }

 private:
  void Fail() {
    failed_ = true;
    reader_.Skip(reader_.Remaining());
  }

  PacketReader& reader_;
  bool failed_ = false;
};

}

std::optional<TradeStatusMessage> DecodeStatus(const std::uint8_t* data,
                                               const std::size_t size) {
  PacketReader reader(data, size);
  TradeStatusMessage message;
  if (!reader.ReadU32(message.status)) {
    return std::nullopt;
  }

  switch (static_cast<TradeStatus>(message.status)) {
    case TradeStatus::kBeginTrade:
      if (!reader.ReadU64(message.partner_guid)) {
        return std::nullopt;
      }
      break;
    case TradeStatus::kOpenWindow:
      if (!reader.ReadU32(message.trade_session_id)) {
        return std::nullopt;
      }
      break;
    case TradeStatus::kCloseWindow: {
      RetailDefaultingTailReader tail(reader);
      message.reason_code = tail.ReadU32();
      message.reason_has_alternate_message = tail.ReadU8() != 0;
      message.item_id = tail.ReadU32();
      break;
    }
    case TradeStatus::kOnlyConjured:
    case TradeStatus::kNotEligible: {
      RetailDefaultingTailReader tail(reader);
      message.slot = tail.ReadU8();
      break;
    }
    default:
      break;
  }
  return message;
}

std::optional<TradeExtendedPrefix> DecodeExtendedPrefix(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  std::uint8_t side = 0;
  TradeExtendedPrefix prefix;
  if (!reader.ReadU8(side) || !reader.ReadU32(prefix.trade_session_id) ||
      !reader.ReadU32(prefix.echoed_local_mutation_index)) {
    return std::nullopt;
  }
  prefix.side_value = side;
  return prefix;
}

std::optional<TradeExtendedSnapshot> DecodeExtendedSnapshot(
    const TradeExtendedPrefix& prefix, const std::uint8_t* body,
    const std::size_t body_size) {
  PacketReader reader(body, body_size);
  TradeExtendedSnapshot snapshot;
  snapshot.prefix = prefix;
  snapshot.side = static_cast<TradeSide>(prefix.side_value);
  snapshot.window.is_trader_data = snapshot.side == TradeSide::kTarget;
  if (!reader.ReadU32(snapshot.server_state_index) ||
      !reader.ReadU32(snapshot.window.gold) ||
      !reader.ReadU32(snapshot.window.slot7_text_id)) {
    return std::nullopt;
  }

  while (reader.Remaining() != 0) {
    TradeSlotItem item;
    if (!reader.ReadU8(item.slot_index) ||
        item.slot_index >= kTradeSlotCount ||
        !reader.ReadU32(item.item_id) ||
        !reader.ReadU32(item.display_info_id) ||
        !reader.ReadU32(item.stack_count) ||
        !reader.ReadU32(item.is_wrapped) ||
        !reader.ReadU64(item.gift_creator) ||
        !reader.ReadU32(item.permanent_enchant)) {
      return std::nullopt;
    }
    for (std::uint32_t& enchant : item.socket_enchants) {
      if (!reader.ReadU32(enchant)) {
        return std::nullopt;
      }
    }
    if (!reader.ReadU64(item.creator) ||
        !reader.ReadU32(item.spell_charges) ||
        !reader.ReadU32(item.suffix_factor) ||
        !reader.ReadI32(item.random_property_id) ||
        !reader.ReadU32(item.lock_id) ||
        !reader.ReadU32(item.max_durability) ||
        !reader.ReadU32(item.durability)) {
      return std::nullopt;
    }
    snapshot.window.slots[item.slot_index] = item;
  }

  for (std::size_t index = 0; index < snapshot.window.slots.size(); ++index) {
    snapshot.window.slots[index].slot_index =
        static_cast<std::uint8_t>(index);
  }
  return snapshot;
}

net::wotlk::WorldPacket EncodeInitiate(const std::uint64_t target_guid) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_INITIATE_TRADE);
  packet.AppendU64(target_guid);
  return packet;
}

net::wotlk::WorldPacket EncodeBegin() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_BEGIN_TRADE);
}

net::wotlk::WorldPacket EncodeSetItem(const std::uint8_t trade_slot,
                                      const std::uint8_t bag,
                                      const std::uint8_t bag_slot) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_SET_TRADE_ITEM);
  packet.AppendU8(trade_slot);
  packet.AppendU8(bag);
  packet.AppendU8(bag_slot);
  return packet;
}

net::wotlk::WorldPacket EncodeClearItem(const std::uint8_t trade_slot) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_CLEAR_TRADE_ITEM);
  packet.AppendU8(trade_slot);
  return packet;
}

net::wotlk::WorldPacket EncodeSetGold(const std::uint32_t copper) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_SET_TRADE_GOLD);
  packet.AppendU32(copper);
  return packet;
}

net::wotlk::WorldPacket EncodeAccept(const std::uint32_t state_index) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_ACCEPT_TRADE);
  packet.AppendU32(state_index);
  return packet;
}

net::wotlk::WorldPacket EncodeUnaccept() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_UNACCEPT_TRADE);
}

net::wotlk::WorldPacket EncodeCancel() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_CANCEL_TRADE);
}

net::wotlk::WorldPacket EncodeBusy() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_BUSY_TRADE);
}

net::wotlk::WorldPacket EncodeIgnore() {
  return net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_IGNORE_TRADE);
}

}

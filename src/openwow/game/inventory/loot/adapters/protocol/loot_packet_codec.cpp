#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"

#include <algorithm>

namespace openwow::game::loot_protocol {

std::optional<LootWindow> DecodeLootResponse(const std::uint8_t* data,
                                             const std::size_t size) {
  PacketReader reader(data, size);
  LootWindow window;
  if (!reader.ReadGuid(window.source_guid)) {
    return std::nullopt;
  }

  std::uint8_t loot_type = 0;
  if (!reader.ReadU8(loot_type)) {
    return std::nullopt;
  }
  window.loot_type = static_cast<LootType>(loot_type);
  if (window.loot_type == LootType::kNone) {
    return reader.ReadU8(window.error_sub_type) && reader.Remaining() == 0
               ? std::optional(std::move(window))
               : std::nullopt;
  }

  std::uint8_t wire_item_count = 0;
  if (!reader.ReadU32(window.gold) || !reader.ReadU8(wire_item_count)) {
    return std::nullopt;
  }
  window.gold_slot_reserved = window.gold > 0;

  window.items.reserve(
      std::min<std::size_t>(wire_item_count, LootInteraction::kMaxLootSlots));
  for (std::size_t index = 0; index < wire_item_count; ++index) {
    LootItem item;
    std::uint8_t slot_type = 0;
    if (!reader.ReadU8(item.slot_index) || !reader.ReadU32(item.item_id) ||
        !reader.ReadU32(item.count) ||
        !reader.ReadU32(item.display_info_id) ||
        !reader.ReadU32(item.random_suffix) ||
        !reader.ReadU32(item.random_property_id) ||
        !reader.ReadU8(slot_type)) {
      return std::nullopt;
    }
    item.display_index = static_cast<std::uint8_t>(window.items.size());
    item.slot_type = static_cast<LootSlotType>(slot_type);
    if (window.items.size() < LootInteraction::kMaxLootSlots) {
      window.items.push_back(std::move(item));
    }
  }
  return reader.Remaining() == 0 ? std::optional(std::move(window))
                                 : std::nullopt;
}

std::optional<std::pair<ObjectGuid, bool>> DecodeLootReleaseResponse(
    const std::uint8_t* data, const std::size_t size) {
  PacketReader reader(data, size);
  ObjectGuid source;
  std::uint8_t accepted = 0;

  (void)reader.ReadGuid(source);
  (void)reader.ReadU8(accepted);
  return std::pair{source, accepted != 0};
}

std::optional<std::uint8_t> DecodeRemovedSlot(const std::uint8_t* data,
                                              const std::size_t size) {
  PacketReader reader(data, size);
  std::uint8_t wire_slot = 0;
  if (!reader.ReadU8(wire_slot)) {
    return std::nullopt;
  }
  return reader.Remaining() == 0 ? std::optional(wire_slot) : std::nullopt;
}

std::optional<LootMoneyNotify> DecodeMoneyNotify(const std::uint8_t* data,
                                                 const std::size_t size) {
  PacketReader reader(data, size);
  LootMoneyNotify notify;
  std::uint8_t solo = 0;

  (void)reader.ReadU32(notify.copper);
  (void)reader.ReadU8(solo);
  notify.is_solo = solo != 0;
  return notify;
}

std::optional<LootRollWon> DecodeRollWon(const std::uint8_t* data,
                                         const std::size_t size) {
  PacketReader reader(data, size);
  LootRollWon result;

  (void)reader.ReadU64(result.source_guid);
  (void)reader.ReadU32(result.slot);
  (void)reader.ReadU32(result.item_id);
  (void)reader.ReadU32(result.random_suffix);
  (void)reader.ReadU32(result.random_property_id);
  (void)reader.ReadU64(result.winner_guid);
  (void)reader.ReadU8(result.roll_number);
  (void)reader.ReadU8(result.roll_type);
  return result;
}

std::optional<LootItemNotify> DecodeItemNotify(const std::uint8_t* data,
                                               const std::size_t size) {
  PacketReader reader(data, size);
  LootItemNotify notify;
  if (!reader.ReadU64(notify.looter_guid) || !reader.ReadU8(notify.slot) ||
      !reader.ReadU8(notify.unknown_byte) || !reader.ReadU32(notify.item_id) ||
      !reader.ReadCString(notify.text, 64)) {
    return std::nullopt;
  }
  return reader.Remaining() == 0 ? std::optional(std::move(notify))
                                 : std::nullopt;
}

std::optional<LootList> DecodeLootList(const std::uint8_t* data,
                                       const std::size_t size) {
  PacketReader reader(data, size);
  LootList list;
  if (!reader.ReadU64(list.creature_guid) ||
      !reader.ReadPackedGuid(list.master_looter) ||
      !reader.ReadPackedGuid(list.group_looter)) {
    return std::nullopt;
  }
  return reader.Remaining() == 0 ? std::optional(list) : std::nullopt;
}

std::optional<LootMasterList> DecodeMasterList(const std::uint8_t* data,
                                               const std::size_t size) {
  PacketReader reader(data, size);
  std::uint8_t count = 0;
  if (!reader.ReadU8(count)) {
    return std::nullopt;
  }

  LootMasterList list;
  list.player_guids.resize(count);
  for (std::uint64_t& player_guid : list.player_guids) {
    if (!reader.ReadU64(player_guid)) {
      return std::nullopt;
    }
  }
  return reader.Remaining() == 0 ? std::optional(std::move(list))
                                 : std::nullopt;
}

std::optional<LootSlotChanged> DecodeSlotChanged(const std::uint8_t* data,
                                                 const std::size_t size) {
  PacketReader reader(data, size);
  LootSlotChanged changed;
  if (!reader.ReadU64(changed.loot_guid) || !reader.ReadU8(changed.slot) ||
      !reader.ReadU32(changed.item_id) ||
      !reader.ReadU32(changed.display_info_id) ||
      !reader.ReadI32(changed.suffix_factor) ||
      !reader.ReadI32(changed.random_property_id) ||
      !reader.ReadU32(changed.count)) {
    return std::nullopt;
  }
  return reader.Remaining() == 0 ? std::optional(changed) : std::nullopt;
}

net::wotlk::WorldPacket EncodeLootRequest(const ObjectGuid source) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_LOOT);
  packet.AppendU64(source.GetRawValue());
  return packet;
}

net::wotlk::WorldPacket EncodeReleaseRequest(const ObjectGuid source) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_LOOT_RELEASE);
  packet.AppendU64(source.GetRawValue());
  return packet;
}

net::wotlk::WorldPacket EncodeTakeItemRequest(const std::uint8_t wire_slot) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_AUTOSTORE_LOOT_ITEM);
  packet.AppendU8(wire_slot);
  return packet;
}

net::wotlk::WorldPacket EncodeRollRequest(const ObjectGuid source,
                                          const std::uint32_t wire_slot,
                                          const LootRollType roll) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_LOOT_ROLL);
  packet.AppendU64(source.GetRawValue());
  packet.AppendU32(wire_slot);
  packet.AppendU8(static_cast<std::uint8_t>(roll));
  return packet;
}

net::wotlk::WorldPacket EncodeMasterGiveRequest(const ObjectGuid source,
                                                const std::uint8_t wire_slot,
                                                const ObjectGuid recipient) {
  net::wotlk::WorldPacket packet(net::wotlk::Opcode::CMSG_LOOT_MASTER_GIVE);
  packet.AppendU64(source.GetRawValue());
  packet.AppendU8(wire_slot);
  packet.AppendU64(recipient.GetRawValue());
  return packet;
}

}

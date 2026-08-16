#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"

namespace openwow::game::equipment_protocol {
namespace {

void append_packed(net::wotlk::WorldPacket& packet, const ObjectGuid guid) {
  const auto bytes = guid.Pack();
  packet.AppendBytes(bytes.data(), bytes.size());
}

}

net::wotlk::WorldPacket encode_use(const EquipmentSetUse& request) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_EQUIPMENT_SET_USE);
  for (const auto& item : request.items) {
    append_packed(packet, item.item);
    packet.AppendU8(item.source_bag);
    packet.AppendU8(item.source_slot);
  }
  return packet;
}

net::wotlk::WorldPacket encode_save(const EquipmentSetSave& request) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_EQUIPMENT_SET_SAVE);
  append_packed(packet, request.guid);
  packet.AppendU32(request.id);
  packet.AppendString(request.name.c_str());
  packet.AppendString(request.icon.c_str());
  for (std::size_t slot = 0; slot < request.items.size(); ++slot) {
    append_packed(packet, request.ignored.test(slot)
                              ? ObjectGuid(1)
                              : request.items[slot].value_or(ObjectGuid{}));
  }
  return packet;
}

net::wotlk::WorldPacket encode_delete(const ObjectGuid set) {
  net::wotlk::WorldPacket packet(
      net::wotlk::Opcode::CMSG_DELETEEQUIPMENT_SET);
  append_packed(packet, set);
  return packet;
}

std::optional<std::vector<EquipmentSet>> decode_list(
    const std::span<const std::uint8_t> payload) {
  PacketReader packet(payload);
  std::uint32_t count = 0;
  if (!packet.ReadU32(count) || count > 1024) {
    return std::nullopt;
  }

  std::vector<EquipmentSet> retained;
  retained.reserve(std::min<std::size_t>(count, kMaximumEquipmentSets));
  for (std::uint32_t index = 0; index < count; ++index) {
    EquipmentSet set;
    if (!packet.ReadPackedGuid(set.guid) ||
        !packet.ReadU32(set.id) ||
        !packet.ReadCString(set.name, 256) ||
        !packet.ReadCString(set.icon, 256)) {
      return std::nullopt;
    }
    for (std::size_t slot = 0; slot < set.items.size(); ++slot) {
      ObjectGuid guid;
      if (!packet.ReadPackedGuid(guid)) {
        return std::nullopt;
      }
      if (guid.GetRawValue() == 1) {
        set.ignored.set(slot);
      } else if (!guid.IsEmpty()) {
        set.items[slot] = guid;
      }
    }
    if (retained.size() < kMaximumEquipmentSets) {
      retained.push_back(std::move(set));
    }
  }
  return packet.Remaining() == 0 ? std::optional(std::move(retained))
                                 : std::nullopt;
}

std::optional<std::pair<std::uint32_t, ObjectGuid>> decode_saved(
    const std::span<const std::uint8_t> payload) {
  PacketReader packet(payload);
  std::pair<std::uint32_t, ObjectGuid> saved;
  if (!packet.ReadU32(saved.first) ||
      !packet.ReadPackedGuid(saved.second) ||
      packet.Remaining() != 0) {
    return std::nullopt;
  }
  return saved;
}

std::optional<std::uint8_t> decode_use_result(
    const std::span<const std::uint8_t> payload) {
  PacketReader packet(payload);
  std::uint8_t result = 0;
  return packet.ReadU8(result) && packet.Remaining() == 0
             ? std::optional(result)
             : std::nullopt;
}

}

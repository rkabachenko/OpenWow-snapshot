#include "openwow/game/inventory/loot/adapters/protocol/loot_roll_packet_codec.h"

#include "openwow/game/packet_reader.h"

namespace openwow::game::loot_protocol {
namespace {

constexpr std::uint32_t kMaxRetailLootSlotIndex = 17;

}

std::optional<GroupLootStartRoll> DecodeStartRoll(
    const std::uint8_t* data, std::size_t size) {
  PacketReader reader(data, size);
  GroupLootStartRoll result;
  if (!reader.ReadU64(result.source_guid) ||
      !reader.ReadU32(result.map_id) ||
      !reader.ReadU32(result.item_slot) ||
      !reader.ReadU32(result.item_id) ||
      !reader.ReadU32(result.random_suffix) ||
      !reader.ReadU32(result.random_prop_id) ||
      !reader.ReadU32(result.item_count) ||
      !reader.ReadU32(result.countdown_ms) ||
      !reader.ReadU8(result.roll_vote_mask)) {
    return std::nullopt;
  }
  return reader.Remaining() == 0 ? std::optional(result) : std::nullopt;
}

std::optional<GroupLootRollResult> DecodeRoll(
    const std::uint8_t* data, std::size_t size) {
  PacketReader reader(data, size);
  GroupLootRollResult result;
  std::uint8_t roll_type = 0;
  std::uint8_t auto_pass = 0;
  if (!reader.ReadU64(result.source_guid) ||
      !reader.ReadU32(result.item_slot) ||
      !reader.ReadU64(result.roller_guid) ||
      !reader.ReadU32(result.item_id) ||
      !reader.ReadU32(result.random_suffix) ||
      !reader.ReadU32(result.random_prop_id) ||
      !reader.ReadU8(result.roll_number) ||
      !reader.ReadU8(roll_type) ||
      !reader.ReadU8(auto_pass)) {
    return std::nullopt;
  }
  result.roll_type = static_cast<GroupRollType>(roll_type);
  result.auto_pass = auto_pass != 0;
  return reader.Remaining() == 0 ? std::optional(result) : std::nullopt;
}

std::optional<GroupLootAllPassed> DecodeAllPassed(
    const std::uint8_t* data, std::size_t size) {
  PacketReader reader(data, size);
  GroupLootAllPassed result;
  if (!reader.ReadU64(result.source_guid) ||
      !reader.ReadU32(result.item_slot) ||
      !reader.ReadU32(result.item_id) ||
      !reader.ReadU32(result.random_suffix) ||
      !reader.ReadU32(result.random_prop_id) ||
      result.item_slot > kMaxRetailLootSlotIndex) {
    return std::nullopt;
  }

  return result;
}

}


#include "openwow/game/inspect_handler.h"

#include <algorithm>

#include "openwow/game/talent_info.h"
#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::game {

namespace {

constexpr int kBarberShopSharedErrorMessage = 40;
constexpr int kBarberShopNotOnChairMessage = 563;

net::CDataStore MakeInspectPacketStore(const std::uint8_t* data,
                                       const std::size_t len) {
  const auto wire_size = static_cast<std::uint32_t>(len);
  return net::CDataStore{
      .data = const_cast<std::uint8_t*>(data),
      .window_base = 0,
      .window_size = wire_size,
      .write_pos = wire_size,
      .read_pos = 0,
  };
}

ObjectGuid ReadInspectPackedGuid(net::CDataStore& store) {
  std::uint64_t raw_guid = 0;
  net::CDataStore_GetPackedGuid(store, &raw_guid);
  return ObjectGuid(raw_guid);
}

}

bool InspectHandler::ParseAndStoreInspectEquipment(PacketReader& reader,
                                                   const ObjectGuid player_guid) {

  inspect_equipment_ = {};
  inspect_equipment_.player = player_guid;
  auto store =
      MakeInspectPacketStore(reader.PeekBytes(reader.Remaining()),
                             reader.Remaining());
  net::CDataStore_GetUInt32(store, &inspect_equipment_.slot_used_mask);

  for (std::size_t slot_index = 0;
       slot_index < inspect_equipment_.slots.size(); ++slot_index) {
    if ((inspect_equipment_.slot_used_mask & (1u << slot_index)) == 0) {
      continue;
    }

    auto& slot = inspect_equipment_.slots[slot_index];
    slot.present = true;

    std::uint32_t raw_item_value = 0;
    net::CDataStore_GetUInt32(store, &raw_item_value);
    const auto signed_item_value = static_cast<std::int32_t>(raw_item_value);
    slot.item_value_was_negative = signed_item_value < 0;
    slot.item_id = signed_item_value < 0
                       ? 0u - raw_item_value
                       : raw_item_value;

    net::CDataStore_GetUInt16(store, &slot.detail_mask);

    std::uint16_t detail_mask = slot.detail_mask;
    for (std::size_t bit_index = 0;
         bit_index < slot.detail_values.size() && detail_mask != 0;
         ++bit_index, detail_mask >>= 1) {
      if ((detail_mask & 1u) == 0) {
        continue;
      }
      net::CDataStore_GetUInt16(store, &slot.detail_values[bit_index]);
    }

    net::CDataStore_GetUInt16(store, &slot.trailing_value);
    slot.item_guid = ReadInspectPackedGuid(store);
    net::CDataStore_GetUInt32(store, &slot.trailing_u32);
  }

  return true;
}

bool InspectHandler::HandleInspectTalent(const std::uint8_t* data,
                                         std::size_t len) {

  static constexpr std::uint8_t kEmptyPacketByte = 0;
  const auto* packet_data = data != nullptr ? data : &kEmptyPacketByte;
  auto store = MakeInspectPacketStore(packet_data, len);
  const ObjectGuid player_guid = ReadInspectPackedGuid(store);
  if (player_guid.GetRawValue() != TalentInfoStore::Get().GetInspectTargetGuid()) {
    return true;
  }

  const auto talent_payload_offset =
      std::min<std::size_t>(store.read_pos, len);
  const auto* talent_payload = packet_data + talent_payload_offset;
  const auto talent_payload_len = len - talent_payload_offset;

  auto& talent_store = TalentInfoStore::Get();
  talent_store.ParseInspectTalentPacket(talent_payload, 0);
  talent_store.ParseInspectTalentPacket(talent_payload, talent_payload_len);

  last_inspect_talent_ = {};
  last_inspect_talent_.player = player_guid;
  net::CDataStore_GetUInt32(store, &last_inspect_talent_.free_points);
  net::CDataStore_GetUInt8(store, &last_inspect_talent_.spec_count);
  net::CDataStore_GetUInt8(store, &last_inspect_talent_.active_spec);

  last_inspect_talent_.specs.resize(last_inspect_talent_.spec_count);
  for (std::uint8_t s = 0; s < last_inspect_talent_.spec_count; ++s) {
    auto& spec = last_inspect_talent_.specs[s];
    std::uint8_t talent_count = 0;
    net::CDataStore_GetUInt8(store, &talent_count);
    spec.talents.resize(talent_count);
    for (std::uint8_t t = 0; t < talent_count; ++t) {
      net::CDataStore_GetUInt32(store, &spec.talents[t].talent_id);
      net::CDataStore_GetUInt8(store, &spec.talents[t].rank);
    }

    std::uint8_t glyph_count = 0;
    net::CDataStore_GetUInt8(store, &glyph_count);
    spec.glyphs.resize(glyph_count);
    for (std::uint8_t g = 0; g < glyph_count; ++g) {
      net::CDataStore_GetUInt16(store, &spec.glyphs[g]);
    }
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::INSPECT_TALENT_READY);

  const auto equipment_payload_offset =
      std::min<std::size_t>(store.read_pos, len);
  PacketReader equipment_reader(packet_data + equipment_payload_offset,
                                len - equipment_payload_offset);
  ParseAndStoreInspectEquipment(equipment_reader, player_guid);
  last_inspect_talent_.slot_used_mask = inspect_equipment_.slot_used_mask;
  return true;
}

bool InspectHandler::HandleInspectResultsUpdate(const std::uint8_t* data,
                                                std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid player_guid{ObjectGuid(0)};
  if (!r.ReadPackedGuid(player_guid)) return false;
  if (player_guid.GetRawValue() != TalentInfoStore::Get().GetInspectTargetGuid()) {
    return true;
  }
  return ParseAndStoreInspectEquipment(r, player_guid);
}

bool InspectHandler::HandleInspectHonorStats(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_honor_stats_.player_guid)) return false;
  if (!r.ReadU8(last_honor_stats_.lifetime_rank)) return false;
  if (!r.ReadU16(last_honor_stats_.today_honorable_kills)) return false;
  if (!r.ReadU16(last_honor_stats_.yesterday_honorable_kills)) return false;
  if (!r.ReadU32(last_honor_stats_.today_contribution)) return false;
  if (!r.ReadU32(last_honor_stats_.yesterday_contribution)) return false;
  if (!r.ReadU32(last_honor_stats_.lifetime_honorable_kills)) return false;
  return true;
}

bool InspectHandler::HandleTitleEarned(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_title_.title_bit_index)) return false;
  if (!r.ReadU32(last_title_.earned)) return false;
  return true;
}

bool InspectHandler::HandleEnableBarberShop(const std::uint8_t* ,
                                            std::size_t ) {
  barber_open_ = true;
  return true;
}

bool InspectHandler::HandleBarberShopResult(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(barber_result_)) return false;

  switch (barber_result_) {
    case 0:
      ui::game::ScriptEventDispatch::Get().FireBarberShopSuccess();
      break;
    case 1:
    case 3:
      ui::game::DisplaySystemMessage(kBarberShopSharedErrorMessage);
      break;
    case 2:
      ui::game::DisplaySystemMessage(kBarberShopNotOnChairMessage);
      break;
    default:
      break;
  }

  return true;
}

bool InspectHandler::HandleMinimapPing(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_ping_.source_guid)) return false;
  if (!r.ReadFloat(last_ping_.map_x)) return false;
  if (!r.ReadFloat(last_ping_.map_y)) return false;
  return true;
}

void InspectHandler::ClearInspectTargetData() {
  last_inspect_talent_ = {};
  inspect_equipment_ = {};
  last_honor_stats_ = {};
}

void InspectHandler::Clear() {
  ClearInspectTargetData();
  last_title_ = {};
  barber_open_ = false;
  barber_result_ = 0;
  last_ping_ = {};
}

}

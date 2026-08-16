
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

struct InspectHonorStats {
  std::uint64_t player_guid = 0;
  std::uint8_t lifetime_rank = 0;
  std::uint16_t today_honorable_kills = 0;
  std::uint16_t yesterday_honorable_kills = 0;
  std::uint32_t today_contribution = 0;
  std::uint32_t yesterday_contribution = 0;
  std::uint32_t lifetime_honorable_kills = 0;
};

struct TitleEarned {
  std::uint32_t title_bit_index = 0;
  std::uint32_t earned = 0;
};

enum class BarberShopResult : std::uint32_t {
  kSuccess = 0,
  kNotEnoughMoney = 1,
  kNotSeated = 2,
  kInvalidPlayer = 3,

};

struct MinimapPing {
  std::uint64_t source_guid = 0;
  float map_x = 0;
  float map_y = 0;
};

inline constexpr std::size_t kInspectEquipmentSlotCount = 19;
inline constexpr std::size_t kInspectEquipmentDetailCount = 16;

struct InspectEquipmentSlotData {
  ObjectGuid item_guid{ObjectGuid(0)};
  std::uint32_t item_id = 0;
  bool item_value_was_negative = false;
  std::uint16_t detail_mask = 0;
  std::array<std::uint16_t, kInspectEquipmentDetailCount> detail_values = {};
  std::uint16_t trailing_value = 0;
  std::uint32_t trailing_u32 = 0;
  bool present = false;
};

struct InspectEquipmentData {
  ObjectGuid player{ObjectGuid(0)};
  std::uint32_t slot_used_mask = 0;
  std::array<InspectEquipmentSlotData, kInspectEquipmentSlotCount> slots = {};
};

struct InspectTalentEntry {
  std::uint32_t talent_id = 0;
  std::uint8_t rank = 0;
};

struct InspectTalentSpec {
  std::vector<InspectTalentEntry> talents;
  std::vector<std::uint16_t> glyphs;
};

struct InspectTalentData {
  ObjectGuid player{ObjectGuid(0)};
  std::uint32_t free_points = 0;
  std::uint8_t spec_count = 0;
  std::uint8_t active_spec = 0;
  std::vector<InspectTalentSpec> specs;
  std::uint32_t slot_used_mask = 0;
};

class InspectHandler {
 public:
  bool HandleInspectTalent(const std::uint8_t* data, std::size_t len);
  bool HandleInspectResultsUpdate(const std::uint8_t* data, std::size_t len);
  bool HandleInspectHonorStats(const std::uint8_t* data, std::size_t len);
  bool HandleTitleEarned(const std::uint8_t* data, std::size_t len);
  bool HandleEnableBarberShop(const std::uint8_t* data, std::size_t len);
  bool HandleBarberShopResult(const std::uint8_t* data, std::size_t len);
  bool HandleMinimapPing(const std::uint8_t* data, std::size_t len);

  const InspectTalentData& last_inspect_talent() const { return last_inspect_talent_; }
  const InspectEquipmentData& inspect_equipment() const { return inspect_equipment_; }
  const InspectHonorStats& last_honor_stats() const { return last_honor_stats_; }
  const TitleEarned& last_title() const { return last_title_; }
  bool barber_shop_open() const { return barber_open_; }
  std::uint32_t barber_shop_result() const { return barber_result_; }
  const MinimapPing& last_ping() const { return last_ping_; }

  void CloseBarberShop() { barber_open_ = false; }
  void ClearInspectTargetData();
  void Clear();

 private:
  bool ParseAndStoreInspectEquipment(PacketReader& reader, ObjectGuid player_guid);

  InspectTalentData last_inspect_talent_{};
  InspectEquipmentData inspect_equipment_{};
  InspectHonorStats last_honor_stats_{};
  TitleEarned last_title_{};
  bool barber_open_ = false;
  std::uint32_t barber_result_ = 0;
  MinimapPing last_ping_{};
};

}

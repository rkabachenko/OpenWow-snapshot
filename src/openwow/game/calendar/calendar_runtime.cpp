#include "openwow/game/calendar/calendar_runtime.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/calendar/calendar_time.h"

namespace openwow::game {

static_assert(GameTimeCallbackRegistry::kInvalidHandle == 0);

namespace {

std::string ResolveRaidTitle(const data::dbc::DbcLoader* dbc,
                             const std::uint32_t map_id) {
  if (dbc != nullptr) {
    if (const auto* map = dbc->map().LookupEntry(map_id); map != nullptr) {
      return std::string(map->name);
    }
  }
  return {};
}

}

CalendarSystemEvent BuildCalendarRaidLockoutEvent(
    const data::dbc::DbcLoader* dbc, const std::uint32_t map_id,
    const std::uint32_t difficulty, const std::uint64_t instance_id,
    const std::uint32_t reset_packed_time) {
  CalendarSystemEvent event{};
  event.event_id = instance_id;
  event.title = ResolveRaidTitle(dbc, map_id);
  event.dungeon_id = static_cast<std::int32_t>(difficulty);
  event.map_id = map_id;
  event.time = reset_packed_time;
  event.end_time = reset_packed_time;
  event.flags = 0x80u;
  return event;
}

CalendarRaidInfo BuildCalendarRaidLockoutInfo(
    const data::dbc::DbcLoader* dbc, const std::uint32_t map_id,
    const std::uint32_t difficulty, const std::uint64_t instance_id,
    const std::uint32_t reset_packed_time) {
  CalendarRaidInfo info{};
  info.map_id = map_id;
  info.instance_id = instance_id;
  const auto parts = DecodePackedCalendarTime(reset_packed_time);
  info.reset_month = parts.month;
  info.reset_day = parts.day;
  info.reset_year = parts.year;
  info.difficulty = static_cast<std::uint8_t>(difficulty);
  info.name = ResolveRaidTitle(dbc, map_id);
  if (const auto* entry = data::DBClient_FindMapDifficulty(
          dbc, map_id, static_cast<std::uint8_t>(difficulty));
      entry != nullptr) {
    info.max_players = entry->max_players;
  }
  return info;
}

}

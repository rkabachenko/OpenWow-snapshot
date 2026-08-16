
#include "openwow/game/current_area_record.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/world_session.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kAreaRowOwnsAmbientFlag = 0x00002000u;

}

const data::dbc::AreaTableEntry* GetCurrentAreaTableRecord(
    const data::dbc::DbcStore<data::dbc::AreaTableEntry>& area_table,
    std::int32_t zone_id,
    std::int32_t area_id) {
  if (zone_id > 0) {
    if (const auto* entry =
            area_table.LookupEntry(static_cast<std::uint32_t>(zone_id));
        entry != nullptr) {
      return entry;
    }
  }

  if (area_id > 0) {
    return area_table.LookupEntry(static_cast<std::uint32_t>(area_id));
  }

  return nullptr;
}

const data::dbc::AreaTableEntry* GetCurrentAreaTableRecord(
    const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  return GetCurrentAreaTableRecord(
      dbc->area_table(),
      session.world_states().zone_id(),
      session.world_states().area_id());
}

const data::dbc::AreaTableEntry* ResolveCharacterAmbientAreaRecord(
    const data::dbc::DbcStore<data::dbc::AreaTableEntry>& area_table,
    const std::int32_t area_id) {
  if (area_id <= 0) {
    return nullptr;
  }

  const auto* row = area_table.LookupEntry(static_cast<std::uint32_t>(area_id));
  if (row == nullptr) {
    return nullptr;
  }

  if ((row->flags & kAreaRowOwnsAmbientFlag) != 0 || row->parent_area == 0) {
    return row;
  }

  return area_table.LookupEntry(row->parent_area);
}

const data::dbc::AreaTableEntry* ResolveCharacterAmbientAreaRecord(
    const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  return ResolveCharacterAmbientAreaRecord(dbc->area_table(),
                                           session.world_states().area_id());
}

}

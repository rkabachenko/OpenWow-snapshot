#pragma once

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>

namespace openwow::game {

class WorldSession;

[[nodiscard]] const data::dbc::AreaTableEntry* GetCurrentAreaTableRecord(
    const data::dbc::DbcStore<data::dbc::AreaTableEntry>& area_table,
    std::int32_t zone_id,
    std::int32_t area_id);

[[nodiscard]] const data::dbc::AreaTableEntry* GetCurrentAreaTableRecord(
    const WorldSession& session);

[[nodiscard]] const data::dbc::AreaTableEntry* ResolveCharacterAmbientAreaRecord(
    const data::dbc::DbcStore<data::dbc::AreaTableEntry>& area_table,
    std::int32_t area_id);

[[nodiscard]] const data::dbc::AreaTableEntry* ResolveCharacterAmbientAreaRecord(
    const WorldSession& session);

}

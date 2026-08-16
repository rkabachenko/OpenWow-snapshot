#include "openwow/game/flyable_area.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/world_session.h"

#include <cstdint>

namespace openwow::game {

namespace {

constexpr std::uint32_t kFlyableAreaFlag = 0x00000400u;
constexpr std::uint32_t kNoFlyAreaFlag = 0x20000000u;

constexpr std::uint32_t kNorthrendMapId = 571u;

bool AreaAllowsFlying(const openwow::data::dbc::AreaTableEntry& entry) {
  return (entry.flags & kFlyableAreaFlag) != 0 &&
         (entry.flags & kNoFlyAreaFlag) == 0;
}

const openwow::data::dbc::AreaTableEntry* LookupAreaEntry(
    const openwow::data::dbc::DbcLoader& dbc, const std::int32_t area_id) {
  if (area_id <= 0) {
    return nullptr;
  }

  return dbc.area_table().LookupEntry(static_cast<std::uint32_t>(area_id));
}

const openwow::data::dbc::AreaTableEntry* ResolveLocationAreaEntry(
    const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  if (const auto* area_entry =
          LookupAreaEntry(*dbc, session.world_states().area_id());
      area_entry != nullptr) {
    return area_entry;
  }

  return LookupAreaEntry(*dbc, session.world_states().zone_id());
}

}

bool IsFlyableAreaByLocation(const WorldSession& session) {
  const auto* area_entry = ResolveLocationAreaEntry(session);
  return area_entry != nullptr && AreaAllowsFlying(*area_entry);
}

bool IsFlyableAreaForActivePlayer(const WorldSession& session) {
  if (!IsFlyableAreaByLocation(session)) {
    return false;
  }

  if (session.objects().GetActivePlayer() == nullptr) {
    return true;
  }

  if (session.world_states().map_id() == static_cast<std::int32_t>(kNorthrendMapId) &&
      !session.spell_book().HasSpell(kNorthrendFlightPermissionSpellId)) {
    return false;
  }

  return true;
}

}

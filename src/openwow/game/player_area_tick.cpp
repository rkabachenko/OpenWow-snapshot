
#include "openwow/game/player_area_tick.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/world_session.h"
#include "openwow/world/streaming/world_map.h"

namespace openwow::game {

namespace {

int s_area_tick_counter = 0;

constexpr int kAreaTickThreshold = 10;

std::int32_t s_cached_area_entry_id = -1;

std::int32_t s_cached_zone_entry_id = -1;

std::int32_t s_indoor_area_active = 0;

std::int32_t s_cached_zone_ambience_id = -1;

}

int Player_C_TickAreaCheck(WorldSession& session,
                           const openwow::world::WorldMap& world_map) {
  if (++s_area_tick_counter >= kAreaTickThreshold) {
    s_area_tick_counter -= kAreaTickThreshold;

    auto& obj_mgr = session.objects();

    const ObjectGuid mover_guid = obj_mgr.player_control().ActiveMoverGuid();
    const CGObject_C* unit = nullptr;

    if (!mover_guid.IsEmpty()) {
      const auto* obj = obj_mgr.Get(mover_guid);
      if (obj && obj->IsUnit()) {
        unit = obj;
      }
    }

    if (!unit) {
      const auto* player = obj_mgr.GetActivePlayer();
      if (player) {
        unit = player;
      }
    }

    if (unit == nullptr) {
      return 1;
    }

    const auto* const dbc = session.GetDbcLoader();
    if (dbc == nullptr) {
      return 1;
    }

    const Position position = unit->GetPosition();

    if (!world_map.IsAreaResolutionSettledAt(position.x, position.y)) {
      return 1;
    }
    const auto area_context = world_map.ResolveAreaEnvironmentContextAtPosition(
        position.x, position.y, position.z);
    const auto* const area = dbc->area_table().LookupEntry(area_context.area_id);
    if (area == nullptr) {
      return 1;
    }

    const auto* zone = area;
    if (area->parent_area != 0u) {
      zone = dbc->area_table().LookupEntry(area->parent_area);
      if (zone == nullptr) {
        return 1;
      }
    }

    const std::int32_t map_id = static_cast<std::int32_t>(world_map.map_id());
    const std::int32_t resolved_area_id = static_cast<std::int32_t>(area->id);
    const std::int32_t resolved_zone_id = static_cast<std::int32_t>(zone->id);
    const std::int32_t resolved_subzone_id =
        area->parent_area != 0u ? resolved_area_id : 0;

    const bool use_plain_terrain_lane =
        !area_context.has_wmo_context &&
        s_indoor_area_active == 0;
    bool should_report_zone = false;
    if (use_plain_terrain_lane) {
      const bool location_changed =
          resolved_zone_id != s_cached_zone_entry_id ||
          resolved_subzone_id != s_cached_area_entry_id ||
          map_id != s_cached_zone_ambience_id;
      if (location_changed) {
        s_cached_zone_entry_id = resolved_zone_id;
        s_cached_area_entry_id = resolved_subzone_id;
        s_cached_zone_ambience_id = map_id;
        should_report_zone = resolved_zone_id != 0;

        auto& world_states = session.world_states();
        world_states.SetMapId(map_id);
        world_states.SetZoneId(resolved_zone_id);
        world_states.SetAreaId(resolved_area_id);
      }
    } else {

      if (resolved_area_id != s_cached_zone_entry_id) {
        s_cached_zone_entry_id = resolved_area_id;

        auto& world_states = session.world_states();
        world_states.SetMapId(map_id);
        world_states.SetZoneId(resolved_zone_id);
        world_states.SetAreaId(resolved_area_id);
      }
    }
    s_indoor_area_active = area_context.has_wmo_context ? 1 : 0;

    if (should_report_zone) {
      session.interaction().SendZoneUpdate(
          static_cast<std::uint32_t>(resolved_zone_id));
    }
  }

  return 1;
}

void Player_C_ResetAreaTickCounter() {
  s_area_tick_counter = 0;
}

void Player_C_ResetAreaStateCache() {
  s_cached_area_entry_id    = -1;
  s_cached_zone_entry_id    = -1;
  s_indoor_area_active      =  0;
  s_cached_zone_ambience_id = -1;
}

std::int32_t Player_C_GetCachedAreaEntryId()   { return s_cached_area_entry_id; }
std::int32_t Player_C_GetCachedZoneEntryId()   { return s_cached_zone_entry_id; }
std::int32_t Player_C_GetIndoorAreaActive()    { return s_indoor_area_active; }
std::int32_t Player_C_GetCachedZoneAmbienceId() { return s_cached_zone_ambience_id; }

}

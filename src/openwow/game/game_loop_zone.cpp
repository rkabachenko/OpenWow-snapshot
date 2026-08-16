#include "openwow/game/game_loop.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/game/world_state_manager.h"
#include "openwow/ui/surfaces/game/runtime/zone_ui_state.h"

#include <cstdint>
#include <string>
#include "openwow/ui/game/world_state_ui_sync.h"

namespace openwow::game {

void GameLoop::SynchronizeZoneUiState() {
  WorldSession* const session = world_session();
  if (session == nullptr || dbc_ == nullptr) {
    return;
  }

  const auto &world_states = session->world_states();
  const std::int32_t map_id = world_states.map_id();
  const std::int32_t zone_id = world_states.zone_id();
  const std::int32_t area_id = world_states.area_id();
  if (map_id == synchronized_zone_map_id_ &&
      zone_id == synchronized_zone_id_ && area_id == synchronized_area_id_) {
    return;
  }

  const auto lookup_area = [this](const std::int32_t id) {
    return id > 0
               ? dbc_->area_table().LookupEntry(static_cast<std::uint32_t>(id))
               : nullptr;
  };
  const auto *area = lookup_area(area_id);
  const auto *zone = lookup_area(zone_id);
  if (zone == nullptr && area != nullptr && area->parent_area != 0u) {
    zone = dbc_->area_table().LookupEntry(area->parent_area);
  }
  if (zone == nullptr) {
    zone = area;
  }
  if (area == nullptr) {
    area = zone;
  }
  if (zone == nullptr || area == nullptr) {
    return;
  }

  const std::string zone_text(zone->name);
  const std::string subzone_text =
      area->id != zone->id ? std::string(area->name) : std::string{};
  const auto *map = map_id >= 0
                        ? dbc_->map().LookupEntry(
                              static_cast<std::uint32_t>(map_id))
                        : nullptr;
  const bool is_instance = map != nullptr && map->map_type != 0u;

  session->objects().SetZoneId(static_cast<std::uint32_t>(zone_id));
  session->objects().SetAreaId(static_cast<std::uint32_t>(area_id));

  openwow::ui::game::RefreshZoneSoundsForActiveMover();
  zone_ui_state_.Apply(
      {
          .location =
              {
                  .area = openwow::ui::game::AreaId(area_id),
                  .zone = openwow::ui::game::ZoneId(zone_id),
                  .map = openwow::ui::game::MapId(map_id),
              },
          .text =
              {
                  .zone = zone_text,
                  .subzone = subzone_text,
                  .real_zone = zone_text,
              },
          .is_instance = is_instance,
      },
      session);

  synchronized_zone_map_id_ = map_id;
  synchronized_zone_id_ = zone_id;
  synchronized_area_id_ = area_id;
}

}

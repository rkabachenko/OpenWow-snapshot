
#include "openwow/game/snow_area.h"

#include "openwow/data/formats/dbc/area_environment_rules.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/world_session.h"

#include <cstdint>

namespace openwow::game {

bool IsPositionInSnowArea(const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto area_id = session.world_states().area_id();
  if (area_id != 0u) {
    return data::dbc::IsAreaFlaggedSnow(*dbc, area_id);
  }

  return data::dbc::IsAreaFlaggedSnow(*dbc, session.world_states().zone_id());
}

}

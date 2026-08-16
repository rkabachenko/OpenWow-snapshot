#include "openwow/ui/game/lua_tooltip_data_sources.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/tooltip_system.h"

namespace openwow::ui::game {

TooltipDataSources ResolveLuaTooltipDataSources(lua_State* lua) {
  if (auto* session = detail::GetWorldSession(lua); session != nullptr) {
    const auto* dbc = session->GetDbcLoader();
    if (dbc == nullptr) {
      dbc = TooltipSystem::Get().GetDbcLoader();
    }
    return {
        .dbc = dbc,
        .world_session = session,
        .equipment = &session->equipment(),
        .inventory = &session->inventory_replica(),
        .item_definitions = &session->item_definitions(),
    };
  }

  auto& fallback = TooltipSystem::Get();
  return {
      .dbc = fallback.GetDbcLoader(),
      .world_session = fallback.GetWorldSession(),
      .equipment = fallback.GetEquipmentSets(),
      .inventory = fallback.GetInventory(),
      .item_definitions = fallback.GetItemDefinitions(),
  };
}

}

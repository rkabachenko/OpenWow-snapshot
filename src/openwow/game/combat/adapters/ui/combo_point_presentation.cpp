#include "openwow/game/combat/adapters/ui/combo_point_presentation.h"

#include "openwow/game/objects/cgobject.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::ui::game {

void GameUI_UpdateComboPoints(
    openwow::game::WorldSession& session, const std::uint64_t,
    const std::uint8_t) {
  auto& events = ScriptEventDispatch::Get();

  session.spellbook_private_usability().Refresh(session);
  const auto player = openwow::game::CGObject_C::GetActivePlayerGuid();
  if (!player.IsEmpty()) events.FireUnitComboPoints(player.GetRawValue());
}

}

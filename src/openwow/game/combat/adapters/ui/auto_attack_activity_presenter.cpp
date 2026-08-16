#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

namespace openwow::game::combat::ui {

void PresentAutoAttackActivityChange(
    WorldSession& session,
    const AutoAttackActivityChange change) {
  if (!change.changed()) {
    return;
  }

  auto& dispatch = ::openwow::ui::game::ScriptEventDispatch::Get();
  if (change.current == AutoAttackActivity::Active) {
    dispatch.FirePlayerEnterCombat();
  } else {
    if (session.held_cursor() != nullptr) {
      session.held_cursor()->Clear();
    }
    CancelPendingCastsForActivePlayer(session);
    dispatch.FirePlayerLeaveCombat();
  }

  if (::openwow::ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
  dispatch.FirePetBarUpdateUsable();
  dispatch.FireEvent(::openwow::ui::game::events::SPELL_UPDATE_USABLE);
}

}

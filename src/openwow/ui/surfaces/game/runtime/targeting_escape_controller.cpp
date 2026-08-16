#include "openwow/ui/surfaces/game/runtime/targeting_escape_controller.h"

#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_missile_runtime.h"
#include "openwow/game/world_session.h"

#include <cstdint>

namespace openwow::ui::game {

int GameUI_HandleTargetingEscape(
    openwow::game::WorldSession* session, const void* event_data) {
  const auto* event_words = static_cast<const std::uint32_t*>(event_data);
  if (event_words[5] != 4) {
    return 0;
  }

  if (session != nullptr) {
    auto& targeting = session->spells().GetTargeting();
    if (targeting.GetSpellId() != 0u) {
      if (targeting.UsesManualPreviewFacing()) {
        targeting.AdvancePreviewFacingQuarterTurn();
      } else {
        ::openwow::game::CancelPendingSpellCast(*session);
      }
    }
    ::openwow::game::inventory::ui::ClearItemTargetCursor(targeting);
  }

  if (auto* cursor = ::openwow::game::GetActiveCursorSurface();
      cursor != nullptr &&
      cursor->GetBaseCursorType() == ::openwow::game::CursorType::kRepair) {
    cursor->SetBaseCursor(::openwow::game::CursorType::kDefault);
    cursor->SetImmediateCursorType(1);
  }

  return 0;
}

}

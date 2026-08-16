#include "openwow/game/actions/macros/adapters/ui/macro_action_bar_adapter.h"

#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/world_session.h"

#include <utility>

namespace openwow::game::actions::macros::adapters::ui {

MacroPresentationRuntime::ClearActionBarMacro
MakeClearActionBarMacroAdapter(
    ActionAssignmentRuntime& assignments,
    std::function<bool()> mutations_allowed,
    std::function<void(actions::ActionSlot)> assignment_cleared) {
  return [&assignments, mutations_allowed = std::move(mutations_allowed),
          assignment_cleared = std::move(assignment_cleared)](
             const MacroId id) {
    if (mutations_allowed && !mutations_allowed()) {
      return;
    }
    for (const auto slot : assignments.ClearMacroReferences(id)) {
      if (assignment_cleared) {
        assignment_cleared(slot);
      }
    }
  };
}

MacroPresentationRuntime::ActiveShapeshiftFormProvider
MakeActiveShapeshiftFormAdapter(WorldSession& session) {
  return [&session] {
    const auto* player = session.objects().GetLocalPlayerTyped();
    return player != nullptr
               ? static_cast<std::uint32_t>(
                     player->Animation().GetShapeshiftForm())
               : 0u;
  };
}

}

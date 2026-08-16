#pragma once

#include "openwow/game/actions/macros/application/macro_presentation_runtime.h"
#include "openwow/game/actions/model/action_assignments.h"

#include <functional>

namespace openwow::game {

class ActionAssignmentRuntime;
class WorldSession;

namespace actions::macros::adapters::ui {

[[nodiscard]] MacroPresentationRuntime::ClearActionBarMacro
MakeClearActionBarMacroAdapter(
    ActionAssignmentRuntime& assignments,
    std::function<bool()> mutations_allowed,
    std::function<void(actions::ActionSlot)> assignment_cleared);
[[nodiscard]] MacroPresentationRuntime::ActiveShapeshiftFormProvider
MakeActiveShapeshiftFormAdapter(WorldSession& session);

}
}

#pragma once

#include "openwow/game/actions/macros/rules/macro_condition_rules.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/model/action_page.h"
#include "openwow/game/world_environment_state.h"

namespace openwow::game {

class GroupSystem;
class MacroCatalog;
class TalentInfoStore;
class UnitQueryBridge;
class VehicleSystem;
class WorldSession;

}

namespace openwow::game {

namespace actions::macros::adapters::retail {

[[nodiscard]] rules::MacroConditionRules::SnapshotProvider
MakeRetailMacroConditionSnapshotProvider(
    MacroCatalog& macros, WorldSession& session,
    const GroupSystem& groups, VehicleSystem& vehicles,
    const actions::held_cursor::HeldCursor& cursor,
    const TalentInfoStore& talents,
    const UnitQueryBridge& unit_queries, const WorldEnvironmentState& environment,
    OutdoorPositionQuery outdoor_query,
    std::function<std::uint16_t()> modifier_state_query,
    std::function<actions::ActionPage()> action_page_query);

}
}

#pragma once

#include "openwow/game/actions/macros/rules/macro_icon_resolution_rules.h"

namespace openwow::game {

class PlayerInventoryReplica;
class MacroCatalog;
class UnitQueryBridge;
class WorldSession;

namespace actions::macros::adapters::retail {

[[nodiscard]] std::optional<rules::ResolvedMacroSpell>
ResolveRetailMacroSpell(std::string_view value);

[[nodiscard]] rules::MacroIconResolutionQueries
MakeRetailMacroIconResolutionQueries(
    MacroCatalog& macros, WorldSession& session,
    const UnitQueryBridge& unit_queries,
    const PlayerInventoryReplica& inventory,
    std::function<std::optional<rules::ResolvedMacroSpell>(
        std::string_view)> spell_query);

}
}

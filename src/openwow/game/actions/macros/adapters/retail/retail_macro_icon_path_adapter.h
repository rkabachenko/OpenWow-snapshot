#pragma once

#include "openwow/game/actions/macros/model/macro_document.h"

#include <functional>
#include <string>

namespace openwow::game {

class ItemDefinitions;
class WorldSession;

}

namespace openwow::game::actions::macros::adapters::retail {

using MacroIconPathResolver =
    std::function<std::string(const MacroDocument&)>;

[[nodiscard]] MacroIconPathResolver MakeRetailMacroIconPathResolver(
    const WorldSession& session, const ItemDefinitions& item_definitions);

}

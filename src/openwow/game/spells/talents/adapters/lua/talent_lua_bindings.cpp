#include "openwow/game/spells/talents/adapters/lua/talent_lua_bindings.h"

#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/game/api/game_lua_api_talent.h"

#include "openwow/ui/lua_binding_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kTalentBindings[] = {

    {"GetNumTalentTabs", LuaGetNumTalentTabs},
    {"GetTalentTabInfo", LuaGetTalentTabInfo},
    {"GetNumTalents", LuaGetNumTalents},
    {"GetTalentInfo", LuaGetTalentInfo},
    {"GetTalentLink", LuaGetTalentLink},
    {"GetTalentPrereqs", LuaGetTalentPrereqs},
    {"LearnTalent", LuaLearnTalent},
    {"GetUnspentTalentPoints", LuaGetUnspentTalentPoints},
    {"GetNumTalentGroups", LuaGetNumTalentGroups},
    {"GetActiveTalentGroup", LuaGetActiveTalentGroup},
    {"SetActiveTalentGroup", LuaSetActiveTalentGroup},
    {"GetPreviewTalentPointsSpent", LuaGetPreviewTalentPointsSpent},
    {"GetGroupPreviewTalentPointsSpent", LuaGetGroupPreviewTalentPointsSpent},
    {"AddPreviewTalentPoints", LuaAddPreviewTalentPoints},
    {"ResetPreviewTalentPoints", LuaResetPreviewTalentPoints},
    {"ResetGroupPreviewTalentPoints", LuaResetGroupPreviewTalentPoints},
    {"LearnPreviewTalents", LuaLearnPreviewTalents},
};

}

openwow::ui::lua::NativeBindingCatalog TalentNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.spells.talents", openwow::ui::lua::BindingScope::kWorld, kTalentBindings);
}

}

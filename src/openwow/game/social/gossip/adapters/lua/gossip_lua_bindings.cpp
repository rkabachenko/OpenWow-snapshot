#include "openwow/game/social/gossip/adapters/lua/gossip_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaCheckBinderDist(lua_State* L);
int LuaGetGossipText(lua_State* L);
int LuaGetNumGossipOptions(lua_State* L);
int LuaGetNumGossipAvailableQuests(lua_State* L);
int LuaGetNumGossipActiveQuests(lua_State* L);
int LuaGetGossipOptions(lua_State* L);
int LuaGetGossipAvailableQuests(lua_State* L);
int LuaGetGossipActiveQuests(lua_State* L);
int LuaSelectGossipOption(lua_State* L);
int LuaSelectGossipAvailableQuest(lua_State* L);
int LuaSelectGossipActiveQuest(lua_State* L);
int LuaCloseGossip(lua_State* L);
int LuaForceGossip(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kGossipLuaBindings[] = {
    {"CheckBinderDist", LuaCheckBinderDist},
    {"GetGossipText", LuaGetGossipText},
    {"GetNumGossipOptions", LuaGetNumGossipOptions},
    {"GetNumGossipAvailableQuests", LuaGetNumGossipAvailableQuests},
    {"GetNumGossipActiveQuests", LuaGetNumGossipActiveQuests},
    {"GetGossipOptions", LuaGetGossipOptions},
    {"GetGossipAvailableQuests", LuaGetGossipAvailableQuests},
    {"GetGossipActiveQuests", LuaGetGossipActiveQuests},
    {"SelectGossipOption", LuaSelectGossipOption},
    {"SelectGossipAvailableQuest", LuaSelectGossipAvailableQuest},
    {"SelectGossipActiveQuest", LuaSelectGossipActiveQuest},
    {"CloseGossip", LuaCloseGossip},
    {"ForceGossip", LuaForceGossip},
};

}

openwow::ui::lua::NativeBindingCatalog GossipNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.social.gossip", openwow::ui::lua::BindingScope::kWorld, kGossipLuaBindings);
}

}

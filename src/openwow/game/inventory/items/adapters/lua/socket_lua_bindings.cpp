#include "openwow/game/inventory/items/adapters/lua/socket_lua_bindings.h"

#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/inventory/items/adapters/lua/item_socket_lua_api.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaCloseSocketInfo(lua_State* L);
int LuaGetSocketItemInfo(lua_State* L);
int LuaGetNumSockets(lua_State* L);
int LuaGetExistingSocketInfo(lua_State* L);
int LuaGetExistingSocketLink(lua_State* L);
int LuaGetNewSocketInfo(lua_State* L);
int LuaGetNewSocketLink(lua_State* L);
int LuaClickSocketButton(lua_State* L);
int LuaAcceptSockets(lua_State* L);
}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kSocketLuaBindings[] = {
    {"CloseSocketInfo", LuaCloseSocketInfo},
    {"GetSocketItemInfo", LuaGetSocketItemInfo},
    {"GetNumSockets", LuaGetNumSockets},
    {"GetExistingSocketInfo", LuaGetExistingSocketInfo},
    {"GetExistingSocketLink", LuaGetExistingSocketLink},
    {"GetNewSocketInfo", LuaGetNewSocketInfo},
    {"GetNewSocketLink", LuaGetNewSocketLink},
    {"ClickSocketButton", LuaClickSocketButton},
    {"AcceptSockets", LuaAcceptSockets},

    {"GetSocketTypes", LuaGetSocketType},
    {"GetSocketItemRefundable", LuaGetSocketItemRefundable},
    {"GetSocketItemBoundTradeable", LuaGetSocketItemBoundTradeable},
};

}

openwow::ui::lua::NativeBindingCatalog SocketNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.inventory.sockets", openwow::ui::lua::BindingScope::kWorld, kSocketLuaBindings);
}

}

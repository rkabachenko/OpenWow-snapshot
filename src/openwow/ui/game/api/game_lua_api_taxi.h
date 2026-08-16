
#pragma once

#include "openwow/ui/lua_binding_registry.h"

#include <array>

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumRoutes(lua_State* L);
int LuaTaxiNodeName(lua_State* L);
int LuaTaxiNodeCost(lua_State* L);
int LuaTaxiNodePosition(lua_State* L);
int LuaTaxiNodeGetType(lua_State* L);
int LuaTakeTaxiNode(lua_State* L);
int LuaNumTaxiNodes(lua_State* L);
int LuaCloseTaxiMap(lua_State* L);
int LuaTaxiGetSrcX(lua_State* L);
int LuaTaxiGetSrcY(lua_State* L);
int LuaTaxiGetDestX(lua_State* L);
int LuaTaxiGetDestY(lua_State* L);
int LuaSetTaxiMap(lua_State* L);

int LuaTaxiNodeSetCurrent(lua_State* L);

}

namespace openwow::ui::game {

inline constexpr std::array<openwow::ui::LuaGlobalBinding, 14> kTaxiMapLuaFunctions{{
    {"SetTaxiMap", detail::LuaSetTaxiMap},
    {"NumTaxiNodes", detail::LuaNumTaxiNodes},
    {"TaxiNodeName", detail::LuaTaxiNodeName},
    {"TaxiNodePosition", detail::LuaTaxiNodePosition},
    {"TaxiNodeCost", detail::LuaTaxiNodeCost},
    {"TakeTaxiNode", detail::LuaTakeTaxiNode},
    {"CloseTaxiMap", detail::LuaCloseTaxiMap},
    {"TaxiNodeGetType", detail::LuaTaxiNodeGetType},
    {"TaxiNodeSetCurrent", detail::LuaTaxiNodeSetCurrent},
    {"TaxiGetSrcX", detail::LuaTaxiGetSrcX},
    {"TaxiGetSrcY", detail::LuaTaxiGetSrcY},
    {"TaxiGetDestX", detail::LuaTaxiGetDestX},
    {"TaxiGetDestY", detail::LuaTaxiGetDestY},
    {"GetNumRoutes", detail::LuaGetNumRoutes},
}};

}

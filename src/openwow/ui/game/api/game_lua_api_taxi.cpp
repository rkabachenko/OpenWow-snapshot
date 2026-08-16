#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/faction_system.h"
#include "openwow/game/taxi_map_frame.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/taxi_handler.h"
#include "openwow/game/taxi_runtime_slice.h"
#include "openwow/game/taxi_system.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/lua_numeric.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {

namespace {

using openwow::data::dbc::DbcLoader;
using openwow::game::TaxiSliceState;
using TaxiPreviewCoordinateGetter =
    float (openwow::game::TaxiSystem::*)(std::size_t, std::size_t) const;

[[nodiscard]] const DbcLoader* GetDbcLoaderFromRegistry(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto* dbc =
      static_cast<const DbcLoader*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

[[nodiscard]] TaxiSliceState BuildTaxiSliceState(lua_State* L) {
  if (const auto cached_state = openwow::game::TaxiSystem::Get().GetCachedDisplaySlice();
      cached_state.has_value()) {
    return *cached_state;
  }
  if (const auto* dbc = GetDbcLoaderFromRegistry(L)) {
    if (auto* session = GetWorldSession(L)) {
      return openwow::game::BuildTaxiRuntimeSliceState(*dbc, *session);
    }
  }
  return {};
}

[[nodiscard]] std::uint32_t ReadTaxiDisplaySlotUnchecked(
    lua_State* L, const int argument_index) {
  return SaturateLuaNumberToU32(lua_tonumber(L, argument_index)) - 1u;
}

[[nodiscard]] std::uint32_t ReadTaxiDisplaySlot(lua_State* L,
                                                int argument_index,
                                                const char* usage_error) {
  if (!lua_isnumber(L, argument_index)) {
    luaL_error(L, usage_error);
    return std::numeric_limits<std::uint32_t>::max();
  }

  return ReadTaxiDisplaySlotUnchecked(L, argument_index);
}

int PushTaxiPreviewCoordinate(lua_State* L,
                              TaxiPreviewCoordinateGetter getter) {
  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto route_index = ReadTaxiDisplaySlotUnchecked(L, 2);

  const auto& taxi_system = openwow::game::TaxiSystem::Get();
  lua_pushnumber(
      L, (taxi_system.*getter)(slot, route_index));
  return 1;
}

void AssignTaxiTexture(lua_State* L,
                       int object_index,
                       std::string_view texture_path) {
  if (texture_path.empty()) {
    return;
  }

  object_index = lua_absindex(L, object_index);
  lua_getfield(L, object_index, "SetTexture");
  if (lua_isfunction(L, -1)) {
    lua_pushvalue(L, object_index);
    lua_pushlstring(L, texture_path.data(),
                    static_cast<size_t>(texture_path.size()));
    if (lua_pcall(L, 2, 0, 0) == LUA_OK) {
      return;
    }
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
  }

  runtime::SetTextureRenderStateString(
      L, object_index, runtime::TextureRenderStateField::kTexture,
      std::string_view(texture_path.data(), texture_path.size()));
}

int ValidateTaxiTextureArgument(lua_State* L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(
        L,
        "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }
  if (!HasLuaScriptObjectThis(L, 1)) {
    return luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }
  if (!LuaScriptObjectIsKindOfCanonicalType(
          L, 1, openwow::ui::widgets::ScriptObjectType::Texture)) {
    return luaL_error(L, "Wrong object type for member function");
  }
  return lua_absindex(L, 1);
}

[[nodiscard]] int ResolveTaxiReputationLevel(
    const openwow::game::WorldSession& session,
    const DbcLoader* dbc) {
  const auto* player = session.objects().GetLocalPlayerTyped();
  const auto* taxi_master =
      session.objects().GetUnit(openwow::game::ObjectGuid(
          session.taxi().GetFlightMasterGuid()));
  if (!player || !taxi_master) {
    return 0;
  }

  int reaction_level =
      static_cast<int>(taxi_master->Interaction().GetReaction(*player));
  if (reaction_level <= 3 || dbc == nullptr) {
    return reaction_level;
  }

  const auto* template_entry =
      dbc->faction_template().LookupEntry(taxi_master->State().GetFactionTemplate());
  if (!template_entry) {
    return 3;
  }

  const auto* faction_entry = dbc->faction().LookupEntry(template_entry->faction);
  if (!faction_entry || faction_entry->reputation_list_id < 0) {
    return 3;
  }

  if (const auto* faction_info =
          openwow::game::FactionSystem::Get().GetFactionById(
              template_entry->faction)) {
    if (faction_info->at_war) {
      return 3;
    }
    reaction_level =
        std::max(reaction_level, static_cast<int>(faction_info->standing));
  }

  return std::clamp(reaction_level, 0, 7);
}

[[nodiscard]] float GetTaxiDirectDiscountForReaction(int reaction_level) {
  switch (reaction_level) {
    case 4:
      return 0.05f;
    case 5:
      return 0.10f;
    case 6:
      return 0.15f;
    case 7:
      return 0.20f;
    default:
      return 0.0f;
  }
}

[[nodiscard]] std::uint32_t CalculateTaxiNodeCost(
    lua_State* L,
    const TaxiSliceState& state,
    std::size_t slot_index) {
  if (slot_index >= state.nodes.size() || slot_index >= state.routes.size()) {
    return 0;
  }

  const auto* session = GetWorldSession(L);
  const auto* dbc = GetDbcLoaderFromRegistry(L);
  const auto& node = state.nodes[slot_index];
  const auto& route = state.routes[slot_index];

  if (route.requires_multi_hop) {
    if (route.has_route && !route.path_nodes.empty()) {
      if (session && dbc) {
        const auto bucket = static_cast<std::size_t>(std::clamp(
            ResolveTaxiReputationLevel(*session, dbc) - 3, 0, 4));
        return static_cast<std::uint32_t>(
            std::max(route.weighted_costs[bucket], 0));
      }
      return static_cast<std::uint32_t>(
          std::max(route.weighted_costs.front(), 0));
    }
    return 0;
  }

  if (session && dbc) {
    const auto ctx = openwow::game::BuildTaxiRuntimeRouteContext(*dbc);
    if (const auto* path = ctx.LookupPath(state.current_node_id, node.id)) {
      const float discount = GetTaxiDirectDiscountForReaction(
          ResolveTaxiReputationLevel(*session, dbc));
      return static_cast<std::uint32_t>(std::lrintf(
          (1.0f - discount) * static_cast<float>(path->cost)));
    }
    return 0;
  }

  return static_cast<std::uint32_t>(
      std::max(route.weighted_costs.front(), 0));
}

void TaxiMapFrameTakeTaxiNode(lua_State* L,
                              const TaxiSliceState& state,
                              std::size_t slot_index,
                              openwow::game::WorldSession& session) {
  constexpr std::uint32_t kTaxiActivationInternalFlag = 0x10000000u;

  if (slot_index >= state.nodes.size() || slot_index >= state.routes.size()) {
    return;
  }

  const auto* player = session.objects().GetLocalPlayerTyped();
  if (!player) {
    return;
  }

  if (player->State().GetHealth() > 0 &&
      (player->GetInternalFlags() & kTaxiActivationInternalFlag) == 0) {
    DisplaySystemMessage(193);
    return;
  }

  const auto& node = state.nodes[slot_index];
  const auto& route = state.routes[slot_index];
  if (node.id == state.current_node_id) {
    DisplaySystemMessage(185);
    return;
  }

  const auto npc_guid = session.taxi().GetFlightMasterGuid();
  if (!route.requires_multi_hop) {
    session.interaction().SendActivateTaxi(npc_guid,
                                           state.current_node_id,
                                           node.id);
    return;
  }

  if (route.has_route && !route.path_nodes.empty()) {
    if (CalculateTaxiNodeCost(L, state, slot_index) > player->GetMoney()) {
      DisplaySystemMessage(188);
      return;
    }
    session.interaction().SendActivateTaxiExpress(npc_guid, route.path_nodes);
    return;
  }

  DisplaySystemMessage(186);
}

}

int LuaGetNumRoutes(lua_State* L) {
  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto state = BuildTaxiSliceState(L);
  if (slot >= state.nodes.size()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto& route = state.routes[slot];
  const auto value = route.requires_multi_hop
                         ? static_cast<lua_Integer>(route.path_count - 1)
                         : 1;
  lua_pushnumber(L, value);
  return 1;
}

int LuaNumTaxiNodes(lua_State* L) {
  const auto state = BuildTaxiSliceState(L);
  lua_pushnumber(L, static_cast<lua_Integer>(state.nodes.size()));
  return 1;
}

int LuaTaxiNodeName(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TaxiNodeName(slot)");
  }

  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto state = BuildTaxiSliceState(L);
  if (slot >= state.nodes.size()) {
    lua_pushstring(L, "INVALID");
    return 1;
  }
  lua_pushstring(
      L, state.nodes[slot].name.c_str());
  return 1;
}

int LuaTaxiNodeCost(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TaxiNodeCost(slot)");
  }

  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto state = BuildTaxiSliceState(L);
  if (slot >= state.nodes.size()) {
    return luaL_error(L, "Invalid taxi node slot");
  }
  const auto total =
      CalculateTaxiNodeCost(L, state, slot);
  lua_pushnumber(L, static_cast<lua_Integer>(total));
  return 1;
}

int LuaTaxiNodePosition(lua_State* L) {
  const auto slot_index =
      ReadTaxiDisplaySlot(L, 1, "Usage: TaxiNodePosition(slot)");
  const auto state = BuildTaxiSliceState(L);
  if (slot_index >= state.nodes.size()) {
    return luaL_error(L, "Invalid taxi node slot");
  }
  const auto& node = state.nodes[slot_index];
  lua_pushnumber(L, static_cast<double>(node.x));
  lua_pushnumber(L, static_cast<double>(node.y));
  return 2;
}

int LuaTaxiNodeGetType(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TaxiNodeGetType(slot)");
  }

  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto state = BuildTaxiSliceState(L);
  if (slot >= state.nodes.size()) {
    lua_pushstring(L, "NONE");
    return 1;
  }
  const auto& node = state.nodes[slot];

  if (node.id == state.current_node_id) {
    lua_pushstring(L, "CURRENT");
    return 1;
  }

  if (!state.routes[slot].has_route) {
    lua_pushstring(L, "NONE");
    return 1;
  }

  lua_pushstring(L,
                 openwow::game::IsKnownTaxiSliceNode(state, node.id)
                     ? "REACHABLE"
                     : "DISTANT");
  return 1;
}

int LuaTakeTaxiNode(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TakeTaxiNode(slot)");
  }

  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto state = BuildTaxiSliceState(L);
  auto* session = GetWorldSession(L);
  if (!session || slot >= state.nodes.size()) {
    return 0;
  }
  TaxiMapFrameTakeTaxiNode(
      L, state, slot, *session);
  return 0;
}

int LuaCloseTaxiMap(lua_State* L) {
  if (auto* session = GetWorldSession(L); session != nullptr) {
    openwow::game::TaxiMapFrame_Close(session->taxi());
  }
  return 0;
}

int LuaTaxiGetSrcX(lua_State* L) {
  return PushTaxiPreviewCoordinate(L, &openwow::game::TaxiSystem::GetPreviewSrcX);
}

int LuaTaxiGetSrcY(lua_State* L) {
  return PushTaxiPreviewCoordinate(L, &openwow::game::TaxiSystem::GetPreviewSrcY);
}

int LuaTaxiGetDestX(lua_State* L) {
  return PushTaxiPreviewCoordinate(L, &openwow::game::TaxiSystem::GetPreviewDestX);
}

int LuaTaxiGetDestY(lua_State* L) {
  return PushTaxiPreviewCoordinate(L, &openwow::game::TaxiSystem::GetPreviewDestY);
}

int LuaSetTaxiMap([[maybe_unused]] lua_State* L) {
  const auto state = BuildTaxiSliceState(L);
  if (!state.texture_path.empty()) {
    AssignTaxiTexture(L, ValidateTaxiTextureArgument(L), state.texture_path);
  }
  return 0;
}

int LuaTaxiNodeSetCurrent(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TaxiNodeSetCurrent(slot)");
  }

  const auto slot = ReadTaxiDisplaySlotUnchecked(L, 1);
  const auto state = BuildTaxiSliceState(L);
  auto& taxi_system = openwow::game::TaxiSystem::Get();
  auto* session = GetWorldSession(L);
  taxi_system.SetSelectedDestination(0);
  if (slot >= state.nodes.size()) {
    if (session) {
      session->taxi().SetSelectedDestination(0);
    }
    return 0;
  }

  const auto& route = state.routes[slot];
  if (!route.has_route) {
    if (session) {
      session->taxi().SetSelectedDestination(0);
    }
    return 0;
  }

  taxi_system.ClearPreviewLinePairs();
  taxi_system.ClearPreviewSegments(slot);

  const std::uint32_t node_id = state.nodes[slot].id;
  if (session) {
    session->taxi().SetSelectedDestination(node_id);
  }
  taxi_system.SetSelectedDestination(node_id);

  if (const auto* dbc = GetDbcLoader(L)) {
    const auto ctx = openwow::game::BuildTaxiRuntimeRouteContext(*dbc);
    const auto append_runtime_preview_segment =
        [&](std::uint32_t from_node_id, std::uint32_t to_node_id) {
          const auto route_index =
              taxi_system.AppendZeroedPreviewSegment(slot);
          if (const auto segment = openwow::game::BuildTaxiRuntimePreviewSegment(
                  ctx, state, from_node_id, to_node_id)) {
            const bool updated =
                taxi_system.SetPreviewSegment(slot, route_index, *segment);
            (void)updated;
          }
        };
    if (route.requires_multi_hop) {
      for (std::size_t i = 0; i + 1 < route.path_nodes.size(); ++i) {
        taxi_system.AppendPreviewLinePair(route.path_nodes[i],
                                          route.path_nodes[i + 1]);
        append_runtime_preview_segment(route.path_nodes[i],
                                       route.path_nodes[i + 1]);
      }
    } else {
      taxi_system.AppendPreviewLinePair(state.current_node_id, node_id);
      append_runtime_preview_segment(state.current_node_id, node_id);
    }
  }
  return 0;
}

}

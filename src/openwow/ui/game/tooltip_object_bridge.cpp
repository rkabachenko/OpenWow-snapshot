#include "openwow/ui/game/tooltip_object_bridge.h"

#include "openwow/ui/game/framescript/core/frame_method_table_runtime.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/lua_tooltip_data_sources.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/game/tooltip_frame_sync.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/runtime/lua/lua_binding.h"

#include <lua.hpp>

#include <algorithm>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <vector>
#include "openwow/ui/game/tooltip_lua_adapter.h"

namespace openwow::ui::game::frame_api {

namespace {

constexpr const char* kTooltipOwnerWeakTableRegistryKey =
    "openwow.game_tooltip_owners";
constexpr const char* kTooltipStateMetatableRegistryKey =
    "openwow.game_tooltip_state";

struct LuaTooltipObjectState final {
  explicit LuaTooltipObjectState(lua_State* state) : lua(state) {}

  void SynchronizeRetainedFrame(bool fire_data_event);
  void FireDataEvent();

  TooltipSystem tooltip;
  lua_State* lua{nullptr};
  bool synchronizing{false};
  struct PublishedPresentation final {
    std::vector<TooltipLine> lines;
    std::vector<TooltipTextureData> textures;
    std::string owner;
    std::string anchor;
    float minimum_width{0.0F};
    float padding{0.0F};
    float status_bar_min{0.0F};
    float status_bar_max{1.0F};
    float status_bar_value{0.0F};
    bool force_minimum_width{false};
    bool shown{false};
    bool has_status_bar{false};
    bool operator==(const PublishedPresentation&) const = default;
  };
  std::optional<PublishedPresentation> published_presentation;
};

LuaTooltipObjectState::PublishedPresentation CapturePresentation(
    const TooltipSystem& tooltip) {
  return {
      .lines = tooltip.GetLines(),
      .textures = tooltip.GetTextures(),
      .owner = tooltip.GetOwnerName(),
      .anchor = tooltip.GetAnchor(),
      .minimum_width = tooltip.GetMinimumWidth(),
      .padding = tooltip.GetPadding(),
      .status_bar_min = tooltip.GetStatusBarMin(),
      .status_bar_max = tooltip.GetStatusBarMax(),
      .status_bar_value = tooltip.GetStatusBarValue(),
      .force_minimum_width = tooltip.IsForceMinWidth(),
      .shown = tooltip.IsShown(),
      .has_status_bar = tooltip.HasStatusBar(),
  };
}

int PushTooltipOwnerWeakTable(lua_State* L) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kTooltipOwnerWeakTableRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }
  lua_pop(L, 1);

  lua_newtable(L);
  const int owners_index = lua_absindex(L, -1);
  lua_newtable(L);
  lua_pushliteral(L, "v");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, owners_index);
  lua_pushvalue(L, owners_index);
  lua_setfield(L, LUA_REGISTRYINDEX, kTooltipOwnerWeakTableRegistryKey);
  return owners_index;
}

int DestroyLuaTooltipObjectState(lua_State* L) {
  auto* state = static_cast<LuaTooltipObjectState*>(lua_touserdata(L, 1));
  if (state != nullptr) {
    state->~LuaTooltipObjectState();
  }
  return 0;
}

void AttachLuaTooltipStateMetatable(lua_State* L, const int userdata_index) {

  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  lua_getfield(L, LUA_REGISTRYINDEX, kTooltipStateMetatableRegistryKey);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, DestroyLuaTooltipObjectState);
    lua_setfield(L, -2, "__gc");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kTooltipStateMetatableRegistryKey);
  }
  lua_setmetatable(L, userdata_index);
}

int DispatchLuaTooltipObjectMethod(lua_State* L) {
  auto* state = static_cast<LuaTooltipObjectState*>(
      lua_touserdata(L, lua_upvalueindex(2)));
  if (state == nullptr) {
    return luaL_error(L, "GameTooltip backing state is unavailable");
  }

  const int argument_count = lua_gettop(L);
  const std::uint64_t presentation_revision_before =
      state->tooltip.GetPresentationRevision();
  const std::uint64_t clear_generation_before =
      state->tooltip.GetClearGeneration();
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);

  int status = LUA_ERRRUN;
  {
    TooltipSystem::ScopedActivation activation(state->tooltip);
    status = lua_pcall(L, argument_count, LUA_MULTRET, 0);
    if (status == LUA_OK && state->tooltip.GetPresentationRevision() !=
                                presentation_revision_before) {
      state->SynchronizeRetainedFrame(
          state->tooltip.GetClearGeneration() != clear_generation_before);
    }
  }
  if (status != LUA_OK) {
    return lua_error(L);
  }
  return lua_gettop(L);
}

void LuaTooltipObjectState::SynchronizeRetainedFrame(
    const bool fire_data_event) {
  if (lua == nullptr || synchronizing) {
    return;
  }

  const int base = lua_gettop(lua);
  const int owners_index = PushTooltipOwnerWeakTable(lua);
  lua_pushlightuserdata(lua, this);
  lua_rawget(lua, owners_index);
  if (lua_istable(lua, -1) == 0) {
    lua_settop(lua, base);
    return;
  }

  synchronizing = true;
  const int tooltip_index = lua_absindex(lua, -1);
  {
    TooltipSystem::ScopedActivation activation(tooltip);
    FirePendingTooltipMoneyScript(lua, tooltip_index);
    auto presentation = CapturePresentation(tooltip);
    if (!published_presentation.has_value() ||
        *published_presentation != presentation) {
      if (!TooltipRetainedPresentationIsCurrent(lua, tooltip_index)) {
        SyncTooltipRegisteredLinesFromSystem(lua, tooltip_index);
      }
      published_presentation = std::move(presentation);
    }
    if (fire_data_event) {
      FireDataEvent();
    }
  }
  synchronizing = false;
  lua_settop(lua, base);
}

void LuaTooltipObjectState::FireDataEvent() {
  if (lua == nullptr) {
    return;
  }

  const int base = lua_gettop(lua);
  const int owners_index = PushTooltipOwnerWeakTable(lua);
  lua_pushlightuserdata(lua, this);
  lua_rawget(lua, owners_index);
  if (lua_istable(lua, -1) != 0) {
    const int tooltip_index = lua_absindex(lua, -1);
    if (tooltip.GetItemId() != 0u) {
      FireScript(lua, tooltip_index, "OnTooltipSetItem");
    } else if (tooltip.GetUnitGuid() != 0u) {
      FireScript(lua, tooltip_index, "OnTooltipSetUnit");
    } else if (tooltip.GetSpellId() != 0u) {
      FireScript(lua, tooltip_index, "OnTooltipSetSpell");
    } else if (tooltip.GetQuestId() != 0u) {
      FireScript(lua, tooltip_index, "OnTooltipSetQuest");
    } else if (tooltip.GetAchievementId() != 0u) {
      FireScript(lua, tooltip_index, "OnTooltipSetAchievement");
    }
  }
  lua_settop(lua, base);
}

}

void ApplyPerObjectGameTooltipMethods(lua_State* L, int object_index) {
  if (L == nullptr || lua_istable(L, object_index) == 0) {
    return;
  }
  object_index = lua_absindex(L, object_index);
  if (!PushRegisteredMethodTable(L, kGameTooltipMethodTableRegistryKey)) {
    return;
  }
  const int source_methods_index = lua_absindex(L, -1);

  void* const storage = lua_newuserdata(L, sizeof(LuaTooltipObjectState));
  auto* const state = new (storage) LuaTooltipObjectState(L);
  const int state_userdata_index = lua_absindex(L, -1);
  AttachLuaTooltipStateMetatable(L, state_userdata_index);

  if (!openwow::ui::lua::PublishMethodReceiver(
          L, object_index, state_userdata_index, &state->tooltip)) {
    lua_pop(L, 2);
    return;
  }

  const auto sources = openwow::ui::game::ResolveLuaTooltipDataSources(L);
  state->tooltip.BindDbcLoader(sources.dbc);
  state->tooltip.BindWorldSession(sources.world_session);
  state->tooltip.BindEquipmentSets(sources.equipment);
  state->tooltip.BindInventory(sources.inventory);
  state->tooltip.BindItemDefinitions(sources.item_definitions);

  const int owners_index = PushTooltipOwnerWeakTable(L);
  lua_pushlightuserdata(L, state);
  lua_pushvalue(L, object_index);
  lua_rawset(L, owners_index);
  lua_pop(L, 1);

  state->tooltip.SetContentChangedCallback(
      [state] { state->SynchronizeRetainedFrame(true); });

  lua_newtable(L);
  const int object_methods_index = lua_absindex(L, -1);
  lua_pushnil(L);
  while (lua_next(L, source_methods_index) != 0) {
    if (lua_isfunction(L, -1) != 0) {
      lua_pushvalue(L, -2);
      lua_pushvalue(L, -2);
      lua_pushvalue(L, state_userdata_index);
      lua_pushcclosure(L, DispatchLuaTooltipObjectMethod, 2);
      lua_rawset(L, object_methods_index);
    }
    lua_pop(L, 1);
  }
  SelfIndexMethodTable(L, object_methods_index);

  RemoveFunctionFieldsFromTable(L, object_index);
  lua_setmetatable(L, object_index);
  lua_pop(L, 2);
}

void UpdateLuaTooltipObjects(lua_State* L, const float elapsed_seconds) {
  if (L == nullptr) {
    return;
  }
  auto* const manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return;
  }

  struct LiveTooltip final {
    LuaTooltipObjectState* state{nullptr};
    int owner_ref{LUA_NOREF};
  };
  std::vector<LiveTooltip> live_tooltips;
  const int base = lua_gettop(L);
  const int owners_index = PushTooltipOwnerWeakTable(L);
  lua_pushnil(L);
  while (lua_next(L, owners_index) != 0) {
    if (lua_islightuserdata(L, -2) != 0 && lua_istable(L, -1) != 0) {
      auto* const state = static_cast<LuaTooltipObjectState*>(
          lua_touserdata(L, -2));
      lua_pushvalue(L, -1);
      live_tooltips.push_back({
          .state = state,
          .owner_ref = luaL_ref(L, LUA_REGISTRYINDEX),
      });
    }
    lua_pop(L, 1);
  }
  lua_settop(L, base);

  for (const auto& live : live_tooltips) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, live.owner_ref);
    if (live.state != nullptr && lua_istable(L, -1) != 0 &&
        live.state->tooltip.IsFading()) {
      const bool reached_hidden =
          live.state->tooltip.AdvanceFade(elapsed_seconds);
      const float alpha = reached_hidden
          ? 1.0F
          : std::clamp(live.state->tooltip.GetFadeTimer(), 0.0F, 1.0F);
      const auto alpha_byte = QuantizeFrameAlphaByteTruncated(alpha);
      openwow::ui::WriteLuaNumberField(
          L, -1, "__ow_alpha", NormalizeFrameAlphaByte(alpha_byte));
      if (const char* const frame_key = GetFrameRuntimeKeyOrName(L, -1);
          frame_key != nullptr && frame_key[0] != '\0') {
        manager->frame_store().SetFrameAlphaByte(frame_key, alpha_byte);
      }
      if (reached_hidden) {
        live.state->SynchronizeRetainedFrame(false);
      }
    }
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, live.owner_ref);
  }
}

}

#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/core/frame_layout_methods.h"
#include "openwow/ui/game/framescript/widgets/scrolling_widget_methods.h"
#include "openwow/ui/lua_table_field.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

static int ReadLuaFrameLevel(lua_State* L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kLuaFrameLevelField);
  int level = 0;
  if (lua_isnumber(L, -1) != 0) {
    level = static_cast<int>(lua_tonumber(L, -1));
  }
  lua_pop(L, 1);
  return level;
}

static void StoreLuaFrameLevel(lua_State* L, int frame_idx, int level) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_pushinteger(L, static_cast<lua_Integer>(level));
  lua_setfield(L, frame_idx, kLuaFrameLevelField);
}

static std::string ReadLuaFrameStrata(lua_State* L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kLuaFrameStrataField);
  const char* stored = lua_tostring(L, -1);
  std::string strata = stored != nullptr ? stored : "";
  lua_pop(L, 1);
  if (strata.empty()) {
    return "MEDIUM";
  }
  return strata;
}

static void StoreLuaFrameStrata(lua_State* L, int frame_idx,
                                const char* canonical_strata) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_pushstring(L, canonical_strata);
  lua_setfield(L, frame_idx, kLuaFrameStrataField);
}

static void SetLuaFrameStrataCascade(lua_State* L, int frame_idx,
                                     const char* canonical_strata) {
  frame_idx = lua_absindex(L, frame_idx);
  if (ReadLuaFrameStrata(L, frame_idx) == canonical_strata) {
    return;
  }

  StoreLuaFrameStrata(L, frame_idx, canonical_strata);
  NotifyFrameInputMutation(L, frame_idx, true);
  RefreshLuaFrameOnUpdateRegistration(L, frame_idx);

  lua_getfield(L, frame_idx, "__ow_children");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const lua_Integer child_count = luaL_len(L, -1);
  for (lua_Integer child_index = 1; child_index <= child_count; ++child_index) {
    lua_geti(L, -1, child_index);
    if (lua_istable(L, -1) != 0 &&
        IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, -1))) {
      SetLuaFrameStrataCascade(L, lua_absindex(L, -1), canonical_strata);
    }
    lua_pop(L, 1);
  }

  lua_pop(L, 1);
}

static void SetLuaFrameLevelCascade(lua_State* L, int frame_idx,
                                    int requested_level,
                                    bool propagate_children) {
  frame_idx = lua_absindex(L, frame_idx);
  const int requested = std::max(requested_level, 0);
  const int current = ReadLuaFrameLevel(L, frame_idx);
  if (requested == current) {
    return;
  }

  int delta = requested - current;
  if (delta > 128) {
    delta = 128;
  }

  StoreLuaFrameLevel(L, frame_idx, current + delta);
  NotifyFrameInputMutation(L, frame_idx, true);
  RefreshLuaFrameOnUpdateRegistration(L, frame_idx);
  if (!propagate_children) {
    return;
  }

  const std::string parent_strata = ReadLuaFrameStrata(L, frame_idx);
  lua_getfield(L, frame_idx, "__ow_children");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const lua_Integer child_count = luaL_len(L, -1);
  for (lua_Integer child_index = 1; child_index <= child_count; ++child_index) {
    lua_geti(L, -1, child_index);
    if (lua_istable(L, -1) != 0 &&
        IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, -1)) &&
        ReadLuaFrameStrata(L, -1) == parent_strata) {
      const int child_level = ReadLuaFrameLevel(L, -1);
      SetLuaFrameLevelCascade(L, lua_absindex(L, -1), child_level + delta, true);
    }
    lua_pop(L, 1);
  }

  lua_pop(L, 1);
}

static void SyncLuaFrameParentOrdering(lua_State* L, int self_idx,
                                       int parent_idx) {
  self_idx = lua_absindex(L, self_idx);
  if (parent_idx != 0) {
    parent_idx = lua_absindex(L, parent_idx);
    const std::string parent_strata = ReadLuaFrameStrata(L, parent_idx);
    SetLuaFrameStrataCascade(L, self_idx, parent_strata.c_str());
    SetLuaFrameLevelCascade(L, self_idx, ReadLuaFrameLevel(L, parent_idx) + 1, true);
    return;
  }

  SetLuaFrameStrataCascade(L, self_idx, "MEDIUM");
  SetLuaFrameLevelCascade(L, self_idx, 0, true);
}

static bool ScriptFrameRectsOverlap(const ScriptFrameDeviceRect& lhs,
                                    const ScriptFrameDeviceRect& rhs) {
  return lhs.left < rhs.right() && lhs.right() > rhs.left &&
         lhs.bottom < rhs.top() && lhs.top() > rhs.bottom;
}

static int PushNearestToplevelFrame(lua_State* L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_pushvalue(L, frame_idx);
  while (lua_istable(L, -1) != 0) {
    if (openwow::ui::ReadLuaBooleanFieldOrDefault(L, -1, "__ow_toplevel", false)) {
      return lua_absindex(L, -1);
    }

    lua_getfield(L, -1, "__ow_parent");
    if (lua_istable(L, -1) == 0 ||
        !IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, -1))) {
      lua_pop(L, 2);
      return 0;
    }
    lua_remove(L, -2);
  }

  lua_pop(L, 1);
  return 0;
}

static bool LuaFrameIsDescendantOf(lua_State* L, int frame_idx, int ancestor_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  ancestor_idx = lua_absindex(L, ancestor_idx);
  lua_getfield(L, frame_idx, "__ow_parent");
  while (lua_istable(L, -1) != 0 &&
         IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, -1))) {
    if (lua_rawequal(L, -1, ancestor_idx) != 0) {
      lua_pop(L, 1);
      return true;
    }

    lua_getfield(L, -1, "__ow_parent");
    lua_remove(L, -2);
  }

  lua_pop(L, 1);
  return false;
}

static bool TrackedFrameUsesStrata(
    const openwow::ui::framexml::UiFrame& frame,
    const std::string_view strata) {

  return (frame.frame_strata.empty() ? std::string_view("MEDIUM")
                                     : std::string_view(frame.frame_strata)) ==
         strata;
}

static std::vector<int> CollectVisibleOccupiedFrameLevels(lua_State* L,
                                                          runtime::WorldUiRuntimeContext* manager,
                                                          const std::string& strata) {
  std::vector<int> occupied_levels;
  if (L == nullptr || manager == nullptr) {
    return occupied_levels;
  }

  const auto frames = manager->frame_store().CollectStableFrames();
  occupied_levels.reserve(frames.size());
  const int top = lua_gettop(L);
  for (const auto& view : frames) {
    const auto& frame = *view.frame;
    if (!TrackedFrameUsesStrata(frame, strata)) {
      continue;
    }

    if (view.lua_ref == LUA_NOREF) {
      continue;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, view.lua_ref);
    if (lua_istable(L, -1) != 0 && IsLuaTableEffectivelyVisible(L, -1)) {
      occupied_levels.push_back(ReadLuaFrameLevel(L, -1));
    }
    lua_settop(L, top);
  }

  std::sort(occupied_levels.begin(), occupied_levels.end());
  occupied_levels.erase(std::unique(occupied_levels.begin(), occupied_levels.end()),
                        occupied_levels.end());
  return occupied_levels;
}

static void CompactVisibleFrameLevels(lua_State* L,
                                      runtime::WorldUiRuntimeContext* manager,
                                      const std::string& strata) {
  if (L == nullptr || manager == nullptr) {
    return;
  }

  const std::vector<int> occupied_levels =
      CollectVisibleOccupiedFrameLevels(L, manager, strata);
  if (occupied_levels.empty()) {
    return;
  }

  const int top = lua_gettop(L);
  for (const auto& view : manager->frame_store().CollectStableFrames()) {
    const auto& frame = *view.frame;
    if (!TrackedFrameUsesStrata(frame, strata)) {
      continue;
    }

    if (view.lua_ref == LUA_NOREF) {
      continue;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, view.lua_ref);
    if (lua_istable(L, -1) == 0 || !IsLuaTableEffectivelyVisible(L, -1)) {
      lua_settop(L, top);
      continue;
    }

    const int current_level = ReadLuaFrameLevel(L, -1);
    const auto level_it =
        std::lower_bound(occupied_levels.begin(), occupied_levels.end(), current_level);
    if (level_it != occupied_levels.end() && *level_it == current_level) {
      StoreLuaFrameLevel(L, -1, static_cast<int>(level_it - occupied_levels.begin()));
      NotifyFrameInputMutation(L, lua_absindex(L, -1), true);
    }
    lua_settop(L, top);
  }
}

int LuaFrame_Raise(lua_State* L) {
  const int self_idx = ValidateFrameSelf(L);
  if (!detail::AllowLuaFrameProtectedMutation(L, self_idx)) {
    return 0;
  }
  const int target_idx = PushNearestToplevelFrame(L, self_idx);
  if (target_idx == 0) {
    return 0;
  }

  auto* manager = runtime::WorldUiRuntimeContext::FromLua(L);
  ScriptFrameDeviceRect target_rect{};

  const bool has_target_rect =
      TryGetScriptFrameDeviceRect(L, target_idx, &target_rect);
  const std::string target_strata = ReadLuaFrameStrata(L, target_idx);
  const int target_level = ReadLuaFrameLevel(L, target_idx);

  bool has_intersecting_visible_peer = false;
  if (manager != nullptr && has_target_rect) {
    const int top = lua_gettop(L);
    for (const auto& view : manager->frame_store().CollectStableFrames()) {
      const auto& peer_frame = *view.frame;
      if (!TrackedFrameUsesStrata(peer_frame, target_strata) ||
          peer_frame.frame_level < target_level) {
        continue;
      }

      if (view.lua_ref == LUA_NOREF) {
        continue;
      }

      lua_rawgeti(L, LUA_REGISTRYINDEX, view.lua_ref);
      if (lua_istable(L, -1) == 0 || !IsLuaTableEffectivelyVisible(L, -1) ||
          lua_rawequal(L, -1, target_idx) != 0 ||
          LuaFrameIsDescendantOf(L, lua_absindex(L, -1), target_idx)) {
        lua_settop(L, top);
        continue;
      }

      ScriptFrameDeviceRect peer_rect{};
      if (TryGetScriptFrameDeviceRect(L, lua_absindex(L, -1), &peer_rect) &&
          ScriptFrameRectsOverlap(target_rect, peer_rect)) {
        has_intersecting_visible_peer = true;
        lua_settop(L, top);
        break;
      }
      lua_settop(L, top);
    }
  }

  if (has_intersecting_visible_peer && manager != nullptr) {
    const std::vector<int> occupied_levels =
        CollectVisibleOccupiedFrameLevels(L, manager, target_strata);
    CompactVisibleFrameLevels(L, manager, target_strata);
    SetLuaFrameLevelCascade(L, target_idx, static_cast<int>(occupied_levels.size()), true);
  }

  lua_pop(L, 1);
  return 0;
}

int LuaFrame_Lower(lua_State* L) {
  const int self_idx = ValidateFrameSelf(L);
  if (!detail::AllowLuaFrameProtectedMutation(L, self_idx)) {
    return 0;
  }

  return 0;
}

int LuaFrame_SetFrameStrata(lua_State* L) {
  ValidateFrameSelf(L);
  if (detail::LuaFrameMutationBlocked(L, 1)) {
    return 0;
  }
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:SetFrameStrata(level)", lua_adapter::ScriptObjectDisplayName(L, 1));
  }

  const char* raw_strata = lua_tostring(L, 2);
  int strata_value = 0;
  if (openwow::ui::StringToScriptFrameStrata(raw_strata, &strata_value) == 0) {
    return luaL_error(L, "%s:SetFrameStrata(): Unknown frame strata: %s",
                      lua_adapter::ScriptObjectDisplayName(L, 1), raw_strata != nullptr ? raw_strata : "");
  }

  SetLuaFrameStrataCascade(L, 1, openwow::ui::ScriptFrameStrataToString(strata_value));
  return 0;
}

int LuaFrame_GetFrameStrata(lua_State* L) {
  ValidateFrameSelf(L);

  lua_getfield(L, 1, kLuaFrameStrataField);
  const char* stored = lua_tostring(L, -1);
  int strata_value = 0;
  if (stored != nullptr && *stored != '\0' &&
      openwow::ui::StringToScriptFrameStrata(stored, &strata_value) != 0) {
    lua_pop(L, 1);
    lua_pushstring(L, openwow::ui::ScriptFrameStrataToString(strata_value));
    return 1;
  }

  const bool has_invalid_value = stored != nullptr && *stored != '\0';
  lua_pop(L, 1);
  lua_pushstring(L, has_invalid_value ? "UNKNOWN" : "MEDIUM");
  return 1;
}

int LuaFrame_SetFrameLevel(lua_State* L) {
  ValidateFrameSelf(L);
  if (detail::LuaFrameMutationBlocked(L, 1)) {
    return 0;
  }
  if (lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:SetFrameLevel(level)", lua_adapter::ScriptObjectDisplayName(L, 1));
  }

  const int level = static_cast<int>(lua_tonumber(L, 2));
  if (level < 0) {
    return luaL_error(L, "%s:SetFrameLevel(): Passed negative frame level: %d",
                      lua_adapter::ScriptObjectDisplayName(L, 1), level);
  }

  SetLuaFrameLevelCascade(L, 1, level, true);
  return 0;
}

int LuaFrame_GetFrameLevel(lua_State* L) {
  ValidateFrameSelf(L);
  lua_pushinteger(L, ReadLuaFrameLevel(L, 1));
  return 1;
}

static int PushNamedFrameLikeScriptObject(lua_State* L, const char* object_name) {
  lua_getglobal(L, object_name != nullptr ? object_name : "");
  if (lua_istable(L, -1) == 0 ||
      !openwow::ui::game::detail::HasLuaScriptObjectThis(L, -1) ||
      !IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, -1))) {
    lua_pop(L, 1);
    return 0;
  }

  return lua_absindex(L, -1);
}

static bool WouldCreateParentLoop(lua_State* L, int self_idx, int new_parent_idx) {
  self_idx = lua_absindex(L, self_idx);
  new_parent_idx = lua_absindex(L, new_parent_idx);

  const int traversal_top = lua_gettop(L) + 1;
  lua_pushvalue(L, new_parent_idx);
  for (int depth = 0; depth < 100 && lua_istable(L, -1) != 0; ++depth) {
    if (lua_rawequal(L, -1, self_idx) != 0) {
      while (lua_gettop(L) >= traversal_top) {
        lua_pop(L, 1);
      }
      return true;
    }

    lua_getfield(L, -1, "__ow_parent");
    if (lua_istable(L, -1) == 0) {
      lua_pop(L, 2);
      break;
    }
    lua_remove(L, -2);
  }

  while (lua_gettop(L) >= traversal_top) {
    lua_pop(L, 1);
  }
  return false;
}

static bool WouldCreateScrollChildLoop(lua_State* L, int self_idx, int child_idx) {
  self_idx = lua_absindex(L, self_idx);
  child_idx = lua_absindex(L, child_idx);

  const int traversal_top = lua_gettop(L) + 1;
  lua_pushvalue(L, self_idx);
  for (int depth = 0; depth < 100 && lua_istable(L, -1) != 0; ++depth) {
    if (lua_rawequal(L, -1, child_idx) != 0) {
      while (lua_gettop(L) >= traversal_top) {
        lua_pop(L, 1);
      }
      return true;
    }

    lua_getfield(L, -1, "__ow_parent");
    if (lua_istable(L, -1) == 0) {
      lua_pop(L, 2);
      break;
    }
    lua_remove(L, -2);
  }

  while (lua_gettop(L) >= traversal_top) {
    lua_pop(L, 1);
  }
  return false;
}

static void SetLuaScrollChildTable(lua_State* L, int self_idx, int new_child_idx) {
  self_idx = lua_absindex(L, self_idx);
  new_child_idx = new_child_idx != 0 ? lua_absindex(L, new_child_idx) : 0;

  lua_getfield(L, self_idx, "__ow_sf_child");
  if (lua_istable(L, -1) != 0 &&
      IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, -1))) {
    const int old_child_idx = lua_absindex(L, -1);
    if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
      manager->frame_store().SetScrollChildSubtree(
          openwow::ui::BorrowRawLuaStringField(L, old_child_idx, kLuaFrameRuntimeKeyField),
          false);
    }
    ReparentScriptObjectTable(L, old_child_idx, 0);
    SyncLuaFrameParentOrdering(L, old_child_idx, 0);
    NotifyFrameInputMutation(L, old_child_idx, false);
  }
  lua_pop(L, 1);

  if (new_child_idx == 0) {
    lua_pushnil(L);
    lua_setfield(L, self_idx, "__ow_sf_child");
    return;
  }

  lua_pushvalue(L, new_child_idx);
  lua_setfield(L, self_idx, "__ow_sf_child");
  ReparentScriptObjectTable(L, new_child_idx, self_idx);
  SyncLuaFrameParentOrdering(L, new_child_idx, self_idx);
  NotifyFrameInputMutation(L, new_child_idx, false);
  if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    manager->frame_store().SetScrollChildSubtree(
        openwow::ui::BorrowRawLuaStringField(L, new_child_idx, kLuaFrameRuntimeKeyField), true);
  }
}

int LuaScriptObject_SetParent(lua_State* L) {
  const int self_idx = ValidateParentableScriptObjectSelf(L);
  if (detail::LuaFrameMutationBlocked(L, self_idx)) {
    return 0;
  }

  const auto self_type = GetLuaScriptObjectType(L, self_idx);
  int new_parent_idx = 0;
  bool pushed_parent_table = false;

  if (lua_isnoneornil(L, 2) != 0) {
    if (IsNilParentForbiddenScriptObjectType(self_type)) {
      return luaL_error(
          L, "%s:SetParent(): Cannot set a 'nil' parent for fonts or textures",
          lua_adapter::ScriptObjectDisplayName(L, self_idx));
    }
  } else if (lua_isstring(L, 2) != 0) {
    const char* target_name = lua_tostring(L, 2);
    new_parent_idx = PushNamedFrameLikeScriptObject(L, target_name);
    if (new_parent_idx == 0) {
      return luaL_error(L, "%s:SetParent(): Couldn't find region named '%s'",
                        lua_adapter::ScriptObjectDisplayName(L, self_idx),
                        target_name != nullptr ? target_name : "");
    }

    pushed_parent_table = true;
  } else if (lua_istable(L, 2) != 0) {
    (void)openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable(L, 2);
    if (!openwow::ui::game::detail::HasLuaScriptObjectThis(L, 2)) {
      return luaL_error(L, "%s:SetParent(): Couldn't find 'this' in parent object",
                        lua_adapter::ScriptObjectDisplayName(L, self_idx));
    }

    const auto parent_type = GetLuaScriptObjectType(L, 2);
    if (!IsFrameLikeScriptObjectType(parent_type)) {
      return luaL_error(L, "%s:SetParent(): Wrong parent object type, expected Frame",
                        lua_adapter::ScriptObjectDisplayName(L, self_idx));
    }

    new_parent_idx = lua_absindex(L, 2);
  } else {
    const char* target_name = lua_tostring(L, 2);
    return luaL_error(L, "%s:SetParent(): Couldn't find region named '%s'",
                      lua_adapter::ScriptObjectDisplayName(L, self_idx),
                      target_name != nullptr ? target_name : "");
  }

  if (new_parent_idx != 0 && WouldCreateParentLoop(L, self_idx, new_parent_idx)) {
    const char* parent_name =
        lua_adapter::ScriptObjectDisplayName(L, new_parent_idx);
    return luaL_error(L, "%s:SetParent(): Would create a loop parenting to %s",
                      lua_adapter::ScriptObjectDisplayName(L, self_idx),
                      parent_name);
  }

  ReparentScriptObjectTable(L, self_idx, new_parent_idx);
  if (IsFrameLikeScriptObjectType(self_type)) {
    SyncLuaFrameParentOrdering(L, self_idx, new_parent_idx);
  } else {
    SyncRegionDrawLayerEnabled(L, self_idx);
  }

  if (pushed_parent_table) {
    lua_pop(L, 1);
  }
  openwow::ui::game::detail::BumpLuaLayoutProtectionGeneration(L);
  NotifyFrameInputMutation(L, self_idx, false);
  return 0;
}

int LuaScriptObject_GetParent(lua_State* L) {
  const int self_idx = ValidateParentableScriptObjectSelf(L);
  lua_getfield(L, self_idx, "__ow_parent");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    lua_pushnil(L);
  }
  return 1;
}

bool StoreLuaFrameScaleAndInvalidate(lua_State* L, int frame_index, float scale) {
  if (L == nullptr || lua_istable(L, frame_index) == 0) {
    return false;
  }

  frame_index = lua_absindex(L, frame_index);

  const float current_scale =
      static_cast<float>(ReadStoredFrameScaleField(L, frame_index));

  constexpr float kNativeFrameScaleChangeEpsilon = 0x1p-22F;
  if (!(std::fabs(scale - current_scale) >= kNativeFrameScaleChangeEpsilon) ||
      scale == 0.0F) {
    return false;
  }

  openwow::ui::WriteLuaNumberField(L, frame_index, "__ow_scale", scale);
  NotifyFrameInputMutation(L, frame_index, false);
  return true;
}

int LuaScrollFrame_SetScrollChild(lua_State* L) {
  const int self_idx = ValidateFrameObjectSelf(L, "ScrollFrame");
  if (!detail::AllowLuaFrameProtectedMutation(L, self_idx)) {
    return 0;
  }

  int new_child_idx = 0;
  bool pushed_child_table = false;

  if (lua_isnoneornil(L, 2) == 0) {
    if (lua_isstring(L, 2) != 0) {
      const char* child_name = lua_tostring(L, 2);
      new_child_idx = PushNamedFrameLikeScriptObject(L, child_name);
      if (new_child_idx == 0) {
        return luaL_error(L, "%s:SetScrollChild(): Couldn't find frame named '%s'",
                          lua_adapter::ScriptObjectDisplayName(L, self_idx),
                          child_name != nullptr ? child_name : "");
      }

      pushed_child_table = true;
    } else if (lua_istable(L, 2) != 0) {
      if (!openwow::ui::game::detail::HasLuaScriptObjectThis(L, 2)) {
        return luaL_error(L, "%s:SetScrollChild(): Couldn't find 'this' in child object",
                          lua_adapter::ScriptObjectDisplayName(L, self_idx));
      }

      if (!IsFrameLikeScriptObjectType(GetLuaScriptObjectType(L, 2))) {
        return luaL_error(L, "%s:SetScrollChild(): Wrong child object type, expected frame",
                          lua_adapter::ScriptObjectDisplayName(L, self_idx));
      }

      new_child_idx = lua_absindex(L, 2);
    } else {
      const char* child_name = lua_tostring(L, 2);
      return luaL_error(L, "%s:SetScrollChild(): Couldn't find frame named '%s'",
                        lua_adapter::ScriptObjectDisplayName(L, self_idx),
                        child_name != nullptr ? child_name : "");
    }

    if (WouldCreateScrollChildLoop(L, self_idx, new_child_idx)) {
      const char* child_name =
          lua_adapter::ScriptObjectDisplayName(L, new_child_idx);
      return luaL_error(L, "%s:SetScrollChild(): Would create a loop adding child %s",
                        lua_adapter::ScriptObjectDisplayName(L, self_idx),
                        child_name);
    }
  }

  SetLuaScrollChildTable(L, self_idx, new_child_idx);
  openwow::ui::game::detail::BumpLuaLayoutProtectionGeneration(L);
  if (pushed_child_table) {
    lua_pop(L, 1);
  }
  return 0;
}

}

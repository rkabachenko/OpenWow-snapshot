#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_taint_api.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_post_hook_closure.h"
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

static int ValidateFrameScriptHandlerSelf(lua_State* L) {
  return ValidateFrameScriptSelf(L);
}

static std::string FrameScriptTaintFieldName(const char* canonical_name) {
  std::string key(openwow::ui::game::kLuaFrameScriptTaintFieldPrefix);
  key += canonical_name != nullptr ? canonical_name : "";
  return key;
}

static std::string FrameScriptStorageFieldName(const char* canonical_name) {
  std::string key = "__ow_script_";
  key += canonical_name != nullptr ? canonical_name : "";
  return key;
}

static void PushLuaFrameScript(lua_State* L, const int frame_index,
                               const char* canonical_name) {
  const int abs_index = lua_absindex(L, frame_index);
  const std::string key = FrameScriptStorageFieldName(canonical_name);
  lua_getfield(L, abs_index, key.c_str());
  if (lua_isfunction(L, -1) != 0) {
    return;
  }
  lua_pop(L, 1);
  lua_getfield(L, abs_index, canonical_name);
}

static void StoreLuaFrameScript(lua_State* L, const int frame_index,
                                const char* canonical_name,
                                const int value_index) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  const int abs_index = lua_absindex(L, frame_index);
  const int abs_value = lua_absindex(L, value_index);
  const std::string key = FrameScriptStorageFieldName(canonical_name);
  lua_pushvalue(L, abs_value);
  lua_setfield(L, abs_index, key.c_str());
}

static void StoreLuaFrameScriptTaintSource(
    lua_State* L, const int frame_index, const char* canonical_name,
    const bool has_handler, const openwow::ui::game::TaintSourceId source) {
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
  const int abs_index = lua_absindex(L, frame_index);
  const std::string key = FrameScriptTaintFieldName(canonical_name);
  if (!has_handler) {
    lua_pushnil(L);
  } else {
    lua_pushinteger(L, source);
  }
  lua_setfield(L, abs_index, key.c_str());
}

static const openwow::ui::FrameScriptTypeInfo* ResolveFrameScriptTypeInfoForSelf(
    lua_State* L, const int self_index, const int handler_index) {
  const char* type_name = openwow::ui::BorrowRawLuaStringField(L, self_index, "__ow_type");
  const char* handler_name = lua_tostring(L, handler_index);
  if (type_name == nullptr || *type_name == '\0' || handler_name == nullptr ||
      *handler_name == '\0') {
    return nullptr;
  }

  return openwow::ui::LookupFrameScriptTypeInfo(type_name, handler_name);
}

static std::uint8_t ReadLuaFrameStrataForOnUpdate(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, kLuaFrameStrataField);
  const char* stored = lua_tostring(L, -1);
  int strata = 3;
  if (stored != nullptr && *stored != '\0') {
    (void)openwow::ui::StringToScriptFrameStrata(stored, &strata);
  }
  lua_pop(L, 1);
  return static_cast<std::uint8_t>(strata);
}

static std::int32_t ReadLuaFrameLevelForOnUpdate(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, kLuaFrameLevelField);
  const std::int32_t level =
      lua_isnumber(L, -1) != 0 ? static_cast<std::int32_t>(lua_tonumber(L, -1)) : 0;
  lua_pop(L, 1);
  return level;
}

static bool LuaFrameHasOnUpdate(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  PushLuaFrameScript(L, frame_index, "OnUpdate");
  const bool has_on_update = lua_isfunction(L, -1) != 0;
  lua_pop(L, 1);
  return has_on_update;
}

static void SyncLuaFrameOnUpdateRegistration(lua_State* L, int frame_index,
                                             const bool enabled) {
  if (auto* mgr = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L); mgr != nullptr) {
    const char* frame_key = GetFrameRuntimeKeyOrName(L, frame_index);
    if (frame_key == nullptr || *frame_key == '\0') {
      return;
    }
    if (enabled) {
      mgr->frame_events().RegisterOnUpdate(
          frame_key, ReadLuaFrameStrataForOnUpdate(L, frame_index),
          ReadLuaFrameLevelForOnUpdate(L, frame_index));
    } else {
      mgr->frame_events().UnregisterOnUpdate(frame_key);
    }
  }
}

void RefreshLuaFrameOnUpdateRegistration(lua_State* L, int frame_index) {
  SyncLuaFrameOnUpdateRegistration(L, frame_index, LuaFrameHasOnUpdate(L, frame_index));
}

static int LuaFrame_SetScript(lua_State* L) {
  const int self_index = ValidateFrameScriptHandlerSelf(L);
  const int arg3_type = lua_type(L, 3);
  if (lua_isstring(L, 2) == 0 ||
      (arg3_type != LUA_TFUNCTION && arg3_type != LUA_TNIL)) {
    return luaL_error(L, "Usage: %s:SetScript(\"type\", function)",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const auto* script_info = ResolveFrameScriptTypeInfoForSelf(L, self_index, 2);
  if (script_info == nullptr) {
    const char* handler_name = lua_tostring(L, 2);
    return luaL_error(L, "%s doesn't have a \"%s\" script",
                      lua_adapter::ScriptObjectDisplayName(L, self_index),
                      handler_name != nullptr ? handler_name : "");
  }

  const auto caller_source =
      openwow::ui::lua_get_execution_taint_state(L).source;
  if (arg3_type == LUA_TFUNCTION) {
    StoreLuaFrameScript(L, self_index, script_info->canonical_name, 3);
  } else {
    lua_pushnil(L);
    StoreLuaFrameScript(L, self_index, script_info->canonical_name, -1);
    lua_pop(L, 1);
  }
  StoreLuaFrameScriptTaintSource(L, self_index, script_info->canonical_name,
                                 arg3_type == LUA_TFUNCTION, caller_source);

  if (std::strcmp(script_info->canonical_name, "OnUpdate") == 0) {
    SyncLuaFrameOnUpdateRegistration(L, self_index, arg3_type == LUA_TFUNCTION);
  }

  NotifyFrameInputMutation(L, self_index, true);

  return 0;
}

static int LuaFrame_GetScript(lua_State* L) {
  const int self_index = ValidateFrameScriptHandlerSelf(L);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:GetScript(\"type\")",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const auto* script_info = ResolveFrameScriptTypeInfoForSelf(L, self_index, 2);
  if (script_info == nullptr) {
    const char* handler_name = lua_tostring(L, 2);
    return luaL_error(L, "%s doesn't have a \"%s\" script",
                      lua_adapter::ScriptObjectDisplayName(L, self_index),
                      handler_name != nullptr ? handler_name : "");
  }

  PushLuaFrameScript(L, self_index, script_info->canonical_name);
  if (lua_isfunction(L, -1) == 0) {
    lua_pop(L, 1);
    lua_pushnil(L);
  }
  return 1;
}

static int LuaFrame_HookScript(lua_State* L) {
  const int self_index = ValidateFrameScriptHandlerSelf(L);
  if (lua_isstring(L, 2) == 0 || lua_type(L, 3) != LUA_TFUNCTION) {
    return luaL_error(L, "Usage: %s:HookScript(\"type\", function)",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const auto* script_info = ResolveFrameScriptTypeInfoForSelf(L, self_index, 2);
  if (script_info == nullptr) {
    const char* handler_name = lua_tostring(L, 2);
    return luaL_error(L, "%s doesn't have a \"%s\" script",
                      lua_adapter::ScriptObjectDisplayName(L, self_index),
                      handler_name != nullptr ? handler_name : "");
  }

  const auto caller_taint = openwow::ui::lua_get_execution_taint_state(L);

  int inherited_source = 0;
  {
    const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(L);
    inherited_source = openwow::ui::game::FrameScriptHandlerTaintSource(
        L, self_index, script_info->canonical_name);
    PushLuaFrameScript(L, self_index, script_info->canonical_name);
  }
  if (lua_isfunction(L, -1) != 0) {
    lua_pushvalue(L, 3);

    openwow::ui::lua_set_taint(L, -2, inherited_source);
    openwow::ui::lua_set_taint(L, -1, caller_taint.source);
    openwow::ui::PushLuaCallOriginalThenHookClosure<ProfiledPCall>(
        L, openwow::ui::kGameLuaErrorHandlerRegistryKey);
    openwow::ui::lua_set_taint(L, -1, inherited_source);
    StoreLuaFrameScript(L, self_index, script_info->canonical_name, -1);
    lua_pop(L, 1);
    StoreLuaFrameScriptTaintSource(L, self_index, script_info->canonical_name,
                                   true, inherited_source);
  } else {

    lua_pop(L, 1);
    StoreLuaFrameScript(L, self_index, script_info->canonical_name, 3);
    StoreLuaFrameScriptTaintSource(L, self_index, script_info->canonical_name,
                                   true, caller_taint.source);
  }

  if (std::strcmp(script_info->canonical_name, "OnUpdate") == 0) {
    SyncLuaFrameOnUpdateRegistration(L, self_index, true);
  }
  NotifyFrameInputMutation(L, self_index, true);

  return 0;
}

int LuaFrame_HasScript(lua_State* L) {
  const int self_index = ValidateFrameScriptHandlerSelf(L);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:HasScript(\"type\")",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  if (ResolveFrameScriptTypeInfoForSelf(L, self_index, 2) != nullptr) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

void ApplyFrameScriptHandlerMethods(lua_State* L, int table_index,
                                           const bool include_hook_script) {
  table_index = lua_absindex(L, table_index);

  lua_pushcfunction(L, LuaFrame_SetScript);
  lua_setfield(L, table_index, "SetScript");

  lua_pushcfunction(L, LuaFrame_GetScript);
  lua_setfield(L, table_index, "GetScript");

  if (include_hook_script) {
    lua_pushcfunction(L, LuaFrame_HookScript);
    lua_setfield(L, table_index, "HookScript");
  }

  lua_pushcfunction(L, LuaFrame_HasScript);
  lua_setfield(L, table_index, "HasScript");
}

static const char* ParentLinkArrayField(
    const openwow::ui::widgets::ScriptObjectType type) {
  switch (GetParentLinkArrayKind(type)) {
    case ParentLinkArrayKind::Children:
      return "__ow_children";
    case ParentLinkArrayKind::Regions:
      return "__ow_regions";
    default:
      return nullptr;
  }
}

bool IsNilParentForbiddenScriptObjectType(
    const openwow::ui::widgets::ScriptObjectType type) {
  using openwow::ui::widgets::ScriptObjectType;
  return type == ScriptObjectType::FontString || type == ScriptObjectType::Texture;
}

int ValidateParentableScriptObjectSelf(lua_State* L) {
  if (lua_istable(L, 1) == 0) {
    return luaL_error(
        L,
        "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  (void)openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable(L, 1);

  const char* type_name = openwow::ui::BorrowRawLuaStringField(L, 1, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    return luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }

  return lua_absindex(L, 1);
}

static void AppendToChildren(lua_State* L, int parent_idx);

void ReparentScriptObjectTable(lua_State* L, int self_idx,
                                      int new_parent_idx) {
  self_idx = lua_absindex(L, self_idx);
  new_parent_idx = new_parent_idx != 0 ? lua_absindex(L, new_parent_idx) : 0;

  const auto self_type = GetLuaScriptObjectType(L, self_idx);
  const char* const array_field = ParentLinkArrayField(self_type);

  lua_getfield(L, self_idx, "__ow_parent");
  const bool has_old_parent = lua_istable(L, -1) != 0;
  const int old_parent_idx = has_old_parent ? lua_absindex(L, -1) : 0;
  const bool same_parent =
      has_old_parent && new_parent_idx != 0 && lua_rawequal(L, old_parent_idx, new_parent_idx) != 0;

  if (same_parent) {
    lua_pop(L, 1);
    return;
  }

  std::optional<detail::LuaVisibilitySnapshot> visibility;
  if (IsFrameLikeScriptObjectType(self_type)) {
    visibility.emplace(detail::CaptureLuaVisibilitySubtree(L, self_idx));
  }

  if (has_old_parent && array_field != nullptr) {
    RemoveExactValueFromArrayField(L, old_parent_idx, array_field, self_idx);
  }

  const bool was_effectively_visible =
      visibility.has_value() && visibility->RootWasVisible();
  if (was_effectively_visible) {

    detail::DispatchForcedLuaVisibilityTransition(&*visibility, false);
  }

  if (new_parent_idx != 0) {
    lua_pushvalue(L, new_parent_idx);
    lua_setfield(L, self_idx, "__ow_parent");

    if (array_field != nullptr &&
        !ArrayFieldContainsExactValue(L, new_parent_idx, array_field, self_idx)) {
      lua_pushvalue(L, self_idx);
      if (GetParentLinkArrayKind(self_type) == ParentLinkArrayKind::Regions) {
        PrependToRegions(L, new_parent_idx);
      } else {
        AppendToChildren(L, new_parent_idx);
      }

      lua_pop(L, 1);
    }
  } else {
    lua_pushnil(L);
    lua_setfield(L, self_idx, "__ow_parent");
  }

  lua_pop(L, 1);
  if (visibility.has_value()) {
    if (was_effectively_visible) {
      auto shown_visibility = detail::CaptureLuaVisibilitySubtree(L, self_idx);
      detail::DispatchForcedLuaVisibilityTransition(&shown_visibility, true);
      detail::ClearLuaEffectiveVisibilityOverrides(&shown_visibility);
    } else {
      detail::DispatchLuaVisibilityTransitions(&*visibility);
    }
    detail::ClearLuaEffectiveVisibilityOverrides(&*visibility);
  }
}

static void AppendToChildren(lua_State *L, int parent_idx) {
  parent_idx = lua_absindex(L, parent_idx);
  int child_idx = lua_absindex(L, -1);

  lua_getfield(L, parent_idx, "__ow_children");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, parent_idx, "__ow_children");
  }
  int arr = lua_absindex(L, -1);
  lua_Integer len = luaL_len(L, arr);
  lua_pushvalue(L, child_idx);
  lua_seti(L, arr, len + 1);
  lua_pop(L, 1);
}

static const char *GetButtonFontObjectFieldForState(lua_State *L,
                                                    int button_idx) {
  button_idx = lua_absindex(L, button_idx);

  lua_getfield(L, button_idx, "__ow_btn_enabled");
  const bool enabled =
      lua_isboolean(L, -1) == 0 || lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (!enabled) {
    return "__ow_btn_dis_font";
  }

  lua_getfield(L, button_idx, "__ow_btn_state");
  const char *state = lua_tostring(L, -1);
  const bool disabled_state =
      state != nullptr && std::strcmp(state, "DISABLED") == 0;
  lua_pop(L, 1);
  if (disabled_state) {
    return "__ow_btn_dis_font";
  }

  return "__ow_btn_normal_font";
}

static bool PushButtonFontObjectForLabel(lua_State *L, int button_idx) {
  button_idx = lua_absindex(L, button_idx);

  const char *field_name = GetButtonFontObjectFieldForState(L, button_idx);
  lua_getfield(L, button_idx, field_name);
  if (lua_istable(L, -1) != 0) {
    return true;
  }
  lua_pop(L, 1);

  if (std::strcmp(field_name, "__ow_btn_dis_font") == 0) {
    lua_getfield(L, button_idx, "__ow_btn_disabled_font");
    if (lua_istable(L, -1) != 0) {
      return true;
    }
    lua_pop(L, 1);
  }

  lua_getfield(L, button_idx, "__ow_btn_normal_font");
  if (lua_istable(L, -1) != 0) {
    return true;
  }
  lua_pop(L, 1);
  return false;
}

const char *ResolveButtonLabelAnchorPoint(lua_State *L, int button_idx,
                                          int font_string_idx) {
  button_idx = lua_absindex(L, button_idx);
  font_string_idx = lua_absindex(L, font_string_idx);

  if (PushButtonFontObjectForLabel(L, button_idx)) {
    const int font_object_idx = lua_absindex(L, -1);
    CopyNamedFontObjectStyle(L, font_string_idx, font_object_idx);
    SetBoundFontObject(L, font_string_idx, font_object_idx);
    const char *justify = openwow::ui::BorrowRawLuaStringField(
        L, font_object_idx, "__ow_justifyH");
    std::uint32_t flags = 0;
    const char* anchor = "CENTER";
    if (openwow::ui::StringToHorizontalJustify(justify, &flags) != 0) {
      if ((flags & 0x1u) != 0u) {
        anchor = "LEFT";
      } else if ((flags & 0x4u) != 0u) {
        anchor = "RIGHT";
      }
    }
    lua_pop(L, 1);
    return anchor;
  }

  std::uint32_t flags = 0;
  const char *justify =
      openwow::ui::BorrowRawLuaStringField(L, font_string_idx, "__ow_justifyH");
  if (openwow::ui::StringToHorizontalJustify(justify, &flags) == 0) {
    return "CENTER";
  }
  if ((flags & 0x1u) != 0u) {
    return "LEFT";
  }
  if ((flags & 0x4u) != 0u) {
    return "RIGHT";
  }
  return "CENTER";
}

bool FontStringHasUsableAnchors(lua_State *L, int font_string_idx) {
  font_string_idx = lua_absindex(L, font_string_idx);

  lua_getfield(L, font_string_idx, "__ow_setAllPoints");
  const bool set_all_points = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (set_all_points) {
    return true;
  }

  lua_getfield(L, font_string_idx, "__ow_anchors");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  const int anchors_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, anchors_idx);
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_geti(L, anchors_idx, i);
    if (lua_istable(L, -1) != 0) {
      lua_getfield(L, -1, "__ow_flags");
      const auto flags = lua_isinteger(L, -1) != 0
                             ? static_cast<std::uint32_t>(lua_tointeger(L, -1))
                             : 0u;
      lua_pop(L, 1);
      if ((flags & 0x800u) == 0u) {
        lua_pop(L, 2);
        return true;
      }
    }
    lua_pop(L, 1);
  }

  lua_pop(L, 1);
  return false;
}

void SetButtonFontStringDefaultAnchor(lua_State *L, int button_idx,
                                             int font_string_idx,
                                             const char *point) {
  button_idx = lua_absindex(L, button_idx);
  font_string_idx = lua_absindex(L, font_string_idx);

  lua_pushnil(L);
  lua_setfield(L, font_string_idx, "__ow_setAllPoints");

  lua_newtable(L);
  const int anchors_idx = lua_absindex(L, -1);
  lua_newtable(L);
  const int anchor_idx = lua_absindex(L, -1);

  lua_pushstring(L, point);
  lua_setfield(L, anchor_idx, "point");
  lua_pushstring(L, point);
  lua_setfield(L, anchor_idx, "relativePoint");
  lua_pushvalue(L, button_idx);
  lua_setfield(L, anchor_idx, "relativeTo");
  lua_pushnumber(L, 0.0);
  lua_setfield(L, anchor_idx, "x");
  lua_pushnumber(L, 0.0);
  lua_setfield(L, anchor_idx, "y");

  lua_seti(L, anchors_idx, 1);
  lua_setfield(L, font_string_idx, "__ow_anchors");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, font_string_idx);

  NotifyFrameInputMutation(L, font_string_idx, false);
}

}

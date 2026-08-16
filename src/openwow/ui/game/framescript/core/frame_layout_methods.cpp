#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_layout_methods.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/lua_script_object_access.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/animation/animation_lua.h"
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

void ApplySimpleFrameLayoutMethods(lua_State *L) {
  if (L != nullptr && lua_istable(L, -1) != 0) {
    ApplyLayoutFrameMethods(L);
  }
}

bool LuaFrameMatchesObjectType(const char *object_type, const char *query) {
  if (query == nullptr || *query == '\0') {
    return false;
  }
  if (object_type == nullptr || *object_type == '\0') {
    object_type = "Frame";
  }

  return openwow::text::EqualsIgnoreCaseAscii(query, object_type) ||
         openwow::text::EqualsIgnoreCaseAscii(query, "Frame") ||
         openwow::text::EqualsIgnoreCaseAscii(query, "Region") ||
         openwow::text::EqualsIgnoreCaseAscii(query, "Object");
}

const char *GetLuaFrameRuntimeTypeName(lua_State *L, const int self_index) {
  const char *object_type =
      openwow::ui::game::detail::GetLuaScriptObjectRuntimeTypeName(L, self_index);
  if (object_type == nullptr || *object_type == '\0') {
    return "Frame";
  }
  return object_type;
}

static const char *DefaultLayoutObjectTypeName(
    const openwow::ui::widgets::ScriptObjectType type) noexcept {
  using openwow::ui::widgets::ScriptObjectType;

  switch (type) {
    case ScriptObjectType::Texture:
      return "Texture";
    case ScriptObjectType::FontString:
      return "FontString";
    case ScriptObjectType::Font:
      return "Font";
    default:
      return "Frame";
  }
}

static const char *GetLuaLayoutRuntimeTypeName(lua_State *L, const int self_index) {
  const auto canonical_type =
      openwow::ui::game::detail::GetLuaCanonicalScriptObjectType(L, self_index);
  const char *object_type =
      openwow::ui::game::detail::GetLuaScriptObjectRuntimeTypeName(L, self_index);
  if (object_type == nullptr || *object_type == '\0') {
    return DefaultLayoutObjectTypeName(canonical_type);
  }
  return object_type;
}

static bool LuaLayoutObjectMatchesObjectType(lua_State *L,
                                             const int self_index,
                                             const char *query) {
  using openwow::ui::widgets::ScriptObjectType;

  if (query == nullptr || *query == '\0') {
    return false;
  }

  const auto canonical_type =
      openwow::ui::game::detail::GetLuaCanonicalScriptObjectType(L, self_index);
  const char *runtime_type = GetLuaLayoutRuntimeTypeName(L, self_index);
  switch (canonical_type) {
    case ScriptObjectType::Texture:
      return openwow::text::EqualsIgnoreCaseAscii(query, runtime_type) ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Region") ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Object");
    case ScriptObjectType::FontString:
      return openwow::text::EqualsIgnoreCaseAscii(query, runtime_type) ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Font") ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Region") ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Object");
    case ScriptObjectType::Font:
      return openwow::text::EqualsIgnoreCaseAscii(query, runtime_type) ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Region") ||
             openwow::text::EqualsIgnoreCaseAscii(query, "Object");
    default:
      return LuaFrameMatchesObjectType(runtime_type, query);
  }
}

void ApplyLayoutFrameMethods(lua_State *L) {
  if (L == nullptr || lua_istable(L, -1) == 0) {
    return;
  }

  const int layout = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    lua_pushstring(Ls, GetLuaLayoutRuntimeTypeName(Ls, self_index));
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetObjectType");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:IsObjectType(\"type\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self_index));
    }

    const char *query = lua_tostring(Ls, 2);
    if (LuaLayoutObjectMatchesObjectType(Ls, self_index, query)) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, layout, "IsObjectType");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    lua_getfield(Ls, self_index, "__ow_name");
    const char *name = lua_tostring(Ls, -1);
    if (name != nullptr && *name != '\0') {
      return 1;
    }

    lua_pop(Ls, 1);
    lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetName");

  lua_pushcfunction(L, LuaScriptObject_GetParent);
  lua_setfield(L, layout, "GetParent");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return detail::PushLuaLayoutProtectionResults(Ls, ValidateFrameScriptSelf(Ls));
  }, 0);
  lua_setfield(L, layout, "IsProtected");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    detail::lua_pushwowbool(Ls, detail::LuaFrameCanChangeProtectedState(Ls, self_index));
    return 1;
  }, 0);
  lua_setfield(L, layout, "CanChangeProtectedState");

  lua_pushcfunction(L, LuaScriptObject_SetParent);
  lua_setfield(L, layout, "SetParent");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self_index, &rect)) {
      return 0;
    }

    lua_pushnumber(Ls, rect.left);
    lua_pushnumber(Ls, rect.bottom);
    lua_pushnumber(Ls, rect.width);
    lua_pushnumber(Ls, rect.height);
    return 4;
  }, 0);
  lua_setfield(L, layout, "GetRect");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self_index, &rect)) {
      lua_pushnil(Ls);
      lua_pushnil(Ls);
      return 2;
    }

    lua_pushnumber(Ls, rect.center_x());
    lua_pushnumber(Ls, rect.center_y());
    return 2;
  }, 0);
  lua_setfield(L, layout, "GetCenter");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self_index, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.left);
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetLeft");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self_index, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.right());
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetRight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self_index, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.top());
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetTop");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self_index, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.bottom);
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetBottom");

  lua_pushcfunction(L, LuaRegion_GetWidth);
  lua_setfield(L, layout, "GetWidth");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetWidth", "width", "__ow_width");
  }, 0);
  lua_setfield(L, layout, "SetWidth");

  lua_pushcfunction(L, LuaRegion_GetHeight);
  lua_setfield(L, layout, "GetHeight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetHeight", "height", "__ow_height");
  }, 0);
  lua_setfield(L, layout, "SetHeight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int { return SetLuaRegionSize(Ls); }, 0);
  lua_setfield(L, layout, "SetSize");

  lua_pushcfunction(L, LuaRegion_GetSize);
  lua_setfield(L, layout, "GetSize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushinteger(Ls, 0);
      return 1;
    }
    const int anchors = EnsureLuaAnchorArray(Ls, 1);
    NormalizeLuaAnchorArray(Ls, anchors);
    lua_pushinteger(Ls, CountVisibleLuaAnchors(Ls, anchors));
    lua_remove(Ls, anchors);
    return 1;
  }, 0);
  lua_setfield(L, layout, "GetNumPoints");

  lua_pushcfunction(L, LuaGetPointInternal);
  lua_setfield(L, layout, "GetPoint");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetPointInternal(Ls, LuaAnchorTargetValidation::kAllowLayoutOnlyTables);
  }, 0);
  lua_setfield(L, layout, "SetPoint");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaClearAllPointsInternal(Ls, LuaAnchorTargetValidation::kAllowLayoutOnlyTables);
  }, 0);
  lua_setfield(L, layout, "ClearAllPoints");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetAllPointsInternal(Ls, LuaAnchorTargetValidation::kAllowLayoutOnlyTables);
  }, 0);
  lua_setfield(L, layout, "SetAllPoints");

  openwow::ui::anim::ApplyAnimationFrameMethods(L);

  lua_pushcfunction(L, LuaRegion_IsDragging);
  lua_setfield(L, layout, "IsDragging");

  lua_pushcfunction(L, LuaRegion_IsMouseOver);
  lua_setfield(L, layout, "IsMouseOver");
}

void SetRegisteredClicks(lua_State *L, int frame_idx,
                         std::initializer_list<const char *> defaults) {
  frame_idx = lua_absindex(L, frame_idx);

  lua_newtable(L);
  const int clicks_idx = lua_absindex(L, -1);
  lua_Integer out_index = 1;
  for (const char *value : defaults) {
    if (value == nullptr || *value == '\0') {
      continue;
    }
    lua_pushstring(L, value);
    lua_seti(L, clicks_idx, out_index++);
  }
  lua_setfield(L, frame_idx, kRegisteredClicksField);
}

void ApplyFontStringIdentityMethods(lua_State *L,
                                           const int font_string_index) {
  const int fs = lua_absindex(L, font_string_index);
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "FontString");
    lua_pushstring(Ls, "FontString");
    return 1;
  }, 0);
  lua_setfield(L, fs, "GetObjectType");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    lua_getfield(Ls, self, "__ow_name");
    const char* name = lua_tostring(Ls, -1);
    if (name != nullptr && *name != '\0') {
      return 1;
    }
    lua_pop(Ls, 1);
    lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, fs, "GetName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "FontString");
    if (lua_isstring(Ls, 2) == 0) {

      return luaL_error(Ls, "Usage: %s:IsObjectType(\"TYPE\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    const char* query = lua_tostring(Ls, 2);
    if (openwow::text::EqualsIgnoreCaseAscii(query, "FontString") ||
        openwow::text::EqualsIgnoreCaseAscii(query, "Font") ||
        openwow::text::EqualsIgnoreCaseAscii(query, "Region") ||
        openwow::text::EqualsIgnoreCaseAscii(query, "Object")) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, fs, "IsObjectType");
}

}

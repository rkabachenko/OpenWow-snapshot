#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/status_bar_methods.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/widgets/status_bar.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace openwow::ui::game::frame_api {

namespace {

using openwow::ui::widgets::StatusBar;

StatusBar* RequireStatusBar(lua_State* lua) {
  if (lua_istable(lua, 1) == 0) {
    luaL_error(lua,
               "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
    return nullptr;
  }

  (void)lua_adapter::CanonicalScriptObjectType(lua, 1);
  auto* status_bar =
      dynamic_cast<StatusBar*>(lua_adapter::BorrowNativeScriptObject(lua, 1));
  if (status_bar == nullptr) {
    luaL_error(lua, "Wrong object type for member function");
  }
  return status_bar;
}

void InvokeStatusBarScript(lua_State* lua, const char* event_name,
                           const int argument_count) {
  const auto invocation =
      InvokeFrameScriptHandler(lua, 1, event_name, argument_count);
  if (invocation.status != LUA_OK) {
    lua_pop(lua, 1);
  }
}

void ApplyAttachedTextureState(lua_State* lua, StatusBar& status_bar,
                               const int texture_index) {
  const int texture = lua_absindex(lua, texture_index);
  auto initial_color = status_bar.AttachTexture(lua_topointer(lua, texture));
  if (!initial_color.has_value()) {
    initial_color = status_bar.TakeInitialColorForAttachedTexture();
  }
  if (initial_color.has_value()) {
    (void)ApplyTextureVertexColor(lua, texture, *initial_color);
  }
  (void)ApplyStatusBarTextureRotation(
      lua, texture, status_bar.Snapshot().rotates_texture);
}

}

void ApplyStatusBarMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    auto* status_bar = RequireStatusBar(Ls);
    if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3)) {
      const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
      return luaL_error(Ls, "Usage: %s:SetMinMaxValues(min, max)", name);
    }
    const float minimum = static_cast<float>(lua_tonumber(Ls, 2));
    const float maximum = static_cast<float>(lua_tonumber(Ls, 3));
    switch (openwow::ui::widgets::ValidateStatusBarRange(minimum, maximum)) {
      case openwow::ui::widgets::StatusBarRangeError::EndpointOutOfRange:
        return luaL_error(Ls, "Min or Max out of range");
      case openwow::ui::widgets::StatusBarRangeError::SpanTooLarge:
        return luaL_error(Ls, "Min and Max too far apart");
      case openwow::ui::widgets::StatusBarRangeError::None:
        break;
    }
    const auto change = status_bar->SetRange(minimum, maximum);
    if (!change.range_changed) {
      return 0;
    }

    const auto snapshot = status_bar->Snapshot();
    lua_pushnumber(Ls, snapshot.minimum);
    lua_pushnumber(Ls, snapshot.maximum);
    InvokeStatusBarScript(Ls, "OnMinMaxChanged", 2);

    if (change.reapply_value &&
        status_bar->SetValue(status_bar->Snapshot().value)) {
      lua_pushnumber(Ls, status_bar->Snapshot().value);
      InvokeStatusBarScript(Ls, "OnValueChanged", 1);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetMinMaxValues");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const auto snapshot = RequireStatusBar(Ls)->Snapshot();
    lua_pushnumber(Ls, snapshot.minimum);
    lua_pushnumber(Ls, snapshot.maximum);
    return 2;
  }, 0);
  lua_setfield(L, f, "GetMinMaxValues");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    auto* status_bar = RequireStatusBar(Ls);
    if (lua_gettop(Ls) != 2 || !lua_isnumber(Ls, 2)) {
      const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
      return luaL_error(Ls, "Usage: %s:SetValue(value)", name);
    }
    if (status_bar->SetValue(static_cast<float>(lua_tonumber(Ls, 2)))) {
      lua_pushnumber(Ls, status_bar->Snapshot().value);
      InvokeStatusBarScript(Ls, "OnValueChanged", 1);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetValue");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    lua_pushnumber(Ls, RequireStatusBar(Ls)->Snapshot().value);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetValue");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    auto* status_bar = RequireStatusBar(Ls);

    int draw_layer = 2;
    if (lua_isstring(Ls, 3)) {
      const char *layer_str = lua_tostring(Ls, 3);
      if (layer_str) {
        TryParseDrawLayerName(layer_str, &draw_layer);
      }
    }

    const auto attach_texture = [&](const int texture_index) {
      const int texture = lua_absindex(Ls, texture_index);
      BindTextureOwnership(
          Ls, texture,
          1,
          openwow::ui::framexml::TextureRole::StatusBarFill);
      lua_pushboolean(Ls, 1);
      lua_setfield(Ls, texture, "__ow_setAllPoints");
      lua_pushinteger(Ls, draw_layer);
      lua_setfield(Ls, texture, "__ow_draw_layer");
      SyncRegionDrawLayerEnabled(Ls, texture);
    };

    const auto detach_current_texture = [&]() {
      lua_getfield(Ls, 1, "__ow_sb_texture");
      if (lua_istable(Ls, -1) != 0) {
        ReleaseTextureOwnership(Ls, -1);
      }
      lua_pop(Ls, 1);
    };

    if (lua_istable(Ls, 2)) {
      if (!lua_adapter::HasScriptObjectIdentity(Ls, 2)) {
        const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
        return luaL_error(
            Ls, "%s:SetCheckedTexture(): Couldn't find 'this' in texture",
            name);
      }
      if (!lua_adapter::HasCanonicalScriptObjectType(
              Ls, 2, openwow::ui::widgets::ScriptObjectType::Texture)) {
        const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);

        return luaL_error(
            Ls, "%s:SetCheckedTexture(): Wrong object type, expected texture",
            name);
      }
      lua_getfield(Ls, 1, "__ow_sb_texture");
      const bool replacing = lua_istable(Ls, -1) != 0 &&
                             lua_rawequal(Ls, -1, 2) == 0;
      if (replacing) {
        ReleaseTextureOwnership(Ls, -1);
      }
      lua_pop(Ls, 1);
      attach_texture(2);
      ApplyAttachedTextureState(Ls, *status_bar, 2);
      lua_pushvalue(Ls, 2);
      lua_setfield(Ls, 1, "__ow_sb_texture");
    } else if (lua_isstring(Ls, 2)) {
      lua_getfield(Ls, 1, "__ow_sb_texture");
      if (lua_istable(Ls, -1) != 0) {
        const int texture = lua_absindex(Ls, -1);
        if (CallTextureSetPath(Ls, texture, 2)) {
          attach_texture(texture);
          ApplyAttachedTextureState(Ls, *status_bar, texture);
          lua_pushvalue(Ls, texture);
          lua_setfield(Ls, 1, "__ow_sb_texture");
        }
        lua_pop(Ls, 1);
        return 0;
      }
      lua_pop(Ls, 1);

      CreateTextureTable(Ls, 1);
      const int texture_index = lua_absindex(Ls, -1);
      attach_texture(texture_index);
      if (!CallTextureSetPath(Ls, texture_index, 2)) {
        ReleaseTextureOwnership(Ls, texture_index);
        lua_pop(Ls, 1);
        return 0;
      }
      ApplyAttachedTextureState(Ls, *status_bar, texture_index);
      TrackRuntimeRegion(Ls, 1, texture_index, "Texture", nullptr,
                         GetDrawLayerNameById(draw_layer),
                         openwow::ui::framexml::TextureRole::StatusBarFill,
                         true);

      lua_pushvalue(Ls, texture_index);
      lua_setfield(Ls, 1, "__ow_sb_texture");
      lua_pop(Ls, 1);
    } else if (lua_isnoneornil(Ls, 2)) {
      detach_current_texture();
      lua_pushnil(Ls);
      lua_setfield(Ls, 1, "__ow_sb_texture");
    } else {
      const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
      return luaL_error(Ls,
          "Usage: %s:SetStatusBarTexture"
          "(texture or \"texture\" or nil [, \"layer\"])", name);
    }

    return 0;
  }, 0);
  lua_setfield(L, f, "SetStatusBarTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    (void)RequireStatusBar(Ls);
    lua_getfield(Ls, 1, "__ow_sb_texture");
    return 1;
  }, 0);
  lua_setfield(L, f, "GetStatusBarTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    (void)RequireStatusBar(Ls);
    const float red = static_cast<float>(luaL_checknumber(Ls, 2));
    const float green = static_cast<float>(luaL_checknumber(Ls, 3));
    const float blue = static_cast<float>(luaL_checknumber(Ls, 4));
    const float supplied_alpha = lua_isnumber(Ls, 5)
                                     ? static_cast<float>(lua_tonumber(Ls, 5))
                                     : 1.0F;
    const float alpha = std::isfinite(supplied_alpha) ? supplied_alpha : 1.0F;
    lua_getfield(Ls, 1, "__ow_sb_texture");
    if (lua_istable(Ls, -1) != 0) {
      (void)ApplyTextureVertexColor(
          Ls, -1, {.red = red, .green = green, .blue = blue, .alpha = alpha});
    }
    lua_pop(Ls, 1);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetStatusBarColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    (void)RequireStatusBar(Ls);
    lua_getfield(Ls, 1, "__ow_sb_texture");
    if (lua_istable(Ls, -1) != 0) {
      const int texture = lua_absindex(Ls, -1);
      lua_getfield(Ls, texture, "GetVertexColor");
      if (lua_isfunction(Ls, -1) != 0) {
        lua_pushvalue(Ls, texture);
        if (lua_pcall(Ls, 1, 4, 0) == 0) {
          lua_remove(Ls, texture);
          return 4;
        }
        return lua_error(Ls);
      }
      lua_pop(Ls, 1);
    }
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, 1.0);
    lua_pushnumber(Ls, 1.0);
    lua_pushnumber(Ls, 1.0);
    lua_pushnumber(Ls, 1.0);
    return 4;
  }, 0);
  lua_setfield(L, f, "GetStatusBarColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    auto* status_bar = RequireStatusBar(Ls);
    if (!lua_isstring(Ls, 2)) {
      const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
      return luaL_error(Ls, "Usage: %s:SetOrientation(\"orientation\")",
                        name);
    }
    const char *orient = lua_tostring(Ls, 2);
    if (!orient) orient = "";
    if (openwow::text::EqualsIgnoreCaseAscii(orient, "HORIZONTAL")) {
      status_bar->SetOrientation(
          openwow::ui::widgets::StatusBarOrientation::Horizontal);
    } else if (openwow::text::EqualsIgnoreCaseAscii(orient, "VERTICAL")) {
      status_bar->SetOrientation(
          openwow::ui::widgets::StatusBarOrientation::Vertical);
    } else {
      const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
      return luaL_error(
          Ls, "%s:SetOrientation(): Unknown orientation: %s", name,
          orient);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetOrientation");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const auto orientation = RequireStatusBar(Ls)->Snapshot().orientation;
    lua_pushstring(
        Ls, orientation == openwow::ui::widgets::StatusBarOrientation::Vertical
                ? "VERTICAL"
                : "HORIZONTAL");
    return 1;
  }, 0);
  lua_setfield(L, f, "GetOrientation");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    auto* status_bar = RequireStatusBar(Ls);
    const bool rotates = lua_toboolean(Ls, 2) != 0;
    status_bar->SetRotatesTexture(rotates);
    lua_getfield(Ls, 1, "__ow_sb_texture");
    if (lua_istable(Ls, -1) != 0) {
      (void)ApplyStatusBarTextureRotation(Ls, -1, rotates);
    }
    lua_pop(Ls, 1);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetRotatesTexture");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (RequireStatusBar(Ls)->Snapshot().rotates_texture) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetRotatesTexture");
}

}

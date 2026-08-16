#include "openwow/ui/display/settings/adapters/lua/display_settings_lua.h"

#include "openwow/ui/display/settings/display_settings_service.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/widgets/script_object.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace openwow::ui::display::lua_adapter {
namespace {

constexpr char kContextRegistryKey[] =
    "openwow.ui.display.settings.lua_context";

struct Context final {
  Context(DisplaySettingsService& settings_value,
          FullscreenFrameScalePort& frame_scale_value)
      : settings(settings_value), frame_scale(frame_scale_value) {}

  DisplaySettingsService& settings;
  FullscreenFrameScalePort& frame_scale;
};

Context& RequireContext(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kContextRegistryKey);
  auto* context = static_cast<Context*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  if (context == nullptr) {
    luaL_error(state, "display settings module is unavailable");
  }
  return *context;
}

void Install(lua_State* state, void* raw_context) {
  lua_pushlightuserdata(state, raw_context);
  lua_setfield(state, LUA_REGISTRYINDEX, kContextRegistryKey);
}

void Uninstall(lua_State* state, void*) {
  lua_pushnil(state);
  lua_setfield(state, LUA_REGISTRYINDEX, kContextRegistryKey);
}

std::optional<std::size_t> OptionalOneBasedIndex(lua_State* state,
                                                const int argument) {
  if (lua_isnumber(state, argument) == 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(state, argument)));
}

void PushRetailBoolean(lua_State* state, const bool value) {
  if (value) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
}

int GetScreenResolutions(lua_State* state) {
  const auto resolutions = RequireContext(state).settings.ScreenResolutions();
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, resolutions.size(), "screen resolutions");
  char text[32];
  for (const auto resolution : resolutions) {
    const int length =
        std::snprintf(text, sizeof(text), "%dx%d", resolution.width,
                      resolution.height);
    lua_pushlstring(state, text, static_cast<std::size_t>(length));
  }
  return result_count;
}

int GetCurrentResolution(lua_State* state) {
  lua_pushnumber(
      state, static_cast<lua_Number>(
                 RequireContext(state).settings.CurrentResolutionIndex()));
  return 1;
}

int SetScreenResolution(lua_State* state) {
  RequireContext(state).settings.SelectResolution(OptionalOneBasedIndex(state, 1));
  return 0;
}

int GetRefreshRates(lua_State* state) {
  const auto rates =
      RequireContext(state).settings.RefreshRates(OptionalOneBasedIndex(state, 1));
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, rates.size(), "refresh rates");
  for (const int rate : rates) {
    lua_pushnumber(state, static_cast<lua_Number>(rate));
  }
  return result_count;
}

int SetupFullscreenScale(lua_State* state) {
  if (lua_type(state, 1) != LUA_TTABLE) {
    return luaL_error(state, "Usage: SetupFullscreenScale(frame)");
  }
  if (!openwow::ui::game::lua_adapter::HasScriptObjectIdentity(state, 1)) {
    return luaL_error(
        state,
        "SetupFullscreenScale(): Couldn't find 'this' in frame object");
  }
  if (!openwow::ui::game::lua_adapter::IsScriptObjectKindOf(
          state, 1, openwow::ui::widgets::ScriptObjectType::Frame)) {
    return luaL_error(
        state, "SetupFullscreenScale(): Wrong object type, expected frame");
  }

  auto& port = RequireContext(state).frame_scale;
  const float aspect_scale = port.AspectScale();
  const float scale =
      std::isnan(aspect_scale) ? 1.0F : std::min(1.0F, aspect_scale);
  port.Apply(state, 1, scale);
  return 0;
}

int GetMultisampleFormats(lua_State* state) {
  const auto formats = RequireContext(state).settings.MultisampleFormats();
  constexpr std::size_t kResultsPerFormat = 3;
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, formats.size(), kResultsPerFormat, "multisample format values");
  for (const auto format : formats) {
    lua_pushnumber(state, static_cast<lua_Number>(format.color_bits));
    lua_pushnumber(state, static_cast<lua_Number>(format.depth_bits));
    lua_pushnumber(state, static_cast<lua_Number>(format.samples));
  }
  return result_count;
}

int GetCurrentMultisampleFormat(lua_State* state) {
  lua_pushnumber(
      state,
      static_cast<lua_Number>(
          RequireContext(state).settings.CurrentMultisampleFormatIndex()));
  return 1;
}

int SetMultisampleFormat(lua_State* state) {
  RequireContext(state).settings.SelectMultisampleFormat(
      OptionalOneBasedIndex(state, 1));
  return 0;
}

int GetVideoCaps(lua_State* state) {

  const VideoCapabilities caps =
      RequireContext(state).settings.Capabilities().value_or(VideoCapabilities{});
  PushRetailBoolean(state, caps.anisotropic);
  PushRetailBoolean(state, caps.pixel_shaders);
  PushRetailBoolean(state, caps.vertex_shaders);
  PushRetailBoolean(state, caps.trilinear);
  lua_pushnumber(state, static_cast<lua_Number>(caps.buffering));
  if (caps.max_anisotropy) {
    lua_pushnumber(state, static_cast<lua_Number>(*caps.max_anisotropy));
  } else {
    lua_pushnil(state);
  }
  PushRetailBoolean(state, caps.hardware_cursor);
  return 7;
}

int GetGamma(lua_State* state) {
  lua_pushnumber(state, RequireContext(state).settings.Gamma());
  return 1;
}

int SetGamma(lua_State* state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SetGamma(value)");
  }
  RequireContext(state).settings.SetGamma(
      static_cast<double>(lua_tonumber(state, 1)));
  return 0;
}

int GetTerrainMip(lua_State* state) {
  lua_pushnumber(state, static_cast<lua_Number>(
                            RequireContext(state).settings.TerrainMip()));
  return 1;
}

int SetTerrainMip(lua_State* state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SetTerrainMip(value)");
  }
  RequireContext(state).settings.SetTerrainMip(
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1)));
  return 0;
}

int IsStereoVideoAvailable(lua_State* state) {
  PushRetailBoolean(state,
                    RequireContext(state).settings.StereoVideoAvailable());
  return 1;
}

int IsPlayerResolutionAvailable(lua_State* state) {
  PushRetailBoolean(
      state, RequireContext(state).settings.PlayerResolutionAvailable());
  return 1;
}

constexpr openwow::ui::LuaGlobalBinding kBindings[] = {
    {"GetScreenResolutions", GetScreenResolutions},
    {"GetCurrentResolution", GetCurrentResolution},
    {"SetScreenResolution", SetScreenResolution},
    {"GetRefreshRates", GetRefreshRates},
    {"SetupFullscreenScale", SetupFullscreenScale},
    {"GetMultisampleFormats", GetMultisampleFormats},
    {"GetCurrentMultisampleFormat", GetCurrentMultisampleFormat},
    {"SetMultisampleFormat", SetMultisampleFormat},
    {"GetVideoCaps", GetVideoCaps},
    {"GetGamma", GetGamma},
    {"SetGamma", SetGamma},
    {"GetTerrainMip", GetTerrainMip},
    {"SetTerrainMip", SetTerrainMip},
    {"IsStereoVideoAvailable", IsStereoVideoAvailable},
    {"IsPlayerResolutionAvailable", IsPlayerResolutionAvailable},
};

}

openwow::ui::lua::NativeBindingCatalog
SharedDisplaySettingsNativeBindingCatalog(
    DisplaySettingsService& settings, FullscreenFrameScalePort& frame_scale) {
  auto context = std::make_shared<Context>(settings, frame_scale);
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "ui.display.settings", openwow::ui::lua::BindingScope::kShared,
      kBindings);
  catalog.install = Install;
  catalog.uninstall = Uninstall;
  catalog.lifecycle_context = std::move(context);
  return catalog;
}

}

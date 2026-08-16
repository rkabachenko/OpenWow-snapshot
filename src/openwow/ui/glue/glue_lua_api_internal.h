
#pragma once

#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/ui/glue/glue_background_controller.h"
#include "openwow/ui/glue/glue_lua_runtime.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/render/ui/ui_texture_capabilities.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::data::dbc {
class DbcLoader;
}

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue::detail {

inline constexpr const char* kWidgetRuntimeRegistryKey = "openwow.widget_runtime";
inline constexpr const char* kGlueRuntimeRegistryKey   = "openwow.glue_runtime";
inline constexpr const char* kGlueHostRegistryKey      = "openwow.glue_host";
inline constexpr const char* kGlueGameStateRegistryKey = "openwow.game_state";
inline constexpr const char* kGameTimeRegistryKey      = "openwow.game_time";
inline constexpr const char* kDbcLoaderRegistryKey      = "openwow.dbc_loader";
inline constexpr const char* kGlueLuaFrameRuntimeKeyField = "__ow_frame_key";
inline constexpr const char* kGlueLuaFrameNameField = "__ow_name";

inline GlueWidgetRuntime* GetWidgetRuntime(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetRuntimeRegistryKey);
  auto* rt = static_cast<GlueWidgetRuntime*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return rt;
}

inline GlueLuaRuntime* GetGlueRuntime(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kGlueRuntimeRegistryKey);
  auto* rt = static_cast<GlueLuaRuntime*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return rt;
}

inline GlueHost* GetGlueHost(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kGlueHostRegistryKey);
  auto* host = static_cast<GlueHost*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return host;
}

inline GlueGameState* GetGameState(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kGlueGameStateRegistryKey);
  auto* gs = static_cast<GlueGameState*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return gs;
}

inline const openwow::core::ida::GameTimeData* GetGameTimeData(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kGameTimeRegistryKey);
  auto* game_time = static_cast<const openwow::core::ida::GameTimeData*>(
      lua_touserdata(state, -1));
  lua_pop(state, 1);
  return game_time;
}

[[nodiscard]] inline bool SendGlueRealmPacket(
    lua_State* state, const openwow::net::wotlk::WorldPacket& packet) {
  auto* game_state = GetGameState(state);
  return game_state != nullptr && game_state->send_realm_packet &&
         game_state->send_realm_packet(packet);
}

inline const openwow::data::dbc::DbcLoader* GetDbcLoader(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kDbcLoaderRegistryKey);
  auto* loader = static_cast<const openwow::data::dbc::DbcLoader*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return loader;
}

inline std::string WidgetNameFromArg(lua_State* state, int index) {
  if (lua_isstring(state, index) != 0) {
    const char* text = lua_tostring(state, index);
    return text ? std::string(text) : std::string();
  }
  if (lua_istable(state, index) != 0) {
    lua_getfield(state, index, kGlueLuaFrameRuntimeKeyField);
    const char* frame_key = lua_tostring(state, -1);
    std::string out = frame_key ? std::string(frame_key) : std::string();
    lua_pop(state, 1);
    if (!out.empty()) {
      return out;
    }

    lua_getfield(state, index, kGlueLuaFrameNameField);
    const char* name = lua_tostring(state, -1);
    out = name ? std::string(name) : std::string();
    lua_pop(state, 1);
    return out;
  }
  return {};
}

inline std::string FindFirstExistingWidgetName(GlueWidgetRuntime* runtime,
                                               const std::string& base,
                                               std::initializer_list<const char*> suffixes) {
  if (runtime == nullptr) return {};
  for (const auto* suffix : suffixes) {
    if (suffix == nullptr) continue;
    const std::string candidate = base + suffix;
    if (runtime->GetWidget(candidate).has_value()) {
      return candidate;
    }
  }
  return {};
}

using openwow::ui::ScriptParseBoolStringOrDefault;
using openwow::ui::ScriptReadBoolArgOrDefault;

inline const char* ReadGlueStringArgWithUsage(lua_State* state, int index,
                                              const char* usage) {
  if (lua_isstring(state, index) == 0) {
    luaL_error(state, "%s", usage);
  }

  return lua_tostring(state, index);
}

inline void lua_pushwowbool(lua_State* state, bool value) {
  if (value) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
}

inline void lua_pushoptstring(lua_State* state, const std::optional<std::string>& value) {
  if (value.has_value()) {
    lua_pushlstring(state, value->data(), value->size());
  } else {
    lua_pushnil(state);
  }
}

inline bool TextureStateSupported(bool desaturated_state) {
  return openwow::render::ui::UiTextureStateSupported(
      desaturated_state ? openwow::render::ui::UiTextureState::kDesaturated
                        : openwow::render::ui::UiTextureState::kNormal);
}

inline void PushGlueLuaValue(lua_State* state, const GlueLuaValue& value) {
  switch (value.kind) {
    case GlueLuaValue::Kind::kString:
      lua_pushstring(state, value.string_value.c_str());
      return;
    case GlueLuaValue::Kind::kNumber:
      lua_pushnumber(state, static_cast<lua_Number>(value.number_value));
      return;
    case GlueLuaValue::Kind::kBoolean:
      lua_pushboolean(state, value.bool_value ? 1 : 0);
      return;
    case GlueLuaValue::Kind::kNil:
    default:
      lua_pushnil(state);
      return;
  }
}

using openwow::text::ToLowerAscii;

inline bool IsUiParentName(const std::string& name) {
  return ToLowerAscii(name) == "uiparent";
}

void PublishWidgetGlobal(lua_State* state, const std::string& name);
void FinalizePublishedWidgetGlobal(lua_State* state, const std::string& name);
void PublishWidgetsAsGlobals(lua_State* state, GlueWidgetRuntime* runtime);

using GlueSpawnProcessFn = int (*)(const char* application_name,
                                   const char* command_line,
                                   std::uintptr_t wait_callback,
                                   std::intptr_t callback_arg);
GlueSpawnProcessFn SetQuitGameAndRunLauncherSpawnProcessForTesting(
    GlueSpawnProcessFn spawn_process);

}

#include "openwow/game/actions/bindings/adapters/lua/binding_script_executor.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/ui/game/framescript/core/click_frame_lookup.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/lua_mouse_button_context.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <algorithm>
#include <string>

extern "C" {
#include <lua.hpp>
}

namespace openwow::game::actions::bindings::adapters::lua {
namespace {

constexpr const char* kBindingArgumentNames[] = {
    "arg1", "arg2", "arg3", "arg4"};
constexpr char kCurrentModifierStateRegistryKey[] =
    "openwow.current_modifier_state";

int PushErrorHandler(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX,
               openwow::ui::kGameLuaErrorHandlerRegistryKey);
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 1);
    return 0;
  }
  return lua_absindex(state, -1);
}

class ScopedBindingArgumentGlobals {
 public:
  ScopedBindingArgumentGlobals(lua_State* state, int first_argument,
                               int argument_count)
      : state_(state),
        first_argument_(lua_absindex(state, first_argument)),
        argument_count_(std::clamp(argument_count, 0, 4)) {
    for (int index = 0; index < argument_count_; ++index) {
      lua_getglobal(state_, kBindingArgumentNames[index]);
      saved_references_[index] = luaL_ref(state_, LUA_REGISTRYINDEX);
      openwow::ui::ReplaceLuaGlobalValue(
          state_, kBindingArgumentNames[index], first_argument_ + index);
    }
  }

  ~ScopedBindingArgumentGlobals() {
    for (int index = argument_count_ - 1; index >= 0; --index) {
      if (saved_references_[index] == LUA_REFNIL) {
        openwow::ui::UnregisterLuaGlobal(state_, kBindingArgumentNames[index]);
      } else {
        lua_rawgeti(state_, LUA_REGISTRYINDEX, saved_references_[index]);
        openwow::ui::ReplaceLuaGlobalValue(
            state_, kBindingArgumentNames[index], -1);
        lua_pop(state_, 1);
        luaL_unref(state_, LUA_REGISTRYINDEX, saved_references_[index]);
      }
    }
  }

 private:
  lua_State* state_;
  int first_argument_;
  int argument_count_;
  int saved_references_[4]{LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF};
};

}

ScopedLuaModifierState::ScopedLuaModifierState(
    lua_State* state, const std::uint16_t modifier_state)
    : state_(state) {
  if (state_ == nullptr) {
    return;
  }
  lua_getfield(state_, LUA_REGISTRYINDEX,
               kCurrentModifierStateRegistryKey);
  if (lua_isnumber(state_, -1) != 0) {
    previous_state_ =
        static_cast<std::uint16_t>(lua_tointeger(state_, -1));
  }
  lua_pop(state_, 1);
  lua_pushinteger(state_, static_cast<lua_Integer>(modifier_state));
  lua_setfield(state_, LUA_REGISTRYINDEX,
               kCurrentModifierStateRegistryKey);
}

ScopedLuaModifierState::~ScopedLuaModifierState() {
  if (state_ == nullptr) {
    return;
  }
  if (previous_state_) {
    lua_pushinteger(state_, static_cast<lua_Integer>(*previous_state_));
  } else {
    lua_pushnil(state_);
  }
  lua_setfield(state_, LUA_REGISTRYINDEX,
               kCurrentModifierStateRegistryKey);
}

void ExecuteBindingScript(lua_State* state, const int script_reference,
                          const BindingScriptInvocation& invocation) {
  if (state == nullptr || script_reference == LUA_NOREF) {
    return;
  }

  const openwow::ui::game::SecureExecution::SecureScope hardware_input_scope(
      state);

  const openwow::ui::game::SecureExecution::HardwareActionGrantScope
      hardware_action_grant;
  const int base_top = lua_gettop(state);
  const int error_handler_index = PushErrorHandler(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, script_reference);
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, base_top);
    return;
  }

  lua_pushstring(state, invocation.key_down ? "down" : "up");
  lua_pushnumber(state, invocation.pressure);
  lua_pushnumber(state, invocation.angle);
  lua_pushnumber(state, invocation.precision);
  {
    const ScopedBindingArgumentGlobals arguments(state, -4, 4);
    if (lua_pcall(state, 4, 0, error_handler_index) != 0) {
      const char* const message =
          lua_isstring(state, -1) != 0 ? lua_tostring(state, -1) : nullptr;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          std::string("Binding script failed: ") +
              (message != nullptr ? message : "unknown Lua error"));
      lua_pop(state, 1);
    }
  }
  lua_settop(state, base_top);
}

void ReleaseBindingScript(lua_State* state, const int script_reference) {
  if (state != nullptr && script_reference != LUA_NOREF) {
    luaL_unref(state, LUA_REGISTRYINDEX, script_reference);
  }
}

int CompileBindingScript(lua_State* state, std::string_view chunk_name,
                         std::string_view body) {
  if (state == nullptr || body.empty()) {
    return LUA_NOREF;
  }

  std::string chunk =
      "return function(keystate, pressure, angle, precision) ";
  chunk.append(body);
  chunk.append("\nend");
  if (luaL_loadbuffer(state, chunk.data(), chunk.size(),
                      std::string(chunk_name).c_str()) != 0) {
    const char* error = lua_tostring(state, -1);
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Binding script compile failed for " + std::string(chunk_name) +
            ": " + (error != nullptr ? error : "(null)"));
    lua_pop(state, 1);
    return LUA_NOREF;
  }
  if (lua_pcall(state, 0, 1, 0) != 0) {
    const char* error = lua_tostring(state, -1);
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Binding closure creation failed for " + std::string(chunk_name) +
            ": " + (error != nullptr ? error : "(null)"));
    lua_pop(state, 1);
    return LUA_NOREF;
  }
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 1);
    return LUA_NOREF;
  }
  return luaL_ref(state, LUA_REGISTRYINDEX);
}

namespace {

bool PushNamedClickFrame(lua_State* state, std::string_view frame_name) {
  return openwow::ui::game::detail::PushNamedFrameLikeObject(
      state, frame_name);
}

bool InvokeFrameMouseScript(lua_State* state, int frame_index,
                            const char* handler,
                            std::string_view mouse_button) {
  const int frame = lua_absindex(state, frame_index);
  lua_pushlstring(state, mouse_button.data(), mouse_button.size());
  const auto invocation = openwow::ui::game::InvokeFrameScriptHandler(
      state, frame, handler, 1);
  if (invocation.status != LUA_OK) {
    lua_pop(state, 1);
  }
  return invocation.invoked;
}

bool InvokeFrameClickMethod(lua_State* state, int frame_index,
                            std::string_view mouse_button, bool key_down) {
  lua_getfield(state, frame_index, "Click");
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 1);
    return false;
  }
  lua_pushvalue(state, frame_index);
  lua_pushlstring(state, mouse_button.data(), mouse_button.size());
  lua_pushboolean(state, key_down ? 1 : 0);
  if (openwow::ui::game::ProfiledPCall(state, 3, 0, 0) != 0) {
    lua_pop(state, 1);
  }
  return true;
}

bool IsButtonDisabled(lua_State* state, int frame_index) {
  lua_getfield(state, frame_index, "__ow_btn_state");
  const char* visual_state = lua_tostring(state, -1);
  const bool disabled_by_state =
      visual_state != nullptr &&
      openwow::text::EqualsIgnoreCaseAscii(visual_state, "DISABLED");
  lua_pop(state, 1);

  lua_getfield(state, frame_index, "__ow_btn_enabled");
  const bool disabled_by_flag =
      lua_isboolean(state, -1) != 0 && lua_toboolean(state, -1) == 0;
  lua_pop(state, 1);
  return disabled_by_state || disabled_by_flag;
}

bool IsButtonVisualState(lua_State* state, int frame_index,
                         std::string_view expected_state) {
  lua_getfield(state, frame_index, "__ow_btn_state");
  const char* visual_state = lua_tostring(state, -1);
  const bool matches =
      visual_state != nullptr &&
      openwow::text::EqualsIgnoreCaseAscii(visual_state, expected_state);
  lua_pop(state, 1);
  return matches;
}

bool IsButtonStateLocked(lua_State* state, int frame_index) {
  lua_getfield(state, frame_index, "__ow_btn_state_locked");
  const bool locked = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return locked;
}

void SetButtonVisualState(lua_State* state, int frame_index,
                          const char* visual_state) {
  lua_pushstring(state, visual_state);
  lua_setfield(state, frame_index, "__ow_btn_state");
}

bool HasSyntheticClickPhase(lua_State* state, int frame_index,
                            bool key_down) {
  lua_getfield(state, frame_index, "__ow_registered_clicks");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return !key_down;
  }

  const std::string_view phase_suffix = key_down ? "Down" : "Up";
  const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(state, -1));
  for (lua_Integer index = 1; index <= count; ++index) {
    lua_geti(state, -1, index);
    const char* token = lua_tostring(state, -1);
    const std::string_view value = token != nullptr ? token : "";
    const bool matches =
        value.size() >= phase_suffix.size() &&
        openwow::text::EqualsIgnoreCaseAscii(
            value.substr(value.size() - phase_suffix.size()), phase_suffix);
    lua_pop(state, 1);
    if (matches) {
      lua_pop(state, 1);
      return true;
    }
  }
  lua_pop(state, 1);
  return false;
}

std::string NormalizeClickButtonName(std::string_view mouse_button) {
  const std::string raw_button(mouse_button);
  return openwow::ui::widgets::MouseButtonName(
      openwow::ui::widgets::MouseButtonFlag(raw_button.c_str()));
}

bool InvokeNamedClickFrame(lua_State* state, std::string_view frame_name,
                           std::string_view mouse_button, bool key_down) {
  const int base_top = lua_gettop(state);
  if (!PushNamedClickFrame(state, frame_name)) {
    return false;
  }

  const int frame_index = lua_absindex(state, -1);
  const auto frame_type =
      openwow::ui::game::lua_adapter::CanonicalScriptObjectType(
          state, frame_index);
  const bool is_button = openwow::ui::widgets::IsScriptTypeKindOf(
      frame_type, openwow::ui::widgets::ScriptObjectType::Button);
  bool handled = InvokeFrameMouseScript(
      state, frame_index, key_down ? "OnMouseDown" : "OnMouseUp",
      mouse_button);

  if (is_button) {
    handled = true;
    if (key_down) {
      if (!IsButtonDisabled(state, frame_index)) {
        if (HasSyntheticClickPhase(state, frame_index, true)) {
          (void)InvokeFrameClickMethod(
              state, frame_index, mouse_button, true);
        }
        if (!IsButtonStateLocked(state, frame_index)) {
          SetButtonVisualState(state, frame_index, "PUSHED");
        }
      }
    } else if (IsButtonVisualState(state, frame_index, "PUSHED")) {
      if (HasSyntheticClickPhase(state, frame_index, false)) {
        (void)InvokeFrameClickMethod(
            state, frame_index, mouse_button, false);
      }
      if (!IsButtonStateLocked(state, frame_index)) {
        SetButtonVisualState(state, frame_index, "NORMAL");
      }
    }
  }

  lua_settop(state, base_top);
  return handled;
}

}

bool DispatchClickBinding(MacroCatalog* macros, lua_State* state,
                          std::string_view command, bool key_down) {
  constexpr std::string_view kClickPrefix = "CLICK ";
  if (command.size() < kClickPrefix.size() ||
      !openwow::text::EqualsIgnoreCaseAscii(
          command.substr(0, kClickPrefix.size()), kClickPrefix)) {
    return false;
  }

  const std::string_view payload = command.substr(kClickPrefix.size());
  const auto separator = payload.find(':');
  if (separator == std::string_view::npos || separator == 0 ||
      state == nullptr) {
    return false;
  }

  const std::string_view frame_name = payload.substr(0, separator);
  const std::string button =
      NormalizeClickButtonName(payload.substr(separator + 1));
  const openwow::ui::game::lua_adapter::ScopedMouseButtonOverride snapshot(
      state, button);
  const auto invoke = [&]() {
    return InvokeNamedClickFrame(state, frame_name, button, key_down);
  };
  return macros != nullptr
             ? macros->WithTransientButtonContext(button, invoke)
             : invoke();
}

}

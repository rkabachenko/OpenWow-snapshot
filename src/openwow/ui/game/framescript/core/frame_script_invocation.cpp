#include "openwow/ui/game/framescript/core/frame_script_invocation.h"

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace openwow::ui::game {

namespace {

constexpr std::string_view kScriptStoragePrefix = "__ow_script_";
constexpr std::string_view kScriptTableField = "__ow_scripts";

constexpr std::size_t kFastMetadataKeyBytes = 96;

void PushPrefixedField(lua_State* const state, const int table_index,
                       const std::string_view prefix, const char* const name) {
  const int table = lua_absindex(state, table_index);
  const std::string_view suffix = name != nullptr ? std::string_view(name)
                                                  : std::string_view{};
  std::array<char, kFastMetadataKeyBytes> key{};
  if (prefix.size() + suffix.size() < key.size()) {
    std::memcpy(key.data(), prefix.data(), prefix.size());
    std::memcpy(key.data() + prefix.size(), suffix.data(), suffix.size());
    key[prefix.size() + suffix.size()] = '\0';
    lua_getfield(state, table, key.data());
    return;
  }

  lua_pushlstring(state, prefix.data(), prefix.size());
  lua_pushlstring(state, suffix.data(), suffix.size());
  lua_concat(state, 2);
  lua_gettable(state, table);
}

void FormatLegacyArgumentName(char (&buffer)[32], const int one_based_index) {
  buffer[0] = 'a';
  buffer[1] = 'r';
  buffer[2] = 'g';
  const auto result =
      std::to_chars(buffer + 3, buffer + sizeof(buffer) - 1, one_based_index);

  *result.ptr = '\0';
}

void PushRawGlobal(lua_State* const state, const char* const name) {
  lua_pushstring(state, name);
  lua_rawget(state, LUA_GLOBALSINDEX);
}

void SetRawGlobalFromIndex(lua_State* const state, const char* const name,
                           const int value_index) {
  const int value = lua_absindex(state, value_index);
  lua_pushstring(state, name);
  lua_pushvalue(state, value);
  lua_rawset(state, LUA_GLOBALSINDEX);
}

void RestoreRawGlobalFromTop(lua_State* const state, const char* const name) {
  lua_pushstring(state, name);
  lua_insert(state, -2);
  lua_rawset(state, LUA_GLOBALSINDEX);
}

int ProtectedFrameScriptCall(lua_State* const state, const int argument_count,
                             const int error_handler_index,
                             const FrameScriptInvocationSecurity security,
                             const TaintSourceId effective_source) {
  if (security == FrameScriptInvocationSecurity::kCurrent &&
      effective_source ==
          openwow::ui::lua_get_execution_taint_state(state).source) {
    return ProfiledPCall(state, argument_count, 0, error_handler_index);
  }

  SecureExecution::TaintScope scope(state, effective_source);
  return ProfiledPCall(state, argument_count, 0, error_handler_index);
}

}

bool PushFrameScriptHandler(lua_State* const state, const int frame_index,
                            const char* const handler) {
  if (state == nullptr || handler == nullptr || handler[0] == '\0') {
    return false;
  }

  const int frame = lua_absindex(state, frame_index);
  PushPrefixedField(state, frame, kScriptStoragePrefix, handler);
  if (lua_isfunction(state, -1) != 0) {
    return true;
  }
  lua_pop(state, 1);

  lua_getfield(state, frame, handler);
  if (lua_isfunction(state, -1) != 0) {
    return true;
  }
  lua_pop(state, 1);

  lua_getfield(state, frame, kScriptTableField.data());
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return false;
  }

  lua_getfield(state, -1, handler);
  lua_remove(state, -2);
  if (lua_isfunction(state, -1) != 0) {
    return true;
  }
  lua_pop(state, 1);
  return false;
}

int FrameScriptHandlerTaintSource(lua_State* const state, const int frame_index,
                                  const char* const handler) {
  if (state == nullptr || handler == nullptr || handler[0] == '\0') {
    return 0;
  }
  const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_taint(state);
  PushPrefixedField(state, frame_index, kLuaFrameScriptTaintFieldPrefix,
                    handler);
  const auto source = static_cast<int>(lua_tointeger(state, -1));
  lua_pop(state, 1);
  return source;
}

int InvokeFrameScriptFunction(
    lua_State* const state, const int self_index, const int argument_count,
    const FrameScriptInvocationKind kind,
    const FrameScriptInvocationSecurity security,
    const int error_handler_index,
    const int taint_source) {
  if (state == nullptr || argument_count < 0) {
    return LUA_ERRRUN;
  }

  const int entry_top = lua_gettop(state);
  if (entry_top <= 0 || argument_count >= entry_top) {
    return LUA_ERRRUN;
  }
  const int function_index = entry_top - argument_count;
  if (lua_isfunction(state, function_index) == 0) {
    return LUA_ERRRUN;
  }

  const int self = self_index != 0 ? lua_absindex(state, self_index) : 0;
  if (self != 0 && self >= function_index) {
    return LUA_ERRRUN;
  }
  const int error_handler = error_handler_index != 0
                                ? lua_absindex(state, error_handler_index)
                                : 0;

  const bool is_event = kind == FrameScriptInvocationKind::kEvent;
  if (is_event && argument_count == 0) {
    return LUA_ERRRUN;
  }
  const int event_argument_count = is_event ? 1 : 0;
  const int legacy_argument_count = argument_count - event_argument_count;
  const std::int64_t saved_global_count =
      static_cast<std::int64_t>(self != 0 ? 1 : 0) +
      event_argument_count + legacy_argument_count;
  const std::int64_t required_stack =
      saved_global_count + 1 + (self != 0 ? 1 : 0) + argument_count + 1;
  if (required_stack > std::numeric_limits<int>::max() ||
      lua_checkstack(state, static_cast<int>(required_stack)) == 0) {
    return LUA_ERRMEM;
  }

  const int first_argument_index = function_index + 1;
  const auto caller_taint = openwow::ui::lua_get_execution_taint_state(state);
  const TaintSourceId slot_taint = [&] {
    switch (security) {
      case FrameScriptInvocationSecurity::kSecure:
        return TaintSourceId{0};
      case FrameScriptInvocationSecurity::kInsecure:
        return taint_source;
      case FrameScriptInvocationSecurity::kCurrent:
        return caller_taint.source;
    }
    return caller_taint.source;
  }();

  const int stored_function_taint =
      openwow::ui::lua_get_taint(state, function_index);
  const TaintSourceId handler_taint =
      stored_function_taint != 0 ? stored_function_taint : slot_taint;

  openwow::ui::lua_set_execution_taint_state(state, {});

  if (self != 0) {
    PushRawGlobal(state, "this");
    lua_pushvalue(state, self);
    openwow::ui::lua_set_taint(state, -1, handler_taint);
    SetRawGlobalFromIndex(state, "this", -1);
    lua_pop(state, 1);
  }

  if (is_event) {
    PushRawGlobal(state, "event");
    SetRawGlobalFromIndex(state, "event", first_argument_index);
  }

  for (int index = 0; index < legacy_argument_count; ++index) {
    char name[32];
    FormatLegacyArgumentName(name, index + 1);
    PushRawGlobal(state, name);
    SetRawGlobalFromIndex(
        state, name,
        first_argument_index + event_argument_count + index);
  }

  const int saved_globals_start = entry_top + 1;
  lua_pushvalue(state, function_index);
  openwow::ui::lua_set_taint(state, -1, handler_taint);
  int call_argument_count = argument_count;
  if (self != 0) {
    lua_pushvalue(state, self);
    openwow::ui::lua_set_taint(state, -1, handler_taint);
    ++call_argument_count;
  }
  for (int index = 0; index < argument_count; ++index) {
    lua_pushvalue(state, first_argument_index + index);
  }

  openwow::ui::lua_set_execution_taint_state(state, caller_taint);
  const int status = ProtectedFrameScriptCall(
      state, call_argument_count, error_handler, security, handler_taint);

  openwow::ui::lua_set_execution_taint_state(state, {});

  if (status != LUA_OK) {
    lua_insert(state, saved_globals_start);
  }

  for (int index = legacy_argument_count; index > 0; --index) {
    char name[32];
    FormatLegacyArgumentName(name, index);
    RestoreRawGlobalFromTop(state, name);
  }
  if (is_event) {
    RestoreRawGlobalFromTop(state, "event");
  }
  if (self != 0) {
    RestoreRawGlobalFromTop(state, "this");
  }

  if (status == LUA_OK) {
    lua_settop(state, function_index - 1);
  } else {

    lua_replace(state, function_index);
    lua_settop(state, function_index);
  }
  openwow::ui::lua_set_execution_taint_state(state, caller_taint);
  return status;
}

FrameScriptInvocationResult InvokeFrameScriptHandler(
    lua_State* const state, const int frame_index, const char* const handler,
    const int argument_count, const FrameScriptInvocationKind kind,
    const int error_handler_index) {
  if (state == nullptr || argument_count < 0 ||
      argument_count > lua_gettop(state)) {
    return {.invoked = false, .status = LUA_ERRRUN};
  }

  const int error_handler = error_handler_index != 0
                                ? lua_absindex(state, error_handler_index)
                                : 0;
  const int frame = lua_absindex(state, frame_index);
  const int top = lua_gettop(state);
  const int first_argument_index = top - argument_count + 1;
  int registered_error_handler = 0;
  if (error_handler == 0) {
    lua_getfield(state, LUA_REGISTRYINDEX,
                 openwow::ui::kGameLuaErrorHandlerRegistryKey);
    if (lua_isfunction(state, -1) != 0) {
      registered_error_handler = first_argument_index;
      lua_insert(state, registered_error_handler);
    } else {
      lua_pop(state, 1);
    }
  }
  const auto caller_taint = openwow::ui::lua_get_execution_taint_state(state);
  openwow::ui::lua_set_execution_taint_state(state, {});
  if (!PushFrameScriptHandler(state, frame, handler)) {
    lua_settop(state, top - argument_count +
                          (registered_error_handler != 0 ? 1 : 0));
    if (registered_error_handler != 0) {
      lua_remove(state, registered_error_handler);
    }
    openwow::ui::lua_set_execution_taint_state(state, caller_taint);
    return {.invoked = false, .status = LUA_OK};
  }

  const int taint_source = FrameScriptHandlerTaintSource(state, frame, handler);
  const auto security = taint_source != 0
                            ? FrameScriptInvocationSecurity::kInsecure
                            : FrameScriptInvocationSecurity::kSecure;
  openwow::ui::lua_set_execution_taint_state(state, caller_taint);
  if (argument_count > 0) {
    lua_insert(state, first_argument_index +
                          (registered_error_handler != 0 ? 1 : 0));
  }
  const int status = InvokeFrameScriptFunction(
      state, frame, argument_count, kind, security,
      registered_error_handler != 0 ? registered_error_handler : error_handler,
      taint_source);
  if (registered_error_handler != 0) {
    lua_remove(state, registered_error_handler);
  }

  if (status != LUA_OK) {
    const char* const message =
        lua_isstring(state, -1) != 0 ? lua_tostring(state, -1) : nullptr;
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        std::string("FrameScript handler failed: ") +
            (handler != nullptr ? handler : "<unnamed>") + ": " +
            (message != nullptr ? message : "unknown Lua error"));
  }
  return {.invoked = true, .status = status};
}

}

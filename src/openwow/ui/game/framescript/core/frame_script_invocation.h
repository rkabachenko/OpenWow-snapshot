#pragma once

#include <cstdint>
#include <string_view>

struct lua_State;

namespace openwow::ui::game {

inline constexpr std::string_view kLuaFrameScriptTaintFieldPrefix =
    "__ow_script_taint_";

enum class FrameScriptInvocationKind : std::uint8_t {
  kHandler,
  kEvent,
};

enum class FrameScriptInvocationSecurity : std::uint8_t {
  kCurrent,
  kSecure,
  kInsecure,
};

struct FrameScriptInvocationResult {
  bool invoked{false};
  int status{0};
};

[[nodiscard]] bool PushFrameScriptHandler(lua_State* state, int frame_index,
                                          const char* handler);

[[nodiscard]] int FrameScriptHandlerTaintSource(lua_State* state,
                                                int frame_index,
                                                const char* handler);

[[nodiscard]] int InvokeFrameScriptFunction(
    lua_State* state, int self_index, int argument_count,
    FrameScriptInvocationKind kind = FrameScriptInvocationKind::kHandler,
    FrameScriptInvocationSecurity security =
        FrameScriptInvocationSecurity::kCurrent,
    int error_handler_index = 0,
    int taint_source = 0);

[[nodiscard]] FrameScriptInvocationResult InvokeFrameScriptHandler(
    lua_State* state, int frame_index, const char* handler,
    int argument_count,
    FrameScriptInvocationKind kind = FrameScriptInvocationKind::kHandler,
    int error_handler_index = 0);

}

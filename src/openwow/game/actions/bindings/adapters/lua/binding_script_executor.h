#pragma once

#include <string_view>
#include <cstdint>
#include <optional>

struct lua_State;

namespace openwow::game {
class MacroCatalog;
}

namespace openwow::game::actions::bindings::adapters::lua {

struct BindingScriptInvocation {
  bool key_down{false};
  float pressure{0.0f};
  float angle{-1.0f};
  float precision{0.0f};
};

class ScopedLuaModifierState {
 public:
  ScopedLuaModifierState(lua_State* state, std::uint16_t modifier_state);
  ~ScopedLuaModifierState();

  ScopedLuaModifierState(const ScopedLuaModifierState&) = delete;
  ScopedLuaModifierState& operator=(const ScopedLuaModifierState&) = delete;

 private:
  lua_State* state_;
  std::optional<std::uint16_t> previous_state_;
};

void ExecuteBindingScript(lua_State* state, int script_reference,
                          const BindingScriptInvocation& invocation);
void ReleaseBindingScript(lua_State* state, int script_reference);
[[nodiscard]] int CompileBindingScript(lua_State* state,
                                       std::string_view chunk_name,
                                       std::string_view body);
[[nodiscard]] bool DispatchClickBinding(MacroCatalog* macros,
                                        lua_State* state,
                                        std::string_view command,
                                        bool key_down);

}

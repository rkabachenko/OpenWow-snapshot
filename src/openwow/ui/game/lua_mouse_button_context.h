#pragma once

#include <lua.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace openwow::ui::game::lua_adapter {

inline constexpr char kCurrentMouseButtonRegistryKey[] =
    "openwow.current_mouse_button";

[[nodiscard]] inline std::optional<std::string> CurrentMouseButtonOverride(
    lua_State* lua) {
  lua_getfield(lua, LUA_REGISTRYINDEX, kCurrentMouseButtonRegistryKey);
  std::optional<std::string> button;
  if (const char* value = lua_tostring(lua, -1); value != nullptr) {
    button = value;
  }
  lua_pop(lua, 1);
  return button;
}

class ScopedMouseButtonOverride final {
 public:
  ScopedMouseButtonOverride(lua_State* lua, const std::string_view button)
      : lua_(lua), previous_(lua != nullptr ? CurrentMouseButtonOverride(lua)
                                            : std::nullopt) {
    Store(button.empty() ? std::nullopt
                         : std::optional<std::string_view>{button});
  }

  ~ScopedMouseButtonOverride() {
    Store(previous_.has_value()
              ? std::optional<std::string_view>{*previous_}
              : std::nullopt);
  }

  ScopedMouseButtonOverride(const ScopedMouseButtonOverride&) = delete;
  ScopedMouseButtonOverride& operator=(const ScopedMouseButtonOverride&) = delete;

 private:
  void Store(const std::optional<std::string_view> button) const {
    if (lua_ == nullptr) {
      return;
    }
    if (button.has_value()) {
      lua_pushlstring(lua_, button->data(), button->size());
    } else {
      lua_pushnil(lua_);
    }
    lua_setfield(lua_, LUA_REGISTRYINDEX, kCurrentMouseButtonRegistryKey);
  }

  lua_State* lua_{nullptr};
  std::optional<std::string> previous_;
};

}

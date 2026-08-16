#pragma once

#include "openwow/game/actions/bindings/model/binding_types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace openwow::game {
class BindingProfiles;
class MacroCatalog;
class WorldSession;
}
struct lua_State;

namespace openwow::game::actions::bindings::adapters::lua {

class BindingCommandExecutor {
 public:
  struct ItemBindingTarget {
    std::uint64_t guid{0};
    std::uint32_t entry{0};
    std::uint32_t flags{0};
  };
  using ItemLookup =
      std::function<std::optional<ItemBindingTarget>(std::uint32_t entry)>;

  [[nodiscard]] bool ExecuteWorld(
      BindingProfiles& profiles,
      WorldSession& session,
      lua_State& state,
      MacroCatalog& macros,
      const ItemLookup& item_lookup,
      const BindingCommand& command,
      bool pressed,
      std::string_view mouse_button = {},
      std::optional<std::uint16_t> modifier_state = std::nullopt,
      std::uint32_t current_mouse_button_flag = 0u);

  [[nodiscard]] bool ExecuteLua(
      BindingProfiles& profiles,
      lua_State& state,
      const BindingCommand& command,
      bool pressed,
      std::string_view mouse_button = {},
      std::optional<std::uint16_t> modifier_state = std::nullopt,
      std::uint32_t current_mouse_button_flag = 0u);

  [[nodiscard]] bool ExecuteMacro(
      BindingProfiles& profiles,
      MacroCatalog& macros,
      const BindingCommand& command,
      bool pressed,
      std::optional<std::uint16_t> modifier_state = std::nullopt);

  [[nodiscard]] bool ExecuteCore(
      BindingProfiles& profiles,
      const BindingCommand& command,
      bool pressed,
      std::optional<std::uint16_t> modifier_state = std::nullopt);
};

}

#include "openwow/game/actions/bindings/adapters/lua/binding_command_executor.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/actions/bindings/adapters/lua/binding_script_executor.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/spells/adapters/lua/spell_name_dispatch.h"
#include "openwow/game/world_session.h"
#include "openwow/input/input_manager.h"
#include "openwow/ui/game/lua_mouse_button_context.h"

#include <SDL.h>

#include <charconv>
#include <string>

extern "C" {
#include <lua.hpp>
}

namespace openwow::game::actions::bindings::adapters::lua {
namespace {

constexpr char kCurrentMouseButtonMaskRegistryKey[] =
    "openwow.current_mouse_button_mask";

std::optional<std::uint32_t> CurrentMouseButtonMaskOverride(
    lua_State* state) {
  lua_getfield(
      state, LUA_REGISTRYINDEX, kCurrentMouseButtonMaskRegistryKey);
  std::optional<std::uint32_t> mask;
  if (lua_isnumber(state, -1) != 0) {
    mask = static_cast<std::uint32_t>(lua_tointeger(state, -1));
  }
  lua_pop(state, 1);
  return mask;
}

class ScopedMouseButtonMask {
 public:
  ScopedMouseButtonMask(
      lua_State* state, const std::optional<std::uint32_t> mask)
      : state_(state),
        previous_(state != nullptr
                      ? CurrentMouseButtonMaskOverride(state)
                      : std::nullopt) {
    Set(mask);
  }

  ~ScopedMouseButtonMask() { Set(previous_); }

 private:
  void Set(const std::optional<std::uint32_t> mask) const {
    if (state_ == nullptr) {
      return;
    }
    if (mask) {
      lua_pushinteger(state_, static_cast<lua_Integer>(*mask));
    } else {
      lua_pushnil(state_);
    }
    lua_setfield(
        state_, LUA_REGISTRYINDEX, kCurrentMouseButtonMaskRegistryKey);
  }

  lua_State* state_;
  std::optional<std::uint32_t> previous_;
};

std::uint32_t ApplyMouseButtonEvent(
    const std::uint32_t current_mask,
    const std::uint32_t button,
    const bool pressed) {
  if (button == 0u) {
    return current_mask;
  }
  return pressed ? current_mask | button : current_mask & ~button;
}

[[nodiscard]] bool StartsWithIgnoreCase(
    const std::string_view value,
    const std::string_view prefix) {
  return value.size() >= prefix.size() &&
         openwow::text::EqualsIgnoreCaseAscii(
             value.substr(0, prefix.size()), prefix);
}

[[nodiscard]] std::uint64_t ResolveTarget(
    BindingProfiles& profiles,
    WorldSession& session,
    const std::uint16_t modifier_state,
    const std::string_view mouse_button) {
  if (::openwow::game::actions::bindings::adapters::retail::
          IsModifiedClickActive(
              profiles, "SELFCAST", modifier_state, mouse_button)) {
    return session.objects().GetActivePlayerGuid().GetRawValue();
  }
  if (::openwow::game::actions::bindings::adapters::retail::
          IsModifiedClickActive(
              profiles, "FOCUSCAST", modifier_state, mouse_button)) {
    return session.objects().GetFocusTargetGuid().GetRawValue();
  }
  return 0;
}

void DispatchSpell(BindingProfiles& profiles,
                   WorldSession& session,
                   lua_State* state,
                   const std::string_view query,
                   const std::string_view mouse_button,
                   const std::uint16_t modifier_state) {
  const auto spell =
      spells::adapters::lua::ResolveSpellName(
          state, std::string(query));
  if (!spell) return;
  spells::adapters::lua::DispatchResolvedSpell(
      state, *spell,
      ResolveTarget(profiles, session, modifier_state, mouse_button));
}

void DispatchItem(BindingProfiles& profiles,
                  WorldSession& session,
                  const BindingCommandExecutor::ItemLookup& item_lookup,
                  const std::string_view item_name,
                  const std::string_view mouse_button,
                  const std::uint16_t modifier_state) {
  if (!item_lookup || item_name.empty()) return;
  const auto* item_template =
      session.query_cache().GetItemTemplateByName(std::string(item_name));
  if (item_template == nullptr) return;
  const auto item = item_lookup(item_template->entry);
  if (!item) return;
  const auto target =
      ResolveTarget(profiles, session, modifier_state, mouse_button);
  if (!session.interaction().TryQueueBindOnUseConfirmation(
          item->guid, item->entry, item->flags, target)) {
    (void)session.interaction().SendUseItemByGuid(item->guid, 0, target);
  }
}

void DispatchMacro(MacroCatalog* macros, const std::string_view query) {
  if (macros == nullptr || query.empty()) return;
  int slot = -1;
  unsigned slot_number = 0;
  const auto parsed =
      std::from_chars(query.data(), query.data() + query.size(), slot_number);
  if (parsed.ec == std::errc{} &&
      parsed.ptr == query.data() + query.size() && slot_number != 0u) {
    slot = static_cast<int>(slot_number) - 1;
  } else if (const auto macro = macros->FindMacroByName(query);
             macro) {
    slot = macros->FindSlotIndex(macro->id);
  }
  if (slot >= 0) {
    macros->ExecuteBody(
        static_cast<std::uint32_t>(slot),
        actions::macros::MacroInputButton::FromCompatibilityText(
            "LeftButton"));
  }
}

}

namespace {

bool ExecuteBindingCommand(
    BindingProfiles& profiles,
    WorldSession* session,
    lua_State* state,
    MacroCatalog* macros,
    const BindingCommandExecutor::ItemLookup* item_lookup,
    const BindingCommand& command,
    const bool pressed,
    const std::string_view mouse_button,
    const std::optional<std::uint16_t> modifier_state,
    const std::uint32_t current_mouse_button_flag) {
  if (command.empty()) return false;
  const std::uint16_t modifiers = modifier_state.value_or(
      static_cast<std::uint16_t>(SDL_GetModState()));
  const ::openwow::game::actions::bindings::adapters::lua::
      ScopedLuaModifierState modifier_snapshot(state, modifiers);
  const openwow::ui::game::lua_adapter::ScopedMouseButtonOverride
      mouse_button_snapshot(state, mouse_button);
  const std::optional<std::uint32_t> button_mask =
      current_mouse_button_flag == 0u
          ? std::nullopt
          : std::optional<std::uint32_t>(
                ApplyMouseButtonEvent(
                    openwow::input::InputManager::Get().GetMouseButtonFlags(),
                    current_mouse_button_flag, pressed));
  const ScopedMouseButtonMask button_mask_snapshot(state, button_mask);

  const std::string_view text = command.value();
  if (StartsWithIgnoreCase(text, "SPELL ")) {
    if (!pressed && session != nullptr) {
      DispatchSpell(profiles, *session, state, text.substr(6), mouse_button,
                    modifiers);
    }
    return true;
  }
  if (StartsWithIgnoreCase(text, "ITEM ")) {
    if (!pressed && session != nullptr && item_lookup != nullptr) {
      DispatchItem(profiles, *session, *item_lookup, text.substr(5),
                   mouse_button, modifiers);
    }
    return true;
  }
  if (StartsWithIgnoreCase(text, "MACRO ")) {
    if (!pressed) DispatchMacro(macros, text.substr(6));
    return true;
  }
  if (StartsWithIgnoreCase(text, "CLICK ")) {
    return ::openwow::game::actions::bindings::adapters::lua::
        DispatchClickBinding(macros, state, text, pressed);
  }
  if (profiles.RunNamedBinding(command, pressed, pressed ? 1.0f : 0.0f,
                               true, false, false, -1.0f, 0.0f,
                               modifier_state)) {
    return true;
  }
  return profiles.ExecuteUnhandledBindingCommand(command, pressed);
}

}

bool BindingCommandExecutor::ExecuteWorld(
    BindingProfiles& profiles,
    WorldSession& session,
    lua_State& state,
    MacroCatalog& macros,
    const ItemLookup& item_lookup,
    const BindingCommand& command,
    const bool pressed,
    const std::string_view mouse_button,
    const std::optional<std::uint16_t> modifier_state,
    const std::uint32_t current_mouse_button_flag) {
  return ExecuteBindingCommand(
      profiles, &session, &state, &macros, &item_lookup, command, pressed,
      mouse_button,
      modifier_state, current_mouse_button_flag);
}

bool BindingCommandExecutor::ExecuteLua(
    BindingProfiles& profiles,
    lua_State& state,
    const BindingCommand& command,
    const bool pressed,
    const std::string_view mouse_button,
    const std::optional<std::uint16_t> modifier_state,
    const std::uint32_t current_mouse_button_flag) {
  return ExecuteBindingCommand(
      profiles, nullptr, &state, nullptr, nullptr, command, pressed,
      mouse_button,
      modifier_state, current_mouse_button_flag);
}

bool BindingCommandExecutor::ExecuteMacro(
    BindingProfiles& profiles,
    MacroCatalog& macros,
    const BindingCommand& command,
    const bool pressed,
    const std::optional<std::uint16_t> modifier_state) {
  return ExecuteBindingCommand(
      profiles, nullptr, nullptr, &macros, nullptr, command, pressed, {},
      modifier_state,
      0u);
}

bool BindingCommandExecutor::ExecuteCore(
    BindingProfiles& profiles,
    const BindingCommand& command,
    const bool pressed,
    const std::optional<std::uint16_t> modifier_state) {
  if (profiles.RunNamedBinding(
          command, pressed, pressed ? 1.0f : 0.0f, true, false, false,
          -1.0f, 0.0f, modifier_state)) {
    return true;
  }
  return profiles.ExecuteUnhandledBindingCommand(command, pressed);
}

}

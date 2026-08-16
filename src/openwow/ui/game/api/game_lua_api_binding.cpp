#include "openwow/ui/game/api/game_lua_api_binding.h"

#include "openwow/game/actions/bindings/adapters/lua/binding_lua_values.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <string_view>
#include <utility>
#include <vector>

namespace binding_values =
    ::openwow::game::actions::bindings::adapters::lua;

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::BindingLuaString,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaString, Policy>::Read(
    lua_State* state, int index) -> Storage {
  if (lua_isstring(state, index) == 0) {
    return {};
  }
  std::size_t size = 0;
  const char* value = lua_tolstring(state, index, &size);
  return {{std::string(value, size)}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaString,
                  Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::BindingLuaNumber,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaNumber, Policy>::Read(
    lua_State* state, int index) noexcept -> Storage {
  return lua_isnumber(state, index) != 0
             ? Storage{{static_cast<double>(lua_tonumber(state, index))}}
             : Storage{};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaNumber,
                  Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::BindingLuaBoolean,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaBoolean, Policy>::Read(
    lua_State* state, int index) noexcept -> Storage {
  return {openwow::ui::game::detail::ReadClientBoolArgOrDefault(state, index,
                                                                false)};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaBoolean,
                  Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::BindingLuaOverrideOwner,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaOverrideOwner,
                  Policy>::Read(lua_State* state, int index) -> Storage {
  using openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable;
  using openwow::ui::game::detail::GetLuaFrameLookupObjectType;
  using openwow::ui::game::detail::IsFrameLikeLookupObjectType;
  using Owner = openwow::game::BindingProfiles::OverrideOwner;
  if (lua_istable(state, index) == 0) {
    return {};
  }
  index = lua_absindex(state, index);
  (void)CanonicalizeLuaScriptObjectTable(state, index);
  if (!IsFrameLikeLookupObjectType(GetLuaFrameLookupObjectType(state, index))) {
    return {};
  }
  lua_getfield(state, index, "__ow_ref");
  const bool has_reference = lua_isinteger(state, -1) != 0;
  const int reference = has_reference ? static_cast<int>(lua_tointeger(state, -1))
                                      : LUA_NOREF;
  lua_pop(state, 1);
  if (!has_reference || reference == LUA_NOREF) {
    return {};
  }
  lua_rawgeti(state, LUA_REGISTRYINDEX, reference);
  const bool matches = lua_istable(state, -1) != 0 &&
                       lua_rawequal(state, index, -1) != 0;
  lua_pop(state, 1);
  return matches ? Storage{{Owner::FromLuaFrameReference(reference)}}
                 : Storage{};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::BindingLuaOverrideOwner,
                   Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template struct LuaConverter<
    openwow::ui::game::detail::BindingLuaString,
    openwow::ui::game::detail::kBindingLuaConversion>;
template struct LuaConverter<
    openwow::ui::game::detail::BindingLuaNumber,
    openwow::ui::game::detail::kBindingLuaConversion>;
template struct LuaConverter<
    openwow::ui::game::detail::BindingLuaBoolean,
    openwow::ui::game::detail::kBindingLuaConversion>;
template struct LuaConverter<
    openwow::ui::game::detail::BindingLuaOverrideOwner,
    openwow::ui::game::detail::kBindingLuaConversion>;

}

namespace openwow::ui::game::detail {
namespace {

using ::openwow::game::BindingChord;
using ::openwow::game::BindingCommand;
using ::openwow::game::BindingProfileScope;
using ::openwow::game::BindingProfiles;
using ::openwow::game::BindingSlot;
using ::openwow::ui::lua::LuaUsageError;
using ::openwow::ui::lua::LuaVariableReturns;
using ::openwow::ui::lua::NoLuaResults;

constexpr int kProtectedBindingOperation =
    ::openwow::ui::game::protected_action_kind::kKeyBinding;
constexpr int kFirstLuaBindingSlot = 1;
constexpr int kLuaBindingSlotCount = 4;
constexpr std::string_view kDefaultMouseButton = "LeftButton";

int BindingMode(const BindingLuaNumber value) {
  return value.value ? TruncateLuaNumberToSseI32(*value.value) : 0;
}

std::optional<BindingSlot> ExplicitBindingSlot(const BindingLuaNumber value) {
  if (!value.value) {
    return std::nullopt;
  }
  const int ordinal = BindingMode(value);
  if (ordinal < kFirstLuaBindingSlot ||
      ordinal >= kFirstLuaBindingSlot + kLuaBindingSlotCount) {
    return std::nullopt;
  }
  return BindingSlot::FromValue(
      static_cast<std::uint8_t>(ordinal - kFirstLuaBindingSlot));
}

BindingSlot ReadBindingSlot(const BindingLuaNumber value) {
  return ExplicitBindingSlot(value).value_or(BindingSlot::Primary());
}

BindingProfiles::BindingSlotSelector ReadBindingSlotSelector(
    const BindingLuaNumber value) {
  const auto slot = ExplicitBindingSlot(value);
  return slot ? static_cast<BindingProfiles::BindingSlotSelector>(slot->value())
              : BindingProfiles::BindingSlotSelector::kCurrent;
}

bool CanMutateBindings(BindingProfiles* profiles) {
  return profiles != nullptr &&
         GameUI_CanPerformProtectedAction(kProtectedBindingOperation);
}

void FireBindingsUpdatedEvent() {
  ScriptEventDispatch::Get().FireEvent(events::UPDATE_BINDINGS);
}

openwow::ui::lua::LuaTruthy MutationResult(const bool succeeded) {
  if (succeeded) {
    FireBindingsUpdatedEvent();
  }
  return {succeeded};
}

enum class BindingCommandKind { kRaw, kSpell, kItem, kMacro, kClick };

std::string BuildBindingCommand(const BindingCommandKind kind,
                                const std::string_view value,
                                const std::string_view detail = {}) {
  switch (kind) {
    case BindingCommandKind::kRaw:
      return std::string(value);
    case BindingCommandKind::kSpell:
      return "SPELL " + std::string(value);
    case BindingCommandKind::kItem:
      return "ITEM " + std::string(value);
    case BindingCommandKind::kMacro:
      return "MACRO " + std::string(value);
    case BindingCommandKind::kClick:
      return "CLICK " + std::string(value) + ":" + std::string(detail);
  }
  return {};
}

BindingMutationOrError SetBindingValue(
    BindingProfiles* profiles, const BindingCommandKind kind,
    const char* usage, const BindingLuaString key,
    const BindingLuaString value, const BindingLuaString detail,
    const BindingLuaNumber mode, const bool value_required) {
  if (!key.value || (value_required && !value.value)) {
    return LuaUsageError{usage};
  }
  const bool succeeded =
      CanMutateBindings(profiles) &&
      binding_values::AssignBindingValue(
          *profiles, *key.value,
          BuildBindingCommand(kind, value.value.value_or(""),
                              detail.value.value_or(
                                  std::string(kDefaultMouseButton))),
          ReadBindingSlot(mode).value());
  return MutationResult(succeeded);
}

BindingVoidOrError SetOverrideBindingValue(
    BindingProfiles* profiles, const BindingCommandKind kind,
    const char* usage, const BindingLuaOverrideOwner owner,
    const BindingLuaBoolean priority, const BindingLuaString key,
    const BindingLuaString value, const BindingLuaString detail,
    const bool value_required) {
  if (!owner.value || !key.value || (value_required && !value.value)) {
    return LuaUsageError{usage};
  }
  if (CanMutateBindings(profiles)) {
    binding_values::AssignOverrideBindingValue(
        *profiles, *owner.value, priority.value, *key.value,
        BuildBindingCommand(kind, value.value.value_or(""),
                            detail.value.value_or(
                                std::string(kDefaultMouseButton))));
  }
  return NoLuaResults{};
}

}

void BindKeyBindingLuaContext(lua_State& state, BindingProfiles* profiles) {
  if (profiles != nullptr) {
    lua_pushlightuserdata(&state, profiles);
  } else {
    lua_pushnil(&state);
  }
  lua_setfield(&state, LUA_REGISTRYINDEX,
               openwow::ui::lua::LuaRegistryContext<BindingProfiles>::key.data());
}

int GetNumBindings(BindingProfiles* profiles) {
  return profiles != nullptr ? profiles->GetNumBindings() : 0;
}

int GetNumModifiedClickActions(BindingProfiles* profiles) {
  return profiles != nullptr ? profiles->GetNumModifiedClickActions() : 0;
}

BindingStringsOrError GetBinding(BindingProfiles* profiles,
                                 const BindingLuaNumber index,
                                 const BindingLuaNumber mode) {
  if (!index.value) {
    return LuaUsageError{"Usage: GetBinding(index[, mode])"};
  }
  if (profiles == nullptr) {
    return LuaVariableReturns<std::string>({""});
  }
  const auto binding = binding_values::ReadBindingValue(
      *profiles, static_cast<int>(*index.value), ReadBindingSlot(mode).value());
  if (!binding) {
    return LuaVariableReturns<std::string>({""});
  }
  std::vector<std::string> values;
  values.reserve(binding->keys.size() + 1);
  values.push_back(binding->command);
  values.insert(values.end(), binding->keys.begin(), binding->keys.end());
  return LuaVariableReturns<std::string>(std::move(values));
}

BindingStringsOrError GetBindingKey(BindingProfiles* profiles,
                                    const BindingLuaString command,
                                    const BindingLuaNumber mode) {
  if (!command.value) {
    return LuaUsageError{"Usage: GetBindingKey(\"COMMAND\"[, mode])"};
  }
  return LuaVariableReturns<std::string>(
      profiles != nullptr
          ? binding_values::ReadBindingKeys(
                *profiles, *command.value, ReadBindingSlotSelector(mode))
          : std::vector<std::string>{});
}

BindingStringOrError GetBindingAction(BindingProfiles* profiles,
                                      const BindingLuaString key,
                                      const BindingLuaBoolean check_override,
                                      const BindingLuaNumber mode) {
  if (!key.value) {
    return LuaUsageError{
        "Usage: GetBindingAction(\"KEY\"[, checkOverride][, mode])"};
  }
  return profiles != nullptr
             ? binding_values::ReadBindingAction(
                   *profiles, *key.value, check_override.value,
                   ReadBindingSlotSelector(mode))
             : std::string{};
}

BindingMutationOrError SetBinding(BindingProfiles* profiles,
                                  const BindingLuaString key,
                                  const BindingLuaString command,
                                  const BindingLuaNumber mode) {
  return SetBindingValue(
      profiles, BindingCommandKind::kRaw,
      "Usage: SetBinding(\"KEY\"[, \"COMMAND\"][, mode])", key, command,
      {}, mode, false);
}

BindingMutationOrError SetBindingSpell(BindingProfiles* profiles,
                                       const BindingLuaString key,
                                       const BindingLuaString spell,
                                       const BindingLuaNumber mode) {
  return SetBindingValue(
      profiles, BindingCommandKind::kSpell,
      "Usage: SetBindingSpell(\"KEY\", \"spellname\"[, mode])", key, spell,
      {}, mode, true);
}

BindingMutationOrError SetBindingItem(BindingProfiles* profiles,
                                      const BindingLuaString key,
                                      const BindingLuaString item,
                                      const BindingLuaNumber mode) {
  return SetBindingValue(
      profiles, BindingCommandKind::kItem,
      "Usage: SetBindingItem(\"KEY\", \"itemname\"[, mode])", key, item, {},
      mode, true);
}

BindingMutationOrError SetBindingMacro(BindingProfiles* profiles,
                                       const BindingLuaString key,
                                       const BindingLuaString macro,
                                       const BindingLuaNumber mode) {
  return SetBindingValue(
      profiles, BindingCommandKind::kMacro,
      "Usage: SetBindingMacro(\"KEY\", \"macroname\"|macroid[, mode])", key,
      macro, {}, mode, true);
}

BindingMutationOrError SetBindingClick(
    BindingProfiles* profiles, const BindingLuaString key,
    const BindingLuaString button, const BindingLuaString mouse_button,
    const BindingLuaNumber mode) {
  return SetBindingValue(
      profiles, BindingCommandKind::kClick,
      "Usage: SetBindingClick(\"KEY\", \"buttonName\"[, \"mouseButton\"][, mode])",
      key, button, mouse_button, mode, true);
}

BindingVoidOrError SetOverrideBinding(
    BindingProfiles* profiles, const BindingLuaOverrideOwner owner,
    const BindingLuaBoolean priority, const BindingLuaString key,
    const BindingLuaString command) {
  return SetOverrideBindingValue(
      profiles, BindingCommandKind::kRaw,
      "Usage: SetOverrideBinding(owner, isPriority, \"KEY\"[, \"COMMAND\"])",
      owner, priority, key, command, {}, false);
}

BindingVoidOrError ClearOverrideBindings(
    BindingProfiles* profiles, const BindingLuaOverrideOwner owner) {
  if (!owner.value) {
    return LuaUsageError{"Usage: ClearOverrideBindings(owner)"};
  }
  if (CanMutateBindings(profiles)) {
    profiles->ClearOverrideBindings(*owner.value);
  }
  return NoLuaResults{};
}

BindingVoidOrError SaveBindings(BindingProfiles* profiles,
                                const BindingLuaNumber mode_value) {
  const int mode = BindingMode(mode_value);
  if (mode != static_cast<int>(BindingProfileScope::kAccount) &&
      mode != static_cast<int>(BindingProfileScope::kCharacter)) {
    return LuaUsageError{"Usage: SaveBindings(1||2)"};
  }
  if (profiles != nullptr) {
    profiles->SaveBindings(static_cast<BindingProfileScope>(mode));
  }
  return NoLuaResults{};
}

BindingVoidOrError LoadBindings(BindingProfiles* profiles,
                                const BindingLuaNumber mode_value) {
  const int mode = BindingMode(mode_value);
  if (mode > static_cast<int>(BindingProfileScope::kCharacter)) {
    return LuaUsageError{"Usage: LoadBindings(0||1||2)"};
  }
  if (profiles != nullptr) {
    profiles->LoadBindings(static_cast<BindingProfileScope>(mode));
  }
  return NoLuaResults{};
}

int GetCurrentBindingSet(BindingProfiles* profiles) {
  return static_cast<int>(
      profiles != nullptr && profiles->GetCurrentBindingSet() ==
                                 BindingProfileScope::kCharacter
          ? BindingProfileScope::kCharacter
          : BindingProfileScope::kAccount);
}

BindingVoidOrError RunBinding(BindingProfiles* profiles,
                              const BindingLuaString command,
                              const BindingLuaString phase) {
  if (!command.value) {
    return LuaUsageError{"Usage: RunBinding(\"COMMAND\")"};
  }
  const bool key_down =
      !openwow::text::EqualsIgnoreCaseAscii(phase.value.value_or(""), "up");
  if (profiles != nullptr) {
    profiles->RunNamedBinding(BindingCommand(*command.value), key_down,
                              key_down ? 1.0f : 0.0f);
  }
  return NoLuaResults{};
}

BindingStringsOrError GetBindingByKey(BindingProfiles* profiles,
                                      const BindingLuaString key,
                                      const BindingLuaNumber mode) {
  if (!key.value) {
    return LuaUsageError{"Usage: GetBindingByKey(\"action\"[, mode])"};
  }
  if (profiles == nullptr || key.value->empty()) {
    return LuaVariableReturns<std::string>({});
  }
  const auto command = profiles->ResolveChordWithFallbackInActiveSlots(
      BindingChord(*key.value), ReadBindingSlotSelector(mode));
  return LuaVariableReturns<std::string>(
      command ? std::vector<std::string>{command->value()}
              : std::vector<std::string>{});
}

BindingVoidOrError SetOverrideBindingSpell(
    BindingProfiles* profiles, const BindingLuaOverrideOwner owner,
    const BindingLuaBoolean priority, const BindingLuaString key,
    const BindingLuaString spell) {
  return SetOverrideBindingValue(
      profiles, BindingCommandKind::kSpell,
      "Usage: SetOverrideBindingSpell(owner, isPriority, \"KEY\", \"spellname\")",
      owner, priority, key, spell, {}, true);
}

BindingVoidOrError SetOverrideBindingClick(
    BindingProfiles* profiles, const BindingLuaOverrideOwner owner,
    const BindingLuaBoolean priority, const BindingLuaString key,
    const BindingLuaString button, const BindingLuaString mouse_button) {
  return SetOverrideBindingValue(
      profiles, BindingCommandKind::kClick,
      "Usage: SetOverrideBindingClick(owner, isPriority, \"KEY\", \"buttonName\"[, \"mouseButton\"])",
      owner, priority, key, button, mouse_button, true);
}

BindingVoidOrError SetOverrideBindingItem(
    BindingProfiles* profiles, const BindingLuaOverrideOwner owner,
    const BindingLuaBoolean priority, const BindingLuaString key,
    const BindingLuaString item) {
  return SetOverrideBindingValue(
      profiles, BindingCommandKind::kItem,
      "Usage: SetOverrideBindingItem(owner, isPriority, \"KEY\", \"itemname\")",
      owner, priority, key, item, {}, true);
}

BindingVoidOrError SetOverrideBindingMacro(
    BindingProfiles* profiles, const BindingLuaOverrideOwner owner,
    const BindingLuaBoolean priority, const BindingLuaString key,
    const BindingLuaString macro) {
  return SetOverrideBindingValue(
      profiles, BindingCommandKind::kMacro,
      "Usage: SetOverrideBindingMacro(owner, isPriority, \"KEY\", \"macroname\"|macroid)",
      owner, priority, key, macro, {}, true);
}

}

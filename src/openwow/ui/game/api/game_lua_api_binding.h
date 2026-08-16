#pragma once

#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/ui/runtime/lua/lua_binding.h"

#include <optional>
#include <string>
#include <variant>

namespace openwow::ui::game::detail {

struct BindingLuaString final {
  std::optional<std::string> value;
};

struct BindingLuaNumber final {
  std::optional<double> value;
};

struct BindingLuaBoolean final {
  bool value{false};
};

struct BindingLuaOverrideOwner final {
  std::optional<openwow::game::BindingProfiles::OverrideOwner> value;
};

using BindingStringsOrError = std::variant<
    openwow::ui::lua::LuaVariableReturns<std::string>,
    openwow::ui::lua::LuaUsageError>;
using BindingStringOrError =
    std::variant<std::string, openwow::ui::lua::LuaUsageError>;
using BindingMutationOrError =
    std::variant<openwow::ui::lua::LuaTruthy,
                 openwow::ui::lua::LuaUsageError>;
using BindingVoidOrError =
    std::variant<openwow::ui::lua::NoLuaResults,
                 openwow::ui::lua::LuaUsageError>;

void BindKeyBindingLuaContext(lua_State& state,
                               openwow::game::BindingProfiles* profiles);
int GetNumBindings(openwow::game::BindingProfiles* profiles);
int GetNumModifiedClickActions(openwow::game::BindingProfiles* profiles);
BindingStringsOrError GetBinding(openwow::game::BindingProfiles* profiles,
                                 BindingLuaNumber index,
                                 BindingLuaNumber mode);
BindingStringsOrError GetBindingKey(openwow::game::BindingProfiles* profiles,
                                    BindingLuaString command,
                                    BindingLuaNumber mode);
BindingStringOrError GetBindingAction(
    openwow::game::BindingProfiles* profiles, BindingLuaString key,
    BindingLuaBoolean check_override, BindingLuaNumber mode);
BindingMutationOrError SetBinding(openwow::game::BindingProfiles* profiles,
                                  BindingLuaString key,
                                  BindingLuaString command,
                                  BindingLuaNumber mode);
BindingMutationOrError SetBindingSpell(
    openwow::game::BindingProfiles* profiles, BindingLuaString key,
    BindingLuaString spell, BindingLuaNumber mode);
BindingMutationOrError SetBindingItem(
    openwow::game::BindingProfiles* profiles, BindingLuaString key,
    BindingLuaString item, BindingLuaNumber mode);
BindingMutationOrError SetBindingMacro(
    openwow::game::BindingProfiles* profiles, BindingLuaString key,
    BindingLuaString macro, BindingLuaNumber mode);
BindingMutationOrError SetBindingClick(
    openwow::game::BindingProfiles* profiles, BindingLuaString key,
    BindingLuaString button, BindingLuaString mouse_button,
    BindingLuaNumber mode);
BindingVoidOrError SetOverrideBinding(
    openwow::game::BindingProfiles* profiles, BindingLuaOverrideOwner owner,
    BindingLuaBoolean priority, BindingLuaString key,
    BindingLuaString command);
BindingVoidOrError ClearOverrideBindings(
    openwow::game::BindingProfiles* profiles,
    BindingLuaOverrideOwner owner);
BindingVoidOrError SaveBindings(openwow::game::BindingProfiles* profiles,
                                BindingLuaNumber mode);
BindingVoidOrError LoadBindings(openwow::game::BindingProfiles* profiles,
                                BindingLuaNumber mode);
int GetCurrentBindingSet(openwow::game::BindingProfiles* profiles);
BindingVoidOrError RunBinding(openwow::game::BindingProfiles* profiles,
                              BindingLuaString command,
                              BindingLuaString phase);
BindingStringsOrError GetBindingByKey(
    openwow::game::BindingProfiles* profiles, BindingLuaString key,
    BindingLuaNumber mode);
BindingVoidOrError SetOverrideBindingSpell(
    openwow::game::BindingProfiles* profiles, BindingLuaOverrideOwner owner,
    BindingLuaBoolean priority, BindingLuaString key,
    BindingLuaString spell);
BindingVoidOrError SetOverrideBindingClick(
    openwow::game::BindingProfiles* profiles, BindingLuaOverrideOwner owner,
    BindingLuaBoolean priority, BindingLuaString key,
    BindingLuaString button, BindingLuaString mouse_button);
BindingVoidOrError SetOverrideBindingItem(
    openwow::game::BindingProfiles* profiles, BindingLuaOverrideOwner owner,
    BindingLuaBoolean priority, BindingLuaString key,
    BindingLuaString item);
BindingVoidOrError SetOverrideBindingMacro(
    openwow::game::BindingProfiles* profiles, BindingLuaOverrideOwner owner,
    BindingLuaBoolean priority, BindingLuaString key,
    BindingLuaString macro);

}

namespace openwow::ui::lua {

template <>
struct LuaRegistryContext<openwow::game::BindingProfiles> {
  static constexpr std::string_view key = "openwow.key_binding_manager";
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::BindingLuaString, Policy> {
  using Storage = openwow::ui::game::detail::BindingLuaString;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::BindingLuaNumber, Policy> {
  using Storage = openwow::ui::game::detail::BindingLuaNumber;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index) noexcept;
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::BindingLuaBoolean, Policy> {
  using Storage = openwow::ui::game::detail::BindingLuaBoolean;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index) noexcept;
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::BindingLuaOverrideOwner, Policy> {
  using Storage = openwow::ui::game::detail::BindingLuaOverrideOwner;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

}

namespace openwow::ui::lua::detail {

template <>
struct IsOptional<openwow::ui::game::detail::BindingLuaString>
    : std::true_type {};

template <>
struct IsOptional<openwow::ui::game::detail::BindingLuaNumber>
    : std::true_type {};

template <>
struct IsOptional<openwow::ui::game::detail::BindingLuaBoolean>
    : std::true_type {};

template <>
struct IsOptional<openwow::ui::game::detail::BindingLuaOverrideOwner>
    : std::true_type {};

}

namespace openwow::ui::game::detail {

inline constexpr openwow::ui::lua::ConversionPolicy kBindingLuaConversion{
    openwow::ui::lua::IntegralConversion::kTruncate,
    true, false, true, true, true};

inline constexpr auto kGetNumBindings =
    openwow::ui::lua::bind<&GetNumBindings, kBindingLuaConversion>(
        "GetNumBindings");
inline constexpr auto kGetNumModifiedClickActions =
    openwow::ui::lua::bind<&GetNumModifiedClickActions,
                           kBindingLuaConversion>(
        "GetNumModifiedClickActions");
inline constexpr auto kGetBinding =
    openwow::ui::lua::bind<&GetBinding, kBindingLuaConversion>("GetBinding");
inline constexpr auto kGetBindingKey =
    openwow::ui::lua::bind<&GetBindingKey, kBindingLuaConversion>(
        "GetBindingKey");
inline constexpr auto kGetBindingAction =
    openwow::ui::lua::bind<&GetBindingAction, kBindingLuaConversion>(
        "GetBindingAction");
inline constexpr auto kSetBinding =
    openwow::ui::lua::bind<&SetBinding, kBindingLuaConversion>("SetBinding");
inline constexpr auto kSetBindingSpell =
    openwow::ui::lua::bind<&SetBindingSpell, kBindingLuaConversion>(
        "SetBindingSpell");
inline constexpr auto kSetBindingItem =
    openwow::ui::lua::bind<&SetBindingItem, kBindingLuaConversion>(
        "SetBindingItem");
inline constexpr auto kSetBindingMacro =
    openwow::ui::lua::bind<&SetBindingMacro, kBindingLuaConversion>(
        "SetBindingMacro");
inline constexpr auto kSetBindingClick =
    openwow::ui::lua::bind<&SetBindingClick, kBindingLuaConversion>(
        "SetBindingClick");
inline constexpr auto kSetOverrideBinding =
    openwow::ui::lua::bind<&SetOverrideBinding, kBindingLuaConversion>(
        "SetOverrideBinding");
inline constexpr auto kClearOverrideBindings =
    openwow::ui::lua::bind<&ClearOverrideBindings, kBindingLuaConversion>(
        "ClearOverrideBindings");
inline constexpr auto kSaveBindings =
    openwow::ui::lua::bind<&SaveBindings, kBindingLuaConversion>(
        "SaveBindings");
inline constexpr auto kLoadBindings =
    openwow::ui::lua::bind<&LoadBindings, kBindingLuaConversion>(
        "LoadBindings");
inline constexpr auto kGetCurrentBindingSet =
    openwow::ui::lua::bind<&GetCurrentBindingSet, kBindingLuaConversion>(
        "GetCurrentBindingSet");
inline constexpr auto kRunBinding =
    openwow::ui::lua::bind<&RunBinding, kBindingLuaConversion>("RunBinding");
inline constexpr auto kGetBindingByKey =
    openwow::ui::lua::bind<&GetBindingByKey, kBindingLuaConversion>(
        "GetBindingByKey");
inline constexpr auto kSetOverrideBindingSpell =
    openwow::ui::lua::bind<&SetOverrideBindingSpell, kBindingLuaConversion>(
        "SetOverrideBindingSpell");
inline constexpr auto kSetOverrideBindingClick =
    openwow::ui::lua::bind<&SetOverrideBindingClick, kBindingLuaConversion>(
        "SetOverrideBindingClick");
inline constexpr auto kSetOverrideBindingItem =
    openwow::ui::lua::bind<&SetOverrideBindingItem, kBindingLuaConversion>(
        "SetOverrideBindingItem");
inline constexpr auto kSetOverrideBindingMacro =
    openwow::ui::lua::bind<&SetOverrideBindingMacro, kBindingLuaConversion>(
        "SetOverrideBindingMacro");

inline constexpr lua_CFunction LuaGetNumBindings = kGetNumBindings.handler;
inline constexpr lua_CFunction LuaGetNumModifiedClickActions =
    kGetNumModifiedClickActions.handler;
inline constexpr lua_CFunction LuaGetBinding = kGetBinding.handler;
inline constexpr lua_CFunction LuaGetBindingKey = kGetBindingKey.handler;
inline constexpr lua_CFunction LuaGetBindingAction = kGetBindingAction.handler;
inline constexpr lua_CFunction LuaSetBinding = kSetBinding.handler;
inline constexpr lua_CFunction LuaSetBindingSpell = kSetBindingSpell.handler;
inline constexpr lua_CFunction LuaSetBindingItem = kSetBindingItem.handler;
inline constexpr lua_CFunction LuaSetBindingMacro = kSetBindingMacro.handler;
inline constexpr lua_CFunction LuaSetBindingClick = kSetBindingClick.handler;
inline constexpr lua_CFunction LuaSetOverrideBinding =
    kSetOverrideBinding.handler;
inline constexpr lua_CFunction LuaClearOverrideBindings =
    kClearOverrideBindings.handler;
inline constexpr lua_CFunction LuaSaveBindings = kSaveBindings.handler;
inline constexpr lua_CFunction LuaLoadBindings = kLoadBindings.handler;
inline constexpr lua_CFunction LuaGetCurrentBindingSet =
    kGetCurrentBindingSet.handler;
inline constexpr lua_CFunction LuaRunBinding = kRunBinding.handler;
inline constexpr lua_CFunction LuaGetBindingByKey = kGetBindingByKey.handler;
inline constexpr lua_CFunction LuaSetOverrideBindingSpell =
    kSetOverrideBindingSpell.handler;
inline constexpr lua_CFunction LuaSetOverrideBindingClick =
    kSetOverrideBindingClick.handler;
inline constexpr lua_CFunction LuaSetOverrideBindingItem =
    kSetOverrideBindingItem.handler;
inline constexpr lua_CFunction LuaSetOverrideBindingMacro =
    kSetOverrideBindingMacro.handler;

}

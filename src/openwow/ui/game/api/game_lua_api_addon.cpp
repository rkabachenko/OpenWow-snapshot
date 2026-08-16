#include "openwow/ui/game/api/game_lua_api_addon.h"
#include "openwow/game/localization.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/game/addon_runtime_loader.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/lua_numeric.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::AddonLuaIdentifier,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::AddonLuaIdentifier, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  using Identifier = openwow::ui::game::detail::AddonLuaIdentifier;
  if (lua_isnumber(state, index) != 0) {
    return {Identifier::Kind::kIndex,
            openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, index)), {}};
  }
  if (lua_isstring(state, index) != 0) {
    const char* value = lua_tostring(state, index);
    return {Identifier::Kind::kName, 0, std::string(value)};
  }
  return {};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::AddonLuaIdentifier,
                  Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::AddonLuaString,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::AddonLuaString, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  if (lua_isstring(state, index) == 0) {
    return {};
  }
  std::size_t size = 0;
  const char* value = lua_tolstring(state, index, &size);
  return {{std::string(value, size)}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::AddonLuaString,
                   Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template struct LuaConverter<
    openwow::ui::game::detail::AddonLuaIdentifier,
    openwow::ui::game::detail::kAddonLuaConversion>;
template struct LuaConverter<openwow::ui::game::detail::AddonLuaString,
                             openwow::ui::game::detail::kAddonLuaConversion>;

}

namespace openwow::ui::game::detail {

namespace {

constexpr bool kAllowLoadOnDemand = true;
constexpr bool kUseAggregateLoadStateFallback = true;

const char* CurrentCharacterName(const AddonRuntimeIdentity& identity) {
  const auto& character_name = identity.character_name;
  return character_name.empty() ? nullptr : character_name.c_str();
}

AddonResult<std::string> ResolveAddonName(
    AddOnsData& addons, const AddonLuaIdentifier& identifier,
    const char* usage_error) {
  if (identifier.kind == AddonLuaIdentifier::Kind::kName) {
    return identifier.name;
  }
  if (identifier.kind != AddonLuaIdentifier::Kind::kIndex) {
    return openwow::ui::lua::LuaUsageError{usage_error};
  }
  const std::string* name =
      identifier.index == 0
          ? nullptr
          : addons.GetAddonNameByIndex(
                static_cast<std::size_t>(identifier.index - 1u));
  if (name != nullptr) {
    return *name;
  }
  const auto count = std::min(
      addons.GetAddonCount(),
      static_cast<std::size_t>(std::numeric_limits<int>::max()));
  return openwow::ui::lua::LuaUsageError{
      "AddOn index must be in the range of 1 to " + std::to_string(count)};
}

AddonVoidResult SetAddonEnabled(AddOnsData& addons,
                                const AddonRuntimeIdentity& identity,
                                const AddonLuaIdentifier& identifier,
                                const bool enabled, const char* usage_error) {
  const auto resolved = ResolveAddonName(addons, identifier, usage_error);
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  addons.SetSavedAddonEnabled(std::get<std::string>(resolved).c_str(),
                              CurrentCharacterName(identity), enabled);
  return openwow::ui::lua::NoLuaResults{};
}

void BindRegistryContext(lua_State& state, const std::string_view key,
                         void* value) {
  if (value != nullptr) {
    lua_pushlightuserdata(&state, value);
  } else {
    lua_pushnil(&state);
  }
  lua_setfield(&state, LUA_REGISTRYINDEX, key.data());
}

}

void BindAddonLuaContext(lua_State& state, AddOnsData& addons,
                         runtime::WorldUiRuntimeContext& runtime_context) {
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<AddOnsData>::key, &addons);
  BindRegistryContext(
      state,
      openwow::ui::lua::LuaRegistryContext<
          runtime::WorldUiRuntimeContext>::key,
      &runtime_context);
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<AddonRuntimeLoader>::key,
      runtime_context.addon_runtime_loader());
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<AddonRuntimeIdentity>::key,
      const_cast<AddonRuntimeIdentity*>(&runtime_context.addon_runtime_identity()));
}

std::size_t GetNumAddOns(AddOnsData& addons) {
  return addons.GetAddonCount();
}

AddonResult<AddonInfoReturns> GetAddOnInfo(
    AddOnsData& addons, const AddonRuntimeIdentity& identity,
    const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier, "Usage: GetAddOnInfo(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  const std::string& addon_name = std::get<std::string>(resolved);
  const char* character_name = CurrentCharacterName(identity);
  std::vector<AddonInfoValue> values;
  values.reserve(7);
  values.emplace_back(addon_name);
  const auto metadata = [&addons, &addon_name](const char* field) {
    const char* value = addons.GetMetadata(addon_name.c_str(), field);
    return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
  };
  values.emplace_back(metadata("Title"));
  values.emplace_back(metadata("Notes"));
  values.emplace_back(openwow::ui::lua::LuaTruthy{
      addons.GetCharacterLoadState(addon_name.c_str(), character_name,
                                   kUseAggregateLoadStateFallback) != 0});

  if (addons.IsAddonLoadFinished(addon_name.c_str())) {
    values.emplace_back(openwow::ui::lua::LuaTruthy{true});
    values.emplace_back(std::optional<std::string>{});
  } else {
    const auto loadability = addons.EvaluateLoadability(
        addon_name.c_str(), kAllowLoadOnDemand, character_name);
    values.emplace_back(openwow::ui::lua::LuaTruthy{loadability.loadable});
    if (loadability.reason == AddonStatusLabel::Loadable &&
        loadability.dependency_reason == AddonStatusLabel::Loadable) {
      values.emplace_back(std::optional<std::string>{});
    } else {
      std::string reason_storage;
      values.emplace_back(std::optional<std::string>{AddOnsData::FormatLoadReason(
          loadability.reason, loadability.dependency_reason, reason_storage)});
    }
  }
  values.emplace_back(std::string(addons.GetSecurityLabel(addon_name.c_str())));
  return AddonInfoReturns(std::move(values));
}

AddonVoidResult EnableAddOn(AddOnsData& addons,
                            const AddonRuntimeIdentity& identity,
                            const AddonLuaIdentifier identifier) {
  return SetAddonEnabled(addons, identity, identifier, true,
                         "Usage: EnableAddOn(index or \"name\")");
}

AddonVoidResult DisableAddOn(AddOnsData& addons,
                             const AddonRuntimeIdentity& identity,
                             const AddonLuaIdentifier identifier) {
  return SetAddonEnabled(addons, identity, identifier, false,
                         "Usage: DisableAddOn(index or \"name\")");
}

AddonResult<AddonLoadReturns> LoadAddOn(
    AddOnsData& addons, runtime::WorldUiRuntimeContext& runtime_context,
    AddonRuntimeLoader* loader, const AddonRuntimeIdentity& identity,
    const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier, "Usage: LoadAddOn(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  const std::string& addon_name = std::get<std::string>(resolved);
  AddonLoadLogSession log_session;
  const bool was_loaded = addons.IsAddonLoaded(addon_name.c_str());

  const bool loaded =
      loader != nullptr &&
      loader->Load(
          addon_name, AddonRuntimeLoadContext{.identity = identity,
                                              .status_sink = log_session.sink(),
                                              .allow_load_on_demand = kAllowLoadOnDemand});

  if (loaded) {
    if (!was_loaded) {
      openwow::game::SyncLocalizedGlobalStrings(runtime_context.lua_state());
    }
    return AddonLoadReturns(openwow::ui::lua::LuaTruthy{true}, std::nullopt);
  }

  const auto loadability = addons.EvaluateLoadability(
      addon_name.c_str(), kAllowLoadOnDemand, CurrentCharacterName(identity));
  std::string reason = "UNKNOWN_ERROR";
  if (!loadability.loadable &&
      (loadability.reason != AddonStatusLabel::Loadable ||
       loadability.dependency_reason != AddonStatusLabel::Loadable)) {
    std::string reason_storage;
    reason = AddOnsData::FormatLoadReason(
        loadability.reason, loadability.dependency_reason, reason_storage);
  }
  return AddonLoadReturns(std::nullopt, std::move(reason));
}

AddonResult<AddonLoadedReturns> IsAddOnLoaded(
    AddOnsData& addons, const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier, "Usage: IsAddOnLoaded(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  const std::string& addon_name = std::get<std::string>(resolved);
  return AddonLoadedReturns(
      openwow::ui::lua::LuaTruthy{addons.IsAddonLoaded(addon_name.c_str())},
      openwow::ui::lua::LuaTruthy{
          addons.IsAddonLoadFinished(addon_name.c_str())});
}

AddonResult<openwow::ui::lua::LuaTruthy> IsAddOnLoadOnDemand(
    AddOnsData& addons, const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier, "Usage: IsAddOnLoadOnDemand(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  return openwow::ui::lua::LuaTruthy{addons.IsAddonLoadOnDemand(
      std::get<std::string>(resolved).c_str())};
}

void EnableAllAddOns(AddOnsData& addons,
                     const AddonRuntimeIdentity& identity) {
  addons.SetAllVisibleAddonsSavedEnabled(CurrentCharacterName(identity), true);
}

void DisableAllAddOns(AddOnsData& addons,
                      const AddonRuntimeIdentity& identity) {
  addons.SetAllVisibleAddonsSavedEnabled(CurrentCharacterName(identity), false);
}

AddonResult<AddonDependenciesReturns> GetAddOnDependencies(
    AddOnsData& addons, const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier,
      "Usage: GetAddOnDependencies(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  const auto* addon = addons.FindAddon(std::get<std::string>(resolved));
  if (addon == nullptr) {
    return AddonDependenciesReturns({});
  }
  return AddonDependenciesReturns(addon->dependencies);
}

void ResetDisabledAddOns(AddOnsData& addons,
                         const AddonRuntimeIdentity& identity) {
  addons.ReloadSavedState(identity.account_name, identity.realm_name,
                          CurrentCharacterName(identity));
}

AddonResult<std::optional<std::string>> GetAddOnMetadata(
    AddOnsData& addons, const AddonLuaIdentifier identifier,
    const AddonLuaString field) {
  constexpr char kUsage[] =
      "Usage: GetAddOnMetadata(index or \"name\", \"variable\")";
  const auto resolved = ResolveAddonName(addons, identifier, kUsage);
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  if (!field.value) {
    return openwow::ui::lua::LuaUsageError{kUsage};
  }
  const char* value = addons.GetMetadata(
      std::get<std::string>(resolved).c_str(), field.value->c_str());
  return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
}

AddonResult<double> GetAddOnMemoryUsage(
    AddOnsData& addons, runtime::WorldUiRuntimeContext& runtime_context,
    const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier, "Usage: GetAddOnMemoryUsage(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  return static_cast<double>(GetLuaAddonMemoryUsageBytes(
             runtime_context.lua_state(), std::get<std::string>(resolved))) /
         1024.0;
}

AddonResult<double> GetAddOnCPUUsage(
    AddOnsData& addons, runtime::WorldUiRuntimeContext& runtime_context,
    const AddonLuaIdentifier identifier) {
  const auto resolved = ResolveAddonName(
      addons, identifier, "Usage: GetAddOnCPUUsage(index or \"name\")");
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&resolved)) {
    return *error;
  }
  return GetLuaAddonCpuUsageMilliseconds(runtime_context.lua_state(),
                                         std::get<std::string>(resolved));
}

}

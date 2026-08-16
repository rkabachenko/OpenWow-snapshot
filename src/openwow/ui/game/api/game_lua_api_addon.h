#pragma once

#include "openwow/ui/runtime/lua/lua_binding.h"

#include <optional>
#include <string>
#include <variant>

namespace openwow::ui {
class AddOnsData;
}

namespace openwow::ui::game {
class AddonRuntimeLoader;
struct AddonRuntimeIdentity;
namespace runtime {
class WorldUiRuntimeContext;
}
}

namespace openwow::ui::game::detail {

struct AddonLuaIdentifier final {
  enum class Kind { kInvalid, kIndex, kName };
  Kind kind{Kind::kInvalid};
  std::uint32_t index{0};
  std::string name;
};

struct AddonLuaString final {
  std::optional<std::string> value;
};

using AddonInfoValue = std::variant<std::string, std::optional<std::string>,
                                    openwow::ui::lua::LuaTruthy>;
using AddonInfoReturns = openwow::ui::lua::LuaVariableReturns<AddonInfoValue>;
using AddonDependenciesReturns =
    openwow::ui::lua::LuaVariableReturns<std::string>;
using AddonLoadReturns = openwow::ui::lua::LuaReturns<
    std::optional<openwow::ui::lua::LuaTruthy>, std::optional<std::string>>;
using AddonLoadedReturns = openwow::ui::lua::LuaReturns<
    openwow::ui::lua::LuaTruthy, openwow::ui::lua::LuaTruthy>;
template <typename Result>
using AddonResult = std::variant<Result, openwow::ui::lua::LuaUsageError>;
using AddonVoidResult = AddonResult<openwow::ui::lua::NoLuaResults>;

void BindAddonLuaContext(lua_State& state, openwow::ui::AddOnsData& addons,
                         runtime::WorldUiRuntimeContext& runtime_context);

std::size_t GetNumAddOns(openwow::ui::AddOnsData& addons);
AddonResult<AddonInfoReturns> GetAddOnInfo(
    openwow::ui::AddOnsData& addons, const AddonRuntimeIdentity& identity,
    AddonLuaIdentifier identifier);
AddonVoidResult EnableAddOn(openwow::ui::AddOnsData& addons,
                            const AddonRuntimeIdentity& identity,
                            AddonLuaIdentifier identifier);
AddonVoidResult DisableAddOn(openwow::ui::AddOnsData& addons,
                             const AddonRuntimeIdentity& identity,
                             AddonLuaIdentifier identifier);
AddonResult<AddonLoadReturns> LoadAddOn(
    openwow::ui::AddOnsData& addons,
    runtime::WorldUiRuntimeContext& runtime_context,
    AddonRuntimeLoader* loader, const AddonRuntimeIdentity& identity,
    AddonLuaIdentifier identifier);
AddonResult<AddonLoadedReturns> IsAddOnLoaded(
    openwow::ui::AddOnsData& addons, AddonLuaIdentifier identifier);
AddonResult<openwow::ui::lua::LuaTruthy> IsAddOnLoadOnDemand(
    openwow::ui::AddOnsData& addons, AddonLuaIdentifier identifier);
void EnableAllAddOns(openwow::ui::AddOnsData& addons,
                     const AddonRuntimeIdentity& identity);
void DisableAllAddOns(openwow::ui::AddOnsData& addons,
                      const AddonRuntimeIdentity& identity);
AddonResult<AddonDependenciesReturns> GetAddOnDependencies(
    openwow::ui::AddOnsData& addons, AddonLuaIdentifier identifier);
void ResetDisabledAddOns(openwow::ui::AddOnsData& addons,
                         const AddonRuntimeIdentity& identity);
AddonResult<std::optional<std::string>> GetAddOnMetadata(
    openwow::ui::AddOnsData& addons, AddonLuaIdentifier identifier,
    AddonLuaString field);
AddonResult<double> GetAddOnMemoryUsage(
    openwow::ui::AddOnsData& addons,
    runtime::WorldUiRuntimeContext& runtime_context,
    AddonLuaIdentifier identifier);
AddonResult<double> GetAddOnCPUUsage(
    openwow::ui::AddOnsData& addons,
    runtime::WorldUiRuntimeContext& runtime_context,
    AddonLuaIdentifier identifier);

}

namespace openwow::ui::lua {

template <>
struct LuaRegistryContext<openwow::ui::AddOnsData> {
  static constexpr std::string_view key = "openwow.addons_data";
};

template <>
struct LuaRegistryContext<openwow::ui::game::runtime::WorldUiRuntimeContext> {
  static constexpr std::string_view key = "openwow.world_ui_runtime_context";
};

template <>
struct LuaRegistryContext<openwow::ui::game::AddonRuntimeLoader> {
  static constexpr std::string_view key = "openwow.addon_runtime_loader";
};

template <>
struct LuaRegistryContext<openwow::ui::game::AddonRuntimeIdentity> {
  static constexpr std::string_view key = "openwow.addon_runtime_identity";
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::AddonLuaIdentifier, Policy> {
  using Storage = openwow::ui::game::detail::AddonLuaIdentifier;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::AddonLuaString, Policy> {
  using Storage = openwow::ui::game::detail::AddonLuaString;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

}

namespace openwow::ui::lua::detail {

template <>
struct IsOptional<openwow::ui::game::detail::AddonLuaIdentifier>
    : std::true_type {};

template <>
struct IsOptional<openwow::ui::game::detail::AddonLuaString>
    : std::true_type {};

}

namespace openwow::ui::game::detail {

inline constexpr openwow::ui::lua::ConversionPolicy kAddonLuaConversion{
    openwow::ui::lua::IntegralConversion::kTruncate,
    true, false, true, true, true};

inline constexpr auto kGetNumAddOns =
    openwow::ui::lua::bind<&GetNumAddOns, kAddonLuaConversion>("GetNumAddOns");
inline constexpr auto kGetAddOnInfo =
    openwow::ui::lua::bind<&GetAddOnInfo, kAddonLuaConversion>("GetAddOnInfo");
inline constexpr auto kEnableAddOn =
    openwow::ui::lua::bind<&EnableAddOn, kAddonLuaConversion>("EnableAddOn");
inline constexpr auto kDisableAddOn =
    openwow::ui::lua::bind<&DisableAddOn, kAddonLuaConversion>("DisableAddOn");
inline constexpr auto kLoadAddOn =
    openwow::ui::lua::bind<&LoadAddOn, kAddonLuaConversion>("LoadAddOn");
inline constexpr auto kIsAddOnLoaded =
    openwow::ui::lua::bind<&IsAddOnLoaded, kAddonLuaConversion>("IsAddOnLoaded");
inline constexpr auto kIsAddOnLoadOnDemand =
    openwow::ui::lua::bind<&IsAddOnLoadOnDemand, kAddonLuaConversion>(
        "IsAddOnLoadOnDemand");
inline constexpr auto kEnableAllAddOns =
    openwow::ui::lua::bind<&EnableAllAddOns, kAddonLuaConversion>(
        "EnableAllAddOns");
inline constexpr auto kDisableAllAddOns =
    openwow::ui::lua::bind<&DisableAllAddOns, kAddonLuaConversion>(
        "DisableAllAddOns");
inline constexpr auto kGetAddOnDependencies =
    openwow::ui::lua::bind<&GetAddOnDependencies, kAddonLuaConversion>(
        "GetAddOnDependencies");
inline constexpr auto kResetDisabledAddOns =
    openwow::ui::lua::bind<&ResetDisabledAddOns, kAddonLuaConversion>(
        "ResetDisabledAddOns");
inline constexpr auto kGetAddOnMetadata =
    openwow::ui::lua::bind<&GetAddOnMetadata, kAddonLuaConversion>(
        "GetAddOnMetadata");
inline constexpr auto kGetAddOnMemoryUsage =
    openwow::ui::lua::bind<&GetAddOnMemoryUsage, kAddonLuaConversion>(
        "GetAddOnMemoryUsage");
inline constexpr auto kGetAddOnCPUUsage =
    openwow::ui::lua::bind<&GetAddOnCPUUsage, kAddonLuaConversion>(
        "GetAddOnCPUUsage");

}

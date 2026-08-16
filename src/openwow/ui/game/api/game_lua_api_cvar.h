#pragma once

#include "openwow/ui/runtime/lua/lua_binding.h"

#include <optional>
#include <string>
#include <variant>

namespace openwow::ui::game {
class CVarSystem;
class ScriptEventDispatch;
class SecureExecution;
}

namespace openwow::ui::game::detail {

struct CVarLuaString final {
  std::optional<std::string> value;
};

struct CVarLuaNumber final {
  std::optional<double> value;
};

using CVarVoidResult = std::variant<openwow::ui::lua::NoLuaResults,
                                    openwow::ui::lua::LuaUsageError>;
using CVarStringResult = std::variant<std::optional<std::string>,
                                      openwow::ui::lua::LuaUsageError>;
using CVarRangeResult = std::variant<std::optional<double>,
                                     openwow::ui::lua::LuaUsageError>;
using CVarTruthyResult = std::variant<openwow::ui::lua::LuaTruthy,
                                      openwow::ui::lua::LuaUsageError>;
using CVarInfoValues = openwow::ui::lua::LuaReturns<
    std::optional<std::string>, std::optional<std::string>,
    openwow::ui::lua::LuaTruthy, openwow::ui::lua::LuaTruthy>;
using CVarInfoResult =
    std::variant<CVarInfoValues, openwow::ui::lua::LuaUsageError>;

void BindCVarLuaContext(lua_State& state, CVarSystem* cvars,
                        SecureExecution* security,
                        ScriptEventDispatch* events);

CVarStringResult GetCVar(CVarSystem& cvars, CVarLuaString name);
double GetFarclip(CVarSystem& cvars);
CVarVoidResult SetFarclip(CVarSystem& cvars, CVarLuaNumber value);
double GetTexLodBias(CVarSystem& cvars);
CVarVoidResult SetTexLodBias(CVarSystem& cvars, CVarLuaNumber value);
CVarVoidResult SetCVar(CVarSystem& cvars, SecureExecution& security,
                       ScriptEventDispatch& events,
                       openwow::ui::lua::RawLuaState state,
                       CVarLuaString name, CVarLuaString value,
                       CVarLuaString script_cvar);
CVarTruthyResult GetCVarBool(CVarSystem& cvars, CVarLuaString name);
CVarStringResult GetCVarDefault(CVarSystem& cvars, CVarLuaString name);
CVarInfoResult GetCVarInfo(CVarSystem& cvars, CVarLuaString name);
CVarRangeResult GetCVarMax(CVarSystem& cvars, CVarLuaString name);
CVarRangeResult GetCVarMin(CVarSystem& cvars, CVarLuaString name);
CVarRangeResult GetCVarAbsoluteMax(CVarSystem& cvars, CVarLuaString name);
CVarRangeResult GetCVarAbsoluteMin(CVarSystem& cvars, CVarLuaString name);
CVarVoidResult RegisterCVar(CVarSystem& cvars, CVarLuaString name,
                            CVarLuaString default_value);
CVarVoidResult SetWaterDetail(CVarLuaNumber value);
double GetBaseMip(CVarSystem& cvars);
CVarVoidResult SetBaseMip(CVarSystem& cvars, CVarLuaNumber value);

}

namespace openwow::ui::lua {

template <>
struct LuaRegistryContext<openwow::ui::game::CVarSystem> {
  static constexpr std::string_view key = "openwow.cvar_system";
};

template <>
struct LuaRegistryContext<openwow::ui::game::SecureExecution> {
  static constexpr std::string_view key = "openwow.cvar_secure_execution";
};

template <>
struct LuaRegistryContext<openwow::ui::game::ScriptEventDispatch> {
  static constexpr std::string_view key = "openwow.cvar_event_dispatch";
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::CVarLuaString, Policy> {
  using Storage = openwow::ui::game::detail::CVarLuaString;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept;
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::CVarLuaNumber, Policy> {
  using Storage = openwow::ui::game::detail::CVarLuaNumber;
  static bool Valid(lua_State*, int) noexcept;
  static Storage Read(lua_State* state, int index) noexcept;
  static Storage Argument(Storage value) noexcept;
};

}

namespace openwow::ui::lua::detail {

template <>
struct IsOptional<openwow::ui::game::detail::CVarLuaString>
    : std::true_type {};

template <>
struct IsOptional<openwow::ui::game::detail::CVarLuaNumber>
    : std::true_type {};

}

namespace openwow::ui::game::detail {

inline constexpr openwow::ui::lua::ConversionPolicy kCVarLuaConversion{
    openwow::ui::lua::IntegralConversion::kTruncate,
    true, false, true, true, true};

inline constexpr auto kGetCVar =
    openwow::ui::lua::bind<&GetCVar, kCVarLuaConversion>("GetCVar");
inline constexpr auto kGetFarclip =
    openwow::ui::lua::bind<&GetFarclip, kCVarLuaConversion>("GetFarclip");
inline constexpr auto kSetFarclip =
    openwow::ui::lua::bind<&SetFarclip, kCVarLuaConversion>("SetFarclip");
inline constexpr auto kGetTexLodBias =
    openwow::ui::lua::bind<&GetTexLodBias, kCVarLuaConversion>("GetTexLodBias");
inline constexpr auto kSetTexLodBias =
    openwow::ui::lua::bind<&SetTexLodBias, kCVarLuaConversion>("SetTexLodBias");
inline constexpr auto kSetCVar =
    openwow::ui::lua::bind<&SetCVar, kCVarLuaConversion>("SetCVar");
inline constexpr auto kGetCVarBool =
    openwow::ui::lua::bind<&GetCVarBool, kCVarLuaConversion>("GetCVarBool");
inline constexpr auto kGetCVarDefault =
    openwow::ui::lua::bind<&GetCVarDefault, kCVarLuaConversion>("GetCVarDefault");
inline constexpr auto kGetCVarInfo =
    openwow::ui::lua::bind<&GetCVarInfo, kCVarLuaConversion>("GetCVarInfo");
inline constexpr auto kGetCVarMax =
    openwow::ui::lua::bind<&GetCVarMax, kCVarLuaConversion>("GetCVarMax");
inline constexpr auto kGetCVarMin =
    openwow::ui::lua::bind<&GetCVarMin, kCVarLuaConversion>("GetCVarMin");
inline constexpr auto kGetCVarAbsoluteMax =
    openwow::ui::lua::bind<&GetCVarAbsoluteMax, kCVarLuaConversion>(
        "GetCVarAbsoluteMax");
inline constexpr auto kGetCVarAbsoluteMin =
    openwow::ui::lua::bind<&GetCVarAbsoluteMin, kCVarLuaConversion>(
        "GetCVarAbsoluteMin");
inline constexpr auto kRegisterCVar =
    openwow::ui::lua::bind<&RegisterCVar, kCVarLuaConversion>("RegisterCVar");
inline constexpr auto kSetWaterDetail =
    openwow::ui::lua::bind<&SetWaterDetail, kCVarLuaConversion>(
        "SetWaterDetail");
inline constexpr auto kGetBaseMip =
    openwow::ui::lua::bind<&GetBaseMip, kCVarLuaConversion>("GetBaseMip");
inline constexpr auto kSetBaseMip =
    openwow::ui::lua::bind<&SetBaseMip, kCVarLuaConversion>("SetBaseMip");

}

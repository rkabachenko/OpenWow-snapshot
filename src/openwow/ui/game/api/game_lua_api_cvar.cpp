#include "openwow/ui/game/api/game_lua_api_cvar.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/script_cvar_ranges.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::CVarLuaString, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CVarLuaString, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  if (index > lua_gettop(state) || lua_isstring(state, index) == 0) {
    return {};
  }
  const char* value = lua_tostring(state, index);
  return {{value != nullptr ? value : ""}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CVarLuaString, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::CVarLuaNumber, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CVarLuaNumber, Policy>::Read(
    lua_State* state, const int index) noexcept -> Storage {
  if (index > lua_gettop(state) || lua_isnumber(state, index) == 0) {
    return {};
  }
  return {{static_cast<double>(lua_tonumber(state, index))}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::CVarLuaNumber, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template struct LuaConverter<
    openwow::ui::game::detail::CVarLuaString,
    openwow::ui::game::detail::kCVarLuaConversion>;
template struct LuaConverter<
    openwow::ui::game::detail::CVarLuaNumber,
    openwow::ui::game::detail::kCVarLuaConversion>;

}

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint8_t kAccountInfoBit = 0x1u;
constexpr std::uint8_t kCharacterInfoBit = 0x2u;

std::optional<CVarSystem::CVarSnapshot> LookupWorldCVar(
    CVarSystem& cvars, const std::string& name) {
  auto snapshot = cvars.LookupCVarByName(name);
  if (!snapshot || HasFlag(snapshot->flags, CVarFlags::Hidden)) {
    return std::nullopt;
  }
  return snapshot;
}

CVarRangeResult ReadCVarRange(CVarSystem& cvars, const CVarLuaString name,
                              const char* usage,
                              const openwow::ui::ScriptCVarRangeQuery query) {
  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{usage};
  }
  const auto snapshot = LookupWorldCVar(cvars, *name.value);
  if (!snapshot) {
    return openwow::ui::lua::LuaUsageError{
        "Couldn't find CVar named '" + *name.value + "'"};
  }
  return openwow::ui::QueryScriptCVarRange(snapshot->registered_name, query);
}

double ReadNamedCVarFloat(CVarSystem& cvars, const char* name) {
  const auto snapshot = cvars.LookupCVarByName(name);
  return static_cast<double>(snapshot->current_float_value);
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

CVarVoidResult SetNamedCVarFromRequiredNumber(CVarSystem& cvars,
                                              const char* name,
                                              const CVarLuaNumber value,
                                              const char* usage) {
  if (!value.value) {
    return openwow::ui::lua::LuaUsageError{usage};
  }
  char formatted_value[16];
  std::snprintf(formatted_value, sizeof(formatted_value), "%f", *value.value);
  cvars.SetRegisteredCVarValue(name, formatted_value);
  return openwow::ui::lua::NoLuaResults{};
}

}

void BindCVarLuaContext(lua_State& state, CVarSystem* cvars,
                        SecureExecution* security,
                        ScriptEventDispatch* events) {
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<CVarSystem>::key, cvars);
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<SecureExecution>::key,
      security);
  BindRegistryContext(
      state, openwow::ui::lua::LuaRegistryContext<ScriptEventDispatch>::key,
      events);
}

CVarStringResult GetCVar(CVarSystem& cvars, const CVarLuaString name) {

  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{"Usage: GetCVar(\"cvar\")"};
  }
  const auto snapshot = LookupWorldCVar(cvars, *name.value);
  return snapshot ? std::optional<std::string>(snapshot->value) : std::nullopt;
}

double GetFarclip(CVarSystem& cvars) {
  return ReadNamedCVarFloat(cvars, "farclip");
}

CVarVoidResult SetFarclip(CVarSystem& cvars, const CVarLuaNumber value) {
  return SetNamedCVarFromRequiredNumber(
      cvars, "farclip", value, "Usage: SetFarclip(value)");
}

double GetTexLodBias(CVarSystem& cvars) {
  return ReadNamedCVarFloat(cvars, "texLodBias");
}

CVarVoidResult SetTexLodBias(CVarSystem& cvars, const CVarLuaNumber value) {
  return SetNamedCVarFromRequiredNumber(
      cvars, "texLodBias", value, "Usage: SetTexLodBias(value)");
}

CVarVoidResult SetCVar(CVarSystem& cvars, SecureExecution& security,
                       ScriptEventDispatch& events,
                       const openwow::ui::lua::RawLuaState state,
                       const CVarLuaString name, const CVarLuaString value,
                       const CVarLuaString script_cvar) {
  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: SetCVar(\"cvar\", value [, \"scriptCvar\")"};
  }
  const auto snapshot = LookupWorldCVar(cvars, *name.value);
  if (!snapshot) {
    return openwow::ui::lua::LuaUsageError{
        "Couldn't find CVar named '" + *name.value + "'"};
  }
  const auto flags = snapshot->flags;
  if (HasFlag(flags, CVarFlags::Immutable) ||
      HasFlag(flags, CVarFlags::ConsoleReadOnly)) {
    return openwow::ui::lua::LuaUsageError{"\"" + *name.value +
                                            "\" is read-only"};
  }
  if (HasFlag(flags, CVarFlags::Protected) && security.InCombatLockdown()) {
    GameUI_ReportProtectedActionFailure(
        state.get(), ProtectedActionFailureMode::kBlockedType4);
    return openwow::ui::lua::NoLuaResults{};
  }
  const std::string written_value = value.value.value_or("0");

  (void)cvars.SetRegisteredCVarValueDirect(snapshot->registered_name,
                                           written_value);
  if (script_cvar.value) {
    events.FireEventArgs(openwow::ui::game::events::CVAR_UPDATE,
                         {*script_cvar.value, written_value});
  }
  return openwow::ui::lua::NoLuaResults{};
}

CVarTruthyResult GetCVarBool(CVarSystem& cvars, const CVarLuaString name) {
  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GetCVarBool(\"cvar\")"};
  }
  const auto snapshot = LookupWorldCVar(cvars, *name.value);
  return openwow::ui::lua::LuaTruthy{
      snapshot && openwow::ui::ScriptParseBoolStringOrDefault(
                      snapshot->value.c_str(), false)};
}

CVarStringResult GetCVarDefault(CVarSystem& cvars,
                                const CVarLuaString name) {

  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GetCVarDefault(\"cvar\")"};
  }
  const auto snapshot = LookupWorldCVar(cvars, *name.value);
  if (!snapshot) {
    return openwow::ui::lua::LuaUsageError{
        "Couldn't find CVar named '" + *name.value + "'"};
  }
  if (!snapshot->has_default_value) {
    return std::optional<std::string>{};
  }
  return std::optional<std::string>{snapshot->default_value};
}

CVarInfoResult GetCVarInfo(CVarSystem& cvars, const CVarLuaString name) {
  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GetCVarInfo(\"cvar\")"};
  }
  const auto snapshot = LookupWorldCVar(cvars, *name.value);
  if (!snapshot) {
    return CVarInfoValues(std::nullopt, std::nullopt,
                          openwow::ui::lua::LuaTruthy{false},
                          openwow::ui::lua::LuaTruthy{false});
  }
  const std::uint8_t info_bits = snapshot->info_bits;
  return CVarInfoValues(
      snapshot->value,
      snapshot->has_default_value
          ? std::optional<std::string>{snapshot->default_value}
          : std::nullopt,
      openwow::ui::lua::LuaTruthy{(info_bits & kAccountInfoBit) != 0},
      openwow::ui::lua::LuaTruthy{(info_bits & kCharacterInfoBit) != 0});
}

CVarRangeResult GetCVarMax(CVarSystem& cvars, const CVarLuaString name) {

  return ReadCVarRange(cvars, name, "Usage: GetCVarMax(\"cvar\")",
                       openwow::ui::ScriptCVarRangeQuery::kMax);
}

CVarRangeResult GetCVarMin(CVarSystem& cvars, const CVarLuaString name) {

  return ReadCVarRange(cvars, name, "Usage: GetCVarMin(\"cvar\")",
                       openwow::ui::ScriptCVarRangeQuery::kMin);
}

CVarRangeResult GetCVarAbsoluteMax(CVarSystem& cvars,
                                   const CVarLuaString name) {
  return ReadCVarRange(cvars, name,
                       "Usage: GetCVarAbsoluteMax(\"cvar\")",
                       openwow::ui::ScriptCVarRangeQuery::kAbsoluteMax);
}

CVarRangeResult GetCVarAbsoluteMin(CVarSystem& cvars,
                                   const CVarLuaString name) {
  return ReadCVarRange(cvars, name,
                       "Usage: GetCVarAbsoluteMin(\"cvar\")",
                       openwow::ui::ScriptCVarRangeQuery::kAbsoluteMin);
}

CVarVoidResult RegisterCVar(CVarSystem& cvars, const CVarLuaString name,
                            const CVarLuaString default_value) {
  if (!name.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: RegisterCVar(\"cvar\" [, default])"};
  }
  cvars.RegisterScriptCVar(*name.value, default_value.value.value_or("0"));
  return openwow::ui::lua::NoLuaResults{};
}

CVarVoidResult SetWaterDetail(const CVarLuaNumber value) {
  if (!value.value) {
    return openwow::ui::lua::LuaUsageError{"Usage: SetWaterDetail(value)"};
  }
  return openwow::ui::lua::NoLuaResults{};
}

double GetBaseMip(CVarSystem& cvars) {
  const auto snapshot = cvars.LookupCVarByName("baseMip");
  return 1.0 - static_cast<double>(snapshot->current_float_value);
}

CVarVoidResult SetBaseMip(CVarSystem& cvars, const CVarLuaNumber value) {
  if (!value.value) {
    return openwow::ui::lua::LuaUsageError{"Usage: SetBaseMip(value)"};
  }
  const auto requested_level =
      openwow::ui::TruncateLuaNumberToI32(1.0 - *value.value);
  CVarSystem::SetRegisteredValueOptions options;
  options.force = false;
  options.populate_startup_if_missing = false;
  options.populate_default_if_missing = false;
  options.mark_dirty = true;
  (void)cvars.SetRegisteredCVarIntValue("baseMip", requested_level, options);
  return openwow::ui::lua::NoLuaResults{};
}

}

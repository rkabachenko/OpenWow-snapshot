#pragma once

#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/script_cvar_ranges.h"

extern "C" {
#include <lua.hpp>
}

#include <cmath>
#include <limits>
#include <optional>

namespace openwow::ui::game::detail {

enum class ScriptCVarLookupMode {
  kWorld,
  kGlue,
};

[[nodiscard]] inline std::optional<CVarSystem::CVarSnapshot>
LookupScriptCVar(CVarSystem &system, const char *name, const ScriptCVarLookupMode mode) {
  auto snapshot = system.LookupCVarByName(name != nullptr ? name : "");
  if (mode == ScriptCVarLookupMode::kWorld &&
      (!snapshot.has_value() || HasFlag(snapshot->flags, CVarFlags::Hidden))) {
    return std::nullopt;
  }
  return snapshot;
}

inline void PushScriptCVarRangeValue(lua_State *state, const double value) {
  if (std::isfinite(value)) {
    const double truncated = std::trunc(value);
    if (truncated == value &&
        truncated >= static_cast<double>(std::numeric_limits<lua_Integer>::min()) &&
        truncated <= static_cast<double>(std::numeric_limits<lua_Integer>::max())) {
      lua_pushinteger(state, static_cast<lua_Integer>(truncated));
      return;
    }
  }

  lua_pushnumber(state, static_cast<lua_Number>(value));
}

inline int PushScriptCVarRangeByName(lua_State *state,
                                     const char *name,
                                     const ScriptCVarLookupMode mode,
                                     const openwow::ui::ScriptCVarRangeQuery query) {
  auto &system = CVarSystem::Instance();
  const auto snapshot = LookupScriptCVar(system, name, mode);
  if (!snapshot.has_value()) {
    return luaL_error(state, "Couldn't find CVar named '%s'", name != nullptr ? name : "");
  }

  const auto value = openwow::ui::QueryScriptCVarRange(snapshot->registered_name, query);
  if (value.has_value()) {
    PushScriptCVarRangeValue(state, *value);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

}

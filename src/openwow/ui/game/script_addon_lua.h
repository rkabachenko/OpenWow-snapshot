#pragma once

#include "openwow/ui/lua_result_capacity.h"

#include "openwow/ui/addons_data.h"
#include "openwow/ui/lua_numeric.h"

extern "C" {
#include <lua.hpp>
}

#include <limits>
#include <string>
#include <vector>

namespace openwow::ui::game::detail {

struct ScriptAddonCharacterIndexQuery {
  const char* character_name = nullptr;
  const std::string* addon_name = nullptr;
};

inline void RaiseScriptAddonIndexRangeError(lua_State* state, const std::size_t addon_count) {
  const int max_index = addon_count > static_cast<std::size_t>(std::numeric_limits<int>::max())
                            ? std::numeric_limits<int>::max()
                            : static_cast<int>(addon_count);
  luaL_error(state, "AddOn index must be in the range of 1 to %d", max_index);
}

inline const std::string* ResolveScriptAddonNameByIndex(lua_State* state,
                                                        const int argument_index) {
  auto& addons = openwow::ui::AddOnsData::Get();
  const std::uint32_t one_based_index =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, argument_index));
  if (one_based_index == 0) {
    return nullptr;
  }
  return addons.GetAddonNameByIndex(static_cast<std::size_t>(one_based_index - 1u));
}

inline const std::string& RequireScriptAddonNameByIndex(lua_State* state,
                                                        const char* usage_error) {
  if (lua_isnumber(state, 1) == 0) {
    luaL_error(state, "%s", usage_error);
  }

  auto& addons = openwow::ui::AddOnsData::Get();
  const auto* addon_name = ResolveScriptAddonNameByIndex(state, 1);
  if (addon_name == nullptr) {
    RaiseScriptAddonIndexRangeError(state, addons.GetAddonCount());
  }
  return *addon_name;
}

inline ScriptAddonCharacterIndexQuery RequireScriptAddonCharacterAndNameByIndex(
    lua_State* state, const char* usage_error) {
  if ((lua_isstring(state, 1) == 0 && lua_type(state, 1) != LUA_TNIL) ||
      lua_isnumber(state, 2) == 0) {
    luaL_error(state, "%s", usage_error);
  }

  auto& addons = openwow::ui::AddOnsData::Get();
  const auto* addon_name = ResolveScriptAddonNameByIndex(state, 2);
  if (addon_name == nullptr) {
    RaiseScriptAddonIndexRangeError(state, addons.GetAddonCount());
  }

  return {
      .character_name = lua_tostring(state, 1),
      .addon_name = addon_name,
  };
}

inline void PushOptionalLuaString(lua_State* state, const char* value) {
  if (value != nullptr) {
    lua_pushstring(state, value);
  } else {
    lua_pushnil(state);
  }
}

inline int PushStringList(lua_State* state, const std::vector<std::string>& values) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, values.size(), "string results");
  for (const auto& value : values) {
    lua_pushstring(state, value.c_str());
  }
  return result_count;
}

}

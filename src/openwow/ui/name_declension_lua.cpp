#include "openwow/ui/name_declension_lua.h"

#include "openwow/game/name_declension.h"
#include "openwow/ui/lua_numeric.h"

#include <lua.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace openwow::ui {

int ReadLuaDeclensionGenderIndex(lua_State* state, const int index) {
  constexpr int kWildcardGenderIndex = 2;
  if (state == nullptr || lua_isnumber(state, index) == 0) {
    return kWildcardGenderIndex;
  }

  const std::int32_t gender_value =
      TruncateLuaNumberToI32(lua_tonumber(state, index));
  return openwow::game::declension::MapLuaGenderValueToIndex(gender_value);
}

int LuaDeclineName(lua_State* state) {
  if (lua_isstring(state, 1) == 0 || lua_isnumber(state, 3) == 0) {
    return luaL_error(
        state, "Usage: DeclineName(\"name\", gender, declensionSet)");
  }

  const char* name = lua_tostring(state, 1);
  const int gender_index = ReadLuaDeclensionGenderIndex(state, 2);
  const std::int32_t declension_set =
      TruncateLuaNumberToI32(lua_tonumber(state, 3));

  std::array<std::string, 5> forms;
  if (openwow::game::declension::BuildForms(
          name != nullptr ? name : "", gender_index,
          static_cast<std::uint32_t>(declension_set) - 1u, forms)) {
    for (const std::string& form : forms) {
      lua_pushlstring(state, form.data(), form.size());
    }
  } else {
    for (std::size_t index = 0; index < forms.size(); ++index) {
      lua_pushnil(state);
    }
  }
  return 5;
}

}

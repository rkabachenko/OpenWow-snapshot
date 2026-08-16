#pragma once

#include <initializer_list>

struct lua_State;

namespace openwow::ui::game::frame_api {

[[nodiscard]] bool FontObjectHasNonEmptyStringField(lua_State* lua, int index,
                                               const char* field_name);
[[nodiscard]] bool FontObjectHasStoredField(lua_State* lua, int index,
                                       const char* field_name);
[[nodiscard]] bool FontObjectHasAnyStoredFields(
    lua_State* lua, int index,
    std::initializer_list<const char*> field_names);

}

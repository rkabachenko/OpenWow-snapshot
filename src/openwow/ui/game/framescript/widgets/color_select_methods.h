#pragma once

#include <string_view>

struct lua_State;

namespace openwow::ui::game::frame_api {

struct ColorSelectTexturePaths {
  std::string_view wheel;
  std::string_view wheel_thumb;
  std::string_view value;
  std::string_view value_thumb;
};

void InitializeColorSelectTextures(lua_State* lua, int frame_index,
                                   const ColorSelectTexturePaths& paths);
void ApplyColorSelectMethods(lua_State* lua);

}

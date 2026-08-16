#pragma once

#include <string>
#include <string_view>

struct lua_State;

namespace openwow::ui::game::frame_api {

inline constexpr char kLuaFrameLevelField[] = "__ow_frame_level";
inline constexpr char kLuaFrameStrataField[] = "__ow_frame_strata";
inline constexpr char kLuaFrameRuntimeKeyField[] = "__ow_frame_key";

[[nodiscard]] const char* GetFrameRuntimeKeyOrName(lua_State* lua,
                                                   int frame_index);
[[nodiscard]] std::string ExpandLuaParentNameToken(lua_State* lua,
                                                   int parent_index,
                                                   const char* name);
[[nodiscard]] std::string ExpandLuaParentNameToken(
    std::string_view nearest_parent_name, const char* name);

}

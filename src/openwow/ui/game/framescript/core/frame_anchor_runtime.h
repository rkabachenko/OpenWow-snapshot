#pragma once

#include "openwow/ui/widgets/script_object.h"

#include <cstdint>

struct lua_State;

namespace openwow::ui::game::frame_api {

enum class LuaAnchorTargetValidation : std::uint8_t {
  kRequireScriptObjectThis,
  kAllowLayoutOnlyTables,
};

void NormalizeLuaAnchorArray(lua_State* lua, int anchors_index);
int EnsureLuaAnchorArray(lua_State* lua, int frame_index);
[[nodiscard]] int CountVisibleLuaAnchors(lua_State* lua, int anchors_index);
[[nodiscard]] int GetLuaAnchorPointSlot(lua_State* lua, int anchor_index);
[[nodiscard]] std::uint32_t GetLuaAnchorFlags(lua_State* lua,
                                             int anchor_index);
[[nodiscard]] bool LuaAnchorIsHidden(lua_State* lua, int anchor_index);
void PushAnchorRelativeToValue(lua_State* lua, int anchor_index);
[[nodiscard]] bool PushVisibleLuaAnchorByIndex(lua_State* lua,
                                              int anchors_index,
                                              int visible_index);
int LuaClearAllPointsInternal(lua_State* lua,
                              LuaAnchorTargetValidation validation);
int LuaSetPointInternal(lua_State* lua, LuaAnchorTargetValidation validation);
int LuaSetAllPointsInternal(lua_State* lua,
                            LuaAnchorTargetValidation validation);
int LuaGetPointInternal(lua_State* lua);
[[nodiscard]] bool FontStringHasUsableAnchors(lua_State* lua,
                                              int font_string_index);
void SetButtonFontStringDefaultAnchor(lua_State* lua, int button_index,
                                      int font_string_index,
                                      const char* point);

}

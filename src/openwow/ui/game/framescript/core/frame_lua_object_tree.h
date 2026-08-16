#pragma once

#include "openwow/ui/widgets/script_object.h"

#include <cstdint>

struct lua_State;

namespace openwow::ui::game::frame_api {

enum class ParentLinkArrayKind : std::uint8_t { None, Children, Regions };

void PrependToRegions(lua_State* lua, int parent_index);
[[nodiscard]] bool ArrayFieldContainsExactValue(lua_State* lua,
                                                int owner_index,
                                                const char* field_name,
                                                int value_index);
void RemoveExactValueFromArrayField(lua_State* lua, int owner_index,
                                    const char* field_name, int value_index);
[[nodiscard]] openwow::ui::widgets::ScriptObjectType GetLuaScriptObjectType(
    lua_State* lua, int index);
[[nodiscard]] bool IsFrameLikeScriptObjectType(
    openwow::ui::widgets::ScriptObjectType type);
[[nodiscard]] ParentLinkArrayKind GetParentLinkArrayKind(
    openwow::ui::widgets::ScriptObjectType type);
[[nodiscard]] bool IsNilParentForbiddenScriptObjectType(
    openwow::ui::widgets::ScriptObjectType type);
void ReparentScriptObjectTable(lua_State* lua, int self_index,
                               int new_parent_index);
int ValidateParentableScriptObjectSelf(lua_State* lua);

}

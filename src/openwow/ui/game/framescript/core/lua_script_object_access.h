#pragma once

#include "openwow/ui/widgets/script_object.h"

struct lua_State;

namespace openwow::ui::game::lua_adapter {

void AttachScriptObjectIdentity(lua_State* lua, int index,
                                void* native_object = nullptr);
void InvalidateNativeScriptObjectIdentity(lua_State* lua, int index);
[[nodiscard]] bool HasScriptObjectIdentity(lua_State* lua, int index);
[[nodiscard]] openwow::ui::widgets::ScriptObjectType CanonicalScriptObjectType(
    lua_State* lua, int index);
[[nodiscard]] bool IsScriptObjectKindOf(
    lua_State* lua, int index,
    openwow::ui::widgets::ScriptObjectType expected_type);
[[nodiscard]] bool HasCanonicalScriptObjectType(
    lua_State* lua, int index,
    openwow::ui::widgets::ScriptObjectType expected_type);
[[nodiscard]] openwow::ui::widgets::CScriptObject* BorrowNativeScriptObject(
    lua_State* lua, int index);

[[nodiscard]] const char* ScriptObjectDisplayName(lua_State* lua, int index);

}

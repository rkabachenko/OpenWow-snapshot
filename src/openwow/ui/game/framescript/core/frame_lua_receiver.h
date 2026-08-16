#pragma once
#include <string>
struct lua_State;
namespace openwow::ui::widgets { class CSimpleFrame; }
namespace openwow::ui::game::frame_api {
int ValidateFrameResizeSelf(lua_State* lua);
int ValidateTypedFramescriptSelf(lua_State* lua, const char* expected_type);
std::string GetFrameUsageObjectName(lua_State* lua, int self_index);
std::string GetFrameManagerKey(lua_State* lua, int self_index);
std::string GetObjectNameOrUnnamed(lua_State* lua, int self_index);
openwow::ui::widgets::CSimpleFrame* GetLuaSimpleFrame(lua_State* lua, int frame_index);
}

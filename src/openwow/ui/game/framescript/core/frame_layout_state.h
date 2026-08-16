#pragma once
#include <cstdint>
#include <string>
struct lua_State;
namespace openwow::ui::game::frame_api {
std::uint8_t GetFrameAlphaByteOrDefault(lua_State* lua, int index, std::uint8_t fallback = 0xFF);
int PushStoredTitleRegion(lua_State* lua, int frame_index);
int PushOrCreateTitleRegion(lua_State* lua, int frame_index);
void SyncTrackedFrameUserPlaced(lua_State* lua, int self_index, bool user_placed);
void SyncTrackedFrameDontSavePosition(lua_State* lua, int self_index, bool dont_save_position);
bool BeginTrackedFrameMoveSizing(lua_State* lua, int self_index, const std::string& frame_name, int mode);
void SyncTrackedRegionDrawLayer(lua_State* lua, int self_index, const char* canonical_layer);
int SetFrameResizeBounds(lua_State* lua, const char* width_field, const char* height_field, const char* usage_format);
int GetFrameResizeBounds(lua_State* lua, const char* width_field, const char* height_field);
void SetLuaHitRectInsetField(lua_State* lua, int frame_index, const char* field_name, double value);
double ReadLuaHitRectInsetField(lua_State* lua, int frame_index, const char* field_name);
}

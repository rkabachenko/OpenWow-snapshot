#pragma once

#include "openwow/ui/framexml/texture_role.h"

struct lua_State;

namespace openwow::ui::widgets {
struct StatusBarColor;
}

namespace openwow::ui::game::frame_api {

void SynchronizeTextureRole(
    lua_State* lua, int texture_index,
    openwow::ui::framexml::TextureRole role);
void ReleaseTextureOwnership(lua_State* lua, int texture_index);
void BindTextureOwnership(lua_State* lua, int texture_index, int owner_index,
                          openwow::ui::framexml::TextureRole texture_role);
void DetachTrackedTextureRole(lua_State* lua, int texture_index);
void SyncTrackedTextureColor(lua_State* lua, int texture_index, float red,
                             float green, float blue, float alpha);
[[nodiscard]] bool ApplyTextureVertexColor(
    lua_State* lua, int texture_index,
    const openwow::ui::widgets::StatusBarColor& color);
[[nodiscard]] bool ApplyStatusBarTextureRotation(lua_State* lua,
                                                 int texture_index,
                                                 bool rotates);
void TrackRuntimeRegion(
    lua_State* lua, int owner_index, int region_index, const char* kind,
    const char* lua_name, const char* draw_layer,
    openwow::ui::framexml::TextureRole texture_role =
        openwow::ui::framexml::TextureRole::Normal,
    bool set_all_points = false);

}

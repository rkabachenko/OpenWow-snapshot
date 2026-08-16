#pragma once

struct lua_State;

namespace openwow::ui::framexml {
struct UiFrame;
}

namespace openwow::ui::game::frame_api {

void CreateFontStringTable(lua_State* lua, int parent_index);
void CreateTextureTable(lua_State* lua, int parent_index);
void ApplyFrameXmlRegionDefinition(
    lua_State* lua, int region_index,
    const openwow::ui::framexml::UiFrame& frame);
void ApplyFrameXmlMessageFontDefinition(
    lua_State* lua, int message_frame_index,
    const openwow::ui::framexml::UiFrame& font_definition);
void BindButtonFontStringRegion(lua_State* lua, int button_index,
                                int font_string_index);
bool BindNativeTextureRegion(
    lua_State* lua, int owner_index, int texture_index,
    const openwow::ui::framexml::UiFrame& frame);
[[nodiscard]] const char* NativeTextureSlotField(
    const openwow::ui::framexml::UiFrame& frame);

}

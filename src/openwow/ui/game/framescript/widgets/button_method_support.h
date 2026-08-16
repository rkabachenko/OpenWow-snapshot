#pragma once

#include "openwow/ui/framexml/texture_role.h"
#include "openwow/ui/script_boolean.h"

#include <initializer_list>
#include <string>

struct lua_State;

namespace openwow::ui::game::frame_api {

using openwow::ui::ScriptReadBoolArgOrDefault;

inline constexpr const char* kRegisteredClicksField =
    "__ow_registered_clicks";

void SetRegisteredClicks(
    lua_State* lua, int frame_index,
    std::initializer_list<const char*> default_clicks);
void ApplyButtonMethods(lua_State* lua);
void ApplyCheckButtonMethods(lua_State* lua);
int ValidateFrameObjectSelf(lua_State* lua, const char* expected_type);
const char* ResolveButtonLabelAnchorPoint(lua_State* lua, int button_index,
                                          int font_string_index);
void InstallNativeTextureSlotMethods(
    lua_State* lua, int owner_index, const char* method_suffix,
    const char* slot, openwow::ui::framexml::TextureRole role,
    const char* draw_layer);
[[nodiscard]] bool CallTextureSetPath(lua_State* lua, int texture_index,
                                      int path_index);
void SetButtonTextValue(lua_State* lua, int self_index, const char* text);
void RefreshButtonLabelFont(lua_State* lua, int button_index);

void InstallButtonFontObjectMethods(lua_State* lua, int button_index,
                                    const char* name, const char* slot,
                                    const char* setter_usage);

}

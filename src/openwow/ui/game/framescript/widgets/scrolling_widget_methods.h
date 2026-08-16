#pragma once

#include <array>

struct lua_State;

namespace openwow::ui::game::frame_api {

inline constexpr std::array<const char*, 20>
    kScrollingMessageFrameOnlyMethods{
        "AtBottom", "AtTop", "GetCurrentLine", "GetCurrentScroll",
        "GetHyperlinksEnabled", "GetMaxLines", "GetMessageInfo",
        "GetNumLinesDisplayed", "GetNumMessages", "PageDown", "PageUp",
        "RemoveMessagesByAccessID", "ScrollDown", "ScrollToBottom",
        "ScrollToTop", "ScrollUp", "SetHyperlinksEnabled", "SetMaxLines",
        "SetScrollOffset", "UpdateColorByID"};

void ApplyScrollFrameMethods(lua_State* lua);
void ApplyScrollingMessageFrameMethods(lua_State* lua);
void ApplyMessageFrameStateMethods(lua_State* lua);
void ApplyScrollingMessageFrameStateMethods(lua_State* lua);
void ApplySliderMethods(lua_State* lua);
void ApplyCooldownMethods(lua_State* lua);
int LuaScrollFrame_SetScrollChild(lua_State* lua);
bool SetScrollFrameOffsetState(lua_State* lua, int frame_index,
                               bool horizontal, double offset,
                               bool invoke_script);

}

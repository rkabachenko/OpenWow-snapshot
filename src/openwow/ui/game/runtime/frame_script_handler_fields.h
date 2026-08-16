#pragma once

#include <array>

namespace openwow::ui::game::runtime {

struct FrameScriptHandlerFields {
  const char* stored;
  const char* direct;
};

inline constexpr std::array<FrameScriptHandlerFields, 9>
    kMouseCategoryHandlerFields{{
        {"__ow_script_OnEnter", "OnEnter"},
        {"__ow_script_OnLeave", "OnLeave"},
        {"__ow_script_OnMouseDown", "OnMouseDown"},
        {"__ow_script_OnMouseUp", "OnMouseUp"},
        {"__ow_script_OnClick", "OnClick"},
        {"__ow_script_OnDragStart", "OnDragStart"},
        {"__ow_script_OnHyperlinkClick", "OnHyperlinkClick"},
        {"__ow_script_OnHyperlinkEnter", "OnHyperlinkEnter"},
        {"__ow_script_OnHyperlinkLeave", "OnHyperlinkLeave"},
    }};

inline constexpr FrameScriptHandlerFields kMouseWheelHandlerFields{
    "__ow_script_OnMouseWheel", "OnMouseWheel"};

}

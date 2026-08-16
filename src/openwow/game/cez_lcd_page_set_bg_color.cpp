
#include "openwow/game/cez_lcd_page_set_bg_color.h"

#include <cstdint>

namespace openwow::game {

namespace {

inline std::int32_t& Field(void* node, int index) {
    return reinterpret_cast<std::int32_t*>(node)[index];
}

}

std::int32_t CEzLcdPageNode_SetBGColor(void* node,
                                        std::int32_t color) noexcept {
    Field(node, kPageNodeFieldBgColor) = color;

    Field(node, kPageNodeFieldHasBgColor) = 1;

    Field(node, kPageNodeFieldHasSubscreen) = 0;

    return color;
}

}

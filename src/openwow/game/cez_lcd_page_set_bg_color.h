
#pragma once

#include <cstdint>

namespace openwow::game {

constexpr int kPageNodeFieldHasSubscreen    = 22;
constexpr int kPageNodeFieldHasBgColor      = 45;
constexpr int kPageNodeFieldBgColor         = 46;

std::int32_t CEzLcdPageNode_SetBGColor(void* node,
                                        std::int32_t color) noexcept;

}

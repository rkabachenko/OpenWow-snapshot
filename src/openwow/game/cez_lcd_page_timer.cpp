
#include "openwow/game/cez_lcd_page_timer.h"

#include "clcd_base_node.h"

#include <cstdint>

namespace openwow::game {

namespace {

inline std::uint32_t& Field(void* node, int index) {
    return reinterpret_cast<std::uint32_t*>(node)[index];
}

inline std::uint32_t Field(const void* node, int index) {
    return reinterpret_cast<const std::uint32_t*>(node)[index];
}

}

bool CEzLcdPageNode_IsShowTimeExpired(const void* node) noexcept {
    if (Field(node, kPageNodeFieldStartTime) != 0) {
        const std::uint32_t duration = Field(node, kPageNodeFieldDuration);
        if (duration != 0) {
            if (Field(node, kPageNodeFieldElapsed) < duration) {
                return false;
            }
        }
    }
    return true;
}

std::uint32_t CEzLcdPageNode_StartShowTimer(void* node,
                                             std::uint32_t duration_ms) noexcept {

    constexpr std::uint32_t kStubTickCount = 0;

    Field(node, kPageNodeFieldStartTime) = kStubTickCount;
    Field(node, kPageNodeFieldElapsed) = 0;
    Field(node, kPageNodeFieldDuration) = duration_ms;

    return duration_ms;
}

int CEzLcdPageNode_UpdateElapsed(void* node,
                                  std::uint32_t current_time) noexcept {
    const std::uint32_t start_time = Field(node, kPageNodeFieldStartTime);
    Field(node, kPageNodeFieldElapsed) = current_time - start_time;

    return 0;
}

void CEzLcdPageNode_Destruct(void* node) noexcept {
    auto* self = static_cast<std::uint32_t*>(node);

    CLCDBASE_NODE_Destruct(self);
}

void* CEzLcdPageNode_ScalarDeletingDtor(void* node,
                                         bool free_memory) noexcept {

    CEzLcdPageNode_Destruct(node);

    if (free_memory) {

        delete[] reinterpret_cast<char*>(node);
    }

    return node;
}

}

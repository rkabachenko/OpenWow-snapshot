
#include "openwow/game/cez_lcd_subscreen_ctor.h"

#include "openwow/game/cez_lcd_page_timer.h"
#include "clcd_base_node.h"

#include <cstdint>

namespace openwow::game {

namespace {

void CEzLcdPageNode_Construct(std::uint32_t* self) noexcept {

    CLCDBASE_NODE_Construct(self);

    constexpr std::uint32_t kStubTickCount = 0;

    self[19] = kStubTickCount;
    self[20] = 0;
    self[21] = 0xFFFFFFFF;
    self[22] = 0;

    for (int i = 23; i <= 44; ++i) {
        self[i] = 0;
    }

    self[45] = 0;
}

}

void CEzLcdSubscreen_Construct(std::uint32_t* self,
                                std::uint32_t owner) noexcept {
    CEzLcdPageNode_Construct(self);

    self[kSubscreenFieldOwner] = owner;

    for (int i = 0; i < kSubscreenZeroCount; ++i) {
        self[kSubscreenFieldDataABase + i] = 0;
        self[kSubscreenFieldDataBBase + i] = 0;
    }
}

}

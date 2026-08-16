#pragma once

#include <cstdint>

struct lua_State;

namespace openwow::game {
struct ItemInstance;
}

namespace openwow::ui::game::detail {

bool TryAttachSendMailContainerItem(
    lua_State* state, const openwow::game::ItemInstance& item,
    std::uint8_t source_bag, std::uint8_t source_slot);

}


#pragma once

#include "openwow/game/objects/cgitem.h"

#include <cstdint>

namespace openwow::game {

struct ItemClassSubClassFilter {
  std::uint32_t target_class{0};
  std::uint32_t subclass_mask{0};
};

[[nodiscard]] bool ItemMatchesClassAndSubClassMask(
    const CGItem_C& item, const ItemClassSubClassFilter& filter);

}

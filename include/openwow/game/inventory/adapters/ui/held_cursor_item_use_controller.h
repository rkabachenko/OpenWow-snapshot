#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {
class ObjectManager;
class QueryCache;

namespace inventory::ui {

struct HeldCursorItem {
  ObjectGuid item;
  ObjectGuid container;
};

enum class HeldCursorItemDropResult : std::uint8_t {
  kPrompted,
  kIndestructible,
  kNoHeldItem,
  kItemNotFound,
  kNoActivePlayer,
  kNotOwned,
  kTemplatePending,
};

[[nodiscard]] HeldCursorItemDropResult PromptDeleteHeldCursorItem(
    ObjectManager& objects, QueryCache& queries, HeldCursorItem held_item);

}
}

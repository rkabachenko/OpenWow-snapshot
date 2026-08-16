
#include "openwow/game/inventory/items/item_class_subclass_predicate.h"

namespace openwow::game {

bool ItemMatchesClassAndSubClassMask(const CGItem_C& item,
                                     const ItemClassSubClassFilter& filter) {

  if (item.GetItemClassFromClientDbc() != filter.target_class) {
    return false;
  }

  if (filter.subclass_mask == 0) {
    return true;
  }

  const std::uint32_t subclass_bit =
      1u << item.GetItemSubClassFromClientDbc();
  return (subclass_bit & filter.subclass_mask) != 0;
}

}

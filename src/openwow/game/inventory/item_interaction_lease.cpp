#include "openwow/game/inventory/item_interaction_lease.h"

#include "openwow/game/character_map_runtime.h"
#include "openwow/game/inventory/model/item_instance.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgitem.h"

namespace openwow::game {

namespace {

CGItem_C *ResolveMutableItem(ObjectManager &objects, const ObjectGuid guid) {
  auto *object = objects.GetMutable(guid);
  return object != nullptr && object->IsItem() ? static_cast<CGItem_C *>(object) : nullptr;
}

}

ItemLockRegistry::~ItemLockRegistry() {
  ReleaseAll();
}

std::uint8_t ItemLockRegistry::OwnerBit(const ItemLockOwner owner) noexcept {
  return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(owner));
}

bool ItemLockRegistry::Acquire(const ObjectGuid item_guid, const ItemLockOwner owner) {
  const auto raw_guid = item_guid.GetRawValue();
  if (raw_guid == 0) {
    return false;
  }
  const auto owner_bit = OwnerBit(owner);
  auto [entry, inserted] = owners_.try_emplace(raw_guid, 0);
  if ((entry->second & owner_bit) != 0) {
    return false;
  }
  auto *item = ResolveMutableItem(map_runtime_.objects(), item_guid);
  if (item == nullptr || (entry->second == 0 && !item->SetLocked(true))) {
    if (inserted) {
      owners_.erase(entry);
    }
    return false;
  }
  entry->second |= owner_bit;
  return true;
}

bool ItemLockRegistry::Release(const ObjectGuid item_guid, const ItemLockOwner owner) {
  const auto raw_guid = item_guid.GetRawValue();
  const auto entry = owners_.find(raw_guid);
  const auto owner_bit = OwnerBit(owner);
  if (entry == owners_.end() || (entry->second & owner_bit) == 0) {
    return false;
  }
  entry->second &= static_cast<std::uint8_t>(~owner_bit);
  if (entry->second != 0) {
    return true;
  }
  owners_.erase(entry);
  auto *item = ResolveMutableItem(map_runtime_.objects(), item_guid);
  return item != nullptr && item->SetLocked(false);
}

bool ItemLockRegistry::ClearForAuthoritativeInventoryPlacement(
    const ObjectGuid item_guid) {
  auto *item = ResolveMutableItem(map_runtime_.objects(), item_guid);
  if (item == nullptr) {
    return false;
  }

  owners_.erase(item_guid.GetRawValue());
  return item->SetLocked(false);
}

void ItemLockRegistry::ReleaseAll() {
  for (const auto &[raw_guid, owners] : owners_) {
    (void)owners;
    if (auto *item = ResolveMutableItem(map_runtime_.objects(), ObjectGuid(raw_guid));
        item != nullptr) {
      (void)item->SetLocked(false);
    }
  }
  owners_.clear();
}

bool ItemLockRegistry::Holds(const ObjectGuid item_guid, const ItemLockOwner owner) const {
  const auto entry = owners_.find(item_guid.GetRawValue());
  return entry != owners_.end() && (entry->second & OwnerBit(owner)) != 0;
}

bool ItemLockRegistry::IsLocked(const ObjectGuid item_guid) const {
  return owners_.contains(item_guid.GetRawValue());
}

bool ItemLockRegistry::IsItemLocked(const ItemInstance &item) const {
  const ObjectGuid item_guid(item.guid);
  if (IsLocked(item_guid)) {
    return true;
  }
  const auto *live_item = map_runtime_.objects().GetItem(item_guid);

  return live_item != nullptr && live_item->IsLocked();
}

}

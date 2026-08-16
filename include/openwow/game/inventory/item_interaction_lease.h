#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <unordered_map>

namespace openwow::game {

class CharacterMapRuntime;
struct ItemInstance;

enum class ItemLockOwner : std::uint8_t {
  kCursor = 0,
  kTrade,
  kMail,
  kAuction,
  kMove,
};

class ItemLockRegistry final {
 public:
  explicit ItemLockRegistry(CharacterMapRuntime& map_runtime)
      : map_runtime_(map_runtime) {}
  ~ItemLockRegistry();

  ItemLockRegistry(const ItemLockRegistry&) = delete;
  ItemLockRegistry& operator=(const ItemLockRegistry&) = delete;

  [[nodiscard]] bool Acquire(ObjectGuid item_guid, ItemLockOwner owner);
  [[nodiscard]] bool Release(ObjectGuid item_guid, ItemLockOwner owner);

  [[nodiscard]] bool ClearForAuthoritativeInventoryPlacement(ObjectGuid item_guid);
  void ReleaseAll();

  [[nodiscard]] bool Holds(ObjectGuid item_guid, ItemLockOwner owner) const;
  [[nodiscard]] bool IsLocked(ObjectGuid item_guid) const;

  [[nodiscard]] bool IsItemLocked(const ItemInstance& item) const;
  [[nodiscard]] bool empty() const noexcept { return owners_.empty(); }
 private:
  [[nodiscard]] static std::uint8_t OwnerBit(ItemLockOwner owner) noexcept;

  CharacterMapRuntime& map_runtime_;
  std::unordered_map<std::uint64_t, std::uint8_t> owners_;
};

}

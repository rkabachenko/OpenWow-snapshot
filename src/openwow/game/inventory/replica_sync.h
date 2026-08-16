
#pragma once

#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/update_fields.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class CharacterMapRuntime;

[[nodiscard]] ItemInstance BuildItemInstanceFromObject(const CGItem_C& item_obj);

[[nodiscard]] ItemInstance BuildItemInstanceFromFields(
    uint64_t guid, uint32_t entry, uint32_t stack_count, uint32_t flags,
    uint32_t durability, uint32_t max_durability, uint32_t duration,
    uint64_t creator_guid, int32_t random_property, uint32_t suffix_factor,
    const int32_t charges[5],
    const uint32_t enchant_ids[12], const uint32_t enchant_durations[12],
    const uint32_t enchant_charges[12], uint32_t create_played_time = 0);

class PlayerInventoryReplicaSync {
 public:
  struct ItemTemplateRefreshRequest {
    std::uint64_t item_guid = 0;
    std::uint32_t entry = 0;
  };

  PlayerInventoryReplicaSync(PlayerInventoryReplica& inventory,
                             CharacterMapRuntime& map_runtime)
      : map_runtime_(map_runtime), inventory_(inventory) {}

  void OnItemCreated(const CGObject_C& obj);

  void OnItemUpdated(const CGObject_C& obj);

  void OnItemDestroyed(ObjectGuid guid);

  void OnPlayerInventoryFieldsChanged(const CGPlayer_C& player);

  void FullResync();

  [[nodiscard]] std::vector<std::uint32_t> ConsumeChangedEntries();
  [[nodiscard]] std::vector<ItemTemplateRefreshRequest> ConsumeItemTemplateRefreshes();

  [[nodiscard]] std::vector<std::int32_t> ConsumeChangedContainers();

  void Reset();

 private:
  enum class BagStorage : std::uint8_t {
    kInventory = 0,
    kBank = 1,
  };

  struct TrackedBagLocation {
    BagStorage storage = BagStorage::kInventory;
    std::uint8_t bag_index = 0;
  };

  struct BagSlotLocation {
    BagStorage storage = BagStorage::kInventory;
    std::uint8_t bag_index = 0;
    std::uint8_t slot = 0;
  };

  CharacterMapRuntime& map_runtime_;
  PlayerInventoryReplica& inventory_;

  std::unordered_map<uint64_t, int16_t> guid_to_slot_;
  std::unordered_map<uint64_t, BagSlotLocation> guid_to_bag_slot_;
  std::vector<std::uint32_t> changed_entries_;
  std::vector<ItemTemplateRefreshRequest> item_template_refreshes_;
  std::vector<std::int32_t> changed_containers_;
  std::uint8_t unresolved_reference_diagnostics_{0};

  void SyncItemObject(const CGObject_C& obj);

  void SyncSlotRange(const CGPlayer_C& player,
                     uint16_t field_base, uint8_t abs_slot_start,
                     uint8_t slot_count);

  void SyncTrackedBagContents(TrackedBagLocation bag, ObjectGuid bag_guid);

  void PopulateSlot(uint8_t abs_slot, const CGItem_C& item_obj);
  void PopulateTrackedBagSlot(BagSlotLocation slot, const CGItem_C& item_obj);
  void ClearTrackedBagContents(TrackedBagLocation bag);
  void ClearTrackedBagSlot(BagSlotLocation slot);
  [[nodiscard]] BagInfo GetTrackedBagSnapshot(TrackedBagLocation bag) const;
  [[nodiscard]] const ItemInstance* GetTrackedBagSlot(BagSlotLocation slot) const;
  void SetTrackedBag(TrackedBagLocation bag, const BagInfo& info);
  void ClearTrackedBag(TrackedBagLocation bag);
  void SetTrackedBagSlot(BagSlotLocation slot, const ItemInstance& item);
  void ForgetTrackedBagItemMappings(const BagInfo& bag);
  [[nodiscard]] static std::optional<TrackedBagLocation> ResolveTrackedBagLocation(
      uint8_t abs_slot);
  [[nodiscard]] std::uint32_t GetTrackedEntryByGuid(std::uint64_t guid) const;
  void TrackChangedEntry(std::uint32_t entry);
  void TrackChangedRootSlot(std::uint8_t abs_slot);
  void TrackChangedBag(TrackedBagLocation bag);
  void TrackItemTemplateRefresh(std::uint64_t item_guid, std::uint32_t entry);
};

}

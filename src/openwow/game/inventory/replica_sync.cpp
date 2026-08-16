
#include "openwow/game/inventory/replica_sync.h"

#include "openwow/game/character_map_runtime.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

namespace {

bool TracksCarriedActionBarState(std::uint8_t abs_slot) {
    return abs_slot < InventorySlots::kBackpackEnd ||
           (abs_slot >= InventorySlots::kKeyringStart &&
            abs_slot < InventorySlots::kKeyringEnd);
}

bool ItemPayloadEqual(const ItemInstance& lhs, const ItemInstance& rhs) {
    if (lhs.guid != rhs.guid || lhs.entry != rhs.entry || lhs.count != rhs.count ||
        lhs.flags != rhs.flags || lhs.random_property != rhs.random_property ||
        lhs.random_suffix != rhs.random_suffix || lhs.durability != rhs.durability ||
        lhs.max_durability != rhs.max_durability || lhs.duration != rhs.duration ||
        lhs.create_played_time != rhs.create_played_time ||
        lhs.creator_guid != rhs.creator_guid || lhs.quality != rhs.quality ||
        lhs.is_locked != rhs.is_locked || lhs.charges != rhs.charges) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.enchantments.size(); ++index) {
        const auto& left = lhs.enchantments[index];
        const auto& right = rhs.enchantments[index];
        if (left.id != right.id || left.duration != right.duration ||
            left.charges != right.charges) {
            return false;
        }
    }
    return true;
}

bool BagPayloadEqual(const BagInfo& lhs, const BagInfo& rhs) {
    if (lhs.guid != rhs.guid || lhs.entry != rhs.entry ||
        lhs.num_slots != rhs.num_slots || !ItemPayloadEqual(lhs.item, rhs.item) ||
        lhs.slots.size() != rhs.slots.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.slots.size(); ++index) {
        if (!ItemPayloadEqual(lhs.slots[index], rhs.slots[index])) {
            return false;
        }
    }
    return true;
}

}

ItemInstance BuildItemInstanceFromObject(const CGItem_C& item_obj) {
    ItemInstance inst;
    inst.guid = item_obj.GetGuid().GetRawValue();
    inst.entry = item_obj.GetEntry();
    inst.count = item_obj.GetStackCount();
    if (inst.count == 0 && inst.entry != 0) inst.count = 1;

    inst.flags = item_obj.GetItemFlags();
    inst.durability = item_obj.GetDurability();
    inst.max_durability = item_obj.GetMaxDurability();
    inst.duration = item_obj.GetUInt32(ITEM_FIELD_DURATION);
    inst.create_played_time = item_obj.GetCreatePlayedTime();
    inst.creator_guid = item_obj.GetCreator().GetRawValue();
    inst.is_locked = item_obj.IsLocked();

    inst.random_property = static_cast<int32_t>(item_obj.GetRandomPropertyID());
    inst.random_suffix = item_obj.GetItemSuffixFactor();

    for (uint8_t i = 0; i < 5; ++i) {
        inst.charges[i] = static_cast<int32_t>(item_obj.GetCharges(i));
    }

    for (uint8_t e = 0; e < static_cast<uint8_t>(EnchantmentSlot::MaxSlots); ++e) {
        uint16_t base = static_cast<uint16_t>(ITEM_FIELD_ENCHANTMENT_1_1 + e * 3);
        inst.enchantments[e].id = item_obj.GetUInt32(base);
        inst.enchantments[e].duration = item_obj.GetUInt32(base + 1);
        inst.enchantments[e].charges = item_obj.GetUInt32(base + 2);
    }

    return inst;
}

ItemInstance BuildItemInstanceFromFields(
    uint64_t guid, uint32_t entry, uint32_t stack_count, uint32_t flags,
    uint32_t durability, uint32_t max_durability, uint32_t duration,
    uint64_t creator_guid, int32_t random_property, uint32_t suffix_factor,
    const int32_t charges[5],
    const uint32_t enchant_ids[12], const uint32_t enchant_durations[12],
    const uint32_t enchant_charges[12], const uint32_t create_played_time) {

    ItemInstance inst;
    inst.guid = guid;
    inst.entry = entry;
    inst.count = stack_count > 0 ? stack_count : (entry > 0 ? 1 : 0);
    inst.flags = flags;
    inst.durability = durability;
    inst.max_durability = max_durability;
    inst.duration = duration;
    inst.create_played_time = create_played_time;
    inst.creator_guid = creator_guid;
    inst.random_property = random_property;
    inst.random_suffix = suffix_factor;

    if (charges) {
        for (int i = 0; i < 5; ++i) inst.charges[i] = charges[i];
    }
    if (enchant_ids && enchant_durations && enchant_charges) {
        for (int e = 0; e < 12; ++e) {
            inst.enchantments[e].id = enchant_ids[e];
            inst.enchantments[e].duration = enchant_durations[e];
            inst.enchantments[e].charges = enchant_charges[e];
        }
    }
    return inst;
}

void PlayerInventoryReplicaSync::OnItemCreated(const CGObject_C& obj) {
    SyncItemObject(obj);
    inventory_.CommitServerRevision();
}

void PlayerInventoryReplicaSync::SyncItemObject(const CGObject_C& obj) {
    if (!obj.IsItem() && !obj.IsContainer()) return;
    const auto* item_obj = static_cast<const CGItem_C*>(&obj);
    const auto guid = item_obj->GetGuid().GetRawValue();

    if (const auto it = guid_to_slot_.find(guid);
        it != guid_to_slot_.end() && it->second >= 0) {
        const auto abs_slot = static_cast<uint8_t>(it->second);
        if (const auto tracked_bag = ResolveTrackedBagLocation(abs_slot);
            tracked_bag.has_value()) {
            SyncTrackedBagContents(*tracked_bag, item_obj->GetGuid());
        } else {
            PopulateSlot(abs_slot, *item_obj);
        }
        inventory_.SetSlotGuid(abs_slot, guid);
    }

    if (const auto it = guid_to_bag_slot_.find(guid);
        it != guid_to_bag_slot_.end()) {
        PopulateTrackedBagSlot(it->second, *item_obj);
    }
}

void PlayerInventoryReplicaSync::OnItemUpdated(const CGObject_C& obj) {
    if (!obj.IsItem() && !obj.IsContainer()) return;

    const auto* item_obj = static_cast<const CGItem_C*>(&obj);
    const auto previous_entry = GetTrackedEntryByGuid(item_obj->GetGuid().GetRawValue());

    SyncItemObject(obj);

    const auto current_entry = item_obj->GetEntry();
    if (previous_entry != 0 && current_entry != 0 && previous_entry != current_entry) {
        TrackItemTemplateRefresh(item_obj->GetGuid().GetRawValue(), current_entry);
    }
    inventory_.CommitServerRevision();
}

void PlayerInventoryReplicaSync::OnItemDestroyed(ObjectGuid guid) {
    const std::uint64_t raw = guid.GetRawValue();
    auto& inv = inventory_;

    if (const auto it = guid_to_slot_.find(raw);
        it != guid_to_slot_.end() && it->second >= 0) {
        const auto abs_slot = static_cast<uint8_t>(it->second);
        if (const auto tracked_bag = ResolveTrackedBagLocation(abs_slot);
            tracked_bag.has_value()) {
            ClearTrackedBagContents(*tracked_bag);
        } else {
            if (TracksCarriedActionBarState(abs_slot)) {
                if (const auto* item = inv.GetItemInSlot(abs_slot);
                    item != nullptr && item->entry != 0) {
                    TrackChangedEntry(item->entry);
                }
            }
            inv.ClearSlot(abs_slot);
            TrackChangedRootSlot(abs_slot);
        }
        inv.SetSlotGuid(abs_slot, 0);
        guid_to_slot_.erase(it);
    } else {
        guid_to_slot_.erase(raw);
    }

    if (const auto it = guid_to_bag_slot_.find(raw);
        it != guid_to_bag_slot_.end()) {
        if (const auto* item = GetTrackedBagSlot(it->second);
            item != nullptr && item->entry != 0) {
            TrackChangedEntry(item->entry);
        }
        ClearTrackedBagSlot(it->second);
        TrackChangedBag(TrackedBagLocation{it->second.storage, it->second.bag_index});
        guid_to_bag_slot_.erase(it);
    }

    inventory_.CommitServerRevision();
}

void PlayerInventoryReplicaSync::OnPlayerInventoryFieldsChanged(const CGPlayer_C& player) {

    SyncSlotRange(player, PLAYER_FIELD_INV_SLOT_HEAD,
                  InventorySlots::kEquipStart, 23);

    SyncSlotRange(player, PLAYER_FIELD_PACK_SLOT_1,
                  InventorySlots::kBackpackStart, 16);

    SyncSlotRange(player, PLAYER_FIELD_BANK_SLOT_1,
                  InventorySlots::kBankStart, 28);

    SyncSlotRange(player, PLAYER_FIELD_BANKBAG_SLOT_1,
                  InventorySlots::kBankBagStart, 7);

    SyncSlotRange(player, PLAYER_FIELD_VENDORBUYBACK_SLOT_1,
                  InventorySlots::kBuybackStart, 12);

    SyncSlotRange(player, PLAYER_FIELD_KEYRING_SLOT_1,
                  InventorySlots::kKeyringStart, 32);

    SyncSlotRange(player, PLAYER_FIELD_CURRENCYTOKEN_SLOT_1,
                  InventorySlots::kCurrencyStart, 32);

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t bag_equip_slot = InventorySlots::kBagSlotsStart + i;
        const auto bag_guid_raw = inventory_.GetSlotGuid(bag_equip_slot);
        if (bag_guid_raw != 0) {
            SyncTrackedBagContents(
                TrackedBagLocation{BagStorage::kInventory, static_cast<std::uint8_t>(i + 1)},
                ObjectGuid(bag_guid_raw));
        } else {
            ClearTrackedBagContents(
                TrackedBagLocation{BagStorage::kInventory, static_cast<std::uint8_t>(i + 1)});
        }
    }

    for (uint8_t i = 0; i < PlayerInventoryReplica::kMaxBankBags; ++i) {
        const auto bag_slot = static_cast<uint8_t>(InventorySlots::kBankBagStart + i);
        const auto bag_guid_raw = inventory_.GetSlotGuid(bag_slot);
        if (bag_guid_raw != 0) {
            SyncTrackedBagContents(TrackedBagLocation{BagStorage::kBank, i},
                                   ObjectGuid(bag_guid_raw));
        } else {
            ClearTrackedBagContents(TrackedBagLocation{BagStorage::kBank, i});
        }
    }
    inventory_.CommitServerRevision();
}

void PlayerInventoryReplicaSync::SyncSlotRange(const CGPlayer_C& player,
                                     uint16_t field_base,
                                     uint8_t abs_slot_start,
                                     uint8_t slot_count) {
    auto& inv = inventory_;
    for (uint8_t i = 0; i < slot_count; ++i) {
        uint16_t field = static_cast<uint16_t>(field_base + i * 2);
        ObjectGuid item_guid = player.GetGuidField(field);
        uint8_t abs_slot = abs_slot_start + i;
        uint64_t guid_raw = item_guid.GetRawValue();

        uint64_t old_guid = inv.GetSlotGuid(abs_slot);
        const bool guid_changed = old_guid != guid_raw;
        if (old_guid != 0 && old_guid != guid_raw) {
            guid_to_slot_.erase(old_guid);
        }

        if (guid_raw == 0) {

            if (TracksCarriedActionBarState(abs_slot)) {
                if (const auto* old_item = inv.GetItemInSlot(abs_slot);
                    old_item != nullptr && old_item->entry != 0) {
                    TrackChangedEntry(old_item->entry);
                }
            }
            inv.ClearSlot(abs_slot);
            inv.SetSlotGuid(abs_slot, 0);
            if (guid_changed) {
                TrackChangedRootSlot(abs_slot);
            }
            continue;
        }

        guid_to_slot_[guid_raw] = static_cast<int16_t>(abs_slot);
        inv.SetSlotGuid(abs_slot, guid_raw);

        const auto* item_obj = map_runtime_.objects().GetItem(item_guid);
        if (item_obj &&
            (guid_changed || inv.GetItemInSlot(abs_slot) == nullptr)) {
            PopulateSlot(abs_slot, *item_obj);
        } else if (unresolved_reference_diagnostics_ < 16) {
            ++unresolved_reference_diagnostics_;
            openwow::diagnostics::Log(
                openwow::diagnostics::LogLevel::kDebug,
                "PlayerInventoryReplicaSync: unresolved root item guid=" +
                    item_guid.ToString() + " slot=" + std::to_string(abs_slot));
        }

    }
}

void PlayerInventoryReplicaSync::SyncTrackedBagContents(const TrackedBagLocation bag,
                                             const ObjectGuid bag_guid) {
    const BagInfo previous_bag = GetTrackedBagSnapshot(bag);

    const auto* container = map_runtime_.objects().GetContainer(bag_guid);
    if (!container) {
        ForgetTrackedBagItemMappings(previous_bag);
        for (const auto& item : previous_bag.slots) {
            TrackChangedEntry(item.entry);
        }

        const auto* item = map_runtime_.objects().GetItem(bag_guid);
        if (item) {
            BagInfo placeholder_bag;
            placeholder_bag.item = BuildItemInstanceFromObject(*item);
            placeholder_bag.guid = placeholder_bag.item.guid;
            placeholder_bag.entry = placeholder_bag.item.entry;
            SetTrackedBag(bag, placeholder_bag);
        } else {
            ClearTrackedBag(bag);
        }
        return;
    }

    ForgetTrackedBagItemMappings(previous_bag);

    BagInfo synced_bag;
    synced_bag.item = BuildItemInstanceFromObject(*container);
    synced_bag.guid = synced_bag.item.guid;
    synced_bag.entry = synced_bag.item.entry;
    synced_bag.num_slots = static_cast<uint8_t>(container->GetNumSlots());
    synced_bag.slots.resize(synced_bag.num_slots);

    for (uint8_t s = 0; s < synced_bag.num_slots; ++s) {
        ObjectGuid slot_guid = container->GetSlot(s);
        if (slot_guid.GetRawValue() == 0) {
            synced_bag.slots[s] = ItemInstance{};
            continue;
        }

        guid_to_bag_slot_[slot_guid.GetRawValue()] = BagSlotLocation{bag.storage, bag.bag_index, s};

        const auto* slot_item = map_runtime_.objects().GetItem(slot_guid);
        if (slot_item) {
            synced_bag.slots[s] = BuildItemInstanceFromObject(*slot_item);
        } else {

            ItemInstance placeholder;
            placeholder.guid = slot_guid.GetRawValue();
            synced_bag.slots[s] = placeholder;
            if (unresolved_reference_diagnostics_ < 16) {
                ++unresolved_reference_diagnostics_;
                openwow::diagnostics::Log(
                    openwow::diagnostics::LogLevel::kDebug,
                    "PlayerInventoryReplicaSync: unresolved bag item guid=" +
                        slot_guid.ToString() + " bag=" +
                        std::to_string(bag.bag_index) + " slot=" +
                        std::to_string(s));
            }
        }

        const std::uint32_t old_entry =
            s < previous_bag.slots.size() ? previous_bag.slots[s].entry : 0;
        if (old_entry != synced_bag.slots[s].entry) {
            TrackChangedEntry(old_entry);
            TrackChangedEntry(synced_bag.slots[s].entry);
        }
    }

    for (std::size_t s = synced_bag.slots.size(); s < previous_bag.slots.size(); ++s) {
        TrackChangedEntry(previous_bag.slots[s].entry);
    }

    SetTrackedBag(bag, synced_bag);
    if (!BagPayloadEqual(previous_bag, synced_bag)) {
        TrackChangedBag(bag);
    }
}

void PlayerInventoryReplicaSync::PopulateSlot(uint8_t abs_slot, const CGItem_C& item_obj) {
    auto inst = BuildItemInstanceFromObject(item_obj);
    auto& inv = inventory_;
    const bool track_changes = TracksCarriedActionBarState(abs_slot);
    if (track_changes) {
        if (const auto* previous = inv.GetItemInSlot(abs_slot);
            previous != nullptr && previous->entry != 0) {
            TrackChangedEntry(previous->entry);
        }
    }
    inv.SetItemInSlot(abs_slot, inst);
    TrackChangedRootSlot(abs_slot);
    if (track_changes) {
        TrackChangedEntry(inst.entry);
    }
}

void PlayerInventoryReplicaSync::PopulateTrackedBagSlot(const BagSlotLocation slot,
                                             const CGItem_C& item_obj) {
    auto inst = BuildItemInstanceFromObject(item_obj);
    if (const auto* previous = GetTrackedBagSlot(slot);
        previous != nullptr && previous->entry != 0) {
        TrackChangedEntry(previous->entry);
    }
    SetTrackedBagSlot(slot, inst);
    TrackChangedBag(TrackedBagLocation{slot.storage, slot.bag_index});
    TrackChangedEntry(inst.entry);
}

void PlayerInventoryReplicaSync::ClearTrackedBagContents(const TrackedBagLocation bag) {
    const auto previous_bag = GetTrackedBagSnapshot(bag);
    if (previous_bag.IsEmpty() && previous_bag.guid == 0 &&
        previous_bag.slots.empty()) {
        return;
    }
    ForgetTrackedBagItemMappings(previous_bag);
    for (const auto& item : previous_bag.slots) {
        TrackChangedEntry(item.entry);
    }
    ClearTrackedBag(bag);
    TrackChangedBag(bag);
}

void PlayerInventoryReplicaSync::ClearTrackedBagSlot(const BagSlotLocation slot) {
    auto& inv = inventory_;
    if (slot.storage == BagStorage::kInventory) {
        inv.ClearBagSlot(slot.bag_index, slot.slot);
        return;
    }
    inv.ClearBankBagSlot(slot.bag_index, slot.slot);
}

BagInfo PlayerInventoryReplicaSync::GetTrackedBagSnapshot(const TrackedBagLocation bag) const {
    const auto& inv = inventory_;
    if (bag.storage == BagStorage::kInventory) {
        if (const auto* info = inv.GetBag(bag.bag_index)) {
            return *info;
        }
        return BagInfo{};
    }

    if (const auto* info = inv.GetBankBag(bag.bag_index)) {
        return *info;
    }
    return BagInfo{};
}

const ItemInstance* PlayerInventoryReplicaSync::GetTrackedBagSlot(const BagSlotLocation slot) const {
    const auto& inv = inventory_;
    if (slot.storage == BagStorage::kInventory) {
        return inv.GetBagSlot(slot.bag_index, slot.slot);
    }
    return inv.GetBankBagSlot(slot.bag_index, slot.slot);
}

void PlayerInventoryReplicaSync::SetTrackedBag(const TrackedBagLocation bag, const BagInfo& info) {
    auto& inv = inventory_;
    if (bag.storage == BagStorage::kInventory) {
        inv.SetBag(bag.bag_index, info);
        return;
    }
    inv.SetBankBag(bag.bag_index, info);
}

void PlayerInventoryReplicaSync::ClearTrackedBag(const TrackedBagLocation bag) {
    SetTrackedBag(bag, BagInfo{});
}

void PlayerInventoryReplicaSync::SetTrackedBagSlot(const BagSlotLocation slot,
                                        const ItemInstance& item) {
    auto& inv = inventory_;
    if (slot.storage == BagStorage::kInventory) {
        inv.SetBagSlot(slot.bag_index, slot.slot, item);
        return;
    }
    inv.SetBankBagSlot(slot.bag_index, slot.slot, item);
}

void PlayerInventoryReplicaSync::ForgetTrackedBagItemMappings(const BagInfo& bag) {
    for (const auto& item : bag.slots) {
        if (item.guid != 0) {
            guid_to_bag_slot_.erase(item.guid);
        }
    }
}

std::optional<PlayerInventoryReplicaSync::TrackedBagLocation> PlayerInventoryReplicaSync::ResolveTrackedBagLocation(
    const uint8_t abs_slot) {
    using namespace InventorySlots;
    if (abs_slot >= kBagSlotsStart && abs_slot < kBagSlotsEnd) {
        return TrackedBagLocation{
            BagStorage::kInventory,
            static_cast<uint8_t>(abs_slot - kBagSlotsStart + 1),
        };
    }
    if (abs_slot >= kBankBagStart && abs_slot < kBankBagEnd) {
        return TrackedBagLocation{
            BagStorage::kBank,
            static_cast<uint8_t>(abs_slot - kBankBagStart),
        };
    }
    return std::nullopt;
}

void PlayerInventoryReplicaSync::FullResync() {
    auto& inv = inventory_;
    inv.Reset();
    guid_to_slot_.clear();
    guid_to_bag_slot_.clear();
    changed_entries_.clear();
    item_template_refreshes_.clear();
    changed_containers_.clear();
    unresolved_reference_diagnostics_ = 0;

    const auto* player = map_runtime_.objects().GetLocalPlayerTyped();
    if (!player) return;

    OnPlayerInventoryFieldsChanged(*player);

    std::uint32_t backpack_guid_count = 0u;
    std::uint32_t backpack_object_count = 0u;
    for (std::uint8_t index = 0u; index < 16u; ++index) {
        const auto guid = player->GetGuidField(
            static_cast<std::uint16_t>(PLAYER_FIELD_PACK_SLOT_1 + index * 2u));
        if (!guid) {
            continue;
        }
        ++backpack_guid_count;
        if (map_runtime_.objects().GetItem(guid) != nullptr) {
            ++backpack_object_count;
        }
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "PlayerInventoryReplicaSync: full resync complete backpack_guids=" +
            std::to_string(backpack_guid_count) + " resolved_objects=" +
            std::to_string(backpack_object_count));
}

void PlayerInventoryReplicaSync::Reset() {
    guid_to_slot_.clear();
    guid_to_bag_slot_.clear();
    changed_entries_.clear();
    item_template_refreshes_.clear();
    changed_containers_.clear();
    unresolved_reference_diagnostics_ = 0;
    inventory_.Reset();
}

std::vector<std::uint32_t> PlayerInventoryReplicaSync::ConsumeChangedEntries() {
    std::vector<std::uint32_t> changed = std::move(changed_entries_);
    changed_entries_.clear();
    changed.erase(std::remove(changed.begin(), changed.end(), 0), changed.end());
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    return changed;
}

std::vector<PlayerInventoryReplicaSync::ItemTemplateRefreshRequest>
PlayerInventoryReplicaSync::ConsumeItemTemplateRefreshes() {
    std::vector<ItemTemplateRefreshRequest> refreshes = std::move(item_template_refreshes_);
    item_template_refreshes_.clear();
    return refreshes;
}

std::vector<std::int32_t> PlayerInventoryReplicaSync::ConsumeChangedContainers() {
    std::vector<std::int32_t> changed = std::move(changed_containers_);
    changed_containers_.clear();
    std::vector<std::int32_t> distinct;
    for (const auto container : changed) {
        if (std::find(distinct.begin(), distinct.end(), container) == distinct.end()) {
            distinct.push_back(container);
        }
    }
    return distinct;
}

std::uint32_t PlayerInventoryReplicaSync::GetTrackedEntryByGuid(const std::uint64_t guid) const {
    if (guid == 0) {
        return 0;
    }

    if (const auto it = guid_to_slot_.find(guid);
        it != guid_to_slot_.end() && it->second >= 0) {
        if (const auto* item =
                inventory_.GetItemInSlot(static_cast<std::uint8_t>(it->second));
            item != nullptr) {
            return item->entry;
        }
    }

    if (const auto it = guid_to_bag_slot_.find(guid); it != guid_to_bag_slot_.end()) {
        if (const auto* item = GetTrackedBagSlot(it->second); item != nullptr) {
            return item->entry;
        }
    }

    return 0;
}

void PlayerInventoryReplicaSync::TrackChangedEntry(std::uint32_t entry) {
    if (entry != 0) {
        changed_entries_.push_back(entry);
    }
}

void PlayerInventoryReplicaSync::TrackChangedRootSlot(const std::uint8_t abs_slot) {
    using namespace InventorySlots;
    if (abs_slot >= kBackpackStart && abs_slot < kBackpackEnd) {
        changed_containers_.push_back(0);
    } else if (abs_slot >= kKeyringStart && abs_slot < kKeyringEnd) {
        changed_containers_.push_back(-2);
    } else if (abs_slot >= kCurrencyStart && abs_slot < kCurrencyEnd) {
        changed_containers_.push_back(-4);
    }
}

void PlayerInventoryReplicaSync::TrackChangedBag(const TrackedBagLocation bag) {
    changed_containers_.push_back(
        bag.storage == BagStorage::kInventory
            ? static_cast<std::int32_t>(bag.bag_index)
            : static_cast<std::int32_t>(bag.bag_index) + 5);
}

void PlayerInventoryReplicaSync::TrackItemTemplateRefresh(const std::uint64_t item_guid,
                                               const std::uint32_t entry) {
    if (item_guid == 0 || entry == 0) {
        return;
    }

    const auto it = std::find_if(item_template_refreshes_.begin(), item_template_refreshes_.end(),
                                 [item_guid](const ItemTemplateRefreshRequest& request) {
                                     return request.item_guid == item_guid;
                                 });
    if (it != item_template_refreshes_.end()) {
        it->entry = entry;
        return;
    }

    item_template_refreshes_.push_back(
        ItemTemplateRefreshRequest{.item_guid = item_guid, .entry = entry});
}

}

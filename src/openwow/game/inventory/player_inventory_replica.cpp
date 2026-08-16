#include "openwow/game/inventory/player_inventory_replica.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace openwow::game {

namespace {

BagInfo NormalizeBagInfo(const BagInfo& info) {
    BagInfo normalized = info;

    if (normalized.item.guid == 0) {
        normalized.item.guid = normalized.guid;
    }
    if (normalized.item.entry == 0) {
        normalized.item.entry = normalized.entry;
    }
    if (normalized.item.entry != 0 && normalized.item.count == 0) {
        normalized.item.count = 1;
    }

    normalized.guid = normalized.item.guid;
    normalized.entry = normalized.item.entry;
    normalized.slots.resize(normalized.num_slots);
    return normalized;
}

std::uint32_t CountItemStack(const ItemInstance& item, const std::uint32_t entry) {
    if (item.entry != entry) {
        return 0;
    }

    return std::max(item.count, 1u);
}

}

void PlayerInventoryReplica::NotifyItemChanged(uint8_t slot, const ItemInstance& item) {
    if (on_item_changed_) {
        on_item_changed_(slot, item);
    }
}

const ItemInstance* PlayerInventoryReplica::GetRootItem(uint8_t slot) const {
    using namespace InventorySlots;
    if (slot < kEquipEnd) {
        return equip_[slot].IsEmpty() ? nullptr : &equip_[slot];
    }
    if (slot >= kBagSlotsStart && slot < kBagSlotsEnd) {
        const auto& bag = bags_[slot - kBagSlotsStart];
        return bag.item.IsEmpty() ? nullptr : &bag.item;
    }
    if (slot >= kBackpackStart && slot < kBackpackEnd) {
        uint8_t local = slot - kBackpackStart;
        return backpack_[local].IsEmpty() ? nullptr : &backpack_[local];
    }
    if (slot >= kBankStart && slot < kBankEnd) {
        uint8_t local = slot - kBankStart;
        return bank_[local].IsEmpty() ? nullptr : &bank_[local];
    }
    if (slot >= kBankBagStart && slot < kBankBagEnd) {
        const auto& bag = bank_bags_[slot - kBankBagStart];
        return bag.item.IsEmpty() ? nullptr : &bag.item;
    }
    if (slot >= kBuybackStart && slot < kBuybackEnd) {
        uint8_t local = slot - kBuybackStart;
        return buyback_[local].IsEmpty() ? nullptr : &buyback_[local];
    }
    if (slot >= kKeyringStart && slot < kKeyringEnd) {
        uint8_t local = slot - kKeyringStart;
        return keyring_[local].IsEmpty() ? nullptr : &keyring_[local];
    }
    if (slot >= kCurrencyStart && slot < kCurrencyEnd) {
        const uint8_t local = slot - kCurrencyStart;
        return currency_items_[local].IsEmpty() ? nullptr : &currency_items_[local];
    }
    return nullptr;
}

ItemInstance* PlayerInventoryReplica::GetMutableRootItem(uint8_t slot) {
    using namespace InventorySlots;
    if (slot < kEquipEnd) {
        return &equip_[slot];
    }
    if (slot >= kBagSlotsStart && slot < kBagSlotsEnd) {
        return &bags_[slot - kBagSlotsStart].item;
    }
    if (slot >= kBackpackStart && slot < kBackpackEnd) {
        return &backpack_[slot - kBackpackStart];
    }
    if (slot >= kBankStart && slot < kBankEnd) {
        return &bank_[slot - kBankStart];
    }
    if (slot >= kBankBagStart && slot < kBankBagEnd) {
        return &bank_bags_[slot - kBankBagStart].item;
    }
    if (slot >= kBuybackStart && slot < kBuybackEnd) {
        return &buyback_[slot - kBuybackStart];
    }
    if (slot >= kKeyringStart && slot < kKeyringEnd) {
        return &keyring_[slot - kKeyringStart];
    }
    if (slot >= kCurrencyStart && slot < kCurrencyEnd) {
        return &currency_items_[slot - kCurrencyStart];
    }
    return nullptr;
}

void PlayerInventoryReplica::SetEquipSlot(uint8_t slot, const ItemInstance& item) {
    if (slot < kMaxEquipSlots) {
        equip_[slot] = item;
        NotifyItemChanged(slot, item);
    }
}

void PlayerInventoryReplica::ClearEquipSlot(uint8_t slot) {
    if (slot < kMaxEquipSlots) {
        equip_[slot] = ItemInstance{};
        NotifyItemChanged(slot, equip_[slot]);
    }
}

const ItemInstance* PlayerInventoryReplica::GetEquipSlot(uint8_t slot) const {
    if (slot >= kMaxEquipSlots) return nullptr;
    return equip_[slot].IsEmpty() ? nullptr : &equip_[slot];
}

void PlayerInventoryReplica::SetBackpackSlot(uint8_t slot, const ItemInstance& item) {
    if (slot < kBackpackSize) {
        backpack_[slot] = item;
        NotifyItemChanged(InventorySlots::kBackpackStart + slot, item);
    }
}

void PlayerInventoryReplica::ClearBackpackSlot(uint8_t slot) {
    if (slot < kBackpackSize) {
        backpack_[slot] = ItemInstance{};
    }
}

const ItemInstance* PlayerInventoryReplica::GetBackpackSlot(uint8_t slot) const {
    if (slot >= kBackpackSize) return nullptr;
    return backpack_[slot].IsEmpty() ? nullptr : &backpack_[slot];
}

void PlayerInventoryReplica::SetBag(uint8_t bagIndex, const BagInfo& bag) {
    if (bagIndex >= 1 && bagIndex <= kMaxBags) {
        bags_[bagIndex - 1] = NormalizeBagInfo(bag);
    }
}

const BagInfo* PlayerInventoryReplica::GetBag(uint8_t bagIndex) const {
    if (bagIndex < 1 || bagIndex > kMaxBags) return nullptr;
    const auto& b = bags_[bagIndex - 1];
    return b.IsEmpty() ? nullptr : &b;
}

void PlayerInventoryReplica::SetBagSlot(uint8_t bagIndex, uint8_t slot,
                                  const ItemInstance& item) {
    if (bagIndex < 1 || bagIndex > kMaxBags) return;
    auto& bag = bags_[bagIndex - 1];
    if (slot < bag.slots.size()) bag.slots[slot] = item;
}

void PlayerInventoryReplica::ClearBagSlot(uint8_t bagIndex, uint8_t slot) {
    if (bagIndex < 1 || bagIndex > kMaxBags) return;
    auto& bag = bags_[bagIndex - 1];
    if (slot < bag.slots.size()) bag.slots[slot] = ItemInstance{};
}

const ItemInstance* PlayerInventoryReplica::GetBagSlot(uint8_t bagIndex,
                                                 uint8_t slot) const {
    if (bagIndex < 1 || bagIndex > kMaxBags) return nullptr;
    const auto& bag = bags_[bagIndex - 1];
    if (slot >= bag.slots.size()) return nullptr;
    return bag.slots[slot].IsEmpty() ? nullptr : &bag.slots[slot];
}

std::uint32_t PlayerInventoryReplica::GetLuaContainerNumSlots(
    const std::int32_t container, const bool bank_frame_open) const {

    if (container == 0) {
        return kBackpackSize;
    }
    if (container == -1) {
        return kBankSlots;
    }
    if (container == -2) {
        return kKeyringSlots;
    }
    if (container == -4) {
        return InventorySlots::kCurrencyEnd - InventorySlots::kCurrencyStart;
    }
    if (container >= 1 && container <= kMaxBags) {
        const auto& bag = bags_[container - 1];
        return bag.IsEmpty() ? 0u : bag.num_slots;
    }
    if (container >= 5 && container <= 11) {
        if (!bank_frame_open) {
            return 0;
        }

        const auto& bag = bank_bags_[container - 5];
        return bag.IsEmpty() ? 0u : bag.num_slots;
    }

    return 0;
}

size_t PlayerInventoryReplica::GetContainerNumSlots(uint8_t container) const {
    if (container == 0) return kBackpackSize;
    if (container >= 1 && container <= kMaxBags) {
        const auto& bag = bags_[container - 1];
        return bag.IsEmpty() ? 0 : bag.num_slots;
    }
    return 0;
}

const ItemInstance* PlayerInventoryReplica::GetContainerSlot(uint8_t container,
                                                       uint8_t slot) const {
    if (container == 0) {
        if (slot >= kBackpackSize) return nullptr;
        return backpack_[slot].IsEmpty() ? nullptr : &backpack_[slot];
    }
    if (container >= 1 && container <= kMaxBags) {
        const auto& bag = bags_[container - 1];
        if (slot >= bag.slots.size()) return nullptr;
        return bag.slots[slot].IsEmpty() ? nullptr : &bag.slots[slot];
    }
    return nullptr;
}

void PlayerInventoryReplica::SetBankSlot(uint8_t slot, const ItemInstance& item) {
    if (slot < kBankSlots) {
        bank_[slot] = item;
        NotifyItemChanged(InventorySlots::kBankStart + slot, item);
    }
}

void PlayerInventoryReplica::ClearBankSlot(uint8_t slot) {
    if (slot < kBankSlots) bank_[slot] = ItemInstance{};
}

const ItemInstance* PlayerInventoryReplica::GetBankSlot(uint8_t slot) const {
    if (slot >= kBankSlots) return nullptr;
    return bank_[slot].IsEmpty() ? nullptr : &bank_[slot];
}

void PlayerInventoryReplica::SetBankBag(uint8_t bagIndex, const BagInfo& bag) {
    if (bagIndex < kMaxBankBags) {
        bank_bags_[bagIndex] = NormalizeBagInfo(bag);
    }
}

const BagInfo* PlayerInventoryReplica::GetBankBag(uint8_t bagIndex) const {
    if (bagIndex >= kMaxBankBags) return nullptr;
    const auto& b = bank_bags_[bagIndex];
    return b.IsEmpty() ? nullptr : &b;
}

void PlayerInventoryReplica::SetBankBagSlot(uint8_t bagIndex, uint8_t slot,
                                      const ItemInstance& item) {
    if (bagIndex >= kMaxBankBags) return;
    auto& bag = bank_bags_[bagIndex];
    if (slot < bag.slots.size()) bag.slots[slot] = item;
}

void PlayerInventoryReplica::ClearBankBagSlot(uint8_t bagIndex, uint8_t slot) {
    if (bagIndex >= kMaxBankBags) return;
    auto& bag = bank_bags_[bagIndex];
    if (slot < bag.slots.size()) bag.slots[slot] = ItemInstance{};
}

const ItemInstance* PlayerInventoryReplica::GetBankBagSlot(uint8_t bagIndex,
                                                     uint8_t slot) const {
    if (bagIndex >= kMaxBankBags) return nullptr;
    const auto& bag = bank_bags_[bagIndex];
    if (slot >= bag.slots.size()) return nullptr;
    return bag.slots[slot].IsEmpty() ? nullptr : &bag.slots[slot];
}

void PlayerInventoryReplica::SetKeyringSlot(uint8_t slot, const ItemInstance& item) {
    if (slot < kKeyringSlots) {
        keyring_[slot] = item;
        NotifyItemChanged(InventorySlots::kKeyringStart + slot, item);
    }
}

void PlayerInventoryReplica::ClearKeyringSlot(uint8_t slot) {
    if (slot < kKeyringSlots) keyring_[slot] = ItemInstance{};
}

const ItemInstance* PlayerInventoryReplica::GetKeyringSlot(uint8_t slot) const {
    if (slot >= kKeyringSlots) return nullptr;
    return keyring_[slot].IsEmpty() ? nullptr : &keyring_[slot];
}

void PlayerInventoryReplica::SetBuybackSlot(uint8_t slot, const ItemInstance& item) {
    if (slot < kBuybackSlots) {
        buyback_[slot] = item;
    }
}

void PlayerInventoryReplica::ClearBuybackSlot(uint8_t slot) {
    if (slot < kBuybackSlots) buyback_[slot] = ItemInstance{};
}

const ItemInstance* PlayerInventoryReplica::GetBuybackSlot(uint8_t slot) const {
    if (slot >= kBuybackSlots) return nullptr;
    return buyback_[slot].IsEmpty() ? nullptr : &buyback_[slot];
}

const ItemInstance* PlayerInventoryReplica::GetItemInSlot(uint8_t slot) const {
    return GetRootItem(slot);
}

void PlayerInventoryReplica::SetItemInSlot(uint8_t slot, const ItemInstance& item) {
    using namespace InventorySlots;
    if (slot < kEquipEnd) {
        equip_[slot] = item;
    } else if (slot >= kBagSlotsStart && slot < kBagSlotsEnd) {
        auto& bag = bags_[slot - kBagSlotsStart];
        bag.item = item;
        bag.guid = item.guid;
        bag.entry = item.entry;
    } else if (slot >= kBackpackStart && slot < kBackpackEnd) {
        backpack_[slot - kBackpackStart] = item;
    } else if (slot >= kBankStart && slot < kBankEnd) {
        bank_[slot - kBankStart] = item;
    } else if (slot >= kBankBagStart && slot < kBankBagEnd) {
        auto& bag = bank_bags_[slot - kBankBagStart];
        bag.item = item;
        bag.guid = item.guid;
        bag.entry = item.entry;
    } else if (slot >= kBuybackStart && slot < kBuybackEnd) {
        buyback_[slot - kBuybackStart] = item;
    } else if (slot >= kKeyringStart && slot < kKeyringEnd) {
        keyring_[slot - kKeyringStart] = item;
    } else if (slot >= kCurrencyStart && slot < kCurrencyEnd) {
        currency_items_[slot - kCurrencyStart] = item;
    } else {
        return;
    }
    NotifyItemChanged(slot, item);
}

void PlayerInventoryReplica::ClearSlot(uint8_t slot) {
    SetItemInSlot(slot, ItemInstance{});
}

bool PlayerInventoryReplica::IsSlotEmpty(uint8_t slot) const {
    const auto* item = GetRootItem(slot);
    return item == nullptr;
}

void PlayerInventoryReplica::SetSlotGuid(uint8_t slot, uint64_t guid) {
    if (slot < InventorySlots::kTotalSlots) {
        slot_guids_[slot] = guid;
    }
}

uint64_t PlayerInventoryReplica::GetSlotGuid(uint8_t slot) const {
    if (slot < InventorySlots::kTotalSlots) {
        return slot_guids_[slot];
    }
    return 0;
}

PlayerInventoryReplica::SlotGuidSnapshot PlayerInventoryReplica::CaptureSlotGuids() const {
    SlotGuidSnapshot snapshot = slot_guids_;
    for (std::uint16_t slot = 0; slot < InventorySlots::kTotalSlots; ++slot) {
        if (snapshot[slot] != 0) {
            continue;
        }

        const auto* item = GetRootItem(static_cast<std::uint8_t>(slot));
        if (item != nullptr) {
            snapshot[slot] = item->guid;
        }
    }
    return snapshot;
}

int16_t PlayerInventoryReplica::FindSlotByGuid(uint64_t guid) const {
    if (guid == 0) return -1;
    for (uint16_t i = 0; i < InventorySlots::kTotalSlots; ++i) {
        if (slot_guids_[i] == guid) return static_cast<int16_t>(i);
    }
    return -1;
}

const ItemInstance* PlayerInventoryReplica::FindItemByGuid(uint64_t guid) const {
    if (guid == 0) {
        return nullptr;
    }

    for (const auto& item : equip_) {
        if (item.guid == guid && !item.IsEmpty()) {
            return &item;
        }
    }

    for (const auto& item : backpack_) {
        if (item.guid == guid && !item.IsEmpty()) {
            return &item;
        }
    }

    for (const auto& bag : bags_) {
        if (bag.item.guid == guid && !bag.item.IsEmpty()) {
            return &bag.item;
        }
        for (const auto& item : bag.slots) {
            if (item.guid == guid && !item.IsEmpty()) {
                return &item;
            }
        }
    }

    for (const auto& item : bank_) {
        if (item.guid == guid && !item.IsEmpty()) {
            return &item;
        }
    }

    for (const auto& bag : bank_bags_) {
        if (bag.item.guid == guid && !bag.item.IsEmpty()) {
            return &bag.item;
        }
        for (const auto& item : bag.slots) {
            if (item.guid == guid && !item.IsEmpty()) {
                return &item;
            }
        }
    }

    for (const auto& item : buyback_) {
        if (item.guid == guid && !item.IsEmpty()) {
            return &item;
        }
    }

    for (const auto& item : keyring_) {
        if (item.guid == guid && !item.IsEmpty()) {
            return &item;
        }
    }

    for (const auto& item : currency_items_) {
        if (item.guid == guid && !item.IsEmpty()) {
            return &item;
        }
    }

    return nullptr;
}

int16_t PlayerInventoryReplica::FindItemByEntry(uint32_t entry) const {

    for (uint8_t i = 0; i < kMaxEquipSlots; ++i) {
        if (equip_[i].entry == entry) return static_cast<int16_t>(i);
    }

    for (uint8_t i = 0; i < kBackpackSize; ++i) {
        if (backpack_[i].entry == entry)
            return static_cast<int16_t>(InventorySlots::kBackpackStart + i);
    }

    for (uint8_t b = 0; b < kMaxBags; ++b) {
        if (bags_[b].item.entry == entry) {
            return static_cast<int16_t>(InventorySlots::kBagSlotsStart + b);
        }
        for (size_t s = 0; s < bags_[b].slots.size(); ++s) {
            if (bags_[b].slots[s].entry == entry)
                return static_cast<int16_t>(InventorySlots::kBackpackStart + s);
        }
    }

    for (uint8_t i = 0; i < kBankSlots; ++i) {
        if (bank_[i].entry == entry)
            return static_cast<int16_t>(InventorySlots::kBankStart + i);
    }

    for (uint8_t i = 0; i < kKeyringSlots; ++i) {
        if (keyring_[i].entry == entry)
            return static_cast<int16_t>(InventorySlots::kKeyringStart + i);
    }

    for (uint8_t b = 0; b < kMaxBankBags; ++b) {
        if (bank_bags_[b].item.entry == entry) {
            return static_cast<int16_t>(InventorySlots::kBankBagStart + b);
        }
    }
    return -1;
}

bool PlayerInventoryReplica::VisitDefaultPlayerItems(
    const std::function<bool(const ItemInstance&)>& visitor) const {
    const auto visit_if_present = [&visitor](const ItemInstance& item) {
        return item.IsEmpty() || visitor(item);
    };

    for (const auto& item : equip_) {
        if (!visit_if_present(item)) {
            return false;
        }
    }
    for (const auto& bag : bags_) {
        if (!visit_if_present(bag.item)) {
            return false;
        }
        for (const auto& item : bag.slots) {
            if (!visit_if_present(item)) {
                return false;
            }
        }
    }
    for (const auto& item : backpack_) {
        if (!visit_if_present(item)) {
            return false;
        }
    }
    for (const auto& item : keyring_) {
        if (!visit_if_present(item)) {
            return false;
        }
    }
    for (const auto& item : currency_items_) {
        if (!visit_if_present(item)) {
            return false;
        }
    }

    return true;
}

uint32_t PlayerInventoryReplica::CountItemsOfEntry(uint32_t entry) const {
    return CountDefaultPlayerItemsOfEntry(entry) + CountBankItemsOfEntry(entry);
}

uint32_t PlayerInventoryReplica::CountDefaultPlayerItemsOfEntry(uint32_t entry) const {
    if (entry == 0) {
        return 0;
    }

    uint32_t total = 0;
    {

        for (const auto& item : equip_) {
            if (item.entry == entry) total += std::max(item.count, 1u);
        }
        for (const auto& item : backpack_) {
            if (item.entry == entry) total += std::max(item.count, 1u);
        }
        for (const auto& bag : bags_) {
            if (bag.item.entry == entry) {
                total += std::max(bag.item.count, 1u);
            }
            for (const auto& item : bag.slots) {
                if (item.entry == entry) total += std::max(item.count, 1u);
            }
        }
        for (const auto& item : keyring_) {
            if (item.entry == entry) total += std::max(item.count, 1u);
        }
    }

    return total +
           (currency_amount_resolver_ ? currency_amount_resolver_(entry) : 0);
}

uint32_t PlayerInventoryReplica::CountCarriedItemsOfEntry(uint32_t entry) const {
    if (entry == 0) {
        return 0;
    }

    uint32_t total = 0;
    {

        for (const auto& item : backpack_) {
            total += CountItemStack(item, entry);
        }

        for (const auto& bag : bags_) {
            for (const auto& item : bag.slots) {
                total += CountItemStack(item, entry);
            }
        }

        for (const auto& item : keyring_) {
            total += CountItemStack(item, entry);
        }
    }

    return total +
           (currency_amount_resolver_ ? currency_amount_resolver_(entry) : 0);
}

uint32_t PlayerInventoryReplica::CountBankItemsOfEntry(uint32_t entry) const {
    if (entry == 0) {
        return 0;
    }
    uint32_t total = 0;

    for (const auto& item : bank_) {
        total += CountItemStack(item, entry);
    }

    for (const auto& bag : bank_bags_) {
        total += CountItemStack(bag.item, entry);
        for (const auto& item : bag.slots) {
            total += CountItemStack(item, entry);
        }
    }

    return total;
}

uint32_t PlayerInventoryReplica::GetItemCount(const uint32_t entry, const bool include_bank) const {
    const auto carried = CountCarriedItemsOfEntry(entry);
    if (!include_bank) {
        return carried;
    }

    return carried + CountBankItemsOfEntry(entry);
}

bool PlayerInventoryReplica::HasItem(uint32_t entry) const {
    return CountItemsOfEntry(entry) > 0;
}

int8_t PlayerInventoryReplica::FindFreeBackpackSlot() const {
    for (uint8_t i = 0; i < kBackpackSize; ++i) {
        if (backpack_[i].IsEmpty()) return static_cast<int8_t>(i);
    }
    return -1;
}

int8_t PlayerInventoryReplica::FindFreeSlotInBag(uint8_t bagIndex) const {
    if (bagIndex == 0) {

        for (uint8_t i = 0; i < kBackpackSize; ++i) {
            if (backpack_[i].IsEmpty()) return static_cast<int8_t>(i);
        }
        return -1;
    }
    if (bagIndex >= 1 && bagIndex <= kMaxBags) {
        const auto& bag = bags_[bagIndex - 1];
        if (bag.IsEmpty()) return -1;
        for (size_t i = 0; i < bag.slots.size(); ++i) {
            if (bag.slots[i].IsEmpty()) return static_cast<int8_t>(i);
        }
        return -1;
    }
    return -1;
}

int PlayerInventoryReplica::GetBagSize(uint8_t bagIndex) const {
    if (bagIndex == 0) return kBackpackSize;
    if (bagIndex >= 1 && bagIndex <= kMaxBags) {
        const auto& bag = bags_[bagIndex - 1];
        return bag.IsEmpty() ? 0 : bag.num_slots;
    }
    return 0;
}

uint32_t PlayerInventoryReplica::GetTotalFreeSlots() const {
    uint32_t free = 0;

    for (const auto& item : backpack_) {
        if (item.IsEmpty()) ++free;
    }
    for (const auto& bag : bags_) {
        if (bag.IsEmpty()) continue;
        for (const auto& item : bag.slots) {
            if (item.IsEmpty()) ++free;
        }
    }

    return free;
}

uint32_t PlayerInventoryReplica::GetFreeSlotCount() const {
    return GetTotalFreeSlots();
}

uint32_t PlayerInventoryReplica::GetEquippedItemDisplayId(uint8_t slot) const {
    if (slot >= kMaxEquipSlots) return 0;
    return equip_[slot].entry;
}

void PlayerInventoryReplica::CommitServerRevision() {
    ++revision_;
}

std::uint64_t PlayerInventoryReplica::revision() const {
    return revision_;
}

void PlayerInventoryReplica::SetCurrency(uint32_t currencyId, uint32_t amount) {
    currencies_[currencyId] = amount;
}

uint32_t PlayerInventoryReplica::GetCurrency(uint32_t currencyId) const {
    auto it = currencies_.find(currencyId);
    return it != currencies_.end() ? it->second : 0;
}

void PlayerInventoryReplica::Reset() {
    equip_ = {};
    backpack_ = {};
    bags_ = {};
    bank_ = {};
    bank_bags_ = {};
    keyring_ = {};
    currency_items_ = {};
    buyback_ = {};
    currencies_.clear();
    slot_guids_ = {};
    ++revision_;
    on_item_changed_ = nullptr;
}

}

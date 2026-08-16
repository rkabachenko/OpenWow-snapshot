
#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/inventory/model/item_instance.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct BagInfo {
    uint64_t guid = 0;
    uint32_t entry = 0;
    ItemInstance item{};
    uint8_t num_slots = 0;
    std::vector<ItemInstance> slots;

    [[nodiscard]] bool IsEmpty() const { return entry == 0; }
};

namespace InventorySlots {

    constexpr uint8_t kHead        = 0;
    constexpr uint8_t kNeck        = 1;
    constexpr uint8_t kShoulders   = 2;
    constexpr uint8_t kBody        = 3;
    constexpr uint8_t kChest       = 4;
    constexpr uint8_t kWaist       = 5;
    constexpr uint8_t kLegs        = 6;
    constexpr uint8_t kFeet        = 7;
    constexpr uint8_t kWrists      = 8;
    constexpr uint8_t kHands       = 9;
    constexpr uint8_t kFinger1     = 10;
    constexpr uint8_t kFinger2     = 11;
    constexpr uint8_t kTrinket1    = 12;
    constexpr uint8_t kTrinket2    = 13;
    constexpr uint8_t kBack        = 14;
    constexpr uint8_t kMainHand    = 15;
    constexpr uint8_t kOffHand     = 16;
    constexpr uint8_t kRanged      = 17;
    constexpr uint8_t kTabard      = 18;

    constexpr uint8_t kEquipStart    = 0;
    constexpr uint8_t kEquipEnd      = 19;
    constexpr uint8_t kBagSlotsStart = 19;
    constexpr uint8_t kBagSlotsEnd   = 23;
    constexpr uint8_t kBackpackStart = 23;
    constexpr uint8_t kBackpackEnd   = 39;
    constexpr uint8_t kBankStart     = 39;
    constexpr uint8_t kBankEnd       = 67;
    constexpr uint8_t kBankBagStart  = 67;
    constexpr uint8_t kBankBagEnd    = 74;
    constexpr uint8_t kBuybackStart  = 74;
    constexpr uint8_t kBuybackEnd    = 86;
    constexpr uint8_t kKeyringStart  = 86;
    constexpr uint8_t kKeyringEnd    = 118;
    constexpr uint8_t kCurrencyStart = 118;
    constexpr uint8_t kCurrencyEnd   = 150;

    constexpr uint16_t kTotalSlots   = 150;

    constexpr uint8_t kMainBag       = 255;
}

class PlayerInventoryReplica {
 public:
    using SlotGuidSnapshot =
        std::array<std::uint64_t, InventorySlots::kTotalSlots>;

    PlayerInventoryReplica() = default;

    void SetEquipSlot(uint8_t slot, const ItemInstance& item);
    void ClearEquipSlot(uint8_t slot);
    [[nodiscard]] const ItemInstance* GetEquipSlot(uint8_t slot) const;
    static constexpr uint8_t kMaxEquipSlots = 19;

    void SetBackpackSlot(uint8_t slot, const ItemInstance& item);
    void ClearBackpackSlot(uint8_t slot);
    [[nodiscard]] const ItemInstance* GetBackpackSlot(uint8_t slot) const;
    static constexpr uint8_t kBackpackSize = 16;

    void SetBag(uint8_t bagIndex, const BagInfo& bag);
    [[nodiscard]] const BagInfo* GetBag(uint8_t bagIndex) const;
    void SetBagSlot(uint8_t bagIndex, uint8_t slot, const ItemInstance& item);
    void ClearBagSlot(uint8_t bagIndex, uint8_t slot);
    [[nodiscard]] const ItemInstance* GetBagSlot(uint8_t bagIndex, uint8_t slot) const;
    static constexpr uint8_t kMaxBags = 4;

    [[nodiscard]] size_t GetContainerNumSlots(uint8_t container) const;
    [[nodiscard]] const ItemInstance* GetContainerSlot(uint8_t container, uint8_t slot) const;
    [[nodiscard]] std::uint32_t GetLuaContainerNumSlots(std::int32_t container,
                                                        bool bank_frame_open) const;

    void SetBankSlot(uint8_t slot, const ItemInstance& item);
    void ClearBankSlot(uint8_t slot);
    [[nodiscard]] const ItemInstance* GetBankSlot(uint8_t slot) const;
    static constexpr uint8_t kBankSlots = 28;

    void SetBankBag(uint8_t bagIndex, const BagInfo& bag);
    [[nodiscard]] const BagInfo* GetBankBag(uint8_t bagIndex) const;
    void SetBankBagSlot(uint8_t bagIndex, uint8_t slot, const ItemInstance& item);
    void ClearBankBagSlot(uint8_t bagIndex, uint8_t slot);
    [[nodiscard]] const ItemInstance* GetBankBagSlot(uint8_t bagIndex, uint8_t slot) const;
    static constexpr uint8_t kMaxBankBags = 7;

    void SetKeyringSlot(uint8_t slot, const ItemInstance& item);
    void ClearKeyringSlot(uint8_t slot);
    [[nodiscard]] const ItemInstance* GetKeyringSlot(uint8_t slot) const;
    static constexpr uint8_t kKeyringSlots = 32;

    void SetBuybackSlot(uint8_t slot, const ItemInstance& item);
    void ClearBuybackSlot(uint8_t slot);
    [[nodiscard]] const ItemInstance* GetBuybackSlot(uint8_t slot) const;
    static constexpr uint8_t kBuybackSlots = 12;

    [[nodiscard]] const ItemInstance* GetItemInSlot(uint8_t slot) const;
    void SetItemInSlot(uint8_t slot, const ItemInstance& item);
    void ClearSlot(uint8_t slot);
    [[nodiscard]] bool IsSlotEmpty(uint8_t slot) const;

    void SetSlotGuid(uint8_t slot, uint64_t guid);
    [[nodiscard]] uint64_t GetSlotGuid(uint8_t slot) const;

    [[nodiscard]] SlotGuidSnapshot CaptureSlotGuids() const;
    [[nodiscard]] int16_t FindSlotByGuid(uint64_t guid) const;
    [[nodiscard]] const ItemInstance* FindItemByGuid(uint64_t guid) const;

    [[nodiscard]] bool VisitDefaultPlayerItems(
        const std::function<bool(const ItemInstance&)>& visitor) const;

    [[nodiscard]] int16_t FindItemByEntry(uint32_t entry) const;

    [[nodiscard]] uint32_t CountDefaultPlayerItemsOfEntry(uint32_t entry) const;
    [[nodiscard]] uint32_t CountCarriedItemsOfEntry(uint32_t entry) const;
    [[nodiscard]] uint32_t CountBankItemsOfEntry(uint32_t entry) const;
    [[nodiscard]] uint32_t CountItemsOfEntry(uint32_t entry) const;
    [[nodiscard]] uint32_t GetItemCount(uint32_t entry,
                                        bool include_bank = false) const;
    [[nodiscard]] bool HasItem(uint32_t entry) const;

    [[nodiscard]] int8_t FindFreeBackpackSlot() const;
    [[nodiscard]] int8_t FindFreeSlotInBag(uint8_t bagIndex) const;
    [[nodiscard]] int GetBagSize(uint8_t bagIndex) const;
    [[nodiscard]] uint32_t GetTotalFreeSlots() const;
    [[nodiscard]] uint32_t GetFreeSlotCount() const;

    [[nodiscard]] uint32_t GetEquippedItemDisplayId(uint8_t slot) const;
    void CommitServerRevision();
    [[nodiscard]] std::uint64_t revision() const;

    void SetCurrency(uint32_t currencyId, uint32_t amount);
    [[nodiscard]] uint32_t GetCurrency(uint32_t currencyId) const;

    using ItemEventCallback = std::function<void(uint8_t slot, const ItemInstance&)>;
    void SetOnItemChanged(ItemEventCallback cb) { on_item_changed_ = std::move(cb); }
    using CurrencyAmountResolver = std::function<uint32_t(uint32_t item_entry)>;
    void SetCurrencyAmountResolver(CurrencyAmountResolver resolver) {
        currency_amount_resolver_ = std::move(resolver);
    }

    void Reset();

 private:
    void NotifyItemChanged(uint8_t slot, const ItemInstance& item);

    [[nodiscard]] const ItemInstance* GetRootItem(uint8_t slot) const;
    ItemInstance* GetMutableRootItem(uint8_t slot);
    std::array<ItemInstance, kMaxEquipSlots> equip_{};
    std::array<ItemInstance, kBackpackSize> backpack_{};
    std::array<BagInfo, kMaxBags> bags_{};
    std::array<ItemInstance, kBankSlots> bank_{};
    std::array<BagInfo, kMaxBankBags> bank_bags_{};
    std::array<ItemInstance, kKeyringSlots> keyring_{};
    std::array<ItemInstance, InventorySlots::kCurrencyEnd -
                                 InventorySlots::kCurrencyStart>
        currency_items_{};
    std::array<ItemInstance, kBuybackSlots> buyback_{};
    std::unordered_map<uint32_t, uint32_t> currencies_;
    std::array<uint64_t, InventorySlots::kTotalSlots> slot_guids_{};

    ItemEventCallback on_item_changed_;
    CurrencyAmountResolver currency_amount_resolver_;
    std::uint64_t revision_{0};
};

}

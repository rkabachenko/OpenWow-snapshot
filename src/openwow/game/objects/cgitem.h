#pragma once

#include "openwow/game/objects/cgobject.h"

#include <array>
#include <cstdint>

namespace openwow::game {

class WorldSession;

class ItemDefinitions;
struct ItemTemplate;

inline constexpr std::uint32_t kItemFlagSoulbound = 0x01;
inline constexpr std::uint32_t kItemFlagConjured = 0x02;
inline constexpr std::uint32_t kItemFlagLootable = 0x04;
inline constexpr std::uint32_t kItemFieldFlagWrapped = 0x08;

inline constexpr std::uint32_t kItemFieldFlagBroken = 0x10;

inline constexpr std::uint32_t kItemFlagIndestructible = 0x20;
inline constexpr std::uint32_t kItemFlagBopTradeable = 0x100;
inline constexpr std::uint32_t kItemFlagWrapped = 0x200;

inline constexpr std::uint8_t kEnchantSlotPermanent = 0;
inline constexpr std::uint8_t kEnchantSlotTemporary = 1;
inline constexpr std::uint8_t kEnchantSlotSocket1 = 2;
inline constexpr std::uint8_t kEnchantSlotSocket2 = 3;
inline constexpr std::uint8_t kEnchantSlotSocket3 = 4;
inline constexpr std::uint8_t kEnchantSlotBonus = 5;
inline constexpr std::uint8_t kEnchantSlotPrismatic = 6;
inline constexpr std::uint8_t kEnchantSlotUse = 7;
inline constexpr std::uint8_t kEnchantSlotProp0 = 8;
inline constexpr std::uint8_t kEnchantSlotProp1 = 9;
inline constexpr std::uint8_t kEnchantSlotProp2 = 10;
inline constexpr std::uint8_t kEnchantSlotProp3 = 11;
inline constexpr std::uint8_t kMaxEnchantSlots = 12;
inline constexpr std::uint8_t kFieldsPerEnchant = 3;

struct ItemEnchantment {
  std::uint32_t enchant_id{0};
  std::uint32_t duration{0};
  std::uint32_t charges{0};
};

class CGItem_C : public CGObject_C {
public:
  explicit CGItem_C(ItemDefinitions& item_definitions,
                    TypeID type_id = TypeID::kItem);
  CGItem_C(ItemDefinitions& item_definitions, ObjectGuid guid,
           TypeID type_id = TypeID::kItem);
  CGItem_C(ObjectManager& objects, ItemDefinitions& item_definitions,
           ObjectGuid guid, TypeID type_id = TypeID::kItem);
  ~CGItem_C() override = default;

  [[nodiscard]] ObjectGuid GetOwner() const;
  [[nodiscard]] ObjectGuid GetContainedIn() const;
  [[nodiscard]] ObjectGuid GetCreator() const;
  [[nodiscard]] ObjectGuid GetGiftCreator() const;

  [[nodiscard]] std::uint32_t GetStackCount() const;
  [[nodiscard]] std::uint32_t GetDuration() const;
  [[nodiscard]] std::uint32_t GetRemainingDurationSeconds() const;
  void SetExpiryDurationSeconds(std::int32_t duration_seconds);

  [[nodiscard]] std::int32_t GetSpellCharges(std::uint8_t slot) const;

  [[nodiscard]] std::uint32_t GetCharges(std::uint8_t index) const;
  void ApplyServerSpellChargeUpdate(
      const std::array<std::uint32_t, 5>& spell_charges);

  [[nodiscard]] std::uint32_t GetItemFlags() const;
  [[nodiscard]] bool HasItemFlag(std::uint32_t flag) const;

  [[nodiscard]] bool IsLocked() const noexcept;

  bool SetLocked(bool locked) noexcept;

  [[nodiscard]] bool HasBindingEnchantSlot() const;

  [[nodiscard]] bool IsSoulbound() const;

  [[nodiscard]] bool IsBoundTradeExpired(
      std::uint32_t current_total_played_time) const;

  [[nodiscard]] bool IsBoundOrHasBindingEnchant(
      std::uint32_t current_total_played_time) const;

  [[nodiscard]] ItemEnchantment GetEnchantment(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetEnchantId(std::uint8_t slot) const;
  [[nodiscard]] bool HasEnchantment(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetEnchantTimeRemainingMs(std::uint8_t slot) const;
  void SetEnchantTimeRemainingSeconds(std::uint8_t slot, std::int32_t duration_seconds);

  [[nodiscard]] std::uint32_t GetEnchantIdIfVisible(std::uint8_t slot) const;

  [[nodiscard]] std::int16_t GetEnchantChargesIfVisible(std::uint8_t slot) const;

  [[nodiscard]] std::uint32_t GetEnchantDurationFieldIfVisible(std::uint8_t slot) const;

  [[nodiscard]] std::uint32_t GetPropertySeed() const;
  [[nodiscard]] std::int32_t GetRandomPropertiesId() const;

  [[nodiscard]] std::uint32_t GetRandomPropertyID() const;
  [[nodiscard]] std::uint32_t GetItemSuffixFactor() const;

  [[nodiscard]] std::uint32_t GetDurability() const;
  [[nodiscard]] std::uint32_t GetMaxDurability() const;
  [[nodiscard]] float GetDurabilityPercent() const;
  [[nodiscard]] bool IsBroken() const;

  [[nodiscard]] std::uint32_t GetRepairCost() const;

  [[nodiscard]] std::uint32_t GetCreatePlayedTime() const;

  std::vector<std::uint16_t> ApplyCreateUpdate(const CreateObjectUpdate &upd) override;
  std::vector<std::uint16_t> ApplyValuesUpdate(const ValuesUpdate &upd) override;
  void FinalizeWorldPublication() override;

  [[nodiscard]] float GetItemFacing() const;

  [[nodiscard]] bool IsItemVisible() const override;

  [[nodiscard]] float GetBoundsRadius() const override;

  [[nodiscard]] static std::int32_t GetDefaultInventoryType();

  [[nodiscard]] std::uint32_t GetItemClassFromClientDbc() const;

  [[nodiscard]] std::uint32_t GetItemSubClassFromClientDbc() const;

  [[nodiscard]] std::uint32_t GetInventoryTypeFromClientDbc() const;

  [[nodiscard]] const ItemTemplate *GetItemTemplate() const;

  [[nodiscard]] std::uint32_t GetCachedQueryFlags() const;

  [[nodiscard]] std::uint32_t GetRequiredLevelFromTemplate() const;

  [[nodiscard]] const ItemTemplate *GetOrRequestQueryItemTemplate() const;

  [[nodiscard]] std::int32_t GetRandomSuffixInventoryBucket() const;

  [[nodiscard]] std::uint32_t GetItemModelDisplayId() const;

  [[nodiscard]] std::uint32_t ResolveUseSpellId() const;

  [[nodiscard]] bool HasResolvedUseSpell() const;

  [[nodiscard]] std::int32_t GetUseSpellCharges() const;

  [[nodiscard]] bool HasUseSpellEnchantment() const;

  [[nodiscard]] bool HasUsableSpell() const;

  [[nodiscard]] std::int32_t GetEnchantVisualAuraId() const;

  [[nodiscard]] std::uint32_t GetExtraSocketCount() const;

  [[nodiscard]] std::uint32_t GetSocketCountForSocketUI() const;

  void RequestRefundInfo() const;

  [[nodiscard]] CGObject_C *FindObjectByGUID(std::uint64_t guid) const;

private:
  friend bool CheckCooldownStartsOnEvent(
      const WorldSession& session,
      const data::dbc::SpellEntry& spell,
      ObjectGuid item_guid);
  [[nodiscard]] std::uint32_t FindCooldownCategory7EnchantmentId() const;
  void RefreshAllEnchantExpirationTicks();
  void RefreshEnchantExpirationTicks(const std::vector<std::uint16_t> &updated_fields);
  void ApplyPendingEnchantTimeUpdates();

  std::uint32_t item_duration_expiration_time_s_{0};
  std::array<std::uint32_t, kMaxEnchantSlots> enchant_expiration_tick_ms_{};
  std::array<std::uint32_t, 4> server_spell_charge_overrides_{};
  std::uint8_t server_spell_charge_override_mask_{0};
  bool is_locked_{false};
  ItemDefinitions& item_definitions_;
};

class CGContainer_C : public CGItem_C {
public:
  explicit CGContainer_C(ItemDefinitions& item_definitions);
  CGContainer_C(ItemDefinitions& item_definitions, ObjectGuid guid);
  CGContainer_C(ObjectManager& objects, ItemDefinitions& item_definitions,
                ObjectGuid guid);
  ~CGContainer_C() override = default;

  [[nodiscard]] std::uint32_t GetNumSlots() const;
  [[nodiscard]] ObjectGuid GetSlot(std::uint8_t index) const;

  [[nodiscard]] ObjectGuid GetSlotItem(std::uint8_t slot) const {
    return GetSlot(slot);
  }

  [[nodiscard]] std::uint32_t GetNumFreeSlots() const;
};

}

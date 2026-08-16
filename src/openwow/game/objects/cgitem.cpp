
#include "openwow/game/objects/cgitem.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/display_info_resolver.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/inventory/items/item_trade_eligibility.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"

#include <ctime>
#include <cstring>

namespace openwow::game {

namespace {

constexpr std::uint16_t kEnchantFieldCount = kMaxEnchantSlots * kFieldsPerEnchant;
constexpr std::uint32_t kUsableSpellItemFlag = 0x40u;
constexpr std::uint32_t kCooldownCategoryEnchantEffectType = 7u;
constexpr std::uint32_t kPrismaticSocketEnchantEffectType = 8u;
constexpr std::uint32_t kQuestItemFlag = 0x2000u;

bool IsTrackedEnchantField(const std::uint16_t field_index) {
  return field_index >= ITEM_FIELD_ENCHANTMENT_1_1 &&
         field_index < ITEM_FIELD_ENCHANTMENT_1_1 + kEnchantFieldCount;
}

std::uint8_t EnchantSlotFromField(const std::uint16_t field_index) {
  return static_cast<std::uint8_t>((field_index - ITEM_FIELD_ENCHANTMENT_1_1) / kFieldsPerEnchant);
}

std::uint32_t ComputeEnchantExpirationTickMs(const ItemEnchantment enchantment,
                                             const std::uint32_t now_tick_ms) {
  if (enchantment.enchant_id == 0 || enchantment.duration == 0) {
    return 0;
  }
  return now_tick_ms + enchantment.duration;
}

}

CGItem_C::CGItem_C(ItemDefinitions& item_definitions, const TypeID type_id)
    : CGObject_C(type_id), item_definitions_(item_definitions) {}

CGItem_C::CGItem_C(ItemDefinitions& item_definitions, ObjectGuid guid, TypeID type_id)
    : CGObject_C(guid, type_id), item_definitions_(item_definitions) {}

CGItem_C::CGItem_C(ObjectManager& objects, ItemDefinitions& item_definitions,
                   ObjectGuid guid, TypeID type_id)
    : CGObject_C(objects, guid, type_id), item_definitions_(item_definitions) {}

ObjectGuid CGItem_C::GetOwner() const {
  return GetGuidField(ITEM_FIELD_OWNER);
}

ObjectGuid CGItem_C::GetContainedIn() const {
  return GetGuidField(ITEM_FIELD_CONTAINED);
}

ObjectGuid CGItem_C::GetCreator() const {
  return GetGuidField(ITEM_FIELD_CREATOR);
}

ObjectGuid CGItem_C::GetGiftCreator() const {
  return GetGuidField(ITEM_FIELD_GIFTCREATOR);
}

std::uint32_t CGItem_C::GetStackCount() const {
  return GetUInt32(ITEM_FIELD_STACK_COUNT);
}

std::uint32_t CGItem_C::GetDuration() const {
  return GetUInt32(ITEM_FIELD_DURATION);
}

std::uint32_t CGItem_C::GetRemainingDurationSeconds() const {
  if (item_duration_expiration_time_s_ == 0) {
    return 0;
  }

  const auto now = static_cast<std::int64_t>(std::time(nullptr));
  const auto remaining =
      static_cast<std::int64_t>(item_duration_expiration_time_s_) - now;
  return remaining > 0 ? static_cast<std::uint32_t>(remaining) : 0;
}

void CGItem_C::SetExpiryDurationSeconds(const std::int32_t duration_seconds) {
  if (duration_seconds <= 0) {
    item_duration_expiration_time_s_ = 0;
    return;
  }

  const auto now = static_cast<std::uint32_t>(std::time(nullptr));
  item_duration_expiration_time_s_ =
      now + static_cast<std::uint32_t>(duration_seconds);
}

std::int32_t CGItem_C::GetSpellCharges(std::uint8_t slot) const {
  if (slot >= 5)
    return 0;
  std::uint32_t raw = 0;
  if (slot < server_spell_charge_overrides_.size() &&
      (server_spell_charge_override_mask_ & (1u << slot)) != 0) {
    raw = server_spell_charge_overrides_[slot];
  } else {
    raw = GetUInt32(static_cast<std::uint16_t>(ITEM_FIELD_SPELL_CHARGES + slot));
  }
  std::int32_t result;
  std::memcpy(&result, &raw, sizeof(result));
  return result;
}

std::uint32_t CGItem_C::GetCharges(std::uint8_t index) const {
  if (index >= 5)
    return 0;
  if (index < server_spell_charge_overrides_.size() &&
      (server_spell_charge_override_mask_ & (1u << index)) != 0) {
    return server_spell_charge_overrides_[index];
  }
  return GetUInt32(static_cast<std::uint16_t>(ITEM_FIELD_SPELL_CHARGES + index));
}

void CGItem_C::ApplyServerSpellChargeUpdate(
    const std::array<std::uint32_t, 5>& spell_charges) {
  for (std::size_t index = 0; index < spell_charges.size(); ++index) {
    const auto raw_charge = spell_charges[index];
    if (raw_charge == 0u) {
      continue;
    }

    server_spell_charge_overrides_[index] = raw_charge;
    server_spell_charge_override_mask_ |= static_cast<std::uint8_t>(1u << index);
  }
}

std::uint32_t CGItem_C::GetItemFlags() const {
  return GetUInt32(ITEM_FIELD_FLAGS);
}

bool CGItem_C::HasItemFlag(std::uint32_t flag) const {
  return (GetItemFlags() & flag) != 0;
}

bool CGItem_C::IsLocked() const noexcept {
  return is_locked_;
}

bool CGItem_C::SetLocked(const bool locked) noexcept {
  if (is_locked_ == locked) {
    return false;
  }
  is_locked_ = locked;
  return true;
}

bool CGItem_C::HasBindingEnchantSlot() const {
  if ((GetItemFlags() & kQuestItemFlag) != 0u) {
    return false;
  }

  const auto *objects = object_manager();
  const auto *dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return false;
  }

  for (std::uint8_t slot = 0; slot < kMaxEnchantSlots; ++slot) {
    const auto enchant_id = GetEnchantId(slot);
    if (enchant_id == 0u) {
      continue;
    }
    const auto *entry = dbc->spell_item_enchantment().LookupEntry(enchant_id);
    if (entry != nullptr && (entry->slot & 1u) != 0u) {
      return true;
    }
  }
  return false;
}

bool CGItem_C::IsSoulbound() const {
  if (HasItemFlag(kItemFlagSoulbound)) {
    return true;
  }
  return HasBindingEnchantSlot();
}

bool CGItem_C::IsBoundTradeExpired(
    const std::uint32_t current_total_played_time) const {
  const bool has_binding_enchant = HasBindingEnchantSlot();
  if (!HasItemFlag(kItemFlagSoulbound) && !has_binding_enchant) {
    return false;
  }

  if (HasItemFlag(kItemFlagBopTradeable)) {
    const auto active_guid = CGObject_C::GetActivePlayerGuid();
    if (active_guid.IsValid() && GetOwner() == active_guid) {
      return ItemBoundTradeExpiredForActivePlayer(
          GetItemFlags(), GetCreatePlayedTime(), current_total_played_time,
          has_binding_enchant);
    }
  }

  return true;
}

bool CGItem_C::IsBoundOrHasBindingEnchant(
    const std::uint32_t current_total_played_time) const {
  return IsBoundTradeExpired(current_total_played_time) ||
         HasBindingEnchantSlot();
}

ItemEnchantment CGItem_C::GetEnchantment(std::uint8_t slot) const {
  if (slot >= kMaxEnchantSlots)
    return {};
  auto base = static_cast<std::uint16_t>(ITEM_FIELD_ENCHANTMENT_1_1 + slot * kFieldsPerEnchant);
  return {
      GetUInt32(base),
      GetUInt32(static_cast<std::uint16_t>(base + 1)),
      GetUInt32(static_cast<std::uint16_t>(base + 2)),
  };
}

std::uint32_t CGItem_C::GetEnchantId(std::uint8_t slot) const {
  if (slot >= kMaxEnchantSlots)
    return 0;
  return GetUInt32(
      static_cast<std::uint16_t>(ITEM_FIELD_ENCHANTMENT_1_1 + slot * kFieldsPerEnchant));
}

bool CGItem_C::HasEnchantment(std::uint8_t slot) const {
  return GetEnchantId(slot) != 0;
}

std::uint32_t CGItem_C::GetEnchantTimeRemainingMs(const std::uint8_t slot) const {
  if (slot >= kMaxEnchantSlots) {
    return 0;
  }

  const auto expiration_tick = enchant_expiration_tick_ms_[slot];
  if (expiration_tick == 0) {
    return 0;
  }

  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  const auto remaining = static_cast<std::int32_t>(expiration_tick - now_tick);
  return remaining > 0 ? static_cast<std::uint32_t>(remaining) : 0;
}

void CGItem_C::SetEnchantTimeRemainingSeconds(const std::uint8_t slot,
                                              const std::int32_t duration_seconds) {
  if (slot >= kMaxEnchantSlots) {
    return;
  }

  if (duration_seconds <= 0) {
    enchant_expiration_tick_ms_[slot] = 0;
    return;
  }

  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  enchant_expiration_tick_ms_[slot] =
      now_tick + static_cast<std::uint32_t>(duration_seconds) * 1000u;
}

std::uint32_t CGItem_C::GetEnchantIdIfVisible(const std::uint8_t slot) const {
  if (!IsItemVisible()) {
    return 0;
  }
  return GetEnchantId(slot);
}

std::int16_t CGItem_C::GetEnchantChargesIfVisible(const std::uint8_t slot) const {
  if (!IsItemVisible()) {
    return 0;
  }
  const auto charges = GetEnchantment(slot).charges;
  return static_cast<std::int16_t>(static_cast<std::uint16_t>(charges));
}

std::uint32_t CGItem_C::GetEnchantDurationFieldIfVisible(const std::uint8_t slot) const {
  if (!IsItemVisible()) {
    return 0;
  }
  return GetEnchantment(slot).duration;
}

std::uint32_t CGItem_C::GetPropertySeed() const {
  return GetUInt32(ITEM_FIELD_PROPERTY_SEED);
}

std::int32_t CGItem_C::GetRandomPropertiesId() const {
  auto raw = GetUInt32(ITEM_FIELD_RANDOM_PROPERTIES_ID);
  std::int32_t result;
  std::memcpy(&result, &raw, sizeof(result));
  return result;
}

std::uint32_t CGItem_C::GetRandomPropertyID() const {
  return GetUInt32(ITEM_FIELD_RANDOM_PROPERTIES_ID);
}

std::uint32_t CGItem_C::GetItemSuffixFactor() const {
  return GetUInt32(ITEM_FIELD_PROPERTY_SEED);
}

std::uint32_t CGItem_C::GetDurability() const {
  if (IsPendingRemoval()) {
    return 0;
  }
  return GetUInt32(ITEM_FIELD_DURABILITY);
}

std::uint32_t CGItem_C::GetMaxDurability() const {
  if (IsPendingRemoval()) {
    return 0;
  }
  return GetUInt32(ITEM_FIELD_MAXDURABILITY);
}

float CGItem_C::GetDurabilityPercent() const {
  auto max = GetMaxDurability();
  if (max == 0)
    return 0.0f;
  return static_cast<float>(GetDurability()) / static_cast<float>(max);
}

bool CGItem_C::IsBroken() const {
  return GetDurability() == 0 && GetMaxDurability() > 0;
}

std::uint32_t CGItem_C::GetRepairCost() const {
  const auto *item_template = item_definitions_.GetItem(GetEntry());
  if (item_template == nullptr) {
    return 0;
  }

  if (IsPendingRemoval()) {
    return 0;
  }

  const auto max_durability = GetMaxDurability();
  if (max_durability == 0) {
    return 0;
  }

  const auto durability = GetDurability();
  if (durability >= max_durability) {
    return 0;
  }

  std::size_t cost_index;
  if (item_template->item_class == ItemClass::Weapon) {
    if (item_template->subclass > 21u) {
      return 0;
    }
    cost_index = item_template->subclass;
  } else if (item_template->item_class == ItemClass::Armor) {
    if (item_template->subclass > 8u) {
      return 0;
    }
    cost_index = 21u + item_template->subclass;
  } else {
    return 0;
  }

  const auto *objects = object_manager();
  if (objects == nullptr) {
    return 0;
  }
  const auto *dbc = &objects->dbc_loader();

  const auto *cost_entry =
      dbc->durability_costs().LookupEntry(item_template->item_level);
  if (cost_entry == nullptr) {
    return 0;
  }

  const auto *quality_entry = dbc->durability_quality().LookupEntry(
      static_cast<std::uint32_t>(item_template->quality) * 2u + 1u);
  if (quality_entry == nullptr) {
    return 0;
  }

  const auto missing = max_durability - durability;
  const auto base = static_cast<float>(missing) * quality_entry->quality_mod *
                    static_cast<float>(cost_entry->cost[cost_index]);

  const auto rounded =
      static_cast<std::int32_t>(base >= 0.0f ? base + 0.5f : base - 0.5f);

  return rounded == 0 ? 1u : static_cast<std::uint32_t>(rounded);
}

std::uint32_t CGItem_C::GetCreatePlayedTime() const {
  return GetUInt32(ITEM_FIELD_CREATE_PLAYED_TIME);
}

std::vector<std::uint16_t> CGItem_C::ApplyCreateUpdate(const CreateObjectUpdate &upd) {
  server_spell_charge_overrides_.fill(0u);
  server_spell_charge_override_mask_ = 0;
  auto updated_fields = CGObject_C::ApplyCreateUpdate(upd);
  RefreshAllEnchantExpirationTicks();
  ApplyPendingEnchantTimeUpdates();
  return updated_fields;
}

std::vector<std::uint16_t> CGItem_C::ApplyValuesUpdate(const ValuesUpdate &upd) {
  auto updated_fields = CGObject_C::ApplyValuesUpdate(upd);
  RefreshEnchantExpirationTicks(updated_fields);
  ApplyPendingEnchantTimeUpdates();
  return updated_fields;
}

void CGItem_C::FinalizeWorldPublication() {

}

float CGItem_C::GetItemFacing() const {
  return GetOrientation();
}

bool CGItem_C::IsItemVisible() const {
  return (GetItemFlags() & 0x2000) == 0;
}

float CGItem_C::GetBoundsRadius() const {
  const float display_scale =
      DisplayInfoResolver::Get().ResolveModelScale(GetItemModelDisplayId());
  return display_scale * GetScale();
}

std::int32_t CGItem_C::GetDefaultInventoryType() {
  return -1;
}

std::uint32_t CGItem_C::GetItemClassFromClientDbc() const {
  const auto* const objects = object_manager();
  const auto* const entry =
      objects != nullptr ? objects->dbc_loader().item().LookupEntry(GetEntry())
                         : nullptr;
  return entry != nullptr ? entry->class_id : 0u;
}

std::uint32_t CGItem_C::GetItemSubClassFromClientDbc() const {
  const auto* const objects = object_manager();
  const auto* const entry =
      objects != nullptr ? objects->dbc_loader().item().LookupEntry(GetEntry())
                         : nullptr;
  return entry != nullptr ? entry->subclass_id : 0u;
}

std::uint32_t CGItem_C::GetInventoryTypeFromClientDbc() const {
  const auto* const objects = object_manager();
  const auto* const entry =
      objects != nullptr ? objects->dbc_loader().item().LookupEntry(GetEntry())
                         : nullptr;
  return entry != nullptr ? entry->inventory_type : 0u;
}

const ItemTemplate *CGItem_C::GetItemTemplate() const {
  const auto *objects = object_manager();
  if (objects == nullptr) {
    return nullptr;
  }

  const auto entry = GetEntry();
  if (entry == 0u) {
    return nullptr;
  }

  return objects->query_cache().GetItemTemplate(entry);
}

std::uint32_t CGItem_C::GetCachedQueryFlags() const {
  if (const auto *item_template = GetItemTemplate(); item_template != nullptr) {
    return item_template->flags;
  }

  return 0;
}

std::uint32_t CGItem_C::GetRequiredLevelFromTemplate() const {
  if (const auto *item_template = GetItemTemplate(); item_template != nullptr) {
    return item_template->required_level;
  }

  return 0;
}

const ItemTemplate *CGItem_C::GetOrRequestQueryItemTemplate() const {
  auto *objects = object_manager();
  if (objects == nullptr) {
    return nullptr;
  }

  const auto entry = GetEntry();
  if (entry == 0u) {
    return nullptr;
  }

  return objects->query_cache().GetOrRequestItemTemplate(entry);
}

std::int32_t CGItem_C::GetRandomSuffixInventoryBucket() const {
  switch (static_cast<InventoryType>(GetInventoryTypeFromClientDbc())) {
  case InventoryType::Head:
  case InventoryType::Body:
  case InventoryType::Chest:
  case InventoryType::Legs:
  case InventoryType::TwoHand:
  case InventoryType::Robe:
    return 0;
  case InventoryType::Shoulders:
  case InventoryType::Waist:
  case InventoryType::Feet:
  case InventoryType::Hands:
  case InventoryType::Trinket:
    return 1;
  case InventoryType::Neck:
  case InventoryType::Wrists:
  case InventoryType::Finger:
  case InventoryType::Shield:
  case InventoryType::Cloak:
  case InventoryType::Holdable:
    return 2;
  case InventoryType::Weapon:
  case InventoryType::MainHand:
  case InventoryType::OffHand:
    return 3;
  case InventoryType::Ranged:
  case InventoryType::Thrown:
  case InventoryType::RangedRight:
    return 4;
  default:
    return -1;
  }
}

std::uint32_t CGItem_C::GetItemModelDisplayId() const {
  const auto* const objects = object_manager();
  const auto* const entry =
      objects != nullptr ? objects->dbc_loader().item().LookupEntry(GetEntry())
                         : nullptr;
  return entry != nullptr ? entry->display_info_id : 0u;
}

std::int32_t CGItem_C::GetEnchantVisualAuraId() const {
  const auto *objects = object_manager();
  const auto *dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return 0;
  }

  const auto display_id = GetItemModelDisplayId();

  const auto *display_info =
      dbc->item_display_info().LookupEntry(display_id);
  if (display_info == nullptr) {
    return 0;
  }

  if (display_info->item_visuals_id != 0) {
    const auto *visuals =
        dbc->item_visuals().LookupEntry(display_info->item_visuals_id);
    if (visuals != nullptr) {
      return -1;
    }
  }

  for (std::uint8_t slot = 0; slot < kMaxEnchantSlots; ++slot) {
    const auto enchant_id = GetEnchantId(slot);
    if (enchant_id == 0u) {
      continue;
    }

    const auto *entry =
        dbc->spell_item_enchantment().LookupEntry(enchant_id);
    if (entry != nullptr && entry->aura_id != 0) {
      return static_cast<std::int32_t>(entry->aura_id);
    }
  }

  return 0;
}

std::uint32_t CGItem_C::FindCooldownCategory7EnchantmentId() const {
  const auto *objects = object_manager();
  const auto *dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return 0;
  }

  for (std::uint8_t slot = 0; slot < kMaxEnchantSlots; ++slot) {
    const auto enchant_id = GetEnchantId(slot);
    if (enchant_id == 0u) {
      continue;
    }

    const auto *entry = dbc->spell_item_enchantment().LookupEntry(enchant_id);
    if (entry == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < entry->type.size(); ++effect_index) {
      if (entry->type[effect_index] == kCooldownCategoryEnchantEffectType &&
          entry->spell_id[effect_index] != 0u) {
        return enchant_id;
      }
    }
  }

  return 0;
}

bool CGItem_C::HasUseSpellEnchantment() const {
  return FindCooldownCategory7EnchantmentId() != 0u;
}

bool CGItem_C::HasUsableSpell() const {
  if ((GetCachedQueryFlags() & kUsableSpellItemFlag) != 0u) {
    return true;
  }

  return FindCooldownCategory7EnchantmentId() != 0u;
}

std::uint32_t CGItem_C::ResolveUseSpellId() const {
  const auto *item_template = item_definitions_.GetItem(GetEntry());
  if (item_template != nullptr) {
    if (const auto *spell = FindFirstOnUseSpell(*item_template); spell != nullptr) {
      return spell->spell_id;
    }
  }

  const auto enchant_id = FindCooldownCategory7EnchantmentId();
  if (enchant_id == 0u) {
    return 0;
  }

  const auto *objects = object_manager();
  const auto *dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return 0;
  }

  const auto *entry = dbc->spell_item_enchantment().LookupEntry(enchant_id);
  if (entry == nullptr) {
    return 0;
  }

  for (std::size_t i = 0; i < entry->type.size(); ++i) {
    if (entry->type[i] == kCooldownCategoryEnchantEffectType &&
        entry->spell_id[i] != 0u) {
      return entry->spell_id[i];
    }
  }

  return 0;
}

bool CGItem_C::HasResolvedUseSpell() const {
  return ResolveUseSpellId() != 0u;
}

std::int32_t CGItem_C::GetUseSpellCharges() const {
  const auto *objects = object_manager();
  const auto *dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;

  const auto enchant_id = FindCooldownCategory7EnchantmentId();
  if (enchant_id != 0u && dbc != nullptr) {
    const auto *entry = dbc->spell_item_enchantment().LookupEntry(enchant_id);
    if (entry != nullptr) {
      for (std::size_t effect_idx = 0; effect_idx < entry->type.size();
           ++effect_idx) {
        if (entry->type[effect_idx] == kCooldownCategoryEnchantEffectType &&
            entry->spell_id[effect_idx] != 0u) {

          if ((GetCachedQueryFlags() & kQuestItemFlag) != 0u) {
            return 0;
          }

          const auto raw_charges =
              GetEnchantment(static_cast<std::uint8_t>(effect_idx)).charges;
          return static_cast<std::int32_t>(
              static_cast<std::int16_t>(raw_charges));
        }
      }
    }
  }

  const auto *item_template = item_definitions_.GetItem(GetEntry());
  if (item_template == nullptr) {
    return 0;
  }

  const auto spell_index = FindFirstOnUseSpellIndex(*item_template);
  if (spell_index < 0) {
    return 0;
  }

  return GetSpellCharges(static_cast<std::uint8_t>(spell_index));
}

void CGItem_C::RequestRefundInfo() const {

}

CGContainer_C::CGContainer_C(ItemDefinitions& item_definitions)
    : CGItem_C(item_definitions, TypeID::kContainer) {}

CGContainer_C::CGContainer_C(ItemDefinitions& item_definitions, ObjectGuid guid)
    : CGItem_C(item_definitions, guid, TypeID::kContainer) {}

CGContainer_C::CGContainer_C(ObjectManager& objects,
                              ItemDefinitions& item_definitions,
                              ObjectGuid guid)
    : CGItem_C(objects, item_definitions, guid, TypeID::kContainer) {}

std::uint32_t CGContainer_C::GetNumSlots() const {
  return GetUInt32(CONTAINER_FIELD_NUM_SLOTS);
}

ObjectGuid CGContainer_C::GetSlot(std::uint8_t index) const {
  if (index >= 36)
    return ObjectGuid();
  return GetGuidField(static_cast<std::uint16_t>(CONTAINER_FIELD_SLOT_1 + index * 2));
}

std::uint32_t CGContainer_C::GetNumFreeSlots() const {
  std::uint32_t num_slots = GetNumSlots();
  std::uint32_t occupied = 0;
  for (std::uint32_t i = 0; i < num_slots; ++i) {
    if (GetSlot(static_cast<std::uint8_t>(i)).IsValid()) {
      ++occupied;
    }
  }
  return num_slots - occupied;
}

CGObject_C *CGItem_C::FindObjectByGUID(const std::uint64_t guid) const {
  auto *objects = object_manager();
  return objects != nullptr
             ? static_cast<CGObject_C *>(
                   objects->GetMutable(ObjectGuid(guid)))
             : nullptr;
}

std::uint32_t CGItem_C::GetExtraSocketCount() const {
  if ((GetItemFlags() & kQuestItemFlag) != 0u) {
    return 0;
  }

  const auto enchant_id = GetEnchantId(kEnchantSlotPrismatic);
  if (enchant_id == 0u) {
    return 0;
  }

  const auto *objects = object_manager();
  const auto *dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    return 0;
  }

  const auto *entry = dbc->spell_item_enchantment().LookupEntry(enchant_id);
  if (entry == nullptr) {
    return 0;
  }

  for (std::size_t i = 0; i < entry->type.size(); ++i) {
    if (entry->type[i] == kPrismaticSocketEnchantEffectType) {
      return static_cast<std::uint32_t>(entry->amount[i]);
    }
  }

  return 0;
}

std::uint32_t CGItem_C::GetSocketCountForSocketUI() const {
  const auto *tmpl = GetOrRequestQueryItemTemplate();
  std::uint32_t count = 0;
  if (tmpl != nullptr) {
    for (const auto &sock : tmpl->sockets) {
      if (sock.color != 0) {
        ++count;
      }
    }
  }
  return count + GetExtraSocketCount();
}

void CGItem_C::RefreshAllEnchantExpirationTicks() {
  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  for (std::uint8_t slot = 0; slot < kMaxEnchantSlots; ++slot) {
    enchant_expiration_tick_ms_[slot] =
        ComputeEnchantExpirationTickMs(GetEnchantment(slot), now_tick);
  }
}

void CGItem_C::RefreshEnchantExpirationTicks(const std::vector<std::uint16_t> &updated_fields) {
  if (updated_fields.empty()) {
    return;
  }

  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  for (const auto field_index : updated_fields) {
    if (!IsTrackedEnchantField(field_index)) {
      continue;
    }

    const auto slot = EnchantSlotFromField(field_index);
    enchant_expiration_tick_ms_[slot] =
        ComputeEnchantExpirationTickMs(GetEnchantment(slot), now_tick);
  }
}

void CGItem_C::ApplyPendingEnchantTimeUpdates() {
  const auto owner_guid = GetOwner();
  if (owner_guid.IsEmpty()) {
    return;
  }

  auto *objects = object_manager();
  if (objects == nullptr) {
    return;
  }
  if (auto *owner = objects->GetMutablePlayer(owner_guid); owner != nullptr) {
    owner->ApplyPendingItemEnchantTimeUpdates(*this);
  }
}

}

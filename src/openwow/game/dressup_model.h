
#pragma once

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/inventory/items/item_definitions.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace openwow::game {

static constexpr int kDressUpSlotMainhand    = 15;
static constexpr int kDressUpSlotOffhand     = 16;

int DressUpModel_GetSlotForInventoryType(uint32_t inventoryType);

int DressUpModelSlotToVisibleItemSlot(int modelSlot);

int DressUpModel_GetNextWeaponSlot();
void DressUpModel_ResetWeaponSlotCycle();

void DressUpModel_SetNextWeaponSlot(int slot);

struct DressUpWeaponCompatibilityInputs {
  std::uint8_t mainhand_inventory_type{0};
  std::uint8_t offhand_inventory_type{0};
  std::uint32_t pending_mainhand_item_id{0};
  std::uint32_t pending_offhand_item_id{0};
  bool uncaught_exception_active{false};
};

using DressUpCanEquipItemInSlotFn =
    std::function<bool(std::uint32_t item_id, std::uint32_t slot_id)>;

bool DressUpModel_AreWeaponOverridesCompatible(
    const DressUpWeaponCompatibilityInputs& inputs,
    const DressUpCanEquipItemInSlotFn& can_equip_item_in_slot);

struct DressUpParsedItemLink {
  std::uint32_t item_id{0};
  std::uint32_t enchant_id{0};
  std::array<std::uint32_t, 3> gem_enchant_ids{};
  std::int32_t extra_enchant_id{0};
};

bool DressUpModel_ParseItemLink(std::string_view link,
                                DressUpParsedItemLink& out);

std::uint32_t DressUpModel_ResolveItemLinkAuraId(
    const ItemTemplate& item,
    const DressUpParsedItemLink& link_fields,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>*
        item_display_info,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemVisualsEntry>*
        item_visuals,
    const openwow::data::dbc::DbcStore<
        openwow::data::dbc::SpellItemEnchantmentEntry>*
        spell_item_enchantments);

std::optional<std::uint32_t> DressUpModel_ResolveEnchantLinkItemId(
    std::uint32_t spell_id,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellEntry>* spells);

bool DressUpModel_IsWeaponInventoryType(std::uint32_t inventory_type);
int DressUpModel_GetPreviewSlotForInventoryType(std::uint32_t inventory_type);
int DressUpModel_SelectWeaponPreviewSlot(
    std::uint32_t inventory_type,
    bool has_mainhand_override,
    std::uint8_t mainhand_inventory_type,
    bool can_equip_in_offhand);

static constexpr std::uint32_t kSpellEffectLearnSpell = 36;
static constexpr std::uint32_t kSpellTriggerLearnSpellId = 6;

struct DressUpSpellEffectInfo {
  std::uint32_t effect_0{0};
  std::uint32_t effect_trigger_spell_0{0};
  std::uint32_t effect_item_type_0{0};
};

using DressUpSpellLookupFn =
    std::function<std::optional<DressUpSpellEffectInfo>(std::uint32_t spell_id)>;

[[nodiscard]] std::optional<std::uint32_t> DressUpModel_DetectEnchantScroll(
    const ItemTemplate& item,
    const DressUpSpellLookupFn& lookup_spell);

struct DressUpWeaponOverrideSlot {
  std::uint8_t item_class{0};
  std::uint8_t subclass{0};
  std::uint8_t inventory_type{0};
  std::uint8_t sheath{0};

  [[nodiscard]] bool IsOccupied() const { return item_class != 0; }

  void Clear() {
    item_class = 0;
    subclass = 0;
    inventory_type = 0;
    sheath = 0;
  }

  void SetFromItem(const ItemTemplate& item) {
    item_class = static_cast<std::uint8_t>(item.item_class);
    subclass = static_cast<std::uint8_t>(item.subclass);
    inventory_type = static_cast<std::uint8_t>(item.inventory_type);
    sheath = static_cast<std::uint8_t>(item.sheath);
  }
};

struct DressUpTryOnItemInput {
  std::uint32_t item_id{0};
  std::int32_t enchant_id{0};
  std::int32_t force_slot{-1};

  bool has_backing_model{false};
  bool has_character_model{false};
  std::uint64_t bound_guid{0};

  DressUpWeaponOverrideSlot mainhand_override{};
  DressUpWeaponOverrideSlot offhand_override{};
};

struct DressUpTryOnItemOutput {

  DressUpWeaponOverrideSlot mainhand_override{};
  DressUpWeaponOverrideSlot offhand_override{};

  std::uint32_t pending_mainhand_item_id{0};
  std::uint32_t pending_offhand_item_id{0};

  int next_weapon_slot{kDressUpSlotMainhand};

  bool did_equip{false};
};

struct DressUpTryOnCallbacks {

  std::function<const ItemTemplate*(std::uint32_t item_id)> lookup_item;

  DressUpSpellLookupFn lookup_spell;

  std::function<void(std::uint32_t target_item_id, std::int32_t enchant_id,
                     std::int32_t force_slot)>
      resolve_and_dispatch;

  std::function<void(int slot, std::uint32_t display_id,
                     std::int32_t enchant_id)>
      equip_armor_slot;

  std::function<void(int slot, std::uint8_t sheath, bool is_shield)>
      clear_weapon_slot;

  struct WeaponDispatchParams {
    int slot{0};
    std::uint32_t display_id{0};
    std::uint32_t sheath{0};
    std::int32_t enchant_id{0};
    bool is_shield{false};
    bool is_ranged_right{false};
  };
  std::function<void(const WeaponDispatchParams& params)> dispatch_weapon;

  std::function<std::uint32_t(std::uint32_t display_id)> get_display_info_flags;

  std::function<void(std::uint64_t bound_guid)> apply_guild_tabard;

  std::function<bool(std::uint64_t bound_guid)> is_active_player;

  std::function<bool()> can_dual_wield;

  std::function<bool(std::uint32_t item_id, std::uint32_t slot)>
      can_equip_in_slot;

  std::function<void(int message_id)> display_system_message;
};

static constexpr std::uint32_t kItemDisplayInfoFlagGuildTabard = 1u;

static constexpr int kSystemMsgNotEquippable = 21;

[[nodiscard]] std::optional<DressUpTryOnItemOutput> DressUpModel_TryOnItemTyped(
    const DressUpTryOnItemInput& input,
    const DressUpTryOnCallbacks& callbacks);

using DressUpResolveTryOnFn =
    std::function<void(std::uint32_t modelThis, std::uint32_t itemEntryId,
                       std::int32_t enchantId, std::int32_t forceSlot)>;

bool DressUpModel_OnItemTemplateCacheLoaded(
    std::uint32_t itemEntryId,
    std::uint32_t modelThis,
    bool success,
    const DressUpResolveTryOnFn& resolve_and_try_on);

struct DressUpVisibleItemEntry {
  std::int32_t item_entry_id{0};
};

struct DressUpNpcDisplayRecord {
  std::uint8_t race{0};
  std::uint8_t gender{0};
  std::uint8_t skin_color{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
  std::uint8_t extra_hair_style{0};
  const char* baked_texture_name{nullptr};
  std::array<std::uint32_t, 11> item_display_ids{};
};

struct DressUpPreviewModelId {
  std::uint32_t value{0};

  [[nodiscard]] bool IsValid() const noexcept {
    return value != 0u;
  }

  explicit operator bool() const noexcept {
    return IsValid();
  }

  bool operator==(const DressUpPreviewModelId&) const = default;
};

struct DressUpRebuildPreviewInput {

  std::uintptr_t frame_this{0};

  std::uintptr_t unit_ptr{0};

  std::uint64_t bound_guid{0};

  std::uint32_t pet_model_id{0};

  DressUpPreviewModelId existing_preview_model{};

  std::uintptr_t resolved_unit{0};

  bool is_active_player{false};
};

struct DressUpUnitAppearance {
  std::uint8_t race{0};
  std::uint8_t gender{0};
  std::uint8_t skin_color{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
  std::uint8_t extra_hair_style{0};
};

struct DressUpRebuildCallbacks {

  std::function<void(std::uintptr_t frame, std::uintptr_t unit)>
      bind_unit_model;

  std::function<void(DressUpPreviewModelId preview_model)> release_preview_model;

  std::function<DressUpPreviewModelId()> allocate_preview_model;

  std::function<void(DressUpPreviewModelId preview_model,
                     const DressUpUnitAppearance& appearance,
                     bool is_self)>
      init_preview_model;

  std::function<DressUpVisibleItemEntry(std::uintptr_t unit,
                                        std::uint32_t slot)>
      get_visible_item;

  std::function<std::int32_t(const DressUpVisibleItemEntry& entry)>
      resolve_aura_visual;

  std::function<void(std::uint32_t item_id, std::int32_t aura_id,
                     std::int32_t force_slot)>
      try_on_item;

  std::function<std::pair<std::uint32_t, std::uint32_t>(
      std::uintptr_t unit, int slot)>
      get_weapon_override;

  std::function<std::uint32_t(std::uintptr_t unit)> get_unit_flags;

  std::function<bool(std::uintptr_t unit)> is_sheathed;

  std::function<const DressUpNpcDisplayRecord*(std::uint32_t display_id)>
      lookup_npc_display;

  std::function<void(std::uint32_t slot, std::uint32_t display_id,
                     std::uint32_t flags)>
      set_item_display;
};

struct DressUpRebuildPreviewOutput {

  DressUpPreviewModelId preview_model{};

  std::uint32_t mainhand_override_lo{0};
  std::uint32_t mainhand_override_hi{0};

  std::uint32_t offhand_override_lo{0};
  std::uint32_t offhand_override_hi{0};

  bool sheathed{false};
};

DressUpRebuildPreviewOutput DressUpModelFrame_RebuildPreviewFromUnit(
    const DressUpRebuildPreviewInput& input,
    const DressUpUnitAppearance& appearance,
    const DressUpRebuildCallbacks& callbacks);

struct SceneRenderVisibilityFlags {
  std::uint32_t flags{0};
  bool is_attached{false};
};

static constexpr std::uint32_t kRenderFlagStandalonePrimary   = 0x8u;
static constexpr std::uint32_t kRenderFlagAttachedPrimary     = 0x80u;
static constexpr std::uint32_t kRenderFlagStandaloneSecondary = 0x10000u;
static constexpr std::uint32_t kRenderFlagAttachedSecondary   = 0x20000u;

void ApplyVisualReadinessToRenderFlags(SceneRenderVisibilityFlags& state,
                                       bool visuals_ready) noexcept;

struct DressUpUpdateModelMotionInput {

  bool has_preview_model{false};

  bool visuals_ready{false};

  SceneRenderVisibilityFlags render_state{};
};

struct DressUpUpdateModelMotionOutput {

  std::uint32_t updated_flags{0};
};

[[nodiscard]] std::optional<DressUpUpdateModelMotionOutput>
DressUpModelFrame_UpdateModelMotionRenderFlags(
    const DressUpUpdateModelMotionInput& input) noexcept;

static constexpr int kDressUpMaxEquipSlots   = 12;
static constexpr int kDressUpWeaponSlotCount = 2;

static constexpr int kSpellEffectEnchantItem = 24;

static constexpr uint32_t kNonPreviewableTypes[] = {
    2,
    11,
    12,
    28,
};

}

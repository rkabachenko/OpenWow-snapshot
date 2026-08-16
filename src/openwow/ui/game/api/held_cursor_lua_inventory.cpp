
#include "openwow/ui/game/api/held_cursor_lua_api.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/macros/adapters/ui/macro_cursor_controller.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/commerce/adapters/ui/money_cursor_controller.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/equipment/item_equip_check.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/localization.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/inventory/operations/inventory_commands.h"
#include "openwow/game/pet_action_slot_setter.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/targeting/adapters/ui/target_interaction_controller.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/trade_cursor_utils.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

namespace openwow::ui::game::detail {

static int HandleResolvedInventorySlotPickup(lua_State* L, int slot);

namespace {

using openwow::game::ApplyPetActionSlotMutation;
using openwow::game::GetPetActionType;
using openwow::game::kPetActionTypeCommand;
using openwow::game::kPetActionTypeReact;
using openwow::game::kPetActionTypeSpell;

bool InventoryCursorTargetAcceptsItem(lua_State* L,
                                      const openwow::game::WorldSession& session,
                                      int slot);

std::uint64_t GetSelectablePetActionGuid(const openwow::game::WorldSession &session) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return 0;
  }

  const auto pet_guid = player->State().GetPetGUID();
  if (pet_guid.IsEmpty()) {
    return 0;
  }

  const auto *pet_unit = session.objects().GetUnit(pet_guid);
  if (pet_unit == nullptr) {
    return 0;
  }

  if (pet_unit->State().HasUnitFlag(
          static_cast<std::uint32_t>(openwow::game::UnitStateFlag::kPossessed))) {
    return 0;
  }

  return pet_guid.GetRawValue();
}

bool IsPassivePetActionSpell(lua_State *L, std::uint32_t raw_action) {
  if (GetPetActionType(raw_action) != kPetActionTypeSpell) {
    return false;
  }

  const auto spell_id = raw_action & 0x00FFFFFFu;
  if (spell_id == 0) {
    return false;
  }

  const auto *spell = cursor_texture::GetSpellEntry(L, spell_id);
  return spell != nullptr && (spell->attributes & 0x40u) != 0;
}

std::string ResolvePetActionCursorTexture(lua_State *L, std::uint32_t raw_action) {
  const auto action_type = GetPetActionType(raw_action);
  switch (action_type) {
  case 1: case 8: case 9: case 10: case 11: case 12:
  case 13: case 14: case 15: case 16: case 17:
    return cursor_texture::ResolveSpellTexturePath(L, raw_action & 0x00FFFFFFu);
  case kPetActionTypeReact:
    switch (raw_action & 0x00FFFFFFu) {
    case 0:
      return "PET_PASSIVE_TEXTURE";
    case 1:
      return "PET_DEFENSIVE_TEXTURE";
    case 2:
      return "PET_AGGRESSIVE_TEXTURE";
    default:
      return {};
    }
  case kPetActionTypeCommand:
    switch (raw_action & 0x00FFFFFFu) {
    case 0:
      return "PET_WAIT_TEXTURE";
    case 1:
      return "PET_FOLLOW_TEXTURE";
    case 2:
      return "PET_ATTACK_TEXTURE";
    case 3:
      return "PET_DISMISS_TEXTURE";
    default:
      return {};
    }
  default:
    return {};
  }
}

void SetHeldPetActionCursor(
    lua_State* L,
    openwow::game::actions::held_cursor::HeldCursor& cursor,
    const std::uint32_t raw_action, const std::uint32_t source_slot,
    const openwow::game::actions::held_cursor::Sound sound =
        openwow::game::actions::held_cursor::Sound::CursorGrabObject) {
  if (raw_action == 0) {
    return;
  }

  namespace held_cursor = openwow::game::actions::held_cursor;
  cursor.HoldPetAction(
      held_cursor::PetAction{
          .packed_action = raw_action,
          .source_slot = source_slot,
      },
      held_cursor::Presentation{
          .texture_path = ResolvePetActionCursorTexture(L, raw_action),
          .sound = sound,
          .grid = held_cursor::Grid::PetBar,
      });
}

void SetHeldSpellbookDropSound(
    openwow::game::actions::held_cursor::HeldCursor& cursor) {
  cursor.PlaySound(
      openwow::game::actions::held_cursor::Sound::SpellbookDrop);
}

void SetHeldSpellbookSpellCursor(
    lua_State* L,
    openwow::game::actions::held_cursor::HeldCursor& cursor,
    const std::uint32_t spell_id, const std::uint32_t spellbook_slot) {
  namespace held_cursor = openwow::game::actions::held_cursor;
  cursor.HoldSpell(
      held_cursor::Spell{
          .spell_id = spell_id,
          .spellbook_slot = spellbook_slot,
      },
      held_cursor::Presentation{
          .texture_path =
              cursor_texture::ResolveSpellTexturePath(L, spell_id),
          .sound = held_cursor::Sound::SpellbookPickup,
          .grid = held_cursor::Grid::ActionBar,
      });
}

std::optional<std::uint32_t> ResolveHeldPetAction(
    lua_State*,
    const openwow::game::actions::held_cursor::HeldCursor& cursor) {
  namespace held_cursor = openwow::game::actions::held_cursor;
  if (const auto* action = cursor.get_if<held_cursor::PetAction>()) {
    return action->packed_action;
  }

  if (const auto* spell = cursor.get_if<held_cursor::Spell>();
      spell != nullptr && spell->from_pet_spellbook && spell->spell_id != 0) {
    return (static_cast<std::uint32_t>(kPetActionTypeSpell) << 24) |
           spell->spell_id;
  }

  return std::nullopt;
}

std::uint32_t ParseRetailDecimalPrefix(const char* value) {
  if (value == nullptr) {
    return 0;
  }

  const bool negative = *value == '-';
  const char* cursor = value + (negative ? 1 : 0);
  std::uint32_t result = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    result = result * 10u + static_cast<std::uint32_t>(*cursor - '0');
    ++cursor;
  }
  return negative ? 0u - result : result;
}

std::uint32_t ResolvePickupItemArgument(lua_State* L) {
  if (lua_isnumber(L, 1) != 0) {
    return static_cast<std::uint32_t>(
        TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  }
  if (lua_isstring(L, 1) == 0) {
    return 0;
  }

  const char* value = lua_tostring(L, 1);
  if (value == nullptr) {
    return 0;
  }
  if (const char* payload = std::strstr(value, "item:");
      payload != nullptr) {
    return ParseRetailDecimalPrefix(payload + 5);
  }

  const auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  const auto* item = session->query_cache().GetItemTemplateByName(value);
  return item != nullptr ? item->entry : 0u;
}

std::uint32_t ResolvePickupItemDisplayId(
    const openwow::data::dbc::DbcLoader* dbc,
    const std::uint32_t item_id) {
  if (dbc == nullptr) {
    return 0;
  }
  const auto* item = dbc->item().LookupEntry(item_id);
  return item != nullptr ? item->display_info_id : 0u;
}

}

bool TryPlaceHeldPetActionOnBar(lua_State* L, openwow::game::WorldSession& session,
                                const std::uint64_t pet_guid,
                                const std::uint32_t slot_index) {
  auto* cursor_ptr = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor_ptr == nullptr) {
    return false;
  }
  auto& held_cursor = *cursor_ptr;
  const auto held_action = ResolveHeldPetAction(L, held_cursor);
  held_cursor.Clear();
  if (!held_action.has_value()) {
    return false;
  }

  if (IsPassivePetActionSpell(L, *held_action)) {
    return true;
  }

  if (pet_guid == 0) {
    return true;
  }

  if (slot_index >= openwow::game::kPetActionBarSlotCount ||
      !GameUI_CanPerformProtectedAction(protected_action_kind::kActionSlotMutation) ||
      !CanUsePetActions(session, false)) {
    return true;
  }

  auto& pet_bar = session.pet().mutable_pet_bar();
  std::uint32_t raw_slots[openwow::game::kPetActionBarSlotCount];
  for (std::size_t index = 0; index < openwow::game::kPetActionBarSlotCount; ++index) {
    raw_slots[index] = pet_bar.action_bar[index].raw;
  }

  const auto mutation = ApplyPetActionSlotMutation(raw_slots, slot_index, *held_action);
  if (!mutation.applied) {
    return true;
  }

  for (std::size_t index = 0; index < openwow::game::kPetActionBarSlotCount; ++index) {
    pet_bar.action_bar[index].raw = raw_slots[index];
  }

  std::optional<openwow::net::wotlk::PetSetActionSlotState> secondary_slot;
  if (mutation.secondary_slot >= 0) {
    secondary_slot = openwow::net::wotlk::PetSetActionSlotState{
        static_cast<std::uint32_t>(mutation.secondary_slot),
        raw_slots[static_cast<std::size_t>(mutation.secondary_slot)]};
  }

  session.interaction().SendPetSetAction(
      pet_guid, secondary_slot, {slot_index, raw_slots[slot_index]});
  ScriptEventDispatch::Get().FirePetBarUpdate();

  if (mutation.secondary_slot < 0) {
    SetHeldPetActionCursor(L, held_cursor, mutation.previous_target_action,
                           slot_index + 1u);
  }

  return true;
}

namespace {

bool IsInventoryBagSlot(const int slot) {
  return (slot >= 19 && slot <= 22) || (slot >= 67 && slot <= 73);
}

const openwow::game::BagInfo* FindHeldBagByGuid(
    const openwow::game::PlayerInventoryReplica& inventory,
    const std::uint64_t held_item_guid) {
  if (held_item_guid == 0) {
    return nullptr;
  }

  for (std::uint8_t bag_index = 1; bag_index <= openwow::game::PlayerInventoryReplica::kMaxBags;
       ++bag_index) {
    const auto* bag = inventory.GetBag(bag_index);
    if (bag != nullptr && bag->guid == held_item_guid) {
      return bag;
    }
  }

  for (std::uint8_t bag_index = 0; bag_index < openwow::game::PlayerInventoryReplica::kMaxBankBags;
       ++bag_index) {
    const auto* bag = inventory.GetBankBag(bag_index);
    if (bag != nullptr && bag->guid == held_item_guid) {
      return bag;
    }
  }

  return nullptr;
}

bool HeldBagContainsItems(const openwow::game::PlayerInventoryReplica& inventory,
                          const std::uint64_t held_item_guid) {
  const auto* held_bag = FindHeldBagByGuid(inventory, held_item_guid);
  if (held_bag == nullptr) {
    return false;
  }

  return std::any_of(
      held_bag->slots.begin(), held_bag->slots.end(),
      [](const openwow::game::ItemInstance& item) { return !item.IsEmpty(); });
}

void HandleResolvedSpellbookPickupOrDropImpl(lua_State* L,
                                             const std::uint32_t spell_id,
                                             const std::uint32_t spellbook_slot,
                                             const bool from_pet_book) {
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kActionSlotMutation)) {
    return;
  }

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return;
  }
  const auto* held_spell =
      cursor->get_if<::openwow::game::actions::held_cursor::Spell>();
  const auto held_spell_id =
      held_spell != nullptr ? held_spell->spell_id : 0u;
  cursor->Clear();

  if (held_spell_id == spell_id) {
    if (spell_id != 0) {
      SetHeldSpellbookDropSound(*cursor);
    }
    return;
  }

  if (static_cast<std::int32_t>(held_spell_id) > 0 || spell_id == 0) {
    return;
  }

  if (from_pet_book) {
    auto* session = GetWorldSession(L);
    if (session == nullptr) {
      return;
    }

    const auto* pet_spell_entry = session->pet().FindSpellEntryBySpellId(spell_id);
    if (pet_spell_entry == nullptr) {
      return;
    }

    SetHeldPetActionCursor(
        L, *cursor, pet_spell_entry->raw, spellbook_slot,
        ::openwow::game::actions::held_cursor::Sound::SpellbookPickup);
    return;
  }

  SetHeldSpellbookSpellCursor(L, *cursor, spell_id, spellbook_slot);
}

std::optional<openwow::game::ItemTemplate> ResolveHeldCursorItemTemplate(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto resolve_item_template =
      [session](const std::uint32_t entry) -> std::optional<openwow::game::ItemTemplate> {
    if (entry == 0) {
      return std::nullopt;
    }

    if (const auto* item_template = session->query_cache().GetItemTemplate(entry);
        item_template != nullptr) {
      return *item_template;
    }

    return std::nullopt;
  };

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return std::nullopt;
  }
  const auto* held_item = cursor->live_item();
  if (held_item == nullptr || held_item->item.entry == 0) {
    return std::nullopt;
  }
  return resolve_item_template(held_item->item.entry);
}

bool CursorItemCanGoInSlot(const openwow::game::ItemTemplate& item_template,
                           const int slot,
                           const openwow::game::CGPlayer_C* player,
                           const openwow::game::PlayerInventoryReplica& inventory,
                           const std::uint64_t held_item_guid,
                           const openwow::data::dbc::DbcLoader* dbc) {
  using InventoryType = openwow::game::InventoryType;

  const auto inventory_type = static_cast<InventoryType>(item_template.inventory_type);
  if (slot >= 0 &&
      (inventory_type == InventoryType::Bag || inventory_type == InventoryType::Quiver) &&
      !IsInventoryBagSlot(slot) && HeldBagContainsItems(inventory, held_item_guid)) {
    return false;
  }

  return CanItemOccupyInventorySlot(item_template, slot, player, dbc);
}

struct InventorySlotCursorSource {
  openwow::game::ItemInstance item;
  std::uint8_t cursor_source_bag = openwow::game::InventorySlots::kMainBag;
  std::uint8_t cursor_source_slot = 0;
};

std::optional<InventorySlotCursorSource> ResolveInventorySlotCursorSource(
    const openwow::game::PlayerInventoryReplica& inventory,
    const int slot,
    const bool bank_open) {
  namespace InventorySlots = openwow::game::InventorySlots;

  if (slot >= 0 && slot < openwow::game::PlayerInventoryReplica::kMaxEquipSlots) {
    if (const auto* item = inventory.GetEquipSlot(static_cast<std::uint8_t>(slot));
        item != nullptr && !item->IsEmpty()) {
      return InventorySlotCursorSource{
          .item = *item,
          .cursor_source_bag = InventorySlots::kMainBag,
          .cursor_source_slot = static_cast<std::uint8_t>(slot),
      };
    }
    return std::nullopt;
  }

  if (slot >= InventorySlots::kBagSlotsStart && slot < InventorySlots::kBagSlotsEnd) {
    const auto bag_index = static_cast<std::uint8_t>(slot - InventorySlots::kBagSlotsStart + 1);
    if (const auto* bag = inventory.GetBag(bag_index);
        bag != nullptr && bag->entry != 0 && bag->guid != 0) {
      openwow::game::ItemInstance bag_item;
      bag_item.guid = bag->guid;
      bag_item.entry = bag->entry;
      bag_item.count = 1;
      return InventorySlotCursorSource{
          .item = bag_item,
          .cursor_source_bag = InventorySlots::kMainBag,
          .cursor_source_slot = static_cast<std::uint8_t>(slot),
      };
    }
    return std::nullopt;
  }

  if (slot >= InventorySlots::kBackpackStart && slot < InventorySlots::kBackpackEnd) {
    if (const auto* item =
            inventory.GetBackpackSlot(static_cast<std::uint8_t>(slot - InventorySlots::kBackpackStart));
        item != nullptr && !item->IsEmpty()) {
      return InventorySlotCursorSource{
          .item = *item,
          .cursor_source_bag = InventorySlots::kMainBag,
          .cursor_source_slot = static_cast<std::uint8_t>(slot),
      };
    }
    return std::nullopt;
  }

  if (slot >= InventorySlots::kBankStart && slot < InventorySlots::kBankEnd) {
    if (!bank_open) {
      return std::nullopt;
    }
    if (const auto* item =
            inventory.GetBankSlot(static_cast<std::uint8_t>(slot - InventorySlots::kBankStart));
        item != nullptr && !item->IsEmpty()) {
      return InventorySlotCursorSource{
          .item = *item,
          .cursor_source_bag = InventorySlots::kMainBag,
          .cursor_source_slot = static_cast<std::uint8_t>(slot),
      };
    }
    return std::nullopt;
  }

  if (slot >= InventorySlots::kBankBagStart && slot < InventorySlots::kBankBagEnd) {
    if (!bank_open) {
      return std::nullopt;
    }
    if (const auto* bag =
            inventory.GetBankBag(static_cast<std::uint8_t>(slot - InventorySlots::kBankBagStart));
        bag != nullptr && bag->entry != 0 && bag->guid != 0) {
      openwow::game::ItemInstance bag_item;
      bag_item.guid = bag->guid;
      bag_item.entry = bag->entry;
      bag_item.count = 1;
      return InventorySlotCursorSource{
          .item = bag_item,
          .cursor_source_bag = InventorySlots::kMainBag,
          .cursor_source_slot = static_cast<std::uint8_t>(slot),
      };
    }
    return std::nullopt;
  }

  if (slot >= InventorySlots::kKeyringStart && slot < InventorySlots::kKeyringEnd) {
    if (const auto* item =
            inventory.GetKeyringSlot(static_cast<std::uint8_t>(slot - InventorySlots::kKeyringStart));
        item != nullptr && !item->IsEmpty()) {
      return InventorySlotCursorSource{
          .item = *item,
          .cursor_source_bag = InventorySlots::kMainBag,
          .cursor_source_slot = static_cast<std::uint8_t>(slot),
      };
    }
  }

  return std::nullopt;
}

void PickupInventorySlotCursorSource(lua_State* L,
                                     const std::uint64_t container_guid,
                                     const InventorySlotCursorSource& source) {
  auto* cursor = openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return;
  }

  cursor->Clear();
  namespace held_cursor = openwow::game::actions::held_cursor;
  cursor->HoldLiveItem(
      held_cursor::LiveItem{
          .item = source.item,
          .source_container_guid = container_guid,
          .source_bag = source.cursor_source_bag,
          .source_slot = source.cursor_source_slot,
      },
      held_cursor::Presentation{
          .texture_path =
              cursor_texture::ResolveItemTexturePath(L, source.item.entry),
          .texture_mode = held_cursor::TextureMode::HeldTexture,
          .sound = held_cursor::Sound::CursorGrabObject,

          .grid = ::openwow::game::inventory::ui::ResolveItemCursorGrid(
              RequireItemDefinitions(L), source.item.entry),
      });
}

}

bool ResolveHeldCursorServerCoords(
    const openwow::game::actions::held_cursor::HeldCursor& cursor,
                                   std::uint8_t* out_bag,
                                   std::uint8_t* out_slot) {
  if (out_bag == nullptr || out_slot == nullptr) {
    return false;
  }

  const auto* item = cursor.live_item();
  if (item == nullptr) {
    return false;
  }
  *out_bag = item->source_bag;
  *out_slot = item->source_slot;
  return true;
}

namespace {

bool ResolveHeldAbsoluteInventorySourceSlot(
    const openwow::game::actions::held_cursor::HeldCursor& cursor,
                                            int* out_slot) {
  const auto* item = cursor.live_item();
  if (out_slot == nullptr || item == nullptr ||
      item->source_bag != openwow::game::InventorySlots::kMainBag) {
    return false;
  }

  *out_slot = static_cast<int>(item->source_slot);
  return true;
}

std::optional<std::uint8_t> ResolveInventoryAutoStoreBagTarget(
    const openwow::game::PlayerInventoryReplica& inventory,
    const int slot,
    const bool bank_open) {
  namespace InventorySlots = openwow::game::InventorySlots;

  if (slot >= InventorySlots::kBagSlotsStart && slot < InventorySlots::kBagSlotsEnd) {
    const auto bag_index = static_cast<std::uint8_t>(slot - InventorySlots::kBagSlotsStart + 1);
    if (const auto* bag = inventory.GetBag(bag_index);
        bag != nullptr && bag->guid != 0 && bag->entry != 0) {
      return static_cast<std::uint8_t>(slot);
    }
    return std::nullopt;
  }

  if (!bank_open) {
    return std::nullopt;
  }

  if (slot >= InventorySlots::kBankBagStart && slot < InventorySlots::kBankBagEnd) {
    const auto bank_bag_index =
        static_cast<std::uint8_t>(slot - InventorySlots::kBankBagStart);
    if (const auto* bag = inventory.GetBankBag(bank_bag_index);
        bag != nullptr && bag->guid != 0 && bag->entry != 0) {
      return static_cast<std::uint8_t>(slot);
    }
  }

  return std::nullopt;
}

bool HeldCursorUsesInventoryBagSwapPath(
    const openwow::game::actions::held_cursor::HeldCursor& cursor,
                                        const std::uint64_t player_guid) {
  int held_inventory_slot = -1;
  const auto* item = cursor.live_item();
  return item != nullptr && item->source_container_guid == player_guid &&
         ResolveHeldAbsoluteInventorySourceSlot(cursor, &held_inventory_slot) &&
         held_inventory_slot >= openwow::game::InventorySlots::kBagSlotsStart &&
         held_inventory_slot < openwow::game::InventorySlots::kBagSlotsEnd;
}

bool TryAutoStoreHeldCursorIntoInventoryBag(
    lua_State* L,
    openwow::game::WorldSession& session,
    openwow::game::actions::held_cursor::HeldCursor& cursor,
    const openwow::game::PlayerInventoryReplica& inventory,
    const int slot,
    const bool bank_open,
    const std::uint64_t player_guid) {
  const auto target_bag = ResolveInventoryAutoStoreBagTarget(inventory, slot, bank_open);
  if (!target_bag.has_value()) {
    return false;
  }

  if (HeldCursorUsesInventoryBagSwapPath(cursor, player_guid) ||
      InventoryCursorTargetAcceptsItem(L, session, slot)) {
    return false;
  }

  std::uint8_t src_bag = 0;
  std::uint8_t src_slot = 0;
  if (!ResolveHeldCursorServerCoords(cursor, &src_bag, &src_slot)) {
    return false;
  }

  const auto* item = cursor.live_item();
  const auto split_count = item != nullptr ? item->auxiliary_value : 0u;
  if (split_count != 0) {
    session.interaction().SendSplitItem(src_bag, src_slot, *target_bag, 0xFF, split_count);
  } else {
    session.interaction().SendAutoStoreBagItem(src_bag, src_slot, *target_bag);
  }

  cursor.Clear({
      .release_source_lease = false,
      .publish_money_owner_update = true,
  });
  return true;
}

bool InventoryCursorTargetAcceptsItem(lua_State* L,
                                      const openwow::game::WorldSession& session,
                                      const int slot) {
  if (slot < 0) {
    return false;
  }

  const auto item_template = ResolveHeldCursorItemTemplate(L);
  if (!item_template.has_value()) {
    return true;
  }

  const auto* player = session.objects().GetLocalPlayerTyped();
  const auto* cursor = openwow::ui::game::lua::FindHeldCursor(*L);
  return CursorItemCanGoInSlot(
      *item_template, slot, player, RequirePlayerInventoryReplica(L),
      cursor != nullptr && cursor->live_item() != nullptr
          ? cursor->live_item()->item.guid
          : 0u,
      GetDbcLoader(L));
}

void SetAmmoCursorState(lua_State* L,
                        const std::uint32_t ammo_item_id,
                        const std::uint32_t ammo_count) {
  auto* cursor = openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return;
  }
  cursor->Clear();
  namespace held_cursor = openwow::game::actions::held_cursor;
  cursor->HoldAmmoItem(
      held_cursor::AmmoItem{
          .item_entry = ammo_item_id,
          .count = std::max(ammo_count, 1u),
      },
      held_cursor::Presentation{
          .texture_path =
              cursor_texture::ResolveItemTexturePath(L, ammo_item_id),
          .texture_mode = held_cursor::TextureMode::HeldTexture,
          .sound = held_cursor::Sound::CursorGrabObject,
      });
}

std::string ResolveMerchantCursorTexturePath(lua_State* L,
                                             const openwow::game::VendorItem& item) {
  if (auto texture = cursor_texture::ResolveItemTexturePath(L, item.item_id);
      texture.empty() == false) {
    return texture;
  }

  const auto* dbc = cursor_texture::GetDbcLoader(L);
  if (dbc == nullptr || item.display_info_id == 0) {
    return {};
  }

  return ::openwow::game::ResolveItemInventoryIconTexturePath(
      dbc, item.display_info_id);
}

bool TrySellHeldCursorItemToMerchant(openwow::game::WorldSession& session,
    ::openwow::game::actions::held_cursor::HeldCursor& cursor) {
  const auto* held = cursor.live_item();
  if (held == nullptr || held->item.guid == 0) {
    return false;
  }

  const auto held_item_guid = held->item.guid;
  const auto split_count = held->auxiliary_value;
  if (const auto* held_item = session.objects().GetItem(
          openwow::game::ObjectGuid(held_item_guid));
      held_item != nullptr && split_count != 0 && held_item->GetStackCount() != split_count) {
    return true;
  }

  std::uint64_t vendor_guid = 0;
  if (const auto* vendor = GetActiveVendorList(session); vendor != nullptr) {
    vendor_guid = vendor->vendor_guid.GetRawValue();
  }

  session.interaction().SendSellItem(vendor_guid, held_item_guid, split_count);
  cursor.Clear({
      .release_source_lease = false,
      .publish_money_owner_update = true,
  });
  return true;
}

bool PaperDollInfo_PickupInventoryItemImpl(
    lua_State* L,
    openwow::game::WorldSession& session,
    unsigned int slot) {
  using namespace openwow::ui::game;
  namespace InventorySlots = openwow::game::InventorySlots;
  using openwow::game::ObjectGuid;

  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return false;
  }

  const auto player_guid = session.objects().GetActivePlayerGuid().GetRawValue();
  if (player_guid == 0) {
    return false;
  }

  if (slot >= InventorySlots::kBankBagStart &&
      slot < InventorySlots::kBankBagEnd &&
      session.bank_npc_guid() == 0) {
    return false;
  }

  auto* cursor = openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return false;
  }
  const auto* held_item = cursor->live_item();
  const auto* held_merchant =
      cursor->get_if<openwow::game::actions::held_cursor::MerchantItem>();
  auto& inv = RequirePlayerInventoryReplica(L);

  std::uint64_t target_guid = 0;
  std::optional<InventorySlotCursorSource> target_source;

  if (slot == 255) {

    target_guid = player_guid;
  } else {
    const bool bank_open = session.bank_npc_guid() != 0;
    target_source =
        ResolveInventorySlotCursorSource(inv, static_cast<int>(slot), bank_open);
    if (!target_source.has_value()) {
      (void)HandleResolvedInventorySlotPickup(L, static_cast<int>(slot));
      return false;
    }
    target_guid = target_source->item.guid;
  }

  if (target_guid == 0) {
    return false;
  }

  const auto* target_item =
      target_source.has_value() ? &target_source->item : nullptr;

  const auto* live_target_item =
      target_item != nullptr
          ? session.objects().GetItem(ObjectGuid(target_item->guid))
          : nullptr;
  if (live_target_item != nullptr && live_target_item->IsLocked()) {
    cursor->Clear();
    return true;
  }

  if (held_item != nullptr && held_item->item.guid == target_guid) {
    cursor->Clear();
    return true;
  }

  const bool has_held_item = held_item != nullptr;
  const bool has_action = held_merchant != nullptr;

  if (!has_held_item && !has_action) {
    cursor->Clear();
    return false;
  }

  if (held_item != nullptr &&
      held_item->source_container_guid == target_guid && target_guid != 0) {
    const bool from_player_inv =
        (held_item->source_container_guid == player_guid);
    const bool in_backpack_range =
        (held_item->source_slot >= InventorySlots::kBackpackStart &&
         held_item->source_slot < InventorySlots::kBackpackEnd);
    if (!from_player_inv || in_backpack_range) {
      cursor->Clear();
      return true;
    }
  }

  if (!has_held_item) {
    if (held_merchant != nullptr) {
      const auto merchant_slot = held_merchant->zero_based_slot + 1;
      (void)SendCursorMerchantItemToTarget(
          session, merchant_slot, target_guid, 255, 1);
    }
    cursor->Clear();
    return false;
  }

  if (slot != 255) {
    bool use_swap = HeldCursorUsesInventoryBagSwapPath(*cursor, player_guid);

    if (!use_swap) {
      if (const auto* held_item_obj =
              session.objects().GetItem(ObjectGuid(held_item->item.guid));
          held_item_obj != nullptr && held_item_obj->IsContainer()) {
        use_swap = true;
      }
    }

    if (use_swap) {
      GameUI_OnMouseoverUnitEnter(target_guid);

      int held_inv_slot = -1;
      if (ResolveHeldAbsoluteInventorySourceSlot(*cursor, &held_inv_slot)) {
        session.interaction().SendSwapInvItem(
            static_cast<std::uint8_t>(slot),
            static_cast<std::uint8_t>(held_inv_slot));
      } else {
        std::uint8_t src_bag = 0;
        std::uint8_t src_slot_val = 0;
        if (ResolveHeldCursorServerCoords(*cursor, &src_bag, &src_slot_val)) {
          session.interaction().SendSwapItem(
              InventorySlots::kMainBag,
              static_cast<std::uint8_t>(slot),
              src_bag, src_slot_val);
        }
      }
      return true;
    }
  }

  std::uint8_t src_bag = 0;
  std::uint8_t src_slot_val = 0;
  if (!ResolveHeldCursorServerCoords(*cursor, &src_bag, &src_slot_val)) {
    cursor->Clear();
    return false;
  }

  const auto dst_bag =
      static_cast<std::uint8_t>(slot == 255 ? InventorySlots::kMainBag : slot);
  const auto split_count = held_item->auxiliary_value;

  if (split_count != 0) {
    session.interaction().SendSplitItem(
        src_bag, src_slot_val, dst_bag, 0xFF, split_count);
  } else {
    session.interaction().SendAutoStoreBagItem(
        src_bag, src_slot_val, dst_bag);
  }

  cursor->Clear({
      .release_source_lease = false,
      .publish_money_owner_update = true,
  });
  return true;
}

}

bool PaperDollInfo_PickupInventoryItem(lua_State* L,
                                       openwow::game::WorldSession& session,
                                       unsigned int slot) {
  return PaperDollInfo_PickupInventoryItemImpl(L, session, slot);
}

bool AutoEquipHeldCursorItem(openwow::game::WorldSession& session,
                             const bool skip_bind_check) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return false;
  }

  session.inventory_commands().PlaceHeldItem(skip_bind_check);
  return true;
}

void HandleResolvedSpellbookPickupOrDrop(lua_State* L,
                                         const std::uint32_t spell_id,
                                         const std::uint32_t spellbook_slot,
                                         const bool from_pet_book) {
  HandleResolvedSpellbookPickupOrDropImpl(
      L, spell_id, spellbook_slot, from_pet_book);
}

int LuaClearCursor(lua_State *L) {
  if (auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L)) {
    cursor->Clear();
  }
  return 1;
}

int LuaPickupItem(lua_State *L) {
  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return 0;
  }

  const std::uint32_t item_id = ResolvePickupItemArgument(L);
  cursor->Clear();
  if (item_id == 0) {
    return 0;
  }

  const auto* dbc = GetDbcLoader(L);
  const std::uint32_t display_id =
      ResolvePickupItemDisplayId(dbc, item_id);
  namespace held_cursor = ::openwow::game::actions::held_cursor;
  cursor->HoldActionBarItem(
      held_cursor::ActionBarItem{
          .item_entry = item_id,
          .display_id = display_id,
      },
      held_cursor::Presentation{
          .texture_path =
              display_id != 0
                  ? ::openwow::game::ResolveItemInventoryIconTexturePath(
                        dbc, display_id)
                  : std::string{},
          .texture_mode = held_cursor::TextureMode::HeldTexture,
          .grid = held_cursor::Grid::ActionBar,
      });
  return 0;
}

static int HandleResolvedInventorySlotPickup(lua_State* L, const int slot) {
  auto *session = GetWorldSession(L);
  if (slot < -1 || session == nullptr ||
      session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  auto &inv = RequirePlayerInventoryReplica(L);
  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return 0;
  }
  const auto* cursor_item = cursor->live_item();
  const bool has_held_item_object = cursor_item != nullptr;
  const auto* held_ammo =
      cursor->get_if<
          ::openwow::game::actions::held_cursor::AmmoItem>();
  const bool bank_open = session->bank_npc_guid() != 0;

  if ((slot >= openwow::game::InventorySlots::kBankStart &&
       slot < openwow::game::InventorySlots::kBankBagEnd) &&
      !bank_open) {
    return 0;
  }

  const auto player_guid =
      session->objects().GetActivePlayerGuid().GetRawValue();

  if (slot == -1) {
    if (has_held_item_object || held_ammo != nullptr) {
      (void)AutoEquipHeldCursorItem(*session);
      return 0;
    }

    const auto *player = session->objects().GetActivePlayer();
    const auto ammo_item_id = player->GetUInt32(PLAYER_AMMO_ID);
    const auto ammo_count = ::openwow::game::CountCarriedItemsOfEntry(
        session->inventory_replica(), ammo_item_id);
    if (ammo_item_id == 0 || ammo_count == 0) {
      cursor->Clear();
      return 0;
    }

    SetAmmoCursorState(L, ammo_item_id, ammo_count);
    session->interaction().SendSetAmmo(0);
    return 0;
  }

  const auto target_source = ResolveInventorySlotCursorSource(inv, slot, bank_open);
  const auto *item = target_source.has_value() ? &target_source->item : nullptr;

  if (const auto* merchant =
          cursor->get_if<
              ::openwow::game::actions::held_cursor::MerchantItem>()) {
    const auto merchant_slot = merchant->zero_based_slot + 1;
    if (item != nullptr && !item->IsEmpty()) {
      if (SendCursorMerchantItemToTarget(*session, merchant_slot, item->guid,
                                         ::openwow::game::InventorySlots::kMainBag)) {
        cursor->Clear();
      }
      return 0;
    }

    if (SendCursorMerchantItemToTarget(*session, merchant_slot, player_guid,
                                       static_cast<std::uint8_t>(slot))) {
      cursor->Clear();
    }
    return 0;
  }

  if (!cursor->empty() && !has_held_item_object) {
    return 0;
  }

  if (item != nullptr && !item->IsEmpty() &&
      has_held_item_object && cursor_item->item.guid == item->guid) {
    cursor->Clear();
    return 0;
  }

  const auto* live_target_item =
      item != nullptr && !item->IsEmpty()
          ? session->objects().GetItem(
                ::openwow::game::ObjectGuid(item->guid))
          : nullptr;
  if (live_target_item != nullptr && live_target_item->IsLocked()) {
    return 0;
  }

  if (has_held_item_object) {
    if (TryAutoStoreHeldCursorIntoInventoryBag(
            L, *session, *cursor, inv, slot, bank_open, player_guid)) {
      return 0;
    }

    if (!InventoryCursorTargetAcceptsItem(L, *session, slot)) {
      int held_inventory_slot = -1;
      if (ResolveHeldAbsoluteInventorySourceSlot(*cursor, &held_inventory_slot) &&
          held_inventory_slot >= 0 &&
          held_inventory_slot < openwow::game::PlayerInventoryReplica::kMaxEquipSlots) {
        cursor->Clear();
        return 0;
      }

      (void)AutoEquipHeldCursorItem(*session);
      return 0;
    }

    int held_inventory_slot = -1;
    const bool has_absolute_source =
        ResolveHeldAbsoluteInventorySourceSlot(*cursor, &held_inventory_slot);
    if (has_absolute_source && held_inventory_slot == slot) {
      cursor->Clear();
      return 0;
    }

    const auto split_count = cursor_item->auxiliary_value;

    if (split_count == 0 &&
        slot < openwow::game::PlayerInventoryReplica::kMaxEquipSlots) {
      if (!session->inventory_commands().PlaceHeldItemInSlot(
              static_cast<std::uint32_t>(slot))) {

        return 0;
      }
      if (item != nullptr && !item->IsEmpty()) {
        GameUI_OnMouseoverUnitEnter(item->guid);
      }
      return 0;
    }

    if (has_absolute_source) {
      if (split_count != 0) {
        session->interaction().SendSplitItem(
            openwow::game::InventorySlots::kMainBag,
            static_cast<std::uint8_t>(held_inventory_slot),
            openwow::game::InventorySlots::kMainBag,
            static_cast<std::uint8_t>(slot),
            split_count);
      } else {
        session->interaction().SendSwapInvItem(
            static_cast<std::uint8_t>(slot),
            static_cast<std::uint8_t>(held_inventory_slot));
      }
    } else {
      std::uint8_t src_bag = 0;
      std::uint8_t src_slot = 0;
      if (!ResolveHeldCursorServerCoords(*cursor, &src_bag, &src_slot)) {
        return 0;
      }
      if (split_count != 0) {
        session->interaction().SendSplitItem(
            src_bag, src_slot,
            ::openwow::game::InventorySlots::kMainBag,
            static_cast<std::uint8_t>(slot),
            split_count);
      } else {
        session->interaction().SendSwapItem(::openwow::game::InventorySlots::kMainBag,
                                            static_cast<std::uint8_t>(slot), src_bag, src_slot);
      }
    }

    cursor->Clear({
        .release_source_lease = false,
        .publish_money_owner_update = true,
    });
    if (item != nullptr && !item->IsEmpty()) {
      GameUI_OnMouseoverUnitEnter(item->guid);
    }
    return 0;
  }

  if (IsRepairCursorModeActive() && live_target_item != nullptr) {
    const auto *repair_vendor =
        ResolveMerchantRepairVendor(session->gossip(), session->objects());
    const auto *player = session->objects().GetActivePlayer();
    if (repair_vendor == nullptr || player == nullptr) {
      return 0;
    }

    const auto *dbc =
        session->GetDbcLoader() != nullptr ? session->GetDbcLoader() : GetDbcLoader(L);
    const auto repair_cost =
        CalculateMerchantRepairCost(
            session->query_cache(), session->objects(),
            ::openwow::game::ReputationInfo::Get(), dbc, *repair_vendor,
            *item);
    if (repair_cost > player->GetUInt32(::openwow::game::PLAYER_FIELD_COINAGE)) {
      DisplaySystemMessage(40);
      return 0;
    }

    (void)session->sound_runtime().PlaySoundKitByName("ITEM_REPAIR");
    session->interaction().SendRepairItem(
        session->gossip().merchant().snapshot().vendor_guid.GetRawValue(), item->guid, false);
    return 0;
  }

  if (item == nullptr || item->IsEmpty()) {
    return 0;
  }

  auto& targeting = session->spells().GetTargeting();
  constexpr auto kItemTargets = ::openwow::game::SpellTargetFlag::kItem |
                                ::openwow::game::SpellTargetFlag::kGoItem;
  if (targeting.GetSpellId() != 0 &&
      ::openwow::game::HasFlag(
          static_cast<::openwow::game::SpellTargetFlag>(
              targeting.GetTargetMask()),
          kItemTargets)) {
    RequireItemLuaAdapter(L).PromptItemTarget(
        ::openwow::game::ObjectGuid(item->guid));
    return 0;
  }

  PickupInventorySlotCursorSource(L, player_guid, *target_source);

  return 0;
}

int LuaPickupInventoryItem(lua_State *L) {
  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot)) {
    return 0;
  }
  return HandleResolvedInventorySlotPickup(L, slot);
}

int LuaPickupSpell(lua_State *L) {

  if (!GameUI_CanPerformProtectedAction(
          protected_action_kind::kActionSlotMutation)) {
    return 0;
  }

  const auto query = ResolveScriptCurrentSpellQuery(L, "PickupSpell");
  if (!query.has_value()) {
    return 0;
  }

  HandleResolvedSpellbookPickupOrDrop(
      L, query->spell_id, query->spellbook_slot, query->from_pet_book);
  return 0;
}

int LuaPickupPetAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: PickupPetAction(index)");
  }

  const auto slot_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (slot_index > openwow::game::kPetActionBarSlotCount) {
    return luaL_error(L, "Invalid slot in PickupPetAction");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr || !GameUI_CanPerformProtectedAction(protected_action_kind::kActionSlotMutation)) {
    return 0;
  }

  const auto pet_guid = GetSelectablePetActionGuid(*session);
  if (pet_guid == 0) {
    return 0;
  }

  if (TryPlaceHeldPetActionOnBar(L, *session, pet_guid, slot_index)) {
    return 0;
  }

  if (slot_index >= openwow::game::kPetActionBarSlotCount) {
    return 0;
  }

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return 0;
  }
  auto &pet_bar = session->pet().mutable_pet_bar();
  std::uint32_t raw_slots[openwow::game::kPetActionBarSlotCount];
  for (std::size_t index = 0; index < openwow::game::kPetActionBarSlotCount; ++index) {
    raw_slots[index] = pet_bar.action_bar[index].raw;
  }

  const auto current_action = raw_slots[slot_index];
  SetHeldPetActionCursor(L, *cursor, current_action,
                         slot_index + 1u);
  if (GetPetActionType(current_action) != kPetActionTypeSpell) {
    return 0;
  }

  const auto cleared_action = current_action & 0xFF000000u;
  const auto mutation = ApplyPetActionSlotMutation(raw_slots, slot_index, cleared_action);
  if (!mutation.applied) {
    return 0;
  }

  for (std::size_t index = 0; index < openwow::game::kPetActionBarSlotCount; ++index) {
    pet_bar.action_bar[index].raw = raw_slots[index];
  }

  session->interaction().SendPetSetAction(
      pet_guid, std::nullopt, {slot_index, raw_slots[slot_index]});
  ScriptEventDispatch::Get().FirePetBarUpdate();
  return 0;
}

int LuaPickupMacro(lua_State *L) {

  if (!GameUI_CanPerformProtectedAction(
          protected_action_kind::kActionSlotMutation)) {
    return 0;
  }
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  auto& macros = session->macros();

  std::optional<openwow::game::MacroDocument> macro;
  if (lua_isnumber(L, 1) != 0) {
    const auto slot =
        ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
    if (slot < openwow::game::MacroCatalog::kTotalSlots) {
      macro = macros.FindMacroAtSlot(slot);
    }
  } else if (lua_isstring(L, 1) != 0) {
    if (const char* name = lua_tostring(L, 1); name != nullptr) {
      macro = macros.FindMacroByName(name);
    }
  }

  if (macro) {
    if (auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L)) {
      (void)::openwow::game::actions::macros::ui::PickupMacroCursor(
          macros, *cursor, macro->id);
    }
  }
  return 0;
}

int LuaPickupMerchantItem(lua_State *L) {
  auto* session = GetWorldSession(L);
  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (session == nullptr || cursor == nullptr ||
      session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  if (TrySellHeldCursorItemToMerchant(*session, *cursor)) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    cursor->Clear();
    return 0;
  }

  const auto zero_based_slot = ::openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(
          TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) -
      1u);
  const auto* vendor_item = GetActiveVendorItemByZeroBasedIndex(
      GetActiveVendorList(*session), zero_based_slot);
  if (vendor_item == nullptr || vendor_item->item_id == 0) {
    cursor->Clear();
    return 0;
  }

  if (const auto* held =
          cursor->get_if<
              ::openwow::game::actions::held_cursor::MerchantItem>();
      held != nullptr && held->item_entry == vendor_item->item_id) {
    cursor->Clear();
    return 0;
  }

  cursor->Clear();
  namespace held_cursor = ::openwow::game::actions::held_cursor;
  cursor->HoldMerchantItem(
      held_cursor::MerchantItem{
          .item_entry = vendor_item->item_id,
          .display_id = vendor_item->display_info_id,
          .zero_based_slot = static_cast<std::uint32_t>(zero_based_slot),
      },
      held_cursor::Presentation{
          .texture_path = ResolveMerchantCursorTexturePath(L, *vendor_item),
          .texture_mode = held_cursor::TextureMode::HeldTexture,
          .sound = held_cursor::Sound::CursorGrabObject,
      });
  return 0;
}

int LuaPickupBagFromSlot(lua_State *L) {
  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot) ||
      slot < openwow::game::InventorySlots::kBagSlotsStart) {
    return 0;
  }
  auto *session = GetWorldSession(L);
  if (session == nullptr ||
      session->objects().GetActivePlayer() == nullptr ||
      session->objects().GetActivePlayerGuid().IsEmpty()) {
    return 0;
  }

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return 0;
  }

  if (cursor->live_item() != nullptr) {
    cursor->Clear();
  }

  auto &inv = RequirePlayerInventoryReplica(L);
  const bool bank_open = session->bank_npc_guid() != 0;
  const auto source = ResolveInventorySlotCursorSource(inv, slot, bank_open);
  if (!source.has_value()) {
    return LuaPickupInventoryItem(L);
  }

  const auto* item = session->objects().GetItem(
      ::openwow::game::ObjectGuid(source->item.guid));
  if (item != nullptr && item->IsLocked()) {
    return 0;
  }

  PickupInventorySlotCursorSource(
      L, session->objects().GetActivePlayerGuid().GetRawValue(), *source);
  return 0;
}

int LuaPutItemInBag(lua_State *L) {
  int inv_slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &inv_slot) || inv_slot < 19) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (PaperDollInfo_PickupInventoryItem(
          L, *session, static_cast<unsigned int>(inv_slot))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaCursorCanGoInSlot(lua_State* L) {
  int slot = 0;
  if (!ResolveInventorySlotArgument(L, 1, &slot)) {
    return 0;
  }

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (slot == -1 && cursor != nullptr &&
      cursor->get_if<
          ::openwow::game::actions::held_cursor::AmmoItem>() != nullptr) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  auto* session = GetWorldSession(L);
  const auto* player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto held_item = ResolveHeldCursorItemTemplate(L);
  if (player == nullptr || !held_item.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, CursorItemCanGoInSlot(
                         *held_item, slot, player,
                         RequirePlayerInventoryReplica(L),
                         cursor != nullptr && cursor->live_item() != nullptr
                             ? cursor->live_item()->item.guid
                             : 0u,
                         GetDbcLoader(L)));
  return 1;
}

int LuaPutItemInBackpack(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (PaperDollInfo_PickupInventoryItem(L, *session, 255)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaDropItemOnUnit(lua_State *L) {
  if (lua_type(L, 1) != LUA_TSTRING)
    return luaL_error(L, "Usage: DropItemOnUnit(\"unit\")");

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }
  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr || cursor->live_item() == nullptr ||
      cursor->live_item()->item.guid == 0) {
    return 0;
  }
  const auto* held_item_object = session->objects().GetItem(
      openwow::game::ObjectGuid(cursor->live_item()->item.guid));
  if (held_item_object == nullptr) {
    return 0;
  }

  const auto target_guid = ResolveUnitId(session, SafeLuaString(L, 1));
  if (target_guid.IsEmpty()) {
    return 0;
  }

  const auto *target_unit = session->objects().GetUnit(target_guid);
  if (!target_unit) {
    return 0;
  }

  const auto *active_player = session->objects().GetActivePlayer();

  if (target_unit->IsPlayer()) {
    if (active_player != nullptr && target_guid == active_player->GetGuid()) {
      (void)AutoEquipHeldCursorItem(*session, false);
    }
    else if (active_player != nullptr &&
             active_player->Interaction().IsFriendlyTo(*target_unit)) {
      const auto trade_guid = session->trade().begin_trade_guid();
      if (trade_guid == target_guid.GetRawValue()) {

        const auto source = ::openwow::game::ResolveCursorTradeSource(
            session->inventory_replica(), *cursor->live_item());
        const auto trade_slot = session->trade().SelectCursorDropTradeSlot(
            cursor->live_item()->item.guid, held_item_object->IsSoulbound());
        if (source.has_value() && trade_slot.has_value()) {
          if (session->interaction().SendSetTradeItem(
                  *trade_slot, source->server_bag, source->slot)) {
            if (!session->trade().SetLocalPlayerTradeSlot(
                    *trade_slot, cursor->live_item()->item.guid,
                    source->server_bag, source->slot)) {
              return 0;
            }
            ::openwow::ui::game::ScriptEventDispatch::Get().FireTradePlayerItemChanged(
                static_cast<int>(*trade_slot) + 1);
            cursor->Clear({
                .release_source_lease = false,
                .publish_money_owner_update = true,
            });
          }
        }
      } else if (trade_guid == 0) {
        session->interaction().SendInitiateTrade(target_guid.GetRawValue(), true);
      }
    }
  }

  if (active_player != nullptr &&
      ::openwow::game::targeting::ui::TryCastHeldItemSpellOnOwnedPet(
          *session, *active_player, *target_unit)) {
    cursor->Clear();
  }

  return 0;
}

int LuaEquipCursorItem(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: EquipCursorItem(slot)");
  }

  auto *session = GetWorldSession(L);
  if (!session || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }
  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return 0;
  }

  const int requested_slot = static_cast<int>(lua_tonumber(L, 1));
  if (requested_slot == 0 || requested_slot == 256) {
    (void)AutoEquipHeldCursorItem(*session, true);
    return 0;
  }

  const int slot = requested_slot - 1;
  if (slot < 0 || slot > 254) {
    return 0;
  }

  auto &inv = RequirePlayerInventoryReplica(L);
  const auto* cursor_item = cursor->live_item();
  if (cursor_item == nullptr) {
    return 0;
  }

  const auto target_source = ResolveInventorySlotCursorSource(inv, slot, true);
  const auto* target_item = target_source.has_value() ? &target_source->item : nullptr;
  if (target_item != nullptr && !target_item->IsEmpty() &&
      cursor_item->item.guid == target_item->guid) {
    cursor->Clear();
    return 0;
  }

  const auto* held_item_object = session->objects().GetItem(
      openwow::game::ObjectGuid(cursor_item->item.guid));
  if (held_item_object == nullptr ||
      !InventoryCursorTargetAcceptsItem(L, *session, slot)) {
    int held_inventory_slot = -1;
    if (ResolveHeldAbsoluteInventorySourceSlot(*cursor, &held_inventory_slot) &&
        held_inventory_slot >= 0 &&
        held_inventory_slot < openwow::game::InventorySlots::kEquipEnd) {
      cursor->Clear();
      return 0;
    }

    (void)AutoEquipHeldCursorItem(*session, true);
    return 0;
  }

  int held_inventory_slot = -1;
  const bool has_absolute_source =
      ResolveHeldAbsoluteInventorySourceSlot(*cursor, &held_inventory_slot);
  if (has_absolute_source && held_inventory_slot == slot) {
    cursor->Clear();
    return 0;
  }

  const auto split_count = cursor_item->auxiliary_value;

  if (split_count == 0 &&
      slot < openwow::game::PlayerInventoryReplica::kMaxEquipSlots) {
    if (!session->inventory_commands().PlaceHeldItemInSlot(
            static_cast<std::uint32_t>(slot))) {

      return 0;
    }
    if (target_item != nullptr && !target_item->IsEmpty()) {
      GameUI_OnMouseoverUnitEnter(target_item->guid);
    }
    if (RefreshAllActionSlotValidation(*session)) {
      ScriptEventDispatch::Get().FireActionbarUpdateUsable();
    }
    return 0;
  }

  if (has_absolute_source) {
    if (split_count != 0) {
      session->interaction().SendSplitItem(
          openwow::game::InventorySlots::kMainBag,
          static_cast<std::uint8_t>(held_inventory_slot),
          openwow::game::InventorySlots::kMainBag,
          static_cast<std::uint8_t>(slot),
          split_count);
    } else {
      session->interaction().SendSwapInvItem(
          static_cast<std::uint8_t>(slot),
          static_cast<std::uint8_t>(held_inventory_slot));
    }
  } else {
    std::uint8_t src_bag = 0;
    std::uint8_t src_slot = 0;
    if (!ResolveHeldCursorServerCoords(*cursor, &src_bag, &src_slot)) {
      return 0;
    }

    if (split_count != 0) {
      session->interaction().SendSplitItem(
          src_bag, src_slot,
          openwow::game::InventorySlots::kMainBag,
          static_cast<std::uint8_t>(slot),
          split_count);
    } else {
      session->interaction().SendSwapItem(::openwow::game::InventorySlots::kMainBag,
                                          static_cast<std::uint8_t>(slot), src_bag, src_slot);
    }
  }

  cursor->Clear({
      .release_source_lease = false,
      .publish_money_owner_update = true,
  });
  if (target_item != nullptr && !target_item->IsEmpty()) {
    GameUI_OnMouseoverUnitEnter(target_item->guid);
  }
  if (RefreshAllActionSlotValidation(*session)) {
    ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }
  return 0;
}

}

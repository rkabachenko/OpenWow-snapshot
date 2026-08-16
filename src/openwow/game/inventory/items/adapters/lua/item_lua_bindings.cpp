#include "openwow/game/inventory/items/adapters/lua/item_lua_bindings.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/inventory/items/adapters/lua/item_socket_lua_api.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/arena_handler.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/group_system.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_arena_use.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/localization.h"
#include "openwow/game/misc_handler.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/quest_manager.h"
#include "openwow/game/session/unit_token_resolver.h"
#include "openwow/game/session_handler.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/world_session.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace openwow::ui::game::detail {

int LuaGetContainerNumSlots(lua_State* L);
int LuaGetContainerItemInfo(lua_State* L);
int LuaGetContainerItemLink(lua_State* L);
int LuaGetContainerNumFreeSlots(lua_State* L);
int LuaGetContainerItemID(lua_State* L);
int LuaGetContainerItemCooldown(lua_State* L);
int LuaGetContainerItemQuestInfo(lua_State* L);
int LuaGetInventoryItemDurability(lua_State* L);
int LuaGetInventoryItemTexture(lua_State* L);
int LuaGetInventoryItemBroken(lua_State* L);
int LuaGetRepairAllCost(lua_State* L);
int LuaHasSoulstone(lua_State* L);
int LuaUseSoulstone(lua_State* L);
int LuaGetItemCooldown(lua_State* L);
int LuaGetInventorySlotInfo(lua_State* L);
int LuaGetInventoryItemID(lua_State* L);
int LuaGetInventoryItemLink(lua_State* L);
int LuaGetInventoryItemCount(lua_State* L);
int LuaGetInventoryItemQuality(lua_State* L);
int LuaGetInventoryItemCooldown(lua_State* L);
int LuaHasWandEquipped(lua_State* L);
int LuaIsInventoryItemLocked(lua_State* L);
int LuaPickupContainerItem(lua_State* L);
int LuaUseContainerItem(lua_State* L);
int LuaSplitContainerItem(lua_State* L);
int LuaGetBagName(lua_State* L);
int LuaGetItemInfo(lua_State* L);
int LuaGetItemCount(lua_State* L);
int LuaGetItemQualityColor(lua_State* L);
int LuaGetItemIcon(lua_State* L);
int LuaGetMoney(lua_State* L);
int LuaGetItemSpell(lua_State* L);
int LuaIsUsableItem(lua_State* L);
int LuaIsEquippableItem(lua_State* L);
int LuaIsConsumableItem(lua_State* L);
int LuaIsDressableItem(lua_State* L);
int LuaGetItemFamily(lua_State* L);
int LuaGetNumEquipmentSets(lua_State* L);
int LuaGetEquipmentSetInfo(lua_State* L);
int LuaGetEquipmentSetInfoByName(lua_State* L);
int LuaSaveEquipmentSet(lua_State* L);
int LuaDeleteEquipmentSet(lua_State* L);
int LuaUseEquipmentSet(lua_State* L);
int LuaGetEquipmentSetItemIDs(lua_State* L);
int LuaCanUseEquipmentSets(lua_State* L);
int LuaGetNumBankSlots(lua_State* L);
int LuaPurchaseSlot(lua_State* L);
int LuaCloseBankFrame(lua_State* L);
int LuaBankButtonIDToInvSlotID(lua_State* L);
int LuaGetBankSlotCost(lua_State* L);
int LuaGetContainerFreeSlots(lua_State* L);
int LuaGetContainerItemDurability(lua_State* L);
int LuaGetItemGem(lua_State* L);
int LuaSocketInventoryItem(lua_State* L);
int LuaEquipItemByName(lua_State* L);
int LuaIsEquippedItem(lua_State* L);
int LuaIsEquippedItemType(lua_State* L);
int LuaIsCurrentItem(lua_State* L);
int LuaGetItemUniqueness(lua_State* L);
int LuaShowingHelm(lua_State* L);
int LuaShowingCloak(lua_State* L);
int LuaShowHelm(lua_State* L);
int LuaShowCloak(lua_State* L);
int LuaGetItemStatDelta(lua_State* L);
int LuaItemHasRange(lua_State* L);
int LuaIsItemInRange(lua_State* L);
int LuaUseItemByName(lua_State* L);
int LuaContainerIDToInventoryID(lua_State* L);
int LuaEquipPendingItem(lua_State* L);
int LuaEquipmentSetContainsLockedItems(lua_State* L);
int LuaGetEquipmentSetLocations(lua_State* L);
int LuaGetInventoryAlertStatus(lua_State* L);
int LuaUpdateInventoryAlertStatus(lua_State* L);
int LuaGetItemStats(lua_State* L);
int LuaIsHarmfulItem(lua_State* L);
int LuaIsHelpfulItem(lua_State* L);
int LuaItemTextGetCreator(lua_State* L);
int LuaItemTextGetItem(lua_State* L);
int LuaItemTextGetMaterial(lua_State* L);
int LuaItemTextGetPage(lua_State* L);
int LuaItemTextGetText(lua_State* L);
int LuaItemTextHasNextPage(lua_State* L);
int LuaItemTextNextPage(lua_State* L);
int LuaItemTextPrevPage(lua_State* L);
int LuaCloseItemText(lua_State* L);
int LuaPickupEquipmentSet(lua_State* L);
int LuaPickupEquipmentSetByName(lua_State* L);
int LuaSetBagPortraitTexture(lua_State* L);
int LuaSetInventoryPortraitTexture(lua_State* L);
int LuaSocketContainerItem(lua_State* L);
int LuaUseInventoryItem(lua_State* L);
int LuaCancelItemTempEnchantment(lua_State* L);
int LuaCancelPendingEquip(lua_State* L);
int LuaGetContainerItemGems(lua_State* L);
int LuaGetInventoryItemGems(lua_State* L);
int LuaGetInventoryItemsForSlot(lua_State* L);
int LuaEquipmentManagerClearIgnoredSlotsForSave(lua_State* L);
int LuaEquipmentManagerIgnoreSlotForSave(lua_State* L);
int LuaEquipmentManagerIsSlotIgnoredForSave(lua_State* L);
int LuaEquipmentManagerUnignoreSlotForSave(lua_State* L);
int LuaRenameEquipmentSet(lua_State* L);
int LuaKeyRingButtonIDToInvSlotID(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr char kSingleItemRepairSoundKit[] = "ITEM_REPAIR";

constexpr openwow::ui::LuaGlobalBinding kItemLuaBindings[] = {
    {"GetContainerNumSlots", LuaGetContainerNumSlots},
    {"GetContainerItemInfo", LuaGetContainerItemInfo},
    {"GetContainerItemLink", LuaGetContainerItemLink},
    {"GetContainerNumFreeSlots", LuaGetContainerNumFreeSlots},
    {"GetContainerItemID", LuaGetContainerItemID},
    {"GetContainerItemCooldown", LuaGetContainerItemCooldown},
    {"GetInventoryItemDurability", LuaGetInventoryItemDurability},
    {"GetInventoryItemTexture", LuaGetInventoryItemTexture},
    {"GetInventoryItemBroken", LuaGetInventoryItemBroken},
    {"GetRepairAllCost", LuaGetRepairAllCost},
    {"HasSoulstone", LuaHasSoulstone},
    {"UseSoulstone", LuaUseSoulstone},
    {"GetItemCooldown", LuaGetItemCooldown},
    {"GetInventorySlotInfo", LuaGetInventorySlotInfo},
    {"GetInventoryItemID", LuaGetInventoryItemID},
    {"GetInventoryItemLink", LuaGetInventoryItemLink},
    {"GetInventoryItemCount", LuaGetInventoryItemCount},
    {"GetInventoryItemQuality", LuaGetInventoryItemQuality},
    {"GetInventoryItemCooldown", LuaGetInventoryItemCooldown},
    {"HasWandEquipped", LuaHasWandEquipped},
    {"IsInventoryItemLocked", LuaIsInventoryItemLocked},
    {"PickupContainerItem", LuaPickupContainerItem},
    {"UseContainerItem", LuaUseContainerItem},
    {"SplitContainerItem", LuaSplitContainerItem},
    {"GetBagName", LuaGetBagName},
    {"GetItemInfo", LuaGetItemInfo},
    {"GetItemCount", LuaGetItemCount},
    {"GetItemQualityColor", LuaGetItemQualityColor},
    {"GetItemIcon", LuaGetItemIcon},
    {"GetMoney", LuaGetMoney},
    {"GetItemSpell", LuaGetItemSpell},
    {"IsUsableItem", LuaIsUsableItem},
    {"IsEquippableItem", LuaIsEquippableItem},
    {"IsConsumableItem", LuaIsConsumableItem},
    {"IsDressableItem", LuaIsDressableItem},
    {"GetItemFamily", LuaGetItemFamily},
    {"GetNumEquipmentSets", LuaGetNumEquipmentSets},
    {"GetEquipmentSetInfo", LuaGetEquipmentSetInfo},
    {"GetEquipmentSetInfoByName", LuaGetEquipmentSetInfoByName},
    {"SaveEquipmentSet", LuaSaveEquipmentSet},
    {"DeleteEquipmentSet", LuaDeleteEquipmentSet},
    {"UseEquipmentSet", LuaUseEquipmentSet},
    {"GetEquipmentSetItemIDs", LuaGetEquipmentSetItemIDs},
    {"CanUseEquipmentSets", LuaCanUseEquipmentSets},
    {"GetNumBankSlots", LuaGetNumBankSlots},
    {"PurchaseSlot", LuaPurchaseSlot},
    {"CloseBankFrame", LuaCloseBankFrame},
    {"BankButtonIDToInvSlotID", LuaBankButtonIDToInvSlotID},
    {"GetBankSlotCost", LuaGetBankSlotCost},
    {"GetContainerFreeSlots", LuaGetContainerFreeSlots},
    {"GetContainerItemDurability", LuaGetContainerItemDurability},
    {"GetItemGem", LuaGetItemGem},
    {"SocketInventoryItem", LuaSocketInventoryItem},
    {"EquipItemByName", LuaEquipItemByName},
    {"IsEquippedItem", LuaIsEquippedItem},
    {"IsEquippedItemType", LuaIsEquippedItemType},
    {"IsCurrentItem", LuaIsCurrentItem},
    {"GetItemUniqueness", LuaGetItemUniqueness},
    {"ShowingHelm", LuaShowingHelm},
    {"ShowingCloak", LuaShowingCloak},
    {"ShowHelm", LuaShowHelm},
    {"ShowCloak", LuaShowCloak},
    {"GetItemStatDelta", LuaGetItemStatDelta},
    {"ItemHasRange", LuaItemHasRange},
    {"IsItemInRange", LuaIsItemInRange},
    {"UseItemByName", LuaUseItemByName},
    {"ContainerIDToInventoryID", LuaContainerIDToInventoryID},
    {"EquipPendingItem", LuaEquipPendingItem},
    {"EquipmentSetContainsLockedItems", LuaEquipmentSetContainsLockedItems},
    {"GetEquipmentSetLocations", LuaGetEquipmentSetLocations},
    {"GetInventoryAlertStatus", LuaGetInventoryAlertStatus},
    {"UpdateInventoryAlertStatus", LuaUpdateInventoryAlertStatus},
    {"GetItemStats", LuaGetItemStats},
    {"IsHarmfulItem", LuaIsHarmfulItem},
    {"IsHelpfulItem", LuaIsHelpfulItem},
    {"ItemTextGetCreator", LuaItemTextGetCreator},
    {"ItemTextGetItem", LuaItemTextGetItem},
    {"ItemTextGetMaterial", LuaItemTextGetMaterial},
    {"ItemTextGetPage", LuaItemTextGetPage},
    {"ItemTextGetText", LuaItemTextGetText},
    {"ItemTextHasNextPage", LuaItemTextHasNextPage},
    {"ItemTextNextPage", LuaItemTextNextPage},
    {"ItemTextPrevPage", LuaItemTextPrevPage},
    {"CloseItemText", LuaCloseItemText},
    {"PickupEquipmentSet", LuaPickupEquipmentSet},
    {"PickupEquipmentSetByName", LuaPickupEquipmentSetByName},
    {"SetBagPortraitTexture", LuaSetBagPortraitTexture},
    {"SetInventoryPortraitTexture", LuaSetInventoryPortraitTexture},
    {"SocketContainerItem", LuaSocketContainerItem},
    {"UseInventoryItem", LuaUseInventoryItem},
    {"CancelItemTempEnchantment", LuaCancelItemTempEnchantment},
    {"CancelPendingEquip", LuaCancelPendingEquip},
    {"GetContainerItemGems", LuaGetContainerItemGems},
    {"GetContainerItemQuestInfo", LuaGetContainerItemQuestInfo},
    {"GetInventoryItemGems", LuaGetInventoryItemGems},
    {"GetInventoryItemsForSlot", LuaGetInventoryItemsForSlot},
    {"EndRefund", LuaApi_EndRefund},
    {"OffhandHasWeapon", LuaApi_OffhandHasWeapon},
    {"EquipmentManagerClearIgnoredSlotsForSave", LuaEquipmentManagerClearIgnoredSlotsForSave},
    {"EquipmentManagerIgnoreSlotForSave", LuaEquipmentManagerIgnoreSlotForSave},
    {"EquipmentManagerIsSlotIgnoredForSave", LuaEquipmentManagerIsSlotIgnoredForSave},
    {"EquipmentManagerUnignoreSlotForSave", LuaEquipmentManagerUnignoreSlotForSave},
    {"RenameEquipmentSet", LuaRenameEquipmentSet},
    {"KeyRingButtonIDToInvSlotID", LuaKeyRingButtonIDToInvSlotID},
};

}

openwow::ui::lua::NativeBindingCatalog ItemNativeBindingCatalog(
    std::shared_ptr<ItemLuaAdapter> adapter) {
  auto catalog = openwow::ui::lua::NativeFunctionCatalog(
      "game.inventory.items", openwow::ui::lua::BindingScope::kWorld, kItemLuaBindings);
  catalog.lifecycle_context = std::move(adapter);
  return catalog;
}

void ItemLuaAdapter::Bind(Dependencies dependencies) {
  equipment_ = &dependencies.equipment;
  cooldowns_ = &dependencies.cooldowns;
  cursor_ = &dependencies.cursor;
  dbc_ = dependencies.dbc;
  events_ = &dependencies.events;
  guild_ = &dependencies.guild;
  held_cursor_ = dependencies.held_cursor;
  interaction_ = &dependencies.interaction;
  inventory_ = &dependencies.inventory;
  items_ = &dependencies.items;
  item_interactions_ = &dependencies.item_interactions;
  localization_ = &dependencies.localization;
  queries_ = &dependencies.queries;
  reputation_ = &dependencies.reputation;
  spell_book_ = &dependencies.spell_book;
  spells_ = &dependencies.spells;
  trade_ = &dependencies.trade;
  world_session_ = &dependencies.world_session;
  session_state_ = &dependencies.session_state;
  arena_ = &dependencies.arena;
  group_ = &dependencies.group;
  battlefield_ = &dependencies.battlefield;
  instance_ = &dependencies.instance;
  gossip_ = &dependencies.gossip;
  quests_ = &dependencies.quests;
  play_time_ = &dependencies.play_time;
  script_events_ = &dependencies.script_events;
  security_ = &dependencies.security;
  ui_errors_ = &dependencies.ui_errors;
}

bool ItemLuaAdapter::bound() const noexcept {
  return equipment_ != nullptr && cooldowns_ != nullptr && cursor_ != nullptr &&
         events_ != nullptr && guild_ != nullptr && interaction_ != nullptr &&
         inventory_ != nullptr && items_ != nullptr &&
         item_interactions_ != nullptr && localization_ != nullptr &&
         queries_ != nullptr &&
         reputation_ != nullptr && spell_book_ != nullptr &&
         spells_ != nullptr && trade_ != nullptr && world_session_ != nullptr &&
         session_state_ != nullptr && arena_ != nullptr && group_ != nullptr &&
         battlefield_ != nullptr && instance_ != nullptr &&
         gossip_ != nullptr && quests_ != nullptr && play_time_ != nullptr &&
         script_events_ != nullptr && security_ != nullptr &&
         ui_errors_ != nullptr;
}

openwow::game::EquipmentSets& ItemLuaAdapter::equipment() const {
  return *equipment_;
}

openwow::game::CooldownTracker& ItemLuaAdapter::cooldowns() const {
  return *cooldowns_;
}

const openwow::data::dbc::DbcLoader* ItemLuaAdapter::dbc() const noexcept {
  return dbc_;
}

EventDispatcher& ItemLuaAdapter::events() const {
  return *events_;
}

openwow::game::GuildSystem& ItemLuaAdapter::guild() const {
  return *guild_;
}

openwow::game::actions::held_cursor::HeldCursor*
ItemLuaAdapter::held_cursor() const noexcept {
  return held_cursor_;
}

openwow::game::InteractionSender& ItemLuaAdapter::interaction() const {
  return *interaction_;
}

openwow::game::PlayerInventoryReplica& ItemLuaAdapter::inventory() const {
  return *inventory_;
}

openwow::game::ItemDefinitions& ItemLuaAdapter::items() const {
  return *items_;
}

openwow::game::ItemInteractionSession& ItemLuaAdapter::item_interactions() const {
  return *item_interactions_;
}

openwow::game::Localization& ItemLuaAdapter::localization() const {
  return *localization_;
}

openwow::game::ObjectManager& ItemLuaAdapter::objects() const {

  return world_session_->objects();
}

openwow::game::QueryCache& ItemLuaAdapter::queries() const {
  return *queries_;
}

openwow::game::ReputationInfo& ItemLuaAdapter::reputation() const {
  return *reputation_;
}

openwow::game::SpellBook& ItemLuaAdapter::spell_book() const {
  return *spell_book_;
}

openwow::game::SpellCastRuntime& ItemLuaAdapter::spells() const {
  return *spells_;
}

openwow::game::TradeInteraction& ItemLuaAdapter::trade() const {
  return *trade_;
}

openwow::game::WorldSession& ItemLuaAdapter::world_session() const {
  return *world_session_;
}

std::optional<std::string> ItemLuaAdapter::ResolveVisibleSlotIcon(
    lua_State* state, const std::uint8_t slot) const {
  const auto* player = objects().GetLocalPlayerTyped();
  return player != nullptr ? VisibleItemIcon(state, *player, slot)
                           : std::nullopt;
}

void ItemLuaAdapter::SaveSet(
    const openwow::game::EquipmentSetSave& request) const {
  interaction().SendSaveEquipmentSet(request);
}

void ItemLuaAdapter::DeleteSet(const openwow::game::ObjectGuid set) const {
  interaction().SendDeleteEquipmentSet(set);
}

void ItemLuaAdapter::UseSet(
    const openwow::game::EquipmentSetUse& request) const {
  interaction().SendUseEquipmentSet(request);
}

void ItemLuaAdapter::PresentSetsChanged() const {
  script_events_->FireEvent(events::EQUIPMENT_SETS_CHANGED);
}

bool ItemLuaAdapter::bank_frame_open() const {
  return world_session().bank_npc_guid() != 0;
}

void ItemLuaAdapter::CloseBank() const {
  script_events_->FireBankFrameClosed();

  world_session().CloseBank();
}

void ItemLuaAdapter::ShowSystemMessage(const int message) const {
  DisplaySystemMessage(message);
}

void ItemLuaAdapter::PresentTradeItemChanged(const int slot) const {
  script_events_->FireTradePlayerItemChanged(slot);
}

bool ItemLuaAdapter::CurrentMapIsArena() const {
  if (dbc_ == nullptr) return false;
  const auto* map = dbc_->map().LookupEntry(objects().GetMapId());
  return map != nullptr && openwow::game::IsArenaMapType(map->map_type);
}

std::uint32_t ItemLuaAdapter::ProficiencyMask(
    const std::uint8_t item_class) const {
  return session_state_->GetProficiencyMask(item_class);
}

std::optional<std::string> ItemLuaAdapter::ReadableCreator(
    const openwow::game::ObjectGuid item) const {
  const auto* instance = objects().GetItem(item);
  if (instance == nullptr || instance->GetCreator().IsEmpty()) {
    return std::nullopt;
  }
  const auto creator = instance->GetCreator().GetRawValue();
  if (const auto* name = queries().GetPlayerName(creator);
      name != nullptr && !name->name.empty()) {
    return name->name;
  }
  static_cast<void>(queries().RequestNameQuery(creator));
  return std::nullopt;
}

std::optional<std::string> ItemLuaAdapter::ReadableName(
    lua_State*, const openwow::game::ObjectGuid item) const {
  if (const auto* instance = objects().GetItem(item); instance != nullptr) {
    const auto* definition =
        queries().GetOrRequestItemTemplate(instance->GetEntry());
    return definition != nullptr ? std::optional<std::string>{definition->name}
                                 : std::nullopt;
  }
  if (const auto* object = objects().GetGameObject(item); object != nullptr) {
    return std::string(object->GetStatsName());
  }
  return std::nullopt;
}

std::optional<std::string> ItemLuaAdapter::ReadableMaterial(
    lua_State*, const openwow::game::ObjectGuid item) const {
  if (dbc_ == nullptr) return std::nullopt;
  std::uint32_t material = 0;
  if (const auto* instance = objects().GetItem(item); instance != nullptr) {
    const auto* definition = queries().GetItemTemplate(instance->GetEntry());
    material = definition != nullptr ? definition->page_material : 0;
  } else if (const auto* object = objects().GetGameObject(item);
             object != nullptr) {
    material = object->GetReadablePageMaterialId();
  }
  const auto* entry =
      material != 0 ? dbc_->page_text_material().LookupEntry(material) : nullptr;
  return entry != nullptr ? std::optional<std::string>{entry->name}
                          : std::nullopt;
}

void ItemLuaAdapter::LoadReadablePage(const bool publish) const {
  auto& readable = item_interactions().readable();
  if (!readable.has_value() || readable->page >= readable->pages.size()) return;
  const auto page = readable->pages[readable->page];
  const auto* text = play_time_->FindCachedPageText(page);
  if (text == nullptr) {
    if (!play_time_->IsPageTextQueryPending(page)) {
      play_time_->MarkPageTextQueryPending(page);
      interaction().SendPageTextQuery(page);
    }
    return;
  }
  if (readable->pages.size() <= readable->page + 1) {
    readable->pages.resize(readable->page + 2);
  }
  readable->pages[readable->page + 1] = text->next_page;
  std::array<char, 8000> expanded{};
  openwow::game::BindSpellTextFormatterDbcLoader(dbc_);
  openwow::game::SpellTextFormatter::ExpandObjectTextVariables(
      text->text.c_str(), expanded.data(),
      static_cast<std::uint32_t>(expanded.size()),
      objects().GetActivePlayerGuid().GetRawValue(), nullptr, 0);
  const std::string expanded_text =
      expanded[0] != '\0' ? expanded.data() : text->text;
  std::uint32_t language = 0;
  if (const auto* item = objects().GetItem(readable->item); item != nullptr) {
    if (const auto* definition = queries().GetItemTemplate(item->GetEntry());
        definition != nullptr) {
      language = definition->language_id;
    }
  } else if (const auto* object = objects().GetGameObject(readable->item);
             object != nullptr) {
    language = object->GetReadableLanguageId();
  }
  std::uint32_t comprehension = 0;
  if (publish && language != 0 && dbc_ != nullptr) {
    if (const auto* player = objects().GetActivePlayer(); player != nullptr) {
      comprehension =
          openwow::game::GetChatLanguageComprehensionValue(
              *player, *dbc_, language);
    }
  }
  readable->text = openwow::game::ChatFrame_FormatMessage(
      objects(), language, comprehension, expanded_text,
      {.output_limit = expanded.size(),
       .preserve_angle_bracket_spans = true,
       .preserve_separators = true});
  readable->opened = true;
  if (publish) script_events_->FireEvent(events::ITEM_TEXT_READY);
}

void ItemLuaAdapter::PresentReadableClosed() const {
  script_events_->FireEvent(events::ITEM_TEXT_CLOSED);
}

openwow::game::ObjectGuid ItemLuaAdapter::ResolveUnit(
    const std::string_view unit) const {
  return openwow::game::ResolveUnitToken(
      objects(), *group_, *battlefield_, *instance_, unit);
}

std::uint64_t ItemLuaAdapter::InspectTarget() const {
  return arena_->inspect_target_guid();
}

std::optional<std::pair<std::uint32_t, bool>>
ItemLuaAdapter::RepairQuote() const {
  const auto* vendor = detail::ResolveMerchantRepairVendor(*gossip_, objects());
  if (vendor == nullptr) return std::nullopt;
  const auto summary = detail::CalculateMerchantRepairSummary(
      inventory(), queries(), objects(), reputation(), dbc_, *vendor);
  return std::pair{summary.total_cost, summary.has_damaged_items};
}

std::optional<std::string> ItemLuaAdapter::VisibleItemIcon(
    lua_State*, const openwow::game::CGPlayer_C& player,
    const std::uint8_t slot) const {
  const auto entry = player.GetVisibleItemTemplateEntry(slot);
  if (!entry.has_value()) return std::nullopt;
  const auto* definition = queries().GetOrRequestItemTemplate(*entry);
  if (definition == nullptr) definition = items().GetItem(*entry);
  return definition != nullptr
             ? std::optional<std::string>{
                   openwow::game::ResolveItemInventoryIconTexturePath(
                       dbc_, definition->display_id)}
             : std::nullopt;
}

bool ItemLuaAdapter::RepairItem(
    const openwow::game::ItemInstance& item) const {
  if (cursor_->GetBaseCursorType() != openwow::game::CursorType::kRepair) {
    return false;
  }
  const auto* vendor = detail::ResolveMerchantRepairVendor(*gossip_, objects());
  const auto* player = objects().GetActivePlayer();
  if (vendor == nullptr || player == nullptr) return false;
  const auto cost = detail::CalculateMerchantRepairCost(
      queries(), objects(), reputation(), dbc_, *vendor, item);
  if (cost > player->GetUInt32(openwow::game::PLAYER_FIELD_COINAGE)) {
    ShowSystemMessage(40);
    return true;
  }
  (void)world_session().sound_runtime().PlaySoundKitByName(
      kSingleItemRepairSoundKit);
  interaction().SendRepairItem(
      vendor->GetGuid().GetRawValue(), item.guid, false);
  return true;
}

bool ItemLuaAdapter::DepositGuildBank(
    lua_State*, const int bag, const int slot) const {
  if (!guild().IsBankFrameOpen() || guild().GetBankerGuid() == 0 ||
      objects().GetActivePlayer() == nullptr || slot < 0) {
    return false;
  }
  const openwow::game::ItemInstance* item = nullptr;
  std::uint8_t wire_bag = openwow::game::InventorySlots::kMainBag;
  std::uint8_t wire_slot = 0;
  if (bag == 0 &&
      slot < static_cast<int>(openwow::game::PlayerInventoryReplica::kBackpackSize)) {
    item = inventory().GetBackpackSlot(static_cast<std::uint8_t>(slot));
    wire_slot = static_cast<std::uint8_t>(
        openwow::game::InventorySlots::kBackpackStart + slot);
  } else if (bag >= 1 &&
             bag <= openwow::game::PlayerInventoryReplica::kMaxBags) {
    const auto* container = inventory().GetBag(static_cast<std::uint8_t>(bag));
    if (container == nullptr || slot >= container->num_slots) return true;
    item = inventory().GetBagSlot(
        static_cast<std::uint8_t>(bag), static_cast<std::uint8_t>(slot));
    wire_bag = static_cast<std::uint8_t>(
        openwow::game::InventorySlots::kBagSlotsStart + bag - 1);
    wire_slot = static_cast<std::uint8_t>(slot);
  } else {
    return false;
  }
  if (item == nullptr || item->IsEmpty()) return true;
  const auto tab = guild().GetCurrentGuildBankTabIndex();
  const auto* definition = queries().GetOrRequestItemTemplate(item->entry);
  if (definition == nullptr) return true;
  if (item->IsSoulbound()) {
    ShowSystemMessage(definition->bonding == 4 ? 130 : 129);
    return true;
  }
  const auto* live_item =
      objects().GetItem(openwow::game::ObjectGuid(item->guid));
  if (live_item == nullptr || live_item->IsLocked() ||
      item->IsConjured() || item->duration != 0 ||
      definition->duration > 0 ||
      cursor_texture::ItemTemplateHasLocationRestriction(*definition)) {
    ShowSystemMessage(127);
    return true;
  }
  if ((item->flags & openwow::game::ItemFlags::kWrapped) != 0) {
    ShowSystemMessage(131);
    return true;
  }
  const auto* player = objects().GetActivePlayer();
  if (player == nullptr ||
      tab >= openwow::game::GuildSystem::kGuildBankMaxTabs ||
      (guild().GetControlBankTabFlags(player->GetGuildRank(), tab) & 0x2u) ==
          0) {
    ShowSystemMessage(95);
    return true;
  }
  interaction().SendGuildBankSwapItemsPlayerToBank(
      guild().GetBankerGuid(), tab, 0xFFu, 0, wire_bag, wire_slot, 0);
  if (held_cursor_ != nullptr) held_cursor_->Clear();
  script_events_->FireEvent(events::GUILDBANKBAGSLOTS_CHANGED);
  return true;
}

void ItemLuaAdapter::PromptItemTarget(
    const openwow::game::ObjectGuid item) const {
  openwow::game::inventory::ui::ProcessItemSpellTarget(
      dbc_, inventory(), queries(), item_interactions(), objects(),
      interaction(), spells(), localization(), *ui_errors_, *script_events_,
      item, openwow::game::inventory::ui::ItemTargetConfirmation::kPrompt);
}

void ItemLuaAdapter::ConfirmItemTarget(
    const openwow::game::ObjectGuid item) const {
  openwow::game::inventory::ui::ProcessItemSpellTarget(
      dbc_, inventory(), queries(), item_interactions(), objects(),
      interaction(), spells(), localization(), *ui_errors_, *script_events_,
      item, openwow::game::inventory::ui::ItemTargetConfirmation::kConfirmed);
}

bool ItemLuaAdapter::StartItemTargeting(
    const openwow::game::ItemInstance& item,
    const openwow::game::ItemTemplate& definition) const {
  return openwow::game::inventory::ui::TryStartItemSpellTargeting(
      dbc_, spells(), item, definition);
}

std::uint64_t ItemLuaAdapter::MerchantVendor() const {
  return gossip_->merchant().active()
             ? gossip_->merchant().snapshot().vendor_guid.GetRawValue()
             : 0;
}

std::optional<std::uint64_t> ItemLuaAdapter::ResolveItemUseTarget(
    const std::string_view unit) const {
  if (unit.empty()) return objects().GetTargetGuid().GetRawValue();
  if (openwow::text::EqualsIgnoreCaseAscii(unit, "none")) return 0;
  if (openwow::text::EqualsIgnoreCaseAscii(unit, "npc")) {
    if (gossip_->has_gossip()) return gossip_->gossip().npc_guid.GetRawValue();
    if (const auto merchant = MerchantVendor(); merchant != 0) return merchant;
    if (world_session().bank_npc_guid() != 0) return world_session().bank_npc_guid();
  }
  if (openwow::text::EqualsIgnoreCaseAscii(unit, "questnpc")) {
    if (quests_->has_active_details())
      return quests_->active_details().npc_guid.GetRawValue();
    if (quests_->has_active_reward())
      return quests_->active_reward().npc_guid.GetRawValue();
    if (quests_->has_active_request())
      return quests_->active_request().npc_guid.GetRawValue();
  }
  const auto guid = ResolveUnit(unit);
  return guid.IsEmpty()
             ? std::nullopt
             : std::optional<std::uint64_t>{guid.GetRawValue()};
}

std::optional<bool> ItemLuaAdapter::ItemInRange(
    const std::uint32_t spell,
    const openwow::game::ObjectGuid target) const {
  const auto* unit = objects().GetUnit(target);
  const auto* player = objects().GetLocalPlayerTyped();
  if (dbc_ == nullptr || unit == nullptr || player == nullptr) {
    return std::nullopt;
  }
  const auto* definition = dbc_->spell().LookupEntry(spell);
  if (definition == nullptr) return std::nullopt;
  const auto* range = dbc_->spell_range().LookupEntry(definition->range_index);
  const auto window = openwow::game::SpellTargetValidator::GetTargetRangeWindow(
      *definition, range, *player, *unit, false, nullptr);
  return openwow::game::SpellTargetValidator::IsTargetInRange(
      *player, *unit, window);
}

std::uint32_t ItemLuaAdapter::CurrentPlayedTime() const {
  return play_time_->current_total_played_time();
}

bool ItemLuaAdapter::HasSpellPower(
    const openwow::data::dbc::SpellEntry& spell,
    const openwow::game::CGPlayer_C& player) const {
  return openwow::game::HasEnoughSpellPower(spell, player, world_session());
}

std::uint32_t ItemLuaAdapter::MonotonicMilliseconds() const {
  return openwow::core::GameClock::GetTickCount32();
}

ItemLuaAdapter& RequireItemLuaAdapter(lua_State* state) {
  auto* adapter = static_cast<ItemLuaAdapter*>(
      openwow::ui::lua::detail::ActiveBindingAdapter(state));
  if (adapter == nullptr) {

    adapter = static_cast<ItemLuaAdapter*>(
        openwow::ui::lua::detail::GlobalBindingAdapter(state, "GetContainerNumSlots"));
  }
  if (adapter == nullptr || !adapter->bound()) {
    luaL_error(state, "item Lua API is not bound");
  }
  return *adapter;
}

}

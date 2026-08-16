#include "openwow/ui/game/tooltip_lua_adapter.h"
#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_internal.h"

#include "openwow/core/storm_containers.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/client_config.h"
#include "openwow/game/container_slot_mapping.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_query_bridge.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/world_session.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/runtime/lua/lua_binding.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {
using namespace tooltip_internal;

namespace detail {

namespace {

constexpr std::string_view kTooltipContextRegistryKey =
    "openwow.game_tooltip_context";
constexpr std::string_view kTooltipOwnerRegistryKey =
    "openwow.game_tooltip_owners";

TooltipSystem* ResolveReceiverTooltip(lua_State* lua) {
  if (lua == nullptr || lua_istable(lua, 1) == 0) {
    return nullptr;
  }

  openwow::ui::lua::LuaStackRestore stack(lua);
  openwow::ui::lua::PushTableField(lua, LUA_REGISTRYINDEX,
                                   kTooltipOwnerRegistryKey);
  if (lua_istable(lua, -1) == 0) {
    return nullptr;
  }

  lua_pushnil(lua);
  while (lua_next(lua, -2) != 0) {
    if (lua_rawequal(lua, 1, -1) != 0) {
      return static_cast<TooltipSystem*>(lua_touserdata(lua, -2));
    }
    lua_pop(lua, 1);
  }
  return nullptr;
}

enum class BagTooltipLookupStatus {
  kNoReturn,
  kNilReturn,
  kItem,
};

struct BagTooltipLookupResult {
  BagTooltipLookupStatus status = BagTooltipLookupStatus::kNoReturn;
  const openwow::game::ItemInstance *item = nullptr;
};

std::uint32_t ResolveTooltipPlayerLevel(const TooltipSystem& tooltip) {
  const auto *session = tooltip.GetWorldSession();
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  return player != nullptr ? player->State().GetLevel() : 0u;
}

bool ResolveTooltipInventorySlot(const TooltipSystem& tooltip,
                                 const TooltipLuaValue& argument,
                                 int& slot) {
  slot = std::numeric_limits<int>::min();
  if (const auto* number = std::get_if<double>(&argument.value)) {
    const auto parsed = openwow::ui::TruncateLuaNumberToI32(*number);
    slot = openwow::ui::SignedI32FromU32Bits(
        static_cast<std::uint32_t>(parsed) - 1u);
  } else if (const auto* name = std::get_if<std::string>(&argument.value)) {
    const auto* dbc = tooltip.GetDbcLoader();
    if (dbc != nullptr) {
      for (const auto& entry : dbc->paper_doll_item_frame().entries()) {
        if (openwow::text::EqualsIgnoreCaseAscii(entry.item_button_name,
                                                 *name)) {
          slot = static_cast<int>(entry.slot_number);
          break;
        }
      }
    }
  }
  return IsValidResolvedInventorySlot(slot);
}

const openwow::game::CGItem_C* GetTooltipLocalInventoryItem(
    openwow::game::WorldSession& session, const int slot) {
  if (slot < 0 || slot >= openwow::game::InventorySlots::kTotalSlots ||
      (InventorySlotRequiresBankInteraction(slot) &&
       session.bank_npc_guid() == 0)) {
    return nullptr;
  }
  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) return nullptr;
  const auto field = static_cast<std::uint16_t>(PLAYER_FIELD_INV_SLOT_HEAD +
                                                slot * 2);
  const auto guid = player->GetGuidField(field);
  return guid.IsEmpty() ? nullptr : session.objects().GetItem(guid);
}

std::uint32_t ResolveTooltipGemId(const TooltipSystem& tooltip,
                                  const std::uint32_t enchant_id) {
  const auto* dbc = tooltip.GetDbcLoader();
  const auto* enchant = dbc != nullptr && enchant_id != 0
                            ? dbc->spell_item_enchantment().LookupEntry(enchant_id)
                            : nullptr;
  return enchant != nullptr ? enchant->gem_id : 0;
}

openwow::ui::TooltipItemInstanceData BuildTooltipInstanceDataFromItem(
    const TooltipSystem& tooltip,
    const openwow::game::ItemInstance &item) {
  openwow::ui::TooltipItemInstanceData instance_data;
  instance_data.permanent_enchant_id = item.GetPermanentEnchant();
  instance_data.gem_item_ids[0] = ResolveTooltipGemId(tooltip, item.GetSocketEnchant(0));
  instance_data.gem_item_ids[1] = ResolveTooltipGemId(tooltip, item.GetSocketEnchant(1));
  instance_data.gem_item_ids[2] = ResolveTooltipGemId(tooltip, item.GetSocketEnchant(2));
  instance_data.durability = item.durability;
  instance_data.max_durability = item.max_durability;
  instance_data.runtime_flags = item.flags;
  instance_data.item_guid = item.guid;
  instance_data.remaining_duration_seconds = item.duration;
  instance_data.create_played_time = item.create_played_time;
  instance_data.creator_guid = item.creator_guid;
  instance_data.spell_charges = item.charges;
  const auto* session = tooltip.GetWorldSession();
  const auto* live_item =
      session != nullptr
          ? session->objects().GetItem(openwow::game::ObjectGuid(item.guid))
          : nullptr;
  instance_data.locked = live_item != nullptr && live_item->IsLocked();
  instance_data.live_item = true;
  return instance_data;
}

openwow::ui::TooltipItemInstanceData BuildTooltipInstanceDataFromMailAttachment(
    const TooltipSystem& tooltip,
    const openwow::game::MailAttachment &attachment) {
  openwow::ui::TooltipItemInstanceData instance_data;
  instance_data.permanent_enchant_id = attachment.enchant_id;
  instance_data.gem_item_ids[0] = ResolveTooltipGemId(tooltip, attachment.gem_ids[0]);
  instance_data.gem_item_ids[1] = ResolveTooltipGemId(tooltip, attachment.gem_ids[1]);
  instance_data.gem_item_ids[2] = ResolveTooltipGemId(tooltip, attachment.gem_ids[2]);
  instance_data.spell_charges[0] = static_cast<std::int32_t>(attachment.charges);
  instance_data.item_guid = attachment.item_guid;
  instance_data.live_item = true;
  return instance_data;
}

bool SetTooltipFromItemInstance(TooltipSystem& tooltip,
                                 const openwow::game::ItemInstance *item) {
  if (item == nullptr || item->entry == 0) {
    return false;
  }

  return tooltip.SetItemWithInstanceData(
      item->entry, item->random_property, item->random_suffix,
       BuildTooltipInstanceDataFromItem(tooltip, *item),
      ResolveTooltipPlayerLevel(tooltip), item->guid);
}

openwow::ui::TooltipItemInstanceData BuildTooltipInstanceDataFromTradeSlot(
    const TooltipSystem& tooltip, const openwow::game::TradeSlotItem &slot) {
  openwow::ui::TooltipItemInstanceData instance_data;
  instance_data.permanent_enchant_id = slot.permanent_enchant;
  instance_data.gem_item_ids[0] = ResolveTooltipGemId(tooltip, slot.socket_enchants[0]);
  instance_data.gem_item_ids[1] = ResolveTooltipGemId(tooltip, slot.socket_enchants[1]);
  instance_data.gem_item_ids[2] = ResolveTooltipGemId(tooltip, slot.socket_enchants[2]);
  instance_data.durability = slot.durability;
  instance_data.max_durability = slot.max_durability;
  instance_data.creator_guid = slot.creator;
  instance_data.spell_charges[0] =
      static_cast<std::int32_t>(slot.spell_charges);
  instance_data.locked = slot.lock_id != 0u;
  instance_data.live_item = true;
  return instance_data;
}

bool SetTooltipFromTradeSlot(TooltipSystem& tooltip,
                             const openwow::game::TradeSlotItem &slot) {
  if (slot.item_id == 0) {
    return false;
  }

  return tooltip.SetItemWithInstanceData(
      slot.item_id, slot.random_property_id, slot.suffix_factor,
      BuildTooltipInstanceDataFromTradeSlot(tooltip, slot),
      ResolveTooltipPlayerLevel(tooltip));
}

bool SetTooltipFromMailAttachment(TooltipSystem& tooltip,
                                   const openwow::game::MailAttachment &attachment) {
  if (attachment.item_id == 0) {
    return false;
  }

  return tooltip.SetItemWithInstanceData(
      attachment.item_id, attachment.random_property_id, attachment.suffix_factor,
       BuildTooltipInstanceDataFromMailAttachment(tooltip, attachment),
      ResolveTooltipPlayerLevel(tooltip),
      attachment.item_guid);
}

std::optional<openwow::game::AuctionItem> ResolveAuctionTooltipItem(
    const openwow::game::WorldSession& session,
    const char *raw_list, const int one_based_index) {
  if (one_based_index < 1) {
    return std::nullopt;
  }

  const std::size_t index = static_cast<std::size_t>(one_based_index - 1);
  const auto &auction = session.auction().state();
  if (raw_list == nullptr || std::strcmp(raw_list, "list") == 0) {
    return auction.GetResult(index);
  }
  if (std::strcmp(raw_list, "owner") == 0) {
    return auction.GetOwnAuction(index);
  }
  if (std::strcmp(raw_list, "bidder") == 0) {
    return auction.GetBid(index);
  }
  return std::nullopt;
}

void SetTooltipFromAuctionItem(TooltipSystem& tooltip,
                               const openwow::game::AuctionItem &item) {
  if (item.item_entry != 0) {
    tooltip.SetItemFromLoot(item.item_entry, item.random_property,
                            item.random_suffix);
  }
}

BagTooltipLookupResult ResolveBagTooltipItem(openwow::game::WorldSession &session,
                                             const int bag_index,
                                             const int zero_based_slot) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return {};
  }

  auto &inventory = session.inventory_replica();
  const auto slot = static_cast<std::uint8_t>(zero_based_slot);

  if (bag_index == 0) {
    if (zero_based_slot < 0 ||
        zero_based_slot >= static_cast<int>(openwow::game::PlayerInventoryReplica::kBackpackSize)) {
      return {BagTooltipLookupStatus::kNilReturn, nullptr};
    }

    return {inventory.GetBackpackSlot(slot) != nullptr ? BagTooltipLookupStatus::kItem
                                                       : BagTooltipLookupStatus::kNilReturn,
            inventory.GetBackpackSlot(slot)};
  }

  if (bag_index >= 1 &&
      bag_index <= static_cast<int>(openwow::game::PlayerInventoryReplica::kMaxBags)) {
    const auto *bag = inventory.GetBag(static_cast<std::uint8_t>(bag_index));
    if (bag == nullptr) {
      return {};
    }

    if (zero_based_slot < 0 || zero_based_slot >= static_cast<int>(bag->num_slots)) {
      return {BagTooltipLookupStatus::kNilReturn, nullptr};
    }

    const auto *item = inventory.GetBagSlot(static_cast<std::uint8_t>(bag_index), slot);
    return {item != nullptr ? BagTooltipLookupStatus::kItem : BagTooltipLookupStatus::kNilReturn,
            item};
  }

  if (bag_index >= 5 && bag_index <= 11) {
    if (session.bank_npc_guid() == 0) {
      return {};
    }

    const auto bank_bag_index = static_cast<std::uint8_t>(bag_index - 5);
    const auto *bag = inventory.GetBankBag(bank_bag_index);
    if (bag == nullptr) {
      return {};
    }

    if (zero_based_slot < 0 || zero_based_slot >= static_cast<int>(bag->num_slots)) {
      return {BagTooltipLookupStatus::kNilReturn, nullptr};
    }

    const auto *item = inventory.GetBankBagSlot(bank_bag_index, slot);
    return {item != nullptr ? BagTooltipLookupStatus::kItem : BagTooltipLookupStatus::kNilReturn,
            item};
  }

  return {};
}

bool CanSynchronouslyRenderBagItemTooltip(const openwow::game::WorldSession &session,
                                         const openwow::game::ItemInstance &item) {
  if (item.entry == 0u) {
    return false;
  }

  if (session.item_definitions().HasItem(item.entry)) {
    return true;
  }

  if (item.guid == 0u) {
    return false;
  }

  return false;
}

std::uint32_t ResolveBagItemRepairCost(const TooltipSystem& tooltip,
                                       openwow::game::WorldSession &session,
                                       const openwow::game::ItemInstance &item) {
  const auto *dbc = tooltip.GetDbcLoader();
  const auto *vendor =
      ResolveMerchantRepairVendor(session.gossip(), session.objects());
  if (dbc == nullptr || vendor == nullptr) {
    return 0;
  }

  return CalculateMerchantRepairCost(
      session.query_cache(), session.objects(),
      openwow::game::ReputationInfo::Get(), dbc, *vendor, item);
}

}

TooltipSystem& ResolveTooltipLuaReceiver(lua_State* lua) {
  if (auto* tooltip = ResolveReceiverTooltip(lua); tooltip != nullptr) {
    return *tooltip;
  }
  if (auto* tooltip = openwow::ui::lua::RegistryContext<TooltipSystem>(
          lua, kTooltipContextRegistryKey);
      tooltip != nullptr) {
    return *tooltip;
  }
  luaL_error(lua, "GameTooltip backing state is unavailable");
  std::abort();
}

void BindTooltipLuaContext(lua_State* lua, TooltipSystem* tooltip) {
  if (lua == nullptr) {
    return;
  }
  openwow::ui::lua::LuaStackRestore stack(lua);
  if (tooltip != nullptr) {
    lua_pushlightuserdata(lua, tooltip);
  } else {
    lua_pushnil(lua);
  }
  lua_setfield(lua, LUA_REGISTRYINDEX, kTooltipContextRegistryKey.data());
}

TooltipVoidResult SetTooltipAnchorType(TooltipSystem& tooltip,
                                       TooltipLuaString anchor,
                                       TooltipLuaValue, TooltipLuaValue) {
  if (!anchor.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetAnchorType( anchorType [,Xoffset] [,Yoffset] )"};
  }
  if (tooltip.HasOwner()) {
    tooltip.SetAnchor(*anchor.value);
  }
  return openwow::ui::lua::NoLuaResults{};
}

std::string GetTooltipAnchorType(TooltipSystem& tooltip) {
  return tooltip.GetAnchor();
}

TooltipVoidResult SetTooltipText(TooltipSystem& tooltip, TooltipLuaString text,
                                 TooltipLuaValue red, TooltipLuaValue green,
                                 TooltipLuaValue blue, TooltipLuaValue,
                                 std::optional<openwow::ui::lua::LuaTruthy> wrap) {
  if (!text.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetText(\"text\" [, color])"};
  }

  float r = 1.0f;
  float g = static_cast<float>(0xD2) / 255.0f;
  float b = 0.0f;
  if (const auto* value = std::get_if<double>(&red.value)) {
    auto clamp01 = [](double v) -> float {
      if (v < 0.0) return 0.0f;
      if (v >= 1.0) return 1.0f;
      return static_cast<float>(v);
    };
    r = clamp01(*value);
    g = clamp01(std::get_if<double>(&green.value) != nullptr
                    ? std::get<double>(green.value)
                    : 0.0);
    b = clamp01(std::get_if<double>(&blue.value) != nullptr
                    ? std::get<double>(blue.value)
                    : 0.0);
  }
  tooltip.ClearLines();
  tooltip.AddLine(*text.value, r, g, b, wrap.value_or(openwow::ui::lua::LuaTruthy{}).value);
  tooltip.Show();
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult AddTooltipTexture(TooltipSystem& tooltip,
                                    TooltipLuaString filename,
                                    TooltipLuaValue min_x,
                                    TooltipLuaValue max_x,
                                    TooltipLuaValue min_y,
                                    TooltipLuaValue max_y) {
  if (!filename.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:AddTexture(\"filename\" [, minx, maxx, miny, maxy])"};
  }
  if (filename.value->empty()) {
    return openwow::ui::lua::NoLuaResults{};
  }
  float tex_coords[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  const auto* x1 = std::get_if<double>(&min_x.value);
  const auto* x2 = std::get_if<double>(&max_x.value);
  const auto* y1 = std::get_if<double>(&min_y.value);
  const auto* y2 = std::get_if<double>(&max_y.value);
  if (x1 != nullptr && x2 != nullptr && y1 != nullptr && y2 != nullptr) {
    tex_coords[1] = static_cast<float>(*x1);
    tex_coords[3] = static_cast<float>(*x2);
    tex_coords[0] = static_cast<float>(*y1);
    tex_coords[2] = static_cast<float>(*y2);
  }
  tooltip.AddTexture(*filename.value, tex_coords, 0xFFFFFFFFu);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult AppendTooltipText(TooltipSystem& tooltip,
                                    TooltipLuaString text) {
  if (!text.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:AppendText(\"text\")"};
  }
  tooltip.AppendToFirstLine(*text.value);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipQuestLogItem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue item_type_argument, TooltipLuaValue item_index_argument) {
  const bool valid_item_type =
      std::holds_alternative<double>(item_type_argument.value) ||
      std::holds_alternative<std::string>(item_type_argument.value);
  const auto* item_index_number =
      std::get_if<double>(&item_index_argument.value);
  if (!valid_item_type || item_index_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid quest item in SetQuestLogItem(\"type\", index)"};
  }

  openwow::ui::lua::LuaCall call(lua.get());
  auto *session = tooltip.GetWorldSession();
  const std::string item_type = call.String(2);
  const int item_index = static_cast<int>(std::trunc(*item_index_number));
  const std::uint32_t item_id = GetSelectedQuestLogItemId(
      session, item_type, item_index);
  if (session == nullptr || item_id == 0) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid quest item in SetQuestLogItem(\"type\", index)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  tooltip.SetItemFromLoot(item_id, 0, 0,
                          ResolveTooltipPlayerLevel(tooltip));
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipTradeSkillItem(TooltipSystem& tooltip,
                                           TooltipLuaValue recipe,
                                           TooltipLuaValue reagent) {
  const auto* recipe_number = std::get_if<double>(&recipe.value);
  if (recipe_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid trade skill item in SetTradeSkillItem(index [,reagent])"};
  }

  const int recipe_index = static_cast<int>(std::trunc(*recipe_number));
  std::optional<int> reagent_index;
  if (const auto* reagent_number = std::get_if<double>(&reagent.value)) {
    reagent_index = static_cast<int>(std::trunc(*reagent_number));
  }

  const auto target = ResolveTradeSkillTooltipTarget(recipe_index, reagent_index);
  if (!target.has_value() || target->id == 0) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid trade skill item in SetTradeSkillItem(index [,reagent])"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  if (target->kind == TradeSkillTooltipTargetKind::kItem) {
    tooltip.SetItemFromLoot(target->id, 0, 0,
                            ResolveTooltipPlayerLevel(tooltip));
  } else {
    tooltip.SetSpellById(target->id);
  }
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipTradePlayerItem(TooltipSystem& tooltip,
                                            TooltipLuaValue slot_argument) {
  const auto* slot_number = std::get_if<double>(&slot_argument.value);
  if (slot_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid trade slot in SetTradePlayerItem"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  const int lua_slot = static_cast<int>(std::trunc(*slot_number));
  if (session == nullptr || lua_slot < 1 || lua_slot > openwow::game::kTradeSlotCount) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto slot =
      session->trade().GetLocalPlayerTradeSlot(static_cast<std::size_t>(lua_slot - 1));
  if (!slot.has_value()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto *item = ::openwow::game::GetTradeContainerItem(
      session->inventory_replica(), slot->source_bag, slot->source_slot);
  (void)SetTooltipFromItemInstance(tooltip, item);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipTradeTargetItem(TooltipSystem& tooltip,
                                            TooltipLuaValue slot_argument) {
  const auto* slot_number = std::get_if<double>(&slot_argument.value);
  if (slot_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid trade slot in SetTradeTargetItem"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto *session = tooltip.GetWorldSession();
  const int lua_slot = static_cast<int>(std::trunc(*slot_number));
  if (session == nullptr || lua_slot < 1 || lua_slot > openwow::game::kTradeSlotCount) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto &window = session->trade().trader_window();
  if (!window.has_value()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto &slot = window->slots[static_cast<std::size_t>(lua_slot - 1)];
  (void)SetTooltipFromTradeSlot(tooltip, slot);
  return openwow::ui::lua::NoLuaResults{};
}

openwow::ui::lua::NoLuaResults FadeTooltip(TooltipSystem& tooltip) {
  tooltip.FadeOut();
  return {};
}

int GetTooltipNumLines(TooltipSystem& tooltip) {
  return tooltip.GetNumLines();
}

openwow::ui::lua::NoLuaResults SetTooltipOwner(TooltipSystem& tooltip,
                                               TooltipLuaValue,
                                               TooltipLuaString anchor) {
  tooltip.SetOwner("", anchor.value.value_or("ANCHOR_LEFT"));
  return {};
}

openwow::ui::lua::NoLuaResults ShowTooltip(TooltipSystem& tooltip) {
  tooltip.Show();
  return {};
}

openwow::ui::lua::NoLuaResults HideTooltip(TooltipSystem& tooltip) {
  tooltip.Hide();
  return {};
}

openwow::ui::lua::NoLuaResults ClearTooltipLines(TooltipSystem& tooltip) {
  tooltip.ClearLines();
  return {};
}

openwow::ui::lua::NoLuaResults SetTooltipMinimumWidth(
    TooltipSystem& tooltip, const float width,
    const std::optional<openwow::ui::lua::LuaTruthy> force) {
  tooltip.SetMinimumWidth(
      width, force.value_or(openwow::ui::lua::LuaTruthy{}).value);
  return {};
}

openwow::ui::lua::LuaReturns<float, openwow::ui::lua::LuaTruthy>
GetTooltipMinimumWidth(TooltipSystem& tooltip) {
  return openwow::ui::lua::LuaReturns<float, openwow::ui::lua::LuaTruthy>(
      tooltip.GetMinimumWidth(),
      openwow::ui::lua::LuaTruthy{tooltip.IsForceMinWidth()});
}

openwow::ui::lua::NoLuaResults SetTooltipPadding(
    TooltipSystem& tooltip, const std::optional<float> padding) {
  tooltip.SetPadding(padding.value_or(0.0f));
  return {};
}

float GetTooltipPadding(TooltipSystem& tooltip) {
  return tooltip.GetPadding();
}

TooltipItemQueryResult GetTooltipItem(TooltipSystem& tooltip) {
  const std::string &item_name = tooltip.GetItemName();
  const std::string &item_link = tooltip.GetItemLink();
  if (item_link.empty()) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{});
  }
  return openwow::ui::lua::LuaReturns(item_name, item_link);
}

TooltipSpellQueryResult GetTooltipSpell(TooltipSystem& tooltip) {
  const auto resolve_spell = [&tooltip](const std::uint32_t spell_id)
      -> std::optional<openwow::ui::lua::LuaReturns<std::string, std::string,
                                                    std::uint32_t>> {
    if (spell_id == 0) {
      return std::nullopt;
    }

    std::string name;
    std::string rank;
    if (const auto query = openwow::game::SpellQueryBridge::Get().Query(spell_id);
        query.has_value()) {
      name = query->name;
      rank = query->subtext;
    } else {
      const auto* session = tooltip.GetWorldSession();
      const auto* dbc = session != nullptr ? session->GetDbcLoader()
                                           : tooltip.GetDbcLoader();
      const auto* spell = dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
      if (spell == nullptr) {
        return std::nullopt;
      }
      name = std::string(spell->spell_name);
      rank = std::string(spell->rank);
    }

    return openwow::ui::lua::LuaReturns(name, rank, spell_id);
  };
  const auto primary = resolve_spell(tooltip.GetSpellId());
  const auto secondary = resolve_spell(tooltip.GetSecondarySpellId());
  if (!primary) return openwow::ui::lua::NoLuaResults{};
  if (!secondary) return *primary;
  return std::apply(
      [&](const auto& name, const auto& rank, const auto id) {
        return std::apply(
            [&](const auto& second_name, const auto& second_rank,
                const auto second_id) -> TooltipSpellQueryResult {
              return openwow::ui::lua::LuaReturns(
                  name, rank, id, second_name, second_rank, second_id);
            }, secondary->values());
      }, primary->values());
}

TooltipUnitQueryResult GetTooltipUnit(TooltipSystem& tooltip) {
  const std::uint64_t guid = tooltip.GetUnitGuid();
  if (guid == 0)
    return openwow::ui::lua::NoLuaResults{};

  const std::string unit_token =
      openwow::ui::game::UnitTokenRegistry::Get().TokenForGuid(guid);
  if (unit_token.empty())
    return openwow::ui::lua::NoLuaResults{};

  auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const char *resolved_name = nullptr;
  if (openwow::game::ScriptEvents_ResolveUnitName(
          *session, unit_token.c_str(), &resolved_name, true, false) &&
      resolved_name != nullptr) {
    return openwow::ui::lua::LuaReturns(std::string(resolved_name), unit_token);
  }

  const auto *objects = tooltip.GetObjectManager();
  if (objects != nullptr) {
    const auto *unit =
        objects->GetUnit(openwow::game::ObjectGuid(guid));
    if (unit != nullptr) {

      const std::string name = unit->ResolveRetailName(*session);
      return openwow::ui::lua::LuaReturns(name, unit_token);
    }
  }

  return openwow::ui::lua::NoLuaResults{};
}

openwow::ui::lua::NoLuaResults AddTooltipLine(
    TooltipSystem& tooltip, TooltipLuaString text, TooltipLuaValue red,
    TooltipLuaValue green, TooltipLuaValue blue,
    std::optional<openwow::ui::lua::LuaTruthy> wrap) {
  const auto number = [](const TooltipLuaValue& value, const double fallback) {
    const auto* parsed = std::get_if<double>(&value.value);
    return static_cast<float>(parsed != nullptr ? *parsed : fallback);
  };
  tooltip.AddLine(text.value.value_or(""), number(red, 1.0),
                  number(green, 210.0 / 255.0), number(blue, 0.0),
                  wrap.value_or(openwow::ui::lua::LuaTruthy{}).value);
  return {};
}

openwow::ui::lua::NoLuaResults AddTooltipDoubleLine(
    TooltipSystem& tooltip, TooltipLuaString left, TooltipLuaString right,
    TooltipLuaValue left_red, TooltipLuaValue left_green,
    TooltipLuaValue left_blue, TooltipLuaValue right_red,
    TooltipLuaValue right_green, TooltipLuaValue right_blue,
    std::optional<openwow::ui::lua::LuaTruthy> wrap) {
  const auto number = [](const TooltipLuaValue& value, const double fallback) {
    const auto* parsed = std::get_if<double>(&value.value);
    return static_cast<float>(parsed != nullptr ? *parsed : fallback);
  };
  tooltip.AddDoubleLine(
      left.value.value_or(""), right.value.value_or(""), number(left_red, 1.0),
      number(left_green, 210.0 / 255.0), number(left_blue, 0.0),
      number(right_red, 1.0), number(right_green, 210.0 / 255.0),
      number(right_blue, 0.0),
      wrap.value_or(openwow::ui::lua::LuaTruthy{}).value);
  return {};
}

TooltipTruthyResult SetTooltipShapeshift(
    TooltipSystem& tooltip, CVarSystem& cvars,
    openwow::ui::lua::RawLuaState lua, TooltipLuaValue slot_argument) {
  const auto* slot_number = std::get_if<double>(&slot_argument.value);
  if (slot_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetShapeshift(slot)"};
  }

  const auto slot = openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(
      *slot_number);

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::LuaNil{};
  }

  const auto form_spells = openwow::game::ResolveShapeshiftFormSpellIds(session);
  const auto zero_based = static_cast<std::uint32_t>(slot) - 1u;
  if (zero_based >= form_spells.size()) {
    return openwow::ui::lua::LuaNil{};
  }

  const std::uint32_t spell_id = form_spells[zero_based];

  const bool uber_tooltips = cvars.GetCVarBool("UberTooltips");
  const int show_simple = uber_tooltips ? 0 : 1;

  const auto cooldown = openwow::game::ResolveSpellbookCooldown(session->spell_book(), spell_id);
  const int cooldown_remaining =
      cooldown.has_value() ? openwow::game::RemainingCooldownMilliseconds(*cooldown) : 0;

  const bool result = BuildSpellTooltip({
      .tooltip = tooltip,
      .spell_id = spell_id,
      .cooldown_remaining = std::chrono::milliseconds(cooldown_remaining),
      .simple = show_simple != 0,
  });

  return openwow::ui::lua::LuaTruthy{result};
}

TooltipTruthyResult SetTooltipPossession(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue slot_argument) {
  const auto* slot_number = std::get_if<double>(&slot_argument.value);
  if (slot_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetPossession(slot)"};
  }

  const auto slot = openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(
      *slot_number);

  if (slot != 1) {
    return openwow::ui::lua::LuaNil{};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::LuaNil{};
  }

  const auto spell_id = session->pet().pet_bar().possess_spell_id;
  if (spell_id == 0) {
    return openwow::ui::lua::LuaNil{};
  }

  const bool uber_tooltips =
      openwow::ui::game::CVarSystem::Instance().GetCVarBool("UberTooltips");
  const int show_simple = uber_tooltips ? 0 : 1;

  const auto cooldown =
      openwow::game::ResolvePetBarSpellCooldown(session->pet().pet_bar(), spell_id,
                                                session->GetDbcLoader());
  const int cooldown_remaining =
      cooldown.has_value() ? openwow::game::RemainingCooldownMilliseconds(*cooldown) : 0;

  const bool result = BuildSpellTooltip({
      .tooltip = tooltip,
      .spell_id = spell_id,
      .cooldown_remaining = std::chrono::milliseconds(cooldown_remaining),
      .simple = show_simple != 0,
  });

  return openwow::ui::lua::LuaTruthy{result};
}

TooltipVoidResult SetTooltipGlyph(TooltipSystem& tooltip,
                                  TooltipLuaValue glyph_slot_argument,
                                  TooltipLuaValue group_argument) {
  const auto* glyph_slot_number =
      std::get_if<double>(&glyph_slot_argument.value);
  if (glyph_slot_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetGlyph(glyphSlot[, groupIndex])"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (!session) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    return openwow::ui::lua::NoLuaResults{};
  }

  std::optional<std::uint32_t> group_arg;
  if (const auto* group_number = std::get_if<double>(&group_argument.value)) {
    group_arg =
        openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(*group_number);
  }
  const auto group_index = openwow::game::TalentInfoStore::Get().GetGroupIndexArg(group_arg);
  const auto *group =
      openwow::game::TalentInfoStore::Get().GetTalentGroupData(group_index, false, false);
  if (!group) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto glyph_slot = static_cast<std::uint32_t>(*glyph_slot_number);
  const auto slot_index = glyph_slot - 1;
  const auto slot_id = player->GetGlyphSlot(static_cast<std::uint8_t>(slot_index));
  const auto glyph_id = group->GetGlyph(slot_index);
  const auto is_enabled = slot_index < 32 && (player->GetGlyphsEnabled() & (1u << slot_index)) != 0;

  BuildGlyphTooltip(tooltip, slot_id, glyph_id, is_enabled, false);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipOptionalTruthyResult SetTooltipUnit(
    TooltipSystem& tooltip, TooltipLuaString unit,
    const std::optional<openwow::ui::lua::LuaTruthy> hide) {
  if (!unit.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetUnit(\"unit\"[, hideStatus])"};
  }
  return openwow::ui::lua::LuaTruthy{tooltip.SetUnit(
      *unit.value, hide.value_or(openwow::ui::lua::LuaTruthy{}).value)};
}

static bool BuildUnitAuraTooltipImpl(TooltipSystem& tooltip,
                                     WorldSession *session,
                                     const std::string &unit_token,
                                     int aura_index,
                                     const std::string &aura_name,
                                     const std::string &aura_rank,
                                     const std::string &filter) {
  if (session == nullptr) return false;

  const auto unit_guid = ResolveUnitId(session, unit_token);
  if (unit_guid.IsEmpty()) return false;

  std::optional<AuraQueryResult> aura_result;
  if (aura_index > 0) {
    aura_result = AuraLuaBridge::Get().GetUnitAura(
        *session, unit_guid, static_cast<std::uint32_t>(aura_index), filter);
  } else if (!aura_name.empty()) {
    aura_result = AuraLuaBridge::Get().FindUnitAura(
        *session, unit_guid, aura_name, aura_rank, filter);
  } else {
    return false;
  }

  if (!aura_result.has_value()) return false;

  BuildSimpleSpellTooltip(tooltip, aura_result->spellId);

  if (aura_result->duration > 0.0f && aura_result->remainingTime > 0.0f) {
    const float remaining_sec = aura_result->remainingTime;
    char time_buf[64];
    if (remaining_sec >= 3600.0f) {
      const int hours = static_cast<int>(remaining_sec / 3600.0f);
      const int mins = static_cast<int>(std::fmod(remaining_sec / 60.0f, 60.0f));
      if (mins > 0) {
        std::snprintf(time_buf, sizeof(time_buf), "%d hr %d min", hours, mins);
      } else {
        std::snprintf(time_buf, sizeof(time_buf), "%d hr", hours);
      }
    } else if (remaining_sec >= 60.0f) {
      std::snprintf(time_buf, sizeof(time_buf), "%d min",
                    static_cast<int>(std::ceil(remaining_sec / 60.0f)));
    } else {
      std::snprintf(time_buf, sizeof(time_buf), "%d sec",
                    static_cast<int>(std::ceil(remaining_sec)));
    }

    tooltip.AddLine(time_buf, kTooltipGrayR, kTooltipGrayG, kTooltipGrayB);
  }

  tooltip.Show();
  return true;
}

TooltipVoidResult SetTooltipUnitAura(
    TooltipSystem& tooltip, TooltipLuaString unit, TooltipLuaValue selector,
    TooltipLuaString rank_or_filter, TooltipLuaString filter_arg) {
  if (!unit.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetUnitAura(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])"};
  }
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) return openwow::ui::lua::NoLuaResults{};
  int aura_index = 0;
  std::string aura_name;
  std::string aura_rank;
  std::string filter = "HELPFUL";
  if (const auto* index = std::get_if<double>(&selector.value)) {
    aura_index = static_cast<int>(*index);
    if (aura_index < 1) return openwow::ui::lua::NoLuaResults{};
    filter = rank_or_filter.value.value_or(filter);
  } else if (const auto* name = std::get_if<std::string>(&selector.value)) {
    aura_name = *name;
    aura_rank = rank_or_filter.value.value_or("");
    filter = filter_arg.value.value_or(filter);
  } else {
    return openwow::ui::lua::NoLuaResults{};
  }
  BuildUnitAuraTooltipImpl(tooltip, session, *unit.value, aura_index, aura_name,
                           aura_rank, filter);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipUnitBuff(
    TooltipSystem& tooltip, TooltipLuaString unit, TooltipLuaValue selector,
    TooltipLuaString rank_or_filter, TooltipLuaString filter_arg) {
  if (!unit.value || std::holds_alternative<std::monostate>(selector.value)) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetUnitBuff(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])"};
  }
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) return openwow::ui::lua::NoLuaResults{};
  int aura_index = 0;
  std::string aura_name;
  std::string aura_rank;
  std::string filter = "HELPFUL";
  if (const auto* index = std::get_if<double>(&selector.value)) {
    aura_index = static_cast<int>(*index);
    if (aura_index < 1) return openwow::ui::lua::NoLuaResults{};
    if (rank_or_filter.value) {
      filter += "|";
      filter += *rank_or_filter.value;
    }
  } else if (const auto* name = std::get_if<std::string>(&selector.value)) {
    aura_name = *name;
    aura_rank = rank_or_filter.value.value_or("");
    if (filter_arg.value) {
      filter += "|";
      filter += *filter_arg.value;
    }
  } else {
    return openwow::ui::lua::NoLuaResults{};
  }
  BuildUnitAuraTooltipImpl(tooltip, session, *unit.value, aura_index, aura_name,
                           aura_rank, filter);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipUnitDebuff(
    TooltipSystem& tooltip, TooltipLuaString unit, TooltipLuaValue selector,
    TooltipLuaString rank_or_filter, TooltipLuaString filter_arg) {
  if (!unit.value || std::holds_alternative<std::monostate>(selector.value)) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetUnitDebuff(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])"};
  }
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) return openwow::ui::lua::NoLuaResults{};
  int aura_index = 0;
  std::string aura_name;
  std::string aura_rank;
  std::string filter = "HARMFUL";
  if (const auto* index = std::get_if<double>(&selector.value)) {
    aura_index = static_cast<int>(*index);
    if (aura_index < 1) return openwow::ui::lua::NoLuaResults{};
    if (rank_or_filter.value) {
      filter += "|";
      filter += *rank_or_filter.value;
    }
  } else if (const auto* name = std::get_if<std::string>(&selector.value)) {
    aura_name = *name;
    aura_rank = rank_or_filter.value.value_or("");
    if (filter_arg.value) {
      filter += "|";
      filter += *filter_arg.value;
    }
  } else {
    return openwow::ui::lua::NoLuaResults{};
  }
  BuildUnitAuraTooltipImpl(tooltip, session, *unit.value, aura_index, aura_name,
                           aura_rank, filter);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipInventoryResult SetTooltipInventoryItem(
    TooltipSystem& tooltip, TooltipLuaString unit, TooltipLuaValue slot_argument,
    const std::optional<openwow::ui::lua::LuaTruthy> name_only) {
  if (!unit.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetInventoryItem(unit, slot [, nameOnly])"};
  }
  int slot = 0;
  if (!ResolveTooltipInventorySlot(tooltip, slot_argument, slot)) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid inventory slot in SetInventoryItem"};
  }
  const TooltipItemDisplayOptions display_options{
      .name_only =
          name_only.value_or(openwow::ui::lua::LuaTruthy{}).value,
  };
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{}, 0.0);
  }
  const auto owner_guid = ResolveUnitId(session, *unit.value);
  if (owner_guid.IsEmpty()) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{}, 0.0);
  }

  const auto *local_player = session->objects().GetLocalPlayerTyped();
  if (local_player == nullptr) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{}, 0.0);
  }

  if (slot == -1) {
    if (owner_guid != local_player->GetGuid()) {
      return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                          openwow::ui::lua::LuaNil{});
    }
    const auto ammo_item_id = local_player->GetUInt32(PLAYER_AMMO_ID);
    if (ammo_item_id == 0) {
      return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                          openwow::ui::lua::LuaNil{});
    }
    tooltip.SetItemFromLoot(ammo_item_id, 0, 0, 0, 0, display_options);
    return openwow::ui::lua::LuaReturns(1.0, openwow::ui::lua::LuaNil{});
  }

  if (owner_guid == local_player->GetGuid()) {
    const auto *item = GetTooltipLocalInventoryItem(*session, slot);
    if (item == nullptr || item->GetEntry() == 0) {
      if (slot >= static_cast<int>(openwow::game::InventorySlots::kEquipStart) &&
          slot < static_cast<int>(openwow::game::InventorySlots::kEquipEnd)) {
        const auto visible = local_player->GetVisibleEquipSlotInfo(
            static_cast<std::uint8_t>(slot));
        if (visible.has_value() && visible->item_id != 0) {
          openwow::ui::TooltipItemInstanceData visible_instance;
          visible_instance.permanent_enchant_id = visible->enchant_id;
          (void)tooltip.SetItemWithInstanceData(
              visible->item_id, 0, visible->suffix_factor,
              visible_instance, ResolveTooltipPlayerLevel(tooltip), 0,
              display_options);
          return openwow::ui::lua::LuaReturns(
              1.0, openwow::ui::lua::LuaNil{}, 0.0);
        }
      }
      return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                          openwow::ui::lua::LuaNil{}, 0.0);
    }

    openwow::ui::TooltipItemInstanceData instance_data;
    instance_data.permanent_enchant_id = item->GetEnchantId(
        static_cast<std::uint8_t>(openwow::game::EnchantmentSlot::Permanent));
    instance_data.gem_item_ids[0] = ResolveTooltipGemId(
        tooltip, item->GetEnchantId(
            static_cast<std::uint8_t>(openwow::game::EnchantmentSlot::Socket1)));
    instance_data.gem_item_ids[1] = ResolveTooltipGemId(
        tooltip, item->GetEnchantId(
            static_cast<std::uint8_t>(openwow::game::EnchantmentSlot::Socket2)));
    instance_data.gem_item_ids[2] = ResolveTooltipGemId(
        tooltip, item->GetEnchantId(
            static_cast<std::uint8_t>(openwow::game::EnchantmentSlot::Socket3)));
    instance_data.durability = item->GetDurability();
    instance_data.max_durability = item->GetMaxDurability();
    instance_data.runtime_flags = item->GetItemFlags();
    instance_data.item_guid = item->GetGuid().GetRawValue();
    instance_data.remaining_duration_seconds =
        item->GetRemainingDurationSeconds();
    instance_data.create_played_time = item->GetCreatePlayedTime();
    instance_data.creator_guid = item->GetCreator().GetRawValue();
    for (std::size_t spell_index = 0;
         spell_index < instance_data.spell_charges.size(); ++spell_index) {
      instance_data.spell_charges[spell_index] = item->GetSpellCharges(
          static_cast<std::uint8_t>(spell_index));
    }
    instance_data.locked = item->IsLocked();
    instance_data.live_item = true;

    const bool render_success = tooltip.SetItemWithInstanceData(
        item->GetEntry(), item->GetRandomPropertiesId(), item->GetItemSuffixFactor(),
        instance_data, ResolveTooltipPlayerLevel(tooltip),
        item->GetGuid().GetRawValue(), display_options);

    return openwow::ui::lua::LuaReturns(
        1.0, openwow::ui::lua::LuaTruthy{render_success},
        static_cast<double>(item->GetRepairCost()));
  }

  if (owner_guid.GetRawValue() != session->arena().inspect_target_guid() ||
      slot < static_cast<int>(openwow::game::InventorySlots::kEquipStart) ||
      slot >= static_cast<int>(openwow::game::InventorySlots::kEquipEnd)) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{}, 0.0);
  }

  const auto *inspect_player = session->objects().GetPlayer(owner_guid);
  if (inspect_player == nullptr) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{}, 0.0);
  }

  const auto &inspect_equipment = session->inspect().inspect_equipment();
  if (inspect_equipment.player == owner_guid) {
    const auto &inspected = inspect_equipment.slots[static_cast<std::size_t>(slot)];
    if (inspected.present && inspected.item_id != 0) {
      openwow::ui::TooltipItemInstanceData instance_data;
      instance_data.permanent_enchant_id = inspected.detail_values[
          static_cast<std::size_t>(openwow::game::EnchantmentSlot::Permanent)];
      instance_data.gem_item_ids[0] = ResolveTooltipGemId(
          tooltip, inspected.detail_values[static_cast<std::size_t>(
                 openwow::game::EnchantmentSlot::Socket1)]);
      instance_data.gem_item_ids[1] = ResolveTooltipGemId(
          tooltip, inspected.detail_values[static_cast<std::size_t>(
                 openwow::game::EnchantmentSlot::Socket2)]);
      instance_data.gem_item_ids[2] = ResolveTooltipGemId(
          tooltip, inspected.detail_values[static_cast<std::size_t>(
                 openwow::game::EnchantmentSlot::Socket3)]);
      instance_data.item_guid = inspected.item_guid.GetRawValue();
      const auto random_property_id = static_cast<std::int16_t>(
          inspected.trailing_value);
      (void)tooltip.SetItemWithInstanceData(
          inspected.item_id, random_property_id, inspected.trailing_u32,
          instance_data, ResolveTooltipPlayerLevel(tooltip),
          inspected.item_guid.GetRawValue(), display_options);
      return openwow::ui::lua::LuaReturns(
          1.0, openwow::ui::lua::LuaNil{}, 0.0);
    }
  }

  const auto visible = inspect_player->GetVisibleEquipSlotInfo(
      static_cast<std::uint8_t>(slot));
  if (!visible.has_value() || visible->item_id == 0) {
    return openwow::ui::lua::LuaReturns(openwow::ui::lua::LuaNil{},
                                        openwow::ui::lua::LuaNil{}, 0.0);
  }

  openwow::ui::TooltipItemInstanceData visible_instance;
  visible_instance.permanent_enchant_id = visible->enchant_id;
  (void)tooltip.SetItemWithInstanceData(
      visible->item_id, 0, visible->suffix_factor, visible_instance,
      ResolveTooltipPlayerLevel(tooltip), 0, display_options);
  return openwow::ui::lua::LuaReturns(
      1.0, openwow::ui::lua::LuaNil{}, 0.0);
}

TooltipBagResult SetTooltipBagItem(TooltipSystem& tooltip,
                                   TooltipLuaValue bag,
                                   TooltipLuaValue slot) {
  auto *session = tooltip.GetWorldSession();
  const auto* bag_number = std::get_if<double>(&bag.value);
  const auto* slot_number = std::get_if<double>(&slot.value);
  if (session == nullptr || bag_number == nullptr || slot_number == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto bag_index = static_cast<int>(*bag_number);
  const auto zero_based_slot = static_cast<int>(*slot_number) - 1;
  const auto resolved = ResolveBagTooltipItem(*session, bag_index, zero_based_slot);
  if (resolved.status == BagTooltipLookupStatus::kNoReturn) {
    return openwow::ui::lua::NoLuaResults{};
  }

  if (resolved.status == BagTooltipLookupStatus::kNilReturn || resolved.item == nullptr) {
    return openwow::ui::lua::LuaNil{};
  }

  const bool render_success = CanSynchronouslyRenderBagItemTooltip(*session, *resolved.item);
  tooltip.SetItemFromLoot(resolved.item->entry, resolved.item->random_property,
                          resolved.item->random_suffix, 0, resolved.item->guid);

  return openwow::ui::lua::LuaReturns(
      openwow::ui::lua::LuaTruthy{render_success},
      ResolveBagItemRepairCost(tooltip, *session, *resolved.item));
}

openwow::ui::lua::LuaTruthy SetTooltipAuctionSellItem(TooltipSystem& tooltip) {
  auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return {false};
  }
  const auto selection = session->auction().state().GetSellItemSelection();
  const auto *item =
      session->inventory_replica().FindItemByGuid(selection.item_guid);
  return {SetTooltipFromItemInstance(tooltip, item)};
}

TooltipVoidResult SetTooltipAuctionItem(TooltipSystem& tooltip,
                                        TooltipLuaString list,
                                        TooltipLuaValue index) {
  const auto* number = std::get_if<double>(&index.value);
  if (!list.value || number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetAuctionItem(\"type\", index)"};
  }
  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto item = ResolveAuctionTooltipItem(
      *session, list.value->c_str(), static_cast<int>(*number));
  if (item.has_value()) {
    SetTooltipFromAuctionItem(tooltip, *item);
  }
  return openwow::ui::lua::NoLuaResults{};
}

constexpr std::uint32_t kMaxSendMailAttachmentSlot = 0xFu;

openwow::ui::lua::LuaTruthy SetTooltipSendMailItem(
    TooltipSystem& tooltip, TooltipLuaValue slot_argument) {
  std::uint32_t slot = 0;
  if (const auto* number = std::get_if<double>(&slot_argument.value)) {
    slot = openwow::ui::SaturateLuaNumberToU32(*number - 1.0);
    if (slot > kMaxSendMailAttachmentSlot) {
      return {false};
    }
  }

  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return {false};
  }
  const auto draft = session->mail().compose().GetDraft();
  for (const auto &attachment : draft.attachments) {
    if (attachment.slot == slot) {
      if (const auto *item =
              session->inventory_replica().FindItemByGuid(attachment.item_guid);
          SetTooltipFromItemInstance(tooltip, item)) {
        return {true};
      }
      return {SetTooltipFromMailAttachment(tooltip, attachment)};
    }
  }
  return {false};
}

TooltipInboxResult SetTooltipInboxItem(TooltipSystem& tooltip,
                                       TooltipLuaValue message,
                                       TooltipLuaValue attachment_argument) {
  const auto* message_number = std::get_if<double>(&message.value);
  if (message_number == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetInboxItem(messageIndex, attachmentIndex)"};
  }
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::LuaNil{};
  }
  const auto mail_index = static_cast<int>(*message_number) - 1;
  const auto *mail = mail_index >= 0 ? session->mail().GetInboxMail(static_cast<std::size_t>(mail_index))
                                     : nullptr;
  if (mail == nullptr) {
    return openwow::ui::lua::LuaNil{};
  }

  std::uint32_t attachment_slot = 0xFFFFFFFFu;
  if (const auto* number = std::get_if<double>(&attachment_argument.value)) {
    const auto attachment_index = static_cast<int>(*number);
    if (attachment_index != 0) {
      if (attachment_index < 1 ||
          attachment_index > openwow::game::kMaxInboxAttachmentSlots) {
        return openwow::ui::lua::LuaNil{};
      }
      attachment_slot = static_cast<std::uint32_t>(attachment_index - 1);
    }
  }

  const auto *attachment = session->mail().GetMailItem(*mail, attachment_slot);
  if (attachment == nullptr || attachment->item_entry == 0u) {
    return openwow::ui::lua::LuaNil{};
  }

  openwow::ui::TooltipItemInstanceData instance_data;
  instance_data.permanent_enchant_id =
      attachment->enchants[static_cast<std::size_t>(openwow::game::EnchantmentSlot::Permanent)]
          .enchant_id;
  instance_data.gem_item_ids[0] = ResolveTooltipGemId(
      tooltip, attachment->enchants[static_cast<std::size_t>(openwow::game::EnchantmentSlot::Socket1)]
             .enchant_id);
  instance_data.gem_item_ids[1] = ResolveTooltipGemId(
      tooltip, attachment->enchants[static_cast<std::size_t>(openwow::game::EnchantmentSlot::Socket2)]
             .enchant_id);
  instance_data.gem_item_ids[2] = ResolveTooltipGemId(
      tooltip, attachment->enchants[static_cast<std::size_t>(openwow::game::EnchantmentSlot::Socket3)]
             .enchant_id);
  instance_data.durability = attachment->durability;
  instance_data.max_durability = attachment->max_durability;
  instance_data.spell_charges[0] =
      static_cast<std::int32_t>(attachment->spell_charges);
  instance_data.live_item = true;

  std::uint32_t player_level = 0;
  if (const auto *player = session->objects().GetLocalPlayerTyped(); player != nullptr) {
    player_level = player->State().GetLevel();
  }

  const bool render_success = tooltip.SetItemWithInstanceData(
      attachment->item_entry, attachment->random_property_id, attachment->suffix_factor,
      instance_data, player_level);
  return openwow::ui::lua::LuaTruthy{render_success};
}

}

}

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
LuaConverter<openwow::ui::game::detail::TooltipLuaValue, Policy>::Storage
LuaConverter<openwow::ui::game::detail::TooltipLuaValue, Policy>::Read(
    lua_State* state, const int index) {
  Storage result;
  if (index <= lua_gettop(state) && lua_isnil(state, index) == 0) {
    if (lua_isnumber(state, index) != 0) {
      result.value = static_cast<double>(lua_tonumber(state, index));
    } else if (lua_isstring(state, index) != 0) {
      std::size_t size = 0;
      const char* value = lua_tolstring(state, index, &size);
      result.value = std::string(value, size);
    }
  }
  return result;
}

template <ConversionPolicy Policy>
LuaConverter<openwow::ui::game::detail::TooltipLuaString, Policy>::Storage
LuaConverter<openwow::ui::game::detail::TooltipLuaString, Policy>::Read(
    lua_State* state, const int index) {
  Storage result;
  if (index <= lua_gettop(state) && lua_type(state, index) == LUA_TSTRING) {
    std::size_t size = 0;
    const char* value = lua_tolstring(state, index, &size);
    result.value = std::string(value, size);
  }
  return result;
}

template struct LuaConverter<openwow::ui::game::detail::TooltipLuaValue,
                             openwow::ui::game::detail::kTooltipLuaConversion>;
template struct LuaConverter<openwow::ui::game::detail::TooltipLuaString,
                             openwow::ui::game::detail::kTooltipLuaConversion>;

}

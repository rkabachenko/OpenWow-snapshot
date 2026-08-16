#include "openwow/ui/game/tooltip_lua_adapter.h"
#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_formatter.h"
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {
using namespace tooltip_internal;

namespace tooltip_internal {

namespace {

constexpr std::uint32_t kTradeSkillSpellAttribute = 0x20u;

struct ItemTooltipTarget {
  std::uint32_t item_id = 0;
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
  std::uint64_t item_guid = 0;
};

void SetItemTooltip(TooltipSystem& tooltip, const ItemTooltipTarget target) {
  tooltip.SetItemFromLoot(target.item_id, target.random_property_id,
                          target.suffix_factor, 0, target.item_guid);
}

std::uint32_t ResolveGemItemId(const TooltipSystem& tooltip,
                               const std::uint32_t enchant_id) {
  const auto* dbc = tooltip.GetDbcLoader();
  const auto* enchant =
      dbc != nullptr && enchant_id != 0
          ? dbc->spell_item_enchantment().LookupEntry(enchant_id)
          : nullptr;
  return enchant != nullptr ? enchant->gem_id : 0;
}

bool SetItemTooltip(TooltipSystem& tooltip,
                    const openwow::game::ItemInstance* item) {
  if (item == nullptr || item->entry == 0) {
    return false;
  }

  openwow::ui::TooltipItemInstanceData instance_data;
  instance_data.permanent_enchant_id = item->GetPermanentEnchant();
  instance_data.gem_item_ids[0] = ResolveGemItemId(tooltip, item->GetSocketEnchant(0));
  instance_data.gem_item_ids[1] = ResolveGemItemId(tooltip, item->GetSocketEnchant(1));
  instance_data.gem_item_ids[2] = ResolveGemItemId(tooltip, item->GetSocketEnchant(2));
  instance_data.durability = item->durability;
  instance_data.max_durability = item->max_durability;
  instance_data.runtime_flags = item->flags;
  instance_data.item_guid = item->guid;
  instance_data.remaining_duration_seconds = item->duration;
  instance_data.create_played_time = item->create_played_time;
  instance_data.creator_guid = item->creator_guid;
  instance_data.spell_charges = item->charges;
  const auto* session = tooltip.GetWorldSession();
  const auto* live_item =
      session != nullptr
          ? session->objects().GetItem(openwow::game::ObjectGuid(item->guid))
          : nullptr;
  instance_data.locked = live_item != nullptr && live_item->IsLocked();
  instance_data.live_item = true;
  const auto* player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  const std::uint32_t player_level =
      player != nullptr ? player->State().GetLevel() : 0u;
  return tooltip.SetItemWithInstanceData(
      item->entry, item->random_property, item->random_suffix, instance_data,
      player_level, item->guid);
}

}

const char *ResolveTooltipObjectName(lua_State *L) {
  lua_getfield(L, 1, "__ow_name");
  const char *name = lua_tostring(L, -1);
  lua_settop(L, -2);
  return (name != nullptr && name[0] != '\0') ? name : "<unnamed>";
}

std::string ResolvePetActionTooltipText(
    const openwow::game::PetActionButton &action) {
  switch (action.ActionKind()) {
    case 6:
      switch (action.ActionId()) {
        case 0:
          return GetTooltipString("PET_MODE_PASSIVE");
        case 1:
          return GetTooltipString("PET_MODE_DEFENSIVE");
        case 2:
          return GetTooltipString("PET_MODE_AGGRESSIVE");
        default:
          return {};
      }
    case 7:
      switch (action.ActionId()) {
        case 0:
          return GetTooltipString("PET_ACTION_WAIT");
        case 1:
          return GetTooltipString("PET_ACTION_FOLLOW");
        case 2:
          return GetTooltipString("PET_ACTION_ATTACK");
        case 3:
          return GetTooltipString("PET_ACTION_DISMISS");
        default:
          return {};
      }
    default:
      return {};
  }
}

detail::TooltipTruthyResult BuildPetActionTooltip(
    TooltipSystem& tooltip_system,
    const openwow::game::PetActionButton& action) {
  TooltipSystem::ScopedActivation activation(tooltip_system);
  if (action.raw == 0) {
    return openwow::ui::lua::LuaNil{};
  }

  if (openwow::game::IsPetSpellActionKind(action.ActionKind())) {
    tooltip_system.SetSpellById(action.ActionId());
    if (tooltip_system.GetNumLines() == 0) {
      return openwow::ui::lua::LuaNil{};
    }

    return openwow::ui::lua::LuaTruthy{true};
  }

  const auto tooltip_text = ResolvePetActionTooltipText(action);
  if (tooltip_text.empty()) {
    return openwow::ui::lua::LuaNil{};
  }

  tooltip_system.AddLine(tooltip_text);
  tooltip_system.Show();
  return openwow::ui::lua::LuaNil{};
}

std::string ResolveTotemTooltipName(const TooltipSystem& tooltip,
                                    const std::uint32_t spell_id) {
  const auto* dbc = tooltip.GetDbcLoader();
  if (const auto *spell = dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
      spell != nullptr && !std::string_view(spell->spell_name).empty()) {
    return std::string(spell->spell_name);
  }

  return {};
}
std::string FormatTotemRemainingText(const std::uint32_t remaining_ms) {
  const std::string duration = openwow::game::FormatRoundedSpellDurationText(remaining_ms);
  auto &localization = openwow::game::Localization::Get();
  const std::string format =
      localization.GetString("SPELL_TIME_REMAINING", "SPELL_TIME_REMAINING");
  return localization.FormatString(format, {duration});
}

}

namespace detail {
TooltipVoidResult SetTooltipHyperlink(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue link_arg) {
  if (std::holds_alternative<std::monostate>(link_arg.value) ||
      lua_isstring(lua.get(), 2) == 0) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetHyperlink(link)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  std::size_t link_size = 0;
  const char* link_data = lua_tolstring(lua.get(), 2, &link_size);
  const std::string link_value(link_data, link_size);
  const char *const link = link_value.c_str();

  std::string_view payload(link);
  if (const auto hyperlink_start = payload.find("|H");
      hyperlink_start != std::string_view::npos) {
    const auto hyperlink_end = payload.find("|h", hyperlink_start + 2);
    if (hyperlink_end != std::string_view::npos) {
      payload = payload.substr(hyperlink_start + 2,
                               hyperlink_end - hyperlink_start - 2);
    }
  }

  static constexpr std::array<std::string_view, 10> kLinkPrefixes = {
      "item:", "enchant:", "dance:", "spell:", "unit:",
      "quest:", "talent:", "trade:", "achievement:", "glyph:",
  };
  std::string_view matched_prefix;
  std::size_t matched_position = std::string_view::npos;
  for (const auto prefix : kLinkPrefixes) {
    const auto position = payload.find(prefix);
    if (position != std::string_view::npos) {
      matched_prefix = prefix;
      matched_position = position;
      break;
    }
  }
  if (matched_position == std::string_view::npos) {
    return openwow::ui::lua::LuaUsageError{
        std::string(ResolveTooltipObjectName(lua.get())) +
        ":SetHyperlink(): Unknown link type"};
  }
  payload.remove_prefix(matched_position);

  std::vector<std::string_view> fields;
  for (std::size_t start = 0; start <= payload.size();) {
    const auto delimiter = payload.find(':', start);
    fields.push_back(payload.substr(
        start, delimiter == std::string_view::npos ? std::string_view::npos
                                                   : delimiter - start));
    if (delimiter == std::string_view::npos) {
      break;
    }
    start = delimiter + 1;
  }
  const auto field = [&](const std::size_t index) {
    return index < fields.size() ? fields[index] : std::string_view{};
  };
  const auto parse_u32 = [](const std::string_view text) {
    const std::string owned(text);
    return static_cast<std::uint32_t>(
        std::strtoull(owned.c_str(), nullptr, 10));
  };
  const auto parse_i32 = [](const std::string_view text) {
    const std::string owned(text);
    return static_cast<std::int32_t>(
        std::strtoll(owned.c_str(), nullptr, 10));
  };
  const auto parse_hex_u64 = [](const std::string_view text) {
    const std::string owned(text);
    return static_cast<std::uint64_t>(
        std::strtoull(owned.c_str(), nullptr, 16));
  };

  const auto id = parse_u32(field(1));

  if (matched_prefix == "item:") {
    if (id == 0u) {
      return openwow::ui::lua::NoLuaResults{};
    }
    if (tooltip.IsShown() && tooltip.GetItemId() == id) {
      tooltip.Hide();
      return openwow::ui::lua::NoLuaResults{};
    }

    openwow::ui::TooltipItemInstanceData instance_data;
    instance_data.permanent_enchant_id = parse_u32(field(2));
    instance_data.gem_item_ids[0] = ResolveGemItemId(tooltip, parse_u32(field(3)));
    instance_data.gem_item_ids[1] = ResolveGemItemId(tooltip, parse_u32(field(4)));
    instance_data.gem_item_ids[2] = ResolveGemItemId(tooltip, parse_u32(field(5)));
    const auto random_property_id = parse_i32(field(7));
    const auto suffix_factor = parse_u32(field(8));
    const auto player_level = parse_u32(field(9));
    (void)tooltip.SetItemWithInstanceData(id, random_property_id, suffix_factor,
                                          instance_data, player_level);

    openwow::game::HyperlinkInfo parsed;
    if (openwow::game::HyperlinkParser::Parse(link, parsed) &&
        !parsed.display_text.empty()) {
      tooltip.OverrideItemIdentity(parsed.display_text, link);
    }
  } else if (matched_prefix == "enchant:") {
    const auto *dbc = tooltip.GetDbcLoader();
    const auto *spell = dbc != nullptr ? dbc->spell().LookupEntry(id) : nullptr;
    if (spell != nullptr &&
        (spell->attributes & kTradeSkillSpellAttribute) != 0u) {
      if (tooltip.IsShown() && tooltip.GetSpellId() == id) {
        tooltip.Hide();
      } else {
        tooltip.SetSpellById(id);
      }
    }
  } else if (matched_prefix == "dance:") {
    if (auto *session = tooltip.GetWorldSession();
        session != nullptr && id != 0u) {
      session->dance_studio().RequestDanceFromCache(
          openwow::game::DanceId{id});
    }
  } else if (matched_prefix == "spell:") {
    if (id != 0u) {
      if (tooltip.IsShown() && tooltip.GetSpellId() == id) {
        tooltip.Hide();
      } else {
        tooltip.SetSpellById(id);
      }
    }
  } else if (matched_prefix == "unit:") {
    const auto guid = parse_hex_u64(field(1));
    if (tooltip.IsShown() && tooltip.GetUnitGuid() == guid) {
      tooltip.Hide();
      return openwow::ui::lua::NoLuaResults{};
    }
    auto *session = tooltip.GetWorldSession();
    const auto *unit = session != nullptr
                           ? session->objects().GetUnit(openwow::game::ObjectGuid(guid))
                           : nullptr;
    if (unit != nullptr) {
      (void)BuildUnitTooltipForUnit(tooltip, *unit, &session->objects(),
                                   session->GetDbcLoader(), false);
    }
  } else if (matched_prefix == "quest:") {
    if (tooltip.IsShown() && tooltip.GetQuestId() == id) {
      tooltip.Hide();
      return openwow::ui::lua::NoLuaResults{};
    }
    auto *session = tooltip.GetWorldSession();
    if (!BuildQuestTooltipFromSession(session, id)) {
      BuildQuestTooltip(tooltip, id);
    }
  } else if (matched_prefix == "talent:") {
    const std::uint32_t talent_id = id;
    auto &store = openwow::game::TalentInfoStore::Get();
    const auto lookup = FindTalentDefinition(talent_id, false, false);
    const auto owned_talent = store.FindTalentDefinitionByID(talent_id);
    if (lookup.talent != nullptr || owned_talent.has_value()) {
      const bool has_explicit_rank = fields.size() > 2;
      const int explicit_rank = has_explicit_rank
                                    ? static_cast<int>(parse_u32(field(2)))
                                    : -1;
      const auto *talent = lookup.talent != nullptr ? lookup.talent : &*owned_talent;
      BuildTalentTooltip({
          .tooltip = tooltip,
          .talent = *talent,
          .explicit_rank = has_explicit_rank ? std::optional<int>(explicit_rank) : std::nullopt,
      });
    }
  } else if (matched_prefix == "trade:") {
    detail::HandleTradeSkillHyperlink(lua.get(), link);
  } else if (matched_prefix == "achievement:") {
    if (tooltip.IsShown() && tooltip.GetAchievementId() == id) {
      tooltip.Hide();
      return openwow::ui::lua::NoLuaResults{};
    }
    const auto player_guid = parse_hex_u64(field(2));
    const auto completed = parse_u32(field(3)) != 0u;
    std::array<std::uint32_t, 8> criteria_data{};
    criteria_data[3] = parse_u32(field(5));
    criteria_data[4] = parse_u32(field(4));
    criteria_data[5] = parse_u32(field(6));
    std::array<std::uint32_t, 4> criteria_mask{};
    for (std::size_t index = 0; index < criteria_mask.size(); ++index) {
      criteria_mask[index] = parse_u32(field(7 + index));
    }
    BuildAchievementTooltip({
        .tooltip = tooltip,
        .achievement_id = id,
        .player_guid = player_guid,
        .completed = completed,
        .criteria_data = criteria_data,
        .criteria_mask = criteria_mask,
    });
  } else if (matched_prefix == "glyph:") {
    if (fields.size() > 2) {
      BuildGlyphTooltip(tooltip, id, parse_u32(field(2)), true, true);
    }
  }

  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipLootItem(TooltipSystem& tooltip,
                                     TooltipLuaValue slot) {
  const auto* slot_value = std::get_if<double>(&slot.value);
  if (slot_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid loot slot in SetInventoryItem"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto ui_slot = static_cast<int>(*slot_value);
  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto item = ResolveLootTooltipItem(session->loot(), ui_slot);
  if (!item.has_value() || item->item_id == 0) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetItemTooltip(tooltip, {item->item_id, item->random_property_id,
                           item->suffix_factor});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipLootRollItem(TooltipSystem& tooltip,
                                         const openwow::ui::lua::RawLuaState lua,
                                         TooltipLuaValue roll_argument) {
  const auto* roll_value = std::get_if<double>(&roll_argument.value);
  if (roll_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetLootRollItem(id)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto roll_id =
      openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(
          *roll_value);
  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto roll = session->loot().state().GetPendingRoll(roll_id);
  if (!roll.has_value() || roll->item_id == 0) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetItemTooltip(tooltip, {roll->item_id, roll->random_property_id,
                           roll->random_suffix});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipCurrencyToken(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index) {
  const auto* index_value = std::get_if<double>(&index.value);
  if (index_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetCurrencyToken(index)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto zero_based_index =
      openwow::ui::SaturateLuaNumberToU32(*index_value) - 1u;
  if (zero_based_index > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto *entry =
      openwow::game::CurrencySystem::Get().GetCurrencyListEntryByIndex(
          static_cast<int>(zero_based_index));

  if (entry == nullptr || entry->isHeader) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto& inventory = session->inventory_replica();
  for (std::uint16_t slot = openwow::game::InventorySlots::kCurrencyStart;
       slot < openwow::game::InventorySlots::kCurrencyEnd; ++slot) {
    const auto* item = inventory.GetItemInSlot(static_cast<std::uint8_t>(slot));
    if (item != nullptr && item->entry == entry->itemId) {
      (void)SetItemTooltip(tooltip, item);
      return openwow::ui::lua::NoLuaResults{};
    }
  }

  SetItemTooltip(tooltip, {entry->itemId});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipBackpackToken(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index) {
  const auto* index_value = std::get_if<double>(&index.value);
  if (index_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetBackpackToken(index)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto zero_based_index =
      openwow::ui::SaturateLuaNumberToU32(*index_value) - 1u;
  if (zero_based_index > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto* objects = tooltip.GetObjectManager();
  if (objects == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto* currency = openwow::game::CurrencySystem::Get().GetBackpackCurrencyEntry(
      *objects, static_cast<int>(zero_based_index));
  if (currency == nullptr || currency->itemId == 0) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto& inventory = session->inventory_replica();
  for (std::uint16_t slot = openwow::game::InventorySlots::kCurrencyStart;
       slot < openwow::game::InventorySlots::kCurrencyEnd; ++slot) {
    const auto* item = inventory.GetItemInSlot(static_cast<std::uint8_t>(slot));
    if (item != nullptr && item->entry == currency->itemId) {
      (void)SetItemTooltip(tooltip, item);
      return openwow::ui::lua::NoLuaResults{};
    }
  }

  SetItemTooltip(tooltip, {currency->itemId});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipQuestLogSpecialItem(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index) {
  const auto* index_value = std::get_if<double>(&index.value);
  if (index_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetQuestLogSpecialItem(index)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto quest_log_index = TruncateLuaNumberToWrappedLowU32(*index_value);
  const auto *item = ResolveQuestLogSpecialItem(*session, quest_log_index);
  if (item == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetTooltipFromQuestSpecialItem(*session, *item);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipLFGDungeonReward(TooltipSystem& tooltip,
                                             TooltipLuaValue dungeon_id_arg,
                                             TooltipLuaValue loot_index) {
  const auto* dungeon_id_value = std::get_if<double>(&dungeon_id_arg.value);
  const auto* loot_index_value = std::get_if<double>(&loot_index.value);
  if (dungeon_id_value == nullptr || loot_index_value == nullptr) {

    return openwow::ui::lua::LuaUsageError{
        "Usage: Tooltip:SetLFGDungeonReward(dungeonID, lootIndex)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  if (session == nullptr || dbc == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto dungeon_id = openwow::ui::TruncateLuaNumberToI32(*dungeon_id_value);
  const auto reward_one_based = openwow::ui::TruncateLuaNumberToI32(*loot_index_value);
  const auto reward_index = static_cast<std::uint32_t>(reward_one_based) - 1u;
  if (dungeon_id < 0 || reward_index > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto *dungeon = dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(dungeon_id));
  if (dungeon == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto packed_dungeon_id = (dungeon->id & 0x00FFFFFFu) | (dungeon->type_id << 24);
  const auto *reward =
      session->lfg().FindPlayerDungeonRewardItemByIndex(packed_dungeon_id,
                                                        static_cast<std::size_t>(reward_index));
  if (reward == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetItemTooltip(tooltip, {reward->item_id});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipLFGCompletionReward(TooltipSystem& tooltip,
                                                TooltipLuaValue loot_index) {
  const auto* loot_index_value = std::get_if<double>(&loot_index.value);
  if (loot_index_value == nullptr) {

    return openwow::ui::lua::LuaUsageError{
        "Usage: Tooltip:SetLFGCompletionReward(lootIndex)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto reward_index = static_cast<lua_Integer>(*loot_index_value) - 1;
  if (reward_index < 0) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto *reward = session->lfg().FindCompletionRewardItemByIndex(
      static_cast<std::size_t>(reward_index));
  if (reward == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetItemTooltip(tooltip, {reward->item_id});
  return openwow::ui::lua::NoLuaResults{};
}

openwow::ui::lua::NoLuaResults SetTooltipQuestRewardSpell(
    TooltipSystem& tooltip) {
  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return {};
  }
  const auto &quests = session->quests();
  std::uint32_t spell_id = 0;
  if (quests.has_active_reward()) {
    spell_id = quests.active_reward().rew_spell;
  } else if (quests.has_active_details()) {
    spell_id = quests.active_details().rew_spell;
  }
  if (spell_id != 0) {
    tooltip.SetSpellById(spell_id);
  }
  return {};
}

TooltipVoidResult SetTooltipQuestItem(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue type, TooltipLuaValue index) {
  const auto* index_value = std::get_if<double>(&index.value);
  if (std::holds_alternative<std::monostate>(type.value) ||
      lua_isstring(lua.get(), 2) == 0 || index_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid quest item in SetQuestItem(\"type\", index)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  std::size_t type_size = 0;
  const char* type_data = lua_tolstring(lua.get(), 2, &type_size);
  const std::string item_type(type_data, type_size);
  const auto item_index = static_cast<int>(std::trunc(*index_value));
  const auto item = GetQuestPreviewItem(session, item_type, item_index);
  if (!item.has_value() || item->item_id == 0) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid quest item in SetQuestItem(\"type\", index)"};
  }

  (void)GetOrRequestQuestPreviewItemTemplate(session, item->item_id);
  SetItemTooltip(tooltip, {item->item_id});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipBuybackItem(TooltipSystem& tooltip,
                                         TooltipLuaValue slot) {
  const auto* slot_value = std::get_if<double>(&slot.value);
  if (slot_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: GameTooltip:SetBuybackItem(slot)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto *player = session->objects().GetActivePlayer();
  if (player == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  auto &inventory = session->inventory_replica();

  struct BuybackSlotRef {
    const ::openwow::game::ItemInstance *item;
    std::uint32_t price;
  };
  std::vector<BuybackSlotRef> visible;
  visible.reserve(::openwow::game::PlayerInventoryReplica::kBuybackSlots);

  for (std::uint8_t buyback_slot = 0;
       buyback_slot < ::openwow::game::PlayerInventoryReplica::kBuybackSlots;
       ++buyback_slot) {
    const auto *item = inventory.GetBuybackSlot(buyback_slot);
    if (item == nullptr || item->IsEmpty()) {
      continue;
    }
    const auto price = player->GetUInt32(static_cast<std::uint16_t>(
        ::openwow::game::PLAYER_FIELD_BUYBACK_PRICE_1 + buyback_slot));
    if (price == 0) {
      continue;
    }
    visible.push_back({item, price});
  }

  std::sort(visible.begin(), visible.end(),
            [](const BuybackSlotRef &a, const BuybackSlotRef &b) {
              return a.price < b.price;
            });

  const auto one_based =
      static_cast<std::size_t>(std::trunc(*slot_value));
  if (one_based < 1 || one_based > visible.size()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto *item = visible[one_based - 1].item;
  if (item == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetItemTooltip(tooltip, {item->entry, item->random_property,
                           item->random_suffix, item->guid});
  return openwow::ui::lua::NoLuaResults{};
}

openwow::ui::lua::NoLuaResults SetTooltipSocketedItem(
    TooltipSystem& tooltip) {
  TooltipSystem::ScopedActivation activation(tooltip);
  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr || !session->item_interactions().socket().has_value()) {
    return {};
  }

  const auto *item =
      session->inventory_replica().FindItemByGuid(
          session->item_interactions().socket()->item.GetRawValue());
  (void)SetItemTooltip(tooltip, item);
  return {};
}

openwow::ui::lua::NoLuaResults SetTooltipExistingSocketGem(
    TooltipSystem& tooltip, TooltipLuaValue index,
    std::optional<openwow::ui::lua::LuaTruthy> socket_to_destroy) {
  const auto* index_value = std::get_if<double>(&index.value);
  if (index_value == nullptr) {
    return {};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto socket_index =
      openwow::ui::SaturateLuaNumberToU32(*index_value) - 1u;
  if (socket_index >= 3u) {
    return {};
  }

  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr || !session->item_interactions().socket().has_value()) {
    return {};
  }

  const auto *item =
      session->inventory_replica().FindItemByGuid(
          session->item_interactions().socket()->item.GetRawValue());
  if (item == nullptr || item->entry == 0) {
    return {};
  }

  const auto gem_item_id =
      ResolveGemItemId(tooltip, item->GetSocketEnchant(static_cast<std::uint8_t>(socket_index)));
  if (gem_item_id != 0) {
    tooltip.SetItemFromLoot(
        gem_item_id, 0, 0, 0, 0,
        TooltipItemDisplayOptions{
            .socket_to_destroy = socket_to_destroy.value_or(
                openwow::ui::lua::LuaTruthy{}).value,
        });
  }
  return {};
}

TooltipVoidResult SetTooltipSocketGem(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index) {
  const auto* index_value = std::get_if<double>(&index.value);
  if (index_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetSocketGem(index)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto socket_index =
      openwow::ui::SaturateLuaNumberToU32(*index_value) - 1u;
  if (socket_index >= 3u) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr || !session->item_interactions().socket().has_value()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto& pending =
      session->item_interactions().socket()->pending_gems[socket_index];
  if (!pending.has_value() || pending->item_id == 0) {
    return openwow::ui::lua::NoLuaResults{};
  }

  SetItemTooltip(tooltip, {pending->item_id});
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipGuildBankItem(TooltipSystem& tooltip,
                                           TooltipLuaValue tab_arg,
                                           TooltipLuaValue slot_arg) {
  const auto* tab_value = std::get_if<double>(&tab_arg.value);
  const auto* slot_value = std::get_if<double>(&slot_arg.value);
  if (tab_value == nullptr || slot_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        "Invalid tab or slot in SetGuildBankItem"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto tab = openwow::ui::TruncateLuaNumberToI32(*tab_value - 1.0);
  const auto slot = openwow::ui::TruncateLuaNumberToI32(*slot_value - 1.0);
  if (tab < 0 || slot < 0) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto *item = openwow::game::GuildSystem::Get().GetGuildBankTabItemSlotState(
      static_cast<std::uint32_t>(tab), static_cast<std::uint32_t>(slot));
  (void)SetItemTooltip(tooltip, item);
  return openwow::ui::lua::NoLuaResults{};
}

TooltipVoidResult SetTooltipEquipmentSet(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue set_name) {
  if (std::holds_alternative<std::monostate>(set_name.value) ||
      lua_isstring(lua.get(), 2) == 0) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetEquipmentSet(\"setName\")"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto* session = tooltip.GetWorldSession();
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  std::size_t set_name_size = 0;
  const char* set_name_data = lua_tolstring(lua.get(), 2, &set_name_size);
  const auto* set =
      session->equipment().find(std::string(set_name_data, set_name_size));
  if (set != nullptr) {
    BuildEquipmentSetTooltip(tooltip, set->id);
  }
  return openwow::ui::lua::NoLuaResults{};
}

openwow::ui::lua::NoLuaResults SetTooltipTalent(
    TooltipSystem& tooltip, const double tab_index_arg,
    const double talent_index_arg,
    const std::optional<openwow::ui::lua::LuaTruthy> is_inspect_arg,
    const std::optional<openwow::ui::lua::LuaTruthy> is_pet_arg,
    TooltipLuaValue group_index_arg,
    const std::optional<openwow::ui::lua::LuaTruthy> is_preview_arg) {
  TooltipSystem::ScopedActivation activation(tooltip);
  const auto* const session = tooltip.GetWorldSession();
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return {};
  }

  const auto tab_index =
      openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(tab_index_arg) - 1u;
  const auto talent_index =
      openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(talent_index_arg) - 1u;
  const bool is_inspect = is_inspect_arg.value_or(
      openwow::ui::lua::LuaTruthy{}).value;
  const bool is_pet = is_pet_arg.value_or(
      openwow::ui::lua::LuaTruthy{}).value;
  std::optional<std::uint32_t> raw_group_arg;
  if (const auto* group_index =
          std::get_if<double>(&group_index_arg.value)) {
    raw_group_arg =
        openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(
            *group_index);
  }
  const bool is_preview = is_preview_arg.value_or(
      openwow::ui::lua::LuaTruthy{}).value;

  auto &store = openwow::game::TalentInfoStore::Get();
  const auto *tab = store.GetTalentTabArray(tab_index, is_inspect, is_pet);
  if (tab == nullptr || talent_index >= tab->talents.size()) {
    return {};
  }

  const auto group_index = store.GetGroupIndexArg(raw_group_arg);
  const auto *group = store.GetTalentGroupData(group_index, is_inspect, is_pet);
  BuildTalentTooltip({
      .tooltip = tooltip,
      .talent = tab->talents[talent_index],
      .group = group,
      .inspect = is_inspect,
      .is_pet = is_pet,
      .preview = is_preview,
  });
  return {};
}

TooltipTruthyResult SetTooltipAction(TooltipSystem& tooltip,
                                     const double slot) {
  TooltipSystem::ScopedActivation activation(tooltip);
  const std::size_t slot_index =
      static_cast<std::size_t>(
          openwow::ui::game::detail::TruncateLuaNumberToWrappedLowU32(
              slot) -
          1u);
  tooltip.ClearLines();

  auto *session = tooltip.GetWorldSession();
  if (session == nullptr || slot_index >= openwow::game::ActionAssignmentRuntime::kMaxActionButtons) {
    return openwow::ui::lua::LuaNil{};
  }

  const auto button =
      openwow::ui::game::detail::GetResolvedActionButton(*session, slot_index);
  if (button.IsEmpty()) {
    return openwow::ui::lua::LuaNil{};
  }

  const auto *dbc = session->GetDbcLoader();
  const auto resolved_spell_id =
      openwow::ui::game::detail::ResolveSpellLikeActionIdForValidation(
          *session, button, slot_index);
  if (resolved_spell_id != 0u &&
      openwow::game::SpellHasAttackActionEffect(resolved_spell_id, dbc)) {
    tooltip.AddLine(GetTooltipString("ATTACK"), 1.0f, 1.0f, 1.0f);
    tooltip.Show();
    return openwow::ui::lua::LuaTruthy{true};
  }

  const auto build_item_action = [&](const std::uint32_t item_id) {
    const auto *item_template = session->item_definitions().GetItem(item_id);
    if (item_template == nullptr) {
      tooltip.SetItemById(item_id);
      item_template = session->item_definitions().GetItem(item_id);
    }
    if (const auto *item = openwow::game::FindActionBarInventoryItemByEntry(
            session->inventory_replica(), item_id, item_template, dbc);
        item != nullptr) {
      return SetItemTooltip(tooltip, item);
    }
    if (tooltip.GetItemId() != item_id) {
      tooltip.SetItemById(item_id);
    }
    return tooltip.GetNumLines() != 0;
  };

  bool built = false;
  switch (button.type) {
    case openwow::game::ActionPresentationKind::kItem:
      built = build_item_action(button.action);
      break;
    case openwow::game::ActionPresentationKind::kSpell:
    case openwow::game::ActionPresentationKind::kCompanion:
      if (resolved_spell_id != 0u) {
        tooltip.SetSpellById(resolved_spell_id);
        built = tooltip.GetNumLines() != 0;
      }
      break;
    case openwow::game::ActionPresentationKind::kMacro:
    case openwow::game::ActionPresentationKind::kCompanionMacro: {
      const auto macro =
          session != nullptr
              ? session->macros().FindMacro(
                    ::openwow::game::actions::macros::MacroId(button.action))
              : std::nullopt;
      if (!macro) {
        break;
      }
      if (macro->resolved_item_id != 0u) {
        built = build_item_action(macro->resolved_item_id);
      } else if (macro->resolved_spell_id > 0) {
        tooltip.SetSpellById(
            static_cast<std::uint32_t>(macro->resolved_spell_id));
        built = tooltip.GetNumLines() != 0;
      } else if (!macro->name.empty()) {
        tooltip.AddLine(macro->name, 1.0f, 1.0f, 1.0f);
        tooltip.Show();
        built = true;
      }
      break;
    }
    case openwow::game::ActionPresentationKind::kEquipmentSet:
      built = BuildEquipmentSetTooltip(tooltip, button.action) != 0;
      break;
    case openwow::game::ActionPresentationKind::kPet: {
      std::size_t pet_slot_index = 0;
      if (!openwow::ui::game::detail::ResolvePetActionBarSlotIndex(
              *session, slot_index, &pet_slot_index)) {
        break;
      }
      return BuildPetActionTooltip(
          tooltip, session->pet().pet_bar().action_bar[pet_slot_index]);
    }
    case openwow::game::ActionPresentationKind::kClick:
      break;
  }

  return openwow::ui::lua::LuaTruthy{built};
}

TooltipTruthyResult SetTooltipPetAction(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue slot) {
  const auto* slot_value = std::get_if<double>(&slot.value);
  if (slot_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetPetAction(slot)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto slot_index = static_cast<int>(std::trunc(*slot_value)) - 1;
  tooltip.ClearLines();

  auto *session = tooltip.GetWorldSession();
  if (session == nullptr || slot_index < 0 ||
      slot_index >= kPetActionBarSlotCount) {
    return openwow::ui::lua::LuaNil{};
  }

  return BuildPetActionTooltip(
      tooltip, session->pet().pet_bar().action_bar[
                   static_cast<std::size_t>(slot_index)]);
}

TooltipVoidResult SetTooltipTotem(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue slot_arg) {
  const auto* slot_value = std::get_if<double>(&slot_arg.value);
  if (slot_value == nullptr) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetTotem(slot)"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  const auto converted_slot = openwow::ui::TruncateLuaNumberToI32(*slot_value);
  const auto slot_index = static_cast<std::uint32_t>(converted_slot) - 1u;
  auto *session = tooltip.GetWorldSession();
  if (session == nullptr || slot_index >= 4u) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto slot =
      session->spell_book().GetTotemSlot(static_cast<std::uint8_t>(slot_index));
  if (!slot.has_value() || !slot->has_totem()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto spell_name = ResolveTotemTooltipName(tooltip, slot->spell_id);
  if (spell_name.empty()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  tooltip.ClearLines();
  tooltip.AddLine(spell_name, kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);
  tooltip.AddLine(FormatTotemRemainingText(
      slot->RemainingTimeMs(session->CurrentClientTimeMs())));
  return openwow::ui::lua::NoLuaResults{};
}

TooltipTruthyResult SetTooltipHyperlinkCompareItem(
    TooltipSystem& tooltip, const openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue link_arg,
    TooltipLuaValue offset_arg,
    std::optional<openwow::ui::lua::LuaTruthy> shift_button) {
  if (std::holds_alternative<std::monostate>(link_arg.value) ||
      lua_isstring(lua.get(), 2) == 0) {
    return openwow::ui::lua::LuaUsageError{
        std::string("Usage: ") + ResolveTooltipObjectName(lua.get()) +
        ":SetHyperlinkCompareItem(\"hyperlink\" [, offset, shiftButton])"};
  }

  TooltipSystem::ScopedActivation activation(tooltip);
  std::size_t link_size = 0;
  const char* link_data = lua_tolstring(lua.get(), 2, &link_size);
  const std::string link_value(link_data, link_size);
  const char *link = link_value.c_str();
  if (link[0] == '\0') {
    return openwow::ui::lua::LuaNil{};
  }

  const auto parsed = openwow::game::ItemLinkParser::Parse(link);
  if (!parsed.has_value() || parsed->itemId == 0) {
    return openwow::ui::lua::LuaNil{};
  }

  const auto* session = tooltip.GetWorldSession();
  const auto *item_template = session != nullptr
                                  ? session->item_definitions().GetItem(parsed->itemId)
                                  : nullptr;
  if (!item_template) {
    return openwow::ui::lua::LuaNil{};
  }

  int offset = 0;
  if (const auto* offset_value = std::get_if<double>(&offset_arg.value)) {
    offset = std::max(0, static_cast<int>(*offset_value) - 1);
  }

  [[maybe_unused]] const bool shift_button_value =
      shift_button.value_or(openwow::ui::lua::LuaTruthy{}).value;

  static constexpr uint32_t kInvTypeToSlotMask[] = {
      0x00000000,
      0x00000001,
      0x00000002,
      0x00000004,
      0x00000008,
      0x00000010,
      0x00000020,
      0x00000040,
      0x00000080,
      0x00000100,
      0x00000200,
      0x00000C00,
      0x00003000,
      0x00018000,
      0x00010000,
      0x00020000,
      0x00004000,
      0x00018000,
      0x00780000,
      0x00040000,
      0x00000010,
      0x00008000,
      0x00010000,
      0x00010000,
      0x00000000,
      0x00020000,
      0x00020000,
      0x00000000,
      0x00020000,
  };

  const auto inv_type_idx =
      static_cast<uint32_t>(item_template->inventory_type);
  if (inv_type_idx >= std::size(kInvTypeToSlotMask)) {
    return openwow::ui::lua::LuaNil{};
  }

  const uint32_t slot_mask = kInvTypeToSlotMask[inv_type_idx];
  if (slot_mask == 0) {
    return openwow::ui::lua::LuaNil{};
  }

  auto &inventory = session->inventory_replica();

  constexpr uint32_t kMainHandBit = 0x8000u;
  constexpr uint32_t kOffHandBit = 0x10000u;
  constexpr uint8_t kMainHandSlot = 15;
  constexpr uint8_t kOffHandSlot = 16;

  if ((slot_mask & kMainHandBit) != 0 || (slot_mask & kOffHandBit) != 0) {
    const bool fits_both =
        ((slot_mask & kMainHandBit) != 0 && (slot_mask & kOffHandBit) != 0) ||
        inv_type_idx == 17;

    if (fits_both) {
      const auto *mh_item = inventory.GetEquipSlot(kMainHandSlot);
      const auto *oh_item = inventory.GetEquipSlot(kOffHandSlot);
      const bool both_equipped =
          mh_item != nullptr && oh_item != nullptr &&
          mh_item->entry != 0 && oh_item->entry != 0 &&
          mh_item->entry != oh_item->entry;

      if (both_equipped) {
        if (offset == 0) {
          tooltip.SetItemById(oh_item->entry);
        } else {
          tooltip.SetItemById(mh_item->entry);
        }
      } else {
        if (offset != 0) {
          return openwow::ui::lua::LuaNil{};
        }
        const auto *equipped = mh_item;
        if (!equipped || equipped->entry == 0)
          equipped = oh_item;
        if (!equipped || equipped->entry == 0) {
          return openwow::ui::lua::LuaNil{};
        }
        tooltip.SetItemById(equipped->entry);
      }
    } else {
      if (offset != 0) {
        return openwow::ui::lua::LuaNil{};
      }
      const uint8_t target_slot =
          (slot_mask & kMainHandBit) != 0 ? kMainHandSlot : kOffHandSlot;
      const auto *equipped = inventory.GetEquipSlot(target_slot);
      if (!equipped || equipped->entry == 0) {
        const uint8_t alt_slot =
            (target_slot == kMainHandSlot) ? kOffHandSlot : kMainHandSlot;
        equipped = inventory.GetEquipSlot(alt_slot);
        if (!equipped || equipped->entry == 0) {
          return openwow::ui::lua::LuaNil{};
        }
      }
      tooltip.SetItemById(equipped->entry);
    }

    return openwow::ui::lua::LuaTruthy{true};
  }

  int remaining_offset = offset;
  for (uint8_t slot = 0; slot <= 18; ++slot) {
    if (((1u << slot) & slot_mask) == 0)
      continue;

    const auto *equipped = inventory.GetEquipSlot(slot);
    if (!equipped || equipped->entry == 0)
      continue;

    if (remaining_offset == 0) {
      tooltip.SetItemById(equipped->entry);
      return openwow::ui::lua::LuaTruthy{true};
    }
    --remaining_offset;
  }

  return openwow::ui::lua::LuaNil{};
}

}

}

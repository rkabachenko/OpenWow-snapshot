#include "openwow/ui/game/tooltip_formatter.h"
#include "openwow/ui/game/tooltip_runtime.h"
#include "openwow/ui/game/tooltip_internal.h"
#include "openwow/ui/game/tooltip_types.h"

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
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {
using namespace tooltip_internal;

bool ItemTooltip_TryResolveComparisonSlot(
    ItemComparisonContext& ctx,
    int pass,
    uint32_t item_entry,
    uint32_t slot_index,
    const openwow::game::QueryCache& query_cache) {
  const auto* tmpl = query_cache.GetItemTemplate(item_entry);
  if (!tmpl) return false;

  if (static_cast<std::uint32_t>(tmpl->item_class) !=
      ctx.expected_item_class) {
    return false;
  }

  constexpr uint32_t kMaxComparisonSlots = 17;

  if (pass == 0) {
    for (uint32_t i = 0; i < kMaxComparisonSlots; ++i) {
      if (ctx.resolved_ids[i] == 0 && item_entry == ctx.equipped_entries[i]) {
        ctx.resolved_ids[i] = item_entry;
        ctx.slot_resolved[slot_index] = 1;
        return true;
      }
    }
    return false;
  }

  if (ctx.slot_resolved[slot_index]) return false;

  const auto item_inv_type = static_cast<uint32_t>(tmpl->inventory_type);
  for (uint32_t i = 0; i < kMaxComparisonSlots; ++i) {
    if (ctx.resolved_ids[i] == 0 && ctx.equipped_inv_types[i] != 0) {
      uint32_t eq_inv_type = ctx.equipped_inv_types[i];
      bool compatible = (item_inv_type == eq_inv_type) ||
                        (item_inv_type == 5 && eq_inv_type == 20) ||
                        (item_inv_type == 20 && eq_inv_type == 5);
      if (compatible) {
        ctx.resolved_ids[i] = item_entry;
        return true;
      }
    }
  }
  return true;
}
int CGTooltip_ShowItemPending([[maybe_unused]] void *tooltip) {
  return 0;
}

void CGTooltip_AddDeltaDescriptionHeader(void *flag, void *tooltip) {
  auto *header_flag = static_cast<int *>(flag);
  if (header_flag == nullptr || *header_flag == 0) {
    return;
  }

  static constexpr std::uint8_t kGoldenYellow[4] = {0x00, 0xD2, 0xFF, 0xFF};

  CGTooltip_AddTextLine(tooltip, " ", nullptr, kGoldenYellow, kGoldenYellow, 0);

  const std::string header = GetTooltipString("ITEM_DELTA_DESCRIPTION");
  CGTooltip_AddTextLine(tooltip, header.c_str(), nullptr,
                        kGoldenYellow, kGoldenYellow, 1);

  *header_flag = 0;
}

void CGTooltip_AddStatDeltaLine(void *tooltip, int delta,
                                const char *statName, int *needsHeader) {
  if (delta == 0) {
    return;
  }

  CGTooltip_AddDeltaDescriptionHeader(needsHeader, tooltip);

  const char *color = (delta > 0) ? "|cff00ff00" : "|cffff2020";
  const char sign = (delta > 0) ? '+' : '-';
  const int abs_delta = (delta > 0) ? delta : -delta;

  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s%c%d%s %s",
                color, sign, abs_delta, "|r",
                statName ? statName : "");

  static constexpr std::uint8_t kWhiteColor[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  CGTooltip_AddTextLine(tooltip, buf, nullptr, kWhiteColor, kWhiteColor, 0);
}

int Tooltip_BuildItemComparisonFromUnit([[maybe_unused]] void *dest,
                                        [[maybe_unused]] const openwow::game::CGItem_C *item,
                                        [[maybe_unused]] std::uint32_t scalingLevel) {
  if (dest == nullptr || item == nullptr) {
    return 0;
  }

  openwow::game::ItemStatTable stat_table{};
  if (!BuildComparisonStatTableFromItemObject(stat_table, *item, scalingLevel)) {
    return 0;
  }

  std::memcpy(dest, stat_table.data(), sizeof(stat_table));
  return 1;
}

int Tooltip_BuildItemComparisonFromTemplate([[maybe_unused]] void *dest,
                                            [[maybe_unused]] const openwow::game::ItemTemplate *itemTemplate,
                                            [[maybe_unused]] const void *enchantData,
                                            [[maybe_unused]] std::uint32_t scalingLevel) {
  if (dest == nullptr || itemTemplate == nullptr) {
    return 0;
  }

  auto* session = ResolveTooltipWorldSession();
  const auto* player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    return 0;
  }

  openwow::game::ItemStatTable stat_table{};
  if (!BuildComparisonStatTableFromTemplate(
          stat_table, *itemTemplate,
          ReadTooltipComparisonS32(enchantData, kTooltipCompareEnchantRandomPropertyOffset),
          ReadTooltipComparisonU32(enchantData, kTooltipCompareEnchantSuffixOffset),
          scalingLevel, *player)) {
    return 0;
  }

  std::memcpy(dest, stat_table.data(), sizeof(stat_table));
  return 1;
}

int Tooltip_BuildItemStatBlock([[maybe_unused]] void *dest, [[maybe_unused]] const void *tooltipData) {
  if (dest == nullptr || tooltipData == nullptr) {
    return 0;
  }

  const auto item_guid =
      openwow::game::ObjectGuid(ReadTooltipComparisonU64(tooltipData, kTooltipCompareItemGuidOffset));

  if (!item_guid.IsEmpty()) {
    if (const auto* session = ResolveTooltipWorldSession(); session != nullptr) {
      if (const auto* item = session->objects().GetItem(item_guid); item != nullptr) {
        return Tooltip_BuildItemComparisonFromUnit(
            dest, item, ReadTooltipComparisonU32(tooltipData, kTooltipCompareLevelOffset));
      }
    }
  }

  const auto item_entry = ReadTooltipComparisonU32(tooltipData, kTooltipCompareItemEntryOffset);
  if (item_entry == 0u) {
    return 0;
  }

  const auto* session = ResolveTooltipWorldSession();
  const auto* item_template =
      session != nullptr ? session->query_cache().GetItemTemplate(item_entry) : nullptr;
  if (item_template == nullptr) {
    return 0;
  }

  return Tooltip_BuildItemComparisonFromTemplate(
      dest, item_template, tooltipData, ReadTooltipComparisonU32(tooltipData, kTooltipCompareLevelOffset));
}

int ItemTooltip_BuildComparisonData([[maybe_unused]] const void *item1,
                                    [[maybe_unused]] const void *item2,
                                    [[maybe_unused]] char *out) {
  if (item1 == nullptr || item2 == nullptr || out == nullptr) {
    return 0;
  }

  TooltipComparisonCacheKey left_key{};
  TooltipComparisonCacheKey right_key{};
  if (!ResolveTooltipComparisonCacheKey(item1, left_key) ||
      !ResolveTooltipComparisonCacheKey(item2, right_key)) {
    return 0;
  }

  struct CachedComparisonData {
    TooltipComparisonCacheKey left{};
    TooltipComparisonCacheKey right{};
    openwow::game::ItemStatTable delta{};
  };

  static std::array<CachedComparisonData, 2> s_cache{};
  static std::uint32_t s_cache_index = 0u;

  for (const auto& cached : s_cache) {
    if (cached.left.Matches(left_key) && cached.right.Matches(right_key)) {
      std::memcpy(out, cached.delta.data(), sizeof(cached.delta));
      return 1;
    }
  }

  openwow::game::ItemStatTable left_stats{};
  openwow::game::ItemStatTable right_stats{};
  if (!Tooltip_BuildItemStatBlock(left_stats.data(), item1) ||
      !Tooltip_BuildItemStatBlock(right_stats.data(), item2)) {
    return 0;
  }

  const auto slot = 1u - s_cache_index;
  auto& cached = s_cache[slot];
  cached.left = left_key;
  cached.right = right_key;
  openwow::game::BuildItemStatDelta(left_stats, right_stats, cached.delta);
  s_cache_index = slot;

  std::memcpy(out, cached.delta.data(), sizeof(cached.delta));
  return 1;
}

}

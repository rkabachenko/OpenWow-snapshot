#include "openwow/ui/game/tooltip_runtime.h"
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

int CGTooltip_OnItemUpdate([[maybe_unused]] uint64_t guid, [[maybe_unused]] int a2,
                           [[maybe_unused]] int a3, [[maybe_unused]] int a4,
                           [[maybe_unused]] void *tooltip) {
  return 1;
}

int CGTooltip_OnSpellUpdate([[maybe_unused]] uint64_t guid, [[maybe_unused]] int a2,
                            [[maybe_unused]] int a3, [[maybe_unused]] int a4,
                            [[maybe_unused]] void *tooltip) {
  return 1;
}

void CGTooltip_OnGuildQueryResolved([[maybe_unused]] int a1, [[maybe_unused]] int a2,
                                    [[maybe_unused]] void *tooltip, bool loaded) {
  if (!loaded) {
    return;
  }

  auto &ts = ResolveTooltipState(tooltip);
  TooltipSystem::ScopedActivation activation(ts);
  const std::uint64_t unit_guid = ts.GetUnitGuid();
  if (unit_guid == 0) {
    return;
  }

  const auto* const objects = ts.GetObjectManager();
  if (objects == nullptr) {
    return;
  }
  const auto *unit = objects->GetUnit(openwow::game::ObjectGuid(unit_guid));
  if (unit == nullptr) {
    return;
  }
  BuildUnitTooltipForUnit(ts, *unit, objects, ts.GetDbcLoader(), false);
  CGTooltip_FinalizeTooltip(tooltip);
  ts.NotifyContentChanged();
}

void CGTooltip_OnSpellTooltipAsyncItemResolved([[maybe_unused]] int a1,
                                                [[maybe_unused]] int a2,
                                                [[maybe_unused]] void *tooltip,
                                                bool loaded) {
  if (!loaded) {
    return;
  }

  auto &ts = ResolveTooltipState(tooltip);
  TooltipSystem::ScopedActivation activation(ts);

  int count = ts.GetPendingSpellAsyncCount();
  if (count > 0) {
    count--;
    ts.SetPendingSpellAsyncCount(count);
  }

  if (count > 0) {
    return;
  }

  const std::uint32_t spell_id = ts.GetSpellId();
  if (spell_id == 0) {
    return;
  }

  BuildSpellTooltip({.tooltip = ts, .spell_id = spell_id, .show_rank = false});
  CGTooltip_FinalizeTooltip(tooltip);
  ts.NotifyContentChanged();
}

void CGTooltip_OnQuestTemplateResolved([[maybe_unused]] int a1,
                                       [[maybe_unused]] int a2,
                                       [[maybe_unused]] void *tooltip,
                                       bool loaded) {
  if (!loaded) {
    return;
  }

  auto &ts = ResolveTooltipState(tooltip);
  TooltipSystem::ScopedActivation activation(ts);

  const std::uint32_t quest_id = ts.GetQuestId();
  if (quest_id == 0) {
    return;
  }

  BuildQuestTooltip(ts, quest_id);
  CGTooltip_FinalizeTooltip(tooltip);
  ts.NotifyContentChanged();
}

void CGTooltip_OnAchievementNameResolved([[maybe_unused]] int a1,
                                          [[maybe_unused]] int a2,
                                          [[maybe_unused]] void *tooltip,
                                          bool loaded) {
  if (!loaded) {
    return;
  }

  auto &ts = ResolveTooltipState(tooltip);
  TooltipSystem::ScopedActivation activation(ts);

  const std::uint32_t achievement_id = ts.GetAchievementId();
  if (achievement_id == 0) {
    return;
  }

  const std::uint64_t player_guid = ts.GetAchievementPlayerGuid();
  const auto completed = static_cast<int>(ts.GetAchievementCompleted());
  const auto &criteria_data = ts.GetAchievementCriteriaData();
  const auto &criteria_bitmask = ts.GetAchievementCriteriaBitmask();

  BuildAchievementTooltip({
      .tooltip = ts,
      .achievement_id = achievement_id,
      .player_guid = player_guid,
      .completed = completed != 0,
      .criteria_data = criteria_data,
      .criteria_mask = criteria_bitmask,
      .async_rebuild = true,
  });
  CGTooltip_FinalizeTooltip(tooltip);
  ts.NotifyContentChanged();
}

}

#include "openwow/ui/game/tooltip_builders.h"
#include "openwow/ui/game/tooltip_runtime.h"
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

bool BuildAchievementTooltip(const AchievementTooltipRequest& request) {
  auto& tooltip_system = request.tooltip;
  const auto achievementId = request.achievement_id;
  const auto playerGuid = request.player_guid;
  const bool completed = request.completed;
  const auto& criteria_data = request.criteria_data;
  const auto& criteria_mask = request.criteria_mask;
  if (achievementId == 0) {
    return false;
  }

  const auto* const dbc = ResolveTooltipDbcLoader();
  if (dbc == nullptr) {
    return false;
  }
  const auto* const achievement =
      dbc->achievement().LookupEntry(static_cast<std::uint32_t>(achievementId));
  if (achievement == nullptr) {
    return false;
  }

  const auto store_state = [&] {
    tooltip_system.SetAchievementId(static_cast<std::uint32_t>(achievementId));
    tooltip_system.SetAchievementPlayerGuid(playerGuid);
    tooltip_system.SetAchievementCompleted(completed ? 1u : 0u);
    tooltip_system.SetAchievementCriteriaData(criteria_data);
    tooltip_system.SetAchievementCriteriaBitmask(criteria_mask);
  };
  store_state();

  std::string player_name;
  auto* const session = tooltip_system.GetWorldSession();
  if (session != nullptr && playerGuid != 0u) {
    const auto object_guid = openwow::game::ObjectGuid(playerGuid);
    player_name = session->objects().GetPlayerName(object_guid);
    if (player_name.empty()) {
      if (const auto* const cached =
              session->query_cache().GetPlayerName(playerGuid);
          cached != nullptr) {
        player_name = cached->name;
      }
    }

    if (player_name.empty() && !request.async_rebuild &&
        session->query_cache().HasNameQueryDispatcher()) {
      const auto expected_achievement_id =
          static_cast<std::uint32_t>(achievementId);
      const openwow::game::QueryCache::QueryRequestOptions request_options{
          .callback_key = openwow::game::QueryCache::CallbackKey(
              kAchievementNameCallbackFunctionId, expected_achievement_id),
          .dedupe_callbacks = true,
          .callback = tooltip_system.BindAsyncCallback(
              [expected_achievement_id, playerGuid](TooltipSystem& current,
                                                    const bool success) {
                TooltipSystem::ScopedActivation activation(current);
                if (current.GetAchievementId() != expected_achievement_id ||
                    current.GetAchievementPlayerGuid() != playerGuid) {
                  return;
                }
                CGTooltip_OnAchievementNameResolved(0, 0, &current, success);
              }),
      };
      if (session->query_cache().GetOrRequestPlayerName(playerGuid,
                                                        request_options) == nullptr) {
        return true;
      }
      if (const auto* const cached =
              session->query_cache().GetPlayerName(playerGuid);
          cached != nullptr) {
        player_name = cached->name;
      }
    }
  }
  if (player_name.empty()) {
    player_name = GetTooltipString("UNKNOWN");
  }

  tooltip_system.ClearLines();
  store_state();

  const auto expand_achievement_text =
      [&](const std::string_view source, const std::size_t capacity) {
        if (source.empty()) {
          return std::string{};
        }
        std::vector<char> expanded(capacity, '\0');
        openwow::game::BindSpellTextFormatterDbcLoader(dbc);
        openwow::game::SpellTextFormatter::ExpandObjectTextVariables(
            std::string(source).c_str(), expanded.data(),
            static_cast<std::uint32_t>(expanded.size()), playerGuid,
            player_name.data(), static_cast<std::int32_t>(player_name.size() + 1),
            {}, 0, achievementId);
        return std::string(expanded.data());
      };

  tooltip_system.AddLine(expand_achievement_text(achievement->name, 800),
                         1.0f, 1.0f, 1.0f);
  tooltip_system.AddLine(" ", kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);

  auto& localization = openwow::game::Localization::Get();
  const char* const status_key = completed
                                     ? "ACHIEVEMENT_TOOLTIP_COMPLETE"
                                     : "ACHIEVEMENT_TOOLTIP_IN_PROGRESS";
  std::string status_format = GetTooltipString(status_key);
  if (status_format == status_key) {
    status_format = completed
                        ? "Achievement earned by %s on %s/%s/%s"
                        : "Achievement in progress by %s";
  }
  std::vector<std::string> status_arguments = {player_name};
  if (completed) {
    status_arguments.push_back(std::to_string(criteria_data[4]));
    status_arguments.push_back(std::to_string(criteria_data[3]));
    status_arguments.push_back(std::to_string(criteria_data[5]));
  }
  tooltip_system.AddLine(
      localization.FormatString(status_format, status_arguments),
      0.0f, 1.0f, 0.0f);
  tooltip_system.AddLine(" ", kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);
  tooltip_system.AddLine(
      expand_achievement_text(achievement->description, 3000),
      1.0f, 1.0f, 1.0f, true);
  tooltip_system.AddLine(" ", kTooltipGoldR, kTooltipGoldG, kTooltipGoldB);

  const auto* criteria_achievement = achievement;
  if (achievement->ref_achievement != 0u) {
    if (const auto* const referenced =
            dbc->achievement().LookupEntry(achievement->ref_achievement);
        referenced != nullptr) {
      criteria_achievement = referenced;
    }
  }

  const bool has_explicit_criteria_mask =
      std::any_of(criteria_mask.begin(), criteria_mask.end(),
                  [](const std::uint32_t word) {
                    return word != 0xFFFFFFFFu;
                  });
  if (criteria_achievement->count == 0u || has_explicit_criteria_mask) {
    struct PendingCriterionLine {
      std::string text;
      bool complete = false;
    };
    std::optional<PendingCriterionLine> pending_line;
    std::size_t visible_index = 0;

    const auto add_criterion_pair =
        [&](const PendingCriterionLine& left,
            const PendingCriterionLine* const right) {
          const auto left_r = left.complete ? kTooltipCompleteR : kTooltipGrayR;
          const auto left_g = left.complete ? kTooltipCompleteG : kTooltipGrayG;
          const auto left_b = left.complete ? kTooltipCompleteB : kTooltipGrayB;
          if (right == nullptr) {
            tooltip_system.AddLine(left.text, left_r, left_g, left_b);
            return;
          }
          const auto right_r = right->complete ? kTooltipCompleteR : kTooltipGrayR;
          const auto right_g = right->complete ? kTooltipCompleteG : kTooltipGrayG;
          const auto right_b = right->complete ? kTooltipCompleteB : kTooltipGrayB;
          tooltip_system.AddDoubleLine(left.text, right->text,
                                       left_r, left_g, left_b,
                                       right_r, right_g, right_b);
        };

    for (const auto* const criteria :
         GetAchievementTooltipCriteria(*dbc,
                                                   criteria_achievement->id)) {
      if (criteria == nullptr || (criteria->flags & 0x2u) != 0u) {
        continue;
      }

      bool criterion_complete = false;
      if (visible_index < 128u) {
        const auto mask_word = visible_index / 32u;
        const auto mask_bit = visible_index % 32u;
        criterion_complete =
            (criteria_mask[mask_word] & (1u << mask_bit)) != 0u;
      }
      ++visible_index;

      PendingCriterionLine current{
          .text = std::string(criteria->description),
          .complete = criterion_complete,
      };
      if (!pending_line.has_value()) {
        pending_line = std::move(current);
        continue;
      }

      add_criterion_pair(*pending_line, &current);
      pending_line.reset();
    }

    if (pending_line.has_value()) {
      add_criterion_pair(*pending_line, nullptr);
    }
  }

  tooltip_system.Show();
  return true;
}

}

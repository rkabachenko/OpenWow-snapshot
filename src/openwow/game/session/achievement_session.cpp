#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"

#include "openwow/game/world_session.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/console.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/achievements/adapters/protocol/achievement_protocol.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/calendar/adapters/protocol/calendar_date_fields_packed.h"
#include "openwow/game/calendar/calendar_time.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/combat/application/client_control_transition.h"
#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/activities/dance/adapters/protocol/dance_protocol.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/emote_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/faction_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/inventory/loot/adapters/ui/loot_roll_result_presenter.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/minimap_ping.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/reputation_info.h"

#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/taxi_map_frame.h"
#include "openwow/game/taxi_runtime_slice.h"
#include "openwow/game/taxi_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/game/combat/adapters/ui/combo_point_presentation.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/quest_log_interleaved.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/title_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/core/init_subsystems.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

const data::dbc::AchievementCriteriaEntry *
LookupAchievementCriteriaEntry(const data::dbc::DbcLoader &dbc, const std::uint32_t criteria_id) {
  return dbc.achievement_criteria().LookupEntry(criteria_id);
}

const data::dbc::AchievementEntry *LookupAchievementEntry(const data::dbc::DbcLoader &dbc,
                                                          const std::uint32_t achievement_id) {
  return dbc.achievement().LookupEntry(achievement_id);
}

bool TrackedAchievementMatchesCriteriaAchievement(const data::dbc::DbcLoader &dbc,
                                                  const AchievementId tracked_achievement_id,
                                                  const std::uint32_t criteria_achievement_id) {
  if (tracked_achievement_id.value == criteria_achievement_id) {
    return true;
  }

  const auto *tracked_achievement =
      LookupAchievementEntry(dbc, tracked_achievement_id.value);
  return tracked_achievement != nullptr &&
         tracked_achievement->ref_achievement == criteria_achievement_id;
}

}

void WorldSession::HandleAllAchievementData(const net::wotlk::WorldPacket &pkt) {
  achievements_.MarkUiReady();

  const auto snapshot =
      achievement::protocol::DecodeAllAchievementData(pkt.payload);
  if (!snapshot) {
    return;
  }
  achievements_.ReplaceLocalData(*snapshot);

  achievements_.LatchComparisonAchievementPointsReady();

  if (dbc_) {
    for (const auto &progress : achievements_.criteria_load_sequence()) {
      if (progress.flags.Contains(
              CriteriaProgressFlag::kTimedFailureRemovesLocalState)) {
        continue;
      }
      if (const auto achievement_id =
              ResolveStatisticsAchievementForCriteria(*dbc_, progress.criteria_id)) {
        achievements_.RecordRecentUpdatedStat(progress.criteria_id, *achievement_id);
      }
    }
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  if (dbc_) {
    for (const auto &progress : achievements_.criteria_load_sequence()) {
      const auto *criteria_entry =
          LookupAchievementCriteriaEntry(*dbc_, progress.criteria_id.value);
      if (criteria_entry == nullptr) {
        continue;
      }
      if (criteria_entry->timer_time != 0) {
        dispatch.FireGlobalEventWithArgs(
            ui::game::events::TRACKED_ACHIEVEMENT_UPDATE,
            {std::to_string(criteria_entry->achievement_id),
             std::to_string(progress.criteria_id.value),
             std::to_string(progress.elapsed_since_update.value.count()),
             std::to_string(criteria_entry->timer_time)});
      }
    }
  }

  dispatch.FireEvent(ui::game::events::CRITERIA_UPDATE);
  dispatch.FireEvent(ui::game::events::RECEIVED_ACHIEVEMENT_LIST);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "All achievement data: " +
                         std::to_string(achievements_.completed().size()) + " completed, " +
                         std::to_string(achievements_.criteria().size()) + " criteria");
}

void WorldSession::HandleAchievementEarned(const net::wotlk::WorldPacket &pkt) {
  const auto earned =
      achievement::protocol::DecodeAchievementEarned(pkt.payload);
  if (!earned) {
    return;
  }
  achievements_.RecordEarned(*earned, map_runtime_.objects().GetActivePlayerGuid());

  const auto &recorded_earned = achievements_.recent_earned().back();
  if (recorded_earned.flags.value == 0 &&
      recorded_earned.owner_relation ==
          AchievementOwnerRelation::kActivePlayer) {
    TrackedAchievementState::Get().RemoveTrackedAchievement(
        recorded_earned.achievement_id);
    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::ACHIEVEMENT_EARNED,
        {static_cast<int>(recorded_earned.achievement_id.value)});
  }
}

void WorldSession::HandleCriteriaUpdate(const net::wotlk::WorldPacket &pkt) {
  achievements_.MarkUiReady();

  const auto decoded_progress =
      achievement::protocol::DecodeCriteriaUpdate(pkt.payload);
  if (!decoded_progress) {
    return;
  }
  achievements_.ApplyCriteriaProgress(*decoded_progress);

  const auto &last_progress = achievements_.last_criteria_update();
  if (!last_progress.has_value()) {
    return;
  }

  const auto &progress = *last_progress;
  achievements_.LatchComparisonAchievementPointsReady();

  const auto timed_failed = progress.flags.Contains(
      CriteriaProgressFlag::kTimedFailureRemovesLocalState);
  const data::dbc::AchievementCriteriaEntry *criteria_entry = nullptr;
  if (dbc_ != nullptr) {
    criteria_entry =
        LookupAchievementCriteriaEntry(*dbc_, progress.criteria_id.value);
    if (!timed_failed) {
      if (const auto achievement_id =
              ResolveStatisticsAchievementForCriteria(*dbc_, progress.criteria_id)) {
        achievements_.RecordRecentUpdatedStat(progress.criteria_id, *achievement_id);
      }
    }
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireEvent(ui::game::events::CRITERIA_UPDATE);

  if (criteria_entry == nullptr) {
    return;
  }

  const auto criteria_achievement_id = criteria_entry->achievement_id;
  if (criteria_entry->timer_time != 0) {
    dispatch.FireGlobalEventWithArgs(
        ui::game::events::TRACKED_ACHIEVEMENT_UPDATE,
        {std::to_string(criteria_achievement_id),
         std::to_string(progress.criteria_id.value),
         std::to_string(progress.elapsed_since_update.value.count()),
         std::to_string(criteria_entry->timer_time)});
    return;
  }

  for (const auto tracked_achievement_id :
       TrackedAchievementState::Get().GetTrackedAchievements()) {
    if (!TrackedAchievementMatchesCriteriaAchievement(*dbc_, tracked_achievement_id,
                                                      criteria_achievement_id)) {
      continue;
    }

    dispatch.FireGlobalEventWithArgs(ui::game::events::TRACKED_ACHIEVEMENT_UPDATE,
                                     {std::to_string(tracked_achievement_id.value)});
  }
}

void WorldSession::HandleCriteriaDeleted(const net::wotlk::WorldPacket &pkt) {
  const auto criteria_id =
      achievement::protocol::DecodeCriteriaDeleted(pkt.payload);
  if (!criteria_id) {
    return;
  }
  const auto result = achievements_.RemoveCriteria(*criteria_id);
  if (result.outcome != AchievementRemovalOutcome::kRemoved) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::CRITERIA_UPDATE);
}

void WorldSession::HandleServerFirstAchievement(const net::wotlk::WorldPacket &pkt) {
  auto server_first =
      achievement::protocol::DecodeServerFirstAchievement(pkt.payload);
  if (!server_first) {
    return;
  }
  achievements_.RecordServerFirst(std::move(*server_first));
  FormatServerFirstAchievement(objects(), achievements_.server_firsts().back());
}

void WorldSession::HandleInspectAchievements(const net::wotlk::WorldPacket &pkt) {
  auto decoded_inspect =
      achievement::protocol::DecodeInspectAchievements(pkt.payload);
  if (!decoded_inspect) {
    return;
  }
  static_cast<void>(
      achievements_.ApplyInspectResult(std::move(*decoded_inspect)));

  if (achievements_.last_inspect_status() ==
      InspectApplicationStatus::kApplied) {
    const auto &inspect = achievements_.last_inspect();
    if (dbc_) {
      for (const auto &progress : inspect.criteria) {
        if (ResolveStatisticsAchievementForCriteria(*dbc_, progress.criteria_id)) {
          achievements_.RecordRecentUpdatedComparisonStat(progress.criteria_id);
        }
      }
    }
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::INSPECT_ACHIEVEMENT_READY);
}

void WorldSession::HandleAchievementDeleted(const net::wotlk::WorldPacket &pkt) {
  const auto achievement_id =
      achievement::protocol::DecodeAchievementDeleted(pkt.payload);
  if (!achievement_id) {
    return;
  }
  const auto result = achievements_.RemoveAchievement(*achievement_id);
  if (result.outcome != AchievementRemovalOutcome::kRemoved) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(ui::game::events::ACHIEVEMENT_EARNED,
                                                     {static_cast<int>(result.achievement_id.value)});
}

}

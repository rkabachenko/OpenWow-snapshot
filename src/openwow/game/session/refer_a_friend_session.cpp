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
#include "openwow/game/packet_reader.h"
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

bool ReferAFriendFailureNeedsName(const std::uint32_t reason) {
  return reason == 9 || reason == 13;
}

void DisplayReferAFriendFailureMessage(const std::uint32_t reason, const char *target_name) {
  using openwow::ui::game::DisplaySystemMessage;

  switch (reason) {
  case 1:
    DisplaySystemMessage(611);
    return;
  case 2:
    DisplaySystemMessage(612);
    return;
  case 3:
    DisplaySystemMessage(613);
    return;
  case 4:
    DisplaySystemMessage(614);
    return;
  case 5:
    DisplaySystemMessage(615);
    return;
  case 6:
    DisplaySystemMessage(616);
    return;
  case 7:
    DisplaySystemMessage(617, 60);
    return;
  case 8:
    DisplaySystemMessage(199);
    return;
  case 9:
    DisplaySystemMessage(81, target_name);
    return;
  case 10:
    DisplaySystemMessage(618, 60);
    return;
  case 11:
    DisplaySystemMessage(619);
    return;
  case 12:
    DisplaySystemMessage(621);
    return;
  case 13:
    DisplaySystemMessage(620, target_name);
    return;
  default:
    return;
  }
}

std::string ResolveReferAFriendTargetName(const WorldSession &session,
                                          const std::uint64_t target_guid) {
  if (target_guid == 0) {
    return {};
  }

  if (const auto *cached_name = session.query_cache().GetPlayerName(target_guid)) {
    return cached_name->name;
  }

  return session.objects().GetPlayerName(ObjectGuid(target_guid));
}

}

void WorldSession::DisplayReferAFriendFailure(const std::uint32_t reason,
                                              const std::uint64_t target_guid) {
  if (!ReferAFriendFailureNeedsName(reason)) {
    DisplayReferAFriendFailureMessage(reason, nullptr);
    return;
  }

  const std::string target_name = ResolveReferAFriendTargetName(*this, target_guid);
  if (!target_name.empty()) {
    DisplayReferAFriendFailureMessage(reason, target_name.c_str());
    return;
  }
  if (target_guid == 0) {
    return;
  }

  auto& pending = refer_a_friend_runtime_.pending_failures();
  const auto duplicate = std::find_if(
      pending.begin(), pending.end(),
      [target_guid, reason](const ReferAFriendRuntime::PendingFailure& failure) {
        return failure.target_guid == target_guid && failure.reason == reason;
      });
  if (duplicate == pending.end()) {
    pending.push_back({target_guid, reason});
  }
  (void)query_cache_.RequestNameQuery(target_guid);
}

void WorldSession::DisplayReferAFriendFailure(const std::uint32_t reason,
                                              const std::string &target_name) {
  DisplayReferAFriendFailureMessage(reason, target_name.c_str());
}

void WorldSession::RetryPendingReferAFriendFailures(const std::uint64_t guid,
                                                    const bool name_unknown) {
  auto& pending_failures = refer_a_friend_runtime_.pending_failures();
  if (pending_failures.empty()) {
    return;
  }

  const std::string resolved_name =
      name_unknown ? std::string{} : ResolveImmediateChatParticipantName(ObjectGuid(guid));

  pending_failures.erase(
      std::remove_if(
          pending_failures.begin(), pending_failures.end(),
          [guid, name_unknown, &resolved_name](const ReferAFriendRuntime::PendingFailure &pending) {
            if (pending.target_guid != guid) {
              return false;
            }
            if (!name_unknown && !resolved_name.empty()) {
              DisplayReferAFriendFailureMessage(pending.reason, resolved_name.c_str());
            }
            return true;
          }),
      pending_failures.end());
}

void WorldSession::AcceptLevelGrant() {
  if (refer_a_friend_runtime_.pending_level_grant_guid() == 0) {
    return;
  }

  interaction_.SendAcceptLevelGrant(
      refer_a_friend_runtime_.pending_level_grant_guid());
  refer_a_friend_runtime_.ClearLevelGrant();
}

void WorldSession::DeclineLevelGrant() {
  refer_a_friend_runtime_.ClearLevelGrant();
}

void WorldSession::HandleProposeLevelGrant(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleProposeLevelGrant(pkt.payload.data(), pkt.payload.size());
  ui::game::GameUI_InitAsyncCharacterRequest(misc_.propose_level_grant_guid());
}

void WorldSession::HandleReferAFriendExpired(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint64_t expired_guid = 0;
  (void)reader.ReadU64(expired_guid);

  (void)party_stats_.ClearCachedReferAFriendFlag(expired_guid);
  (void)social_.ClearFriendReferAFriendFlag(ObjectGuid(expired_guid));
  ui::game::ScriptEventDispatch::Get().FireFriendListUpdate();
}

void WorldSession::HandleReferAFriendFailure(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleReferAFriendFailure(pkt.payload.data(), pkt.payload.size());
  const auto &failure = misc_.last_raf_failure();
  if (!failure.has_value()) {
    return;
  }

  if (!failure->name.empty()) {
    DisplayReferAFriendFailure(failure->reason, failure->name);
    return;
  }

  DisplayReferAFriendFailure(failure->reason);
}

}

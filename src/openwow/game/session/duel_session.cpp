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
#include <cstdint>
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

constexpr std::uint32_t kDuelCompleteSystemMessageId = 0x149;

}

void WorldSession::HandleDuelRequested(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleDuelRequested(pkt.payload.data(), pkt.payload.size());

  const auto &req = misc_.duel_request();
  ObjectGuid challenger_guid{req.challenger_guid};
  ObjectGuid flag_guid{req.flag_guid};
  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid();

  if (challenger_guid == active_player_guid) {
    duel_.SetFlagGuid(flag_guid);
    ui::game::DisplaySystemMessage(328);
    interaction_.SendDuelAccepted(req.flag_guid);
    return;
  }

  if (social_.IsIgnored(challenger_guid)) {
    duel_.SetFlagGuid(flag_guid);
    interaction_.SendDuelCancelled(req.flag_guid);
    duel_.SetFlagGuid(ObjectGuid{});
    return;
  }

  const std::string challenger_name = map_runtime_.objects().GetPlayerName(challenger_guid);
  duel_.ReceiveChallenge(challenger_guid, challenger_name, flag_guid);
  if (!challenger_name.empty()) {
    ui::game::ScriptEventDispatch::Get().FireDuelRequested(challenger_name);
  }
}

void WorldSession::HandleDuelWinner(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleDuelWinner(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &w = misc_.duel_winner();
  const auto winType = (w.win_type == 0) ? DuelWinType::Knockout : DuelWinType::Retreat;
  duel_.SetWinner(winType, w.winner_name, w.loser_name);

  const std::string announcement = duel_.GetWinnerAnnouncement();
  if (!announcement.empty()) {
    ChatFrame_DisplayMessage(objects(), announcement.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr,
                             nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
  }
}

void WorldSession::HandleDuelComplete(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleDuelComplete(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto duel_complete = duel_.HandleDuelCompletePacket(pkt.payload.data(), pkt.payload.size());

  if (duel_complete.show_system_msg) {
    ui::game::DisplaySystemMessage(kDuelCompleteSystemMessageId);
  }

  if (duel_complete.was_in_duel) {
    ui::game::ScriptEventDispatch::Get().FireDuelFinished();
  }

  if (auto *player = objects().GetActivePlayer(); player != nullptr) {
    player->Animation().RefreshSelectedStandAnimation(*this, 0u, ~0u);
  }
}

void WorldSession::HandleDuelCountdown(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleDuelCountdown(pkt.payload.data(), pkt.payload.size());

  duel_.StartCountdown(misc_.duel_countdown().countdown_ms);
}

void WorldSession::HandleDuelOutOfBounds(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;

  ui::game::ScriptEventDispatch::Get().FireDuelOutOfBounds();
}

void WorldSession::HandleDuelInBounds(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;

  ui::game::ScriptEventDispatch::Get().FireDuelInBounds();
}

}

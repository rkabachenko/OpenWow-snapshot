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

constexpr std::uint32_t kGmTicketResultRefresh = 2;
constexpr std::uint32_t kGmTicketResultSubmitted = 1;
constexpr std::uint32_t kGmTicketResultErrorDuplicate = 3;
constexpr std::uint32_t kGmTicketResultErrorThrottle = 5;
constexpr std::uint32_t kGmTicketResultRefreshAlt = 4;
constexpr std::uint32_t kGmTicketStatusHasTicket = 6;
constexpr std::uint32_t kGmTicketStatusRefresh = 1;
constexpr std::uint32_t kGmTicketStatusClosed = 2;
constexpr std::uint32_t kGmTicketStatusSurvey = 3;
constexpr std::uint32_t kGmTicketStatusRefreshAlt = 4;
constexpr std::uint32_t kGmTicketDeleteSuccess = 9;
constexpr std::size_t kGmResponseLineCount = 4;

void RequestGmTicketSnapshot(WorldSession &session) {
  session.interaction().SendGMTicketGetTicket();
}

void FireGmTicketUpdateEvent() {
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::UPDATE_TICKET);
}

void FireGmTicketUpdateEvent(const GMTicketData &ticket) {
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::UPDATE_TICKET,
      {static_cast<int>(ticket.category), ticket.text,
       static_cast<double>(std::max(ticket.time_since_updated, 0.0f)),
       static_cast<double>(ticket.time_oldest), static_cast<double>(ticket.time_since_updated2),
       static_cast<int>(ticket.escalated), static_cast<int>(ticket.viewed)});
}

void FireGmSurveyDisplayEvent() {
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GMSURVEY_DISPLAY);
}

std::string JoinGmResponseText(const GMResponse &response) {
  std::string combined;
  for (std::size_t index = 0; index < kGmResponseLineCount; ++index) {
    combined += response.response[index];
  }
  return combined;
}

}

void WorldSession::HandleGMTicketSystemStatus(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMTicketSystemStatus(pkt.payload.data(),
                                             pkt.payload.size())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::UPDATE_GM_STATUS,
      {static_cast<int>(gm_ticket_.system_status())});
}

void WorldSession::HandleGMTicketGetTicket(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMTicketGetTicket(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &ticket = gm_ticket_.last_ticket();
  if (!ticket.has_value()) {
    return;
  }

  if (ticket->status != kGmTicketStatusHasTicket) {
    gm_ticket_.ClearActiveTicketState();
    if (ticket->status == 0) {
      ui::game::DisplaySystemMessage(364);
    }
    FireGmTicketUpdateEvent();
    return;
  }

  gm_ticket_.SetActiveTicketId(ticket->id);
  gm_ticket_.ClearActiveResponse();
  FireGmTicketUpdateEvent(*ticket);
}

void WorldSession::HandleGMTicketCreate(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMTicketCreate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  switch (gm_ticket_.ticket_create_result()) {
  case kGmTicketResultSubmitted:
    ui::game::DisplaySystemMessage(361);
    return;
  case kGmTicketResultRefresh:
  case kGmTicketResultRefreshAlt:
    RequestGmTicketSnapshot(*this);
    return;
  case kGmTicketResultErrorDuplicate:
    ui::game::DisplaySystemMessage(362);
    return;
  case kGmTicketResultErrorThrottle:
    ui::game::DisplaySystemMessage(363);
    return;
  default:
    return;
  }
}

void WorldSession::HandleGMTicketStatusUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMTicketStatusUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  switch (gm_ticket_.ticket_status_update()) {
  case kGmTicketStatusRefresh:
  case kGmTicketStatusRefreshAlt:
    RequestGmTicketSnapshot(*this);
    return;
  case kGmTicketStatusClosed:
    gm_ticket_.ClearActiveTicketState();
    FireGmTicketUpdateEvent();
    return;
  case kGmTicketStatusSurvey:
    gm_ticket_.ClearActiveTicketState();
    FireGmSurveyDisplayEvent();
    return;
  default:
    return;
  }
}

void WorldSession::HandleGMResponseReceived(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMResponseReceived(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &response = gm_ticket_.last_gm_response();
  if (!response.has_value()) {
    return;
  }

  gm_ticket_.SetActiveResponse(response->response_id, response->ticket_id);
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::GMRESPONSE_RECEIVED,
      {response->description, JoinGmResponseText(*response)});
}

void WorldSession::HandleGMResponseStatusUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMResponseStatusUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  gm_ticket_.ClearActiveTicketState();
  if (gm_ticket_.gm_response_active() != 0) {
    FireGmSurveyDisplayEvent();
    return;
  }

  FireGmTicketUpdateEvent();
}

void WorldSession::HandleKickReason(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleKickReason(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleGMResponseCreateTicket(const net::wotlk::WorldPacket &pkt) {
  gm_ticket_.HandleGMResponseCreateTicket(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleGMResponseDbError(const net::wotlk::WorldPacket & ) {
  if (!gm_ticket_.HandleGMResponseDbError()) {
    return;
  }
  ui::game::DisplaySystemMessage(673);
}

void WorldSession::HandleGMTicketDeleteTicket(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMTicketDeleteTicket(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (gm_ticket_.ticket_delete_result() == kGmTicketDeleteSuccess) {
    gm_ticket_.ClearActiveTicketState();
    FireGmTicketUpdateEvent();
    return;
  }

  ui::game::DisplaySystemMessage(364);
}

void WorldSession::HandleGMTicketUpdateText(const net::wotlk::WorldPacket &pkt) {
  if (!gm_ticket_.HandleGMTicketUpdateText(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  switch (gm_ticket_.ticket_update_text_result()) {
  case kGmTicketResultSubmitted:
    ui::game::DisplaySystemMessage(361);
    return;
  case kGmTicketResultRefresh:
  case kGmTicketResultRefreshAlt:
    RequestGmTicketSnapshot(*this);
    return;
  case kGmTicketResultErrorDuplicate:
    ui::game::DisplaySystemMessage(362);
    return;
  case kGmTicketResultErrorThrottle:
    ui::game::DisplaySystemMessage(363);
    return;
  default:
    return;
  }
}

}

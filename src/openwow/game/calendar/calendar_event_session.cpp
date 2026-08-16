#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"

#include "openwow/game/world_session.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/localized_format.h"
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

void FireCalendarUiEvent(const char *event_name,
                         std::initializer_list<ui::game::EventArg> args = {}) {
  if (auto *ui = ui::game::runtime::WorldUiRuntimeContext::FromLua(
          ui::frame_script_events::FrameScript_GetLuaStateTyped())) {
    ui->frame_events().dispatcher().FireEventArgs(event_name, args);
  }
}

}

void ClearCalendarActionPending(CalendarSystem &calendar_system) {
  calendar_system.SetActionPending(false);
  FireCalendarUiEvent(ui::game::events::CALENDAR_ACTION_PENDING, {false});
}

namespace {

std::string ResolveCalendarCommandGlobalString(
    const std::string_view localization_key) {
  if (auto *ui = ui::game::runtime::WorldUiRuntimeContext::FromLua(
          ui::frame_script_events::FrameScript_GetLuaStateTyped());
      ui != nullptr && ui->lua_state() != nullptr) {
    auto value = ResolveLocalizedGlobalString(ui->lua_state(), localization_key);
    if (!value.empty()) {
      return value;
    }
  }

  const std::string key(localization_key);
  return Localization::Get().GetString(key, key);
}

void ApplyCalendarCommandError(
    CalendarSystem &calendar_system, const CalendarCommandResult &result,
    const CalendarCommandErrorDisplay &display) {
  std::string message =
      ResolveCalendarCommandGlobalString(display.localization_key);
  std::array<char, 3000> formatted{};

  switch (display.argument) {
  case CalendarCommandErrorArgument::kNone:
    break;
  case CalendarCommandErrorArgument::kPlayerName:
    core::FormatLocalized(formatted.data(), formatted.size(), message.c_str(),
                          result.player_name.c_str());
    message = formatted.data();
    break;
  case CalendarCommandErrorArgument::kNumericLimit:
    core::FormatLocalized(formatted.data(), formatted.size(), message.c_str(),
                          static_cast<int>(display.numeric_limit));
    message = formatted.data();
    break;
  }

  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_ERROR, {message});
  ClearCalendarActionPending(calendar_system);
}

bool CalendarInviteRemovedAlertRefreshesPendingMail(const std::uint8_t status) {
  return status != 0 && status != 9;
}

}

bool IsCalendarPendingInviteVisible(const WorldSession &session, const std::uint64_t sender_guid) {
  return sender_guid != 0 && !session.social().IsIgnored(ObjectGuid(sender_guid));
}

namespace {

void RequestCalendarPendingInviteCount(WorldSession &session) {
  session.Send(net::wotlk::PacketSender::BuildCalendarGetNumPending());
}

bool UpdateOpenCalendarInvite(CalendarSystem &calendar_system, const std::uint64_t event_id,
                              const std::uint64_t invitee_guid, const std::uint8_t invite_status,
                              const std::optional<std::uint32_t> response_time = std::nullopt) {
  if (invitee_guid == 0) {
    return false;
  }

  const auto *context_event = calendar_system.GetContextEvent();
  if (context_event == nullptr || context_event->event_id != event_id) {
    return false;
  }

  return calendar_system.SetInviteStatusForInvitee(event_id, invitee_guid, invite_status,
                                                   response_time);
}

void FinalizeCalendarInviteListUpdate(WorldSession &session, CalendarSystem &calendar_system,
                                      const std::uint64_t event_id) {
  std::uint32_t sort_criterion = 3;
  bool sort_reverse = false;
  if (const auto *context_event = calendar_system.GetContextEvent();
      context_event != nullptr && context_event->event_id == event_id) {
    sort_criterion = context_event->invite_sort_criterion;
    sort_reverse = context_event->invite_sort_reverse;
  }

  calendar_system.SortInvitesByCriterion(event_id, static_cast<int>(sort_criterion), sort_reverse,
                                         &session);
  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
}

std::string ResolveCalendarInviteeName(const WorldSession &session,
                                       const std::uint64_t guid_value) {
  if (guid_value == 0) {
    return {};
  }

  const ObjectGuid guid(guid_value);

  if (const auto *player = session.objects().GetPlayer(guid)) {
    if (auto name = player->ResolveRetailName(session); !name.empty()) {
      return name;
    }
  }
  if (const auto *name_info = session.query_cache().GetPlayerName(guid_value)) {
    return name_info->name;
  }
  if (const auto *name_entry = session.objects().GetNameEntry(guid)) {
    return name_entry->name;
  }
  return {};
}

std::uint8_t ResolveCalendarInviteeClassId(const WorldSession &session,
                                           const std::uint64_t guid_value) {
  if (guid_value == 0) {
    return 0;
  }

  const ObjectGuid guid(guid_value);
  if (const auto *player = session.objects().GetPlayer(guid)) {
    return player->State().GetClass();
  }
  if (const auto *name_info = session.query_cache().GetPlayerName(guid_value)) {
    return name_info->class_id;
  }
  if (const auto *name_entry = session.objects().GetNameEntry(guid)) {
    return name_entry->class_id;
  }
  return 0;
}

void QueueCalendarInviteNameQuery(WorldSession &session, const std::uint64_t guid_value,
                                  std::vector<std::uint64_t> &pending_name_queries) {
  if (guid_value == 0 || !ResolveCalendarInviteeName(session, guid_value).empty()) {
    return;
  }
  if (std::find(pending_name_queries.begin(), pending_name_queries.end(), guid_value) !=
      pending_name_queries.end()) {
    return;
  }

  pending_name_queries.push_back(guid_value);
  if (session.query_cache().RequestNameQuery(guid_value) &&
      !session.query_cache().HasNameQueryDispatcher()) {
    session.Send(QueryCache::BuildNameQuery(guid_value));
  }
}

CalendarInviteLookupCompletionAction
GetCalendarSendEventCompletionAction(const std::uint8_t send_type) {
  if (send_type == 1) {
    return CalendarInviteLookupCompletionAction::kUpdateEventNew;
  }
  if (send_type == 2) {
    return CalendarInviteLookupCompletionAction::kUpdateEventExisting;
  }
  return CalendarInviteLookupCompletionAction::kOpenEvent;
}

void ApplyCalendarInviteList(WorldSession &session) {
  const auto &entries = session.calendar().invite_list_entries();
  auto &calendar_system = CalendarSystem::Get();
  const auto *current_context = calendar_system.GetContextEvent();

  const std::uint64_t event_id = current_context ? current_context->event_id : 0;
  CalendarSystemEvent event{};
  if (const auto *stored_event = calendar_system.GetEvent(event_id)) {
    event = *stored_event;
  } else {
    event.event_id = event_id;
    if (current_context) {
      event.flags = current_context->flags;
    }
  }

  const auto *local_player = session.objects().GetLocalPlayerTyped();
  const std::uint64_t local_player_guid = local_player ? local_player->GetGuid().GetRawValue() : 0;
  const std::uint8_t local_player_level =
      local_player ? static_cast<std::uint8_t>(local_player->State().GetLevel()) : 0;

  std::vector<CalendarSystemInvite> invites;
  invites.reserve(entries.size() + (local_player != nullptr ? 1u : 0u));
  std::vector<std::uint64_t> pending_name_queries;
  pending_name_queries.reserve(entries.size());

  bool found_local_player = false;
  for (const auto &entry : entries) {
    CalendarSystemInvite invite{};
    invite.event_id = event_id;
    invite.invitee_guid = entry.guid.GetRawValue();
    invite.invitee_name = ResolveCalendarInviteeName(session, invite.invitee_guid);
    invite.class_id = ResolveCalendarInviteeClassId(session, invite.invitee_guid);
    invite.level = entry.level;
    if (local_player != nullptr && invite.invitee_guid == local_player_guid) {
      invite.status = 3;
      invite.rank = 2;
      invite.class_id = local_player->State().GetClass();
      found_local_player = true;
    } else if (invite.invitee_name.empty()) {
      QueueCalendarInviteNameQuery(session, invite.invitee_guid, pending_name_queries);
    }
    invites.push_back(std::move(invite));
  }

  if (local_player != nullptr && !found_local_player) {
    CalendarSystemInvite invite{};
    invite.event_id = event_id;
    invite.invitee_guid = local_player_guid;
    invite.invitee_name = local_player->GetName();
    invite.level = local_player_level;
    invite.class_id = local_player->State().GetClass();
    invite.status = 3;
    invite.rank = 2;
    invites.push_back(std::move(invite));
  }

  calendar_system.SetEventDetails(event_id, event, invites);
  calendar_system.ClearPendingInviteListNameQueries();
  calendar_system.SetSelectedInviteId(0);

  if (current_context) {
    auto updated_context = *current_context;
    updated_context.local_edit = true;
    updated_context.invite_sort_criterion = 3;
    updated_context.invite_sort_reverse = false;
    calendar_system.SetContextEvent(updated_context);
  }

  if (pending_name_queries.empty()) {
    calendar_system.SortInvitesByCriterion(event_id, 3, false, &session);
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST, {true});
  } else {
    calendar_system.SetPendingInviteListNameQueries(
        event_id, pending_name_queries,
        CalendarInviteLookupCompletionAction::kUpdateInviteListWithSelection);
  }
}

}

void WorldSession::HandleCalendarSendEvent(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleSendEvent(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &send_event = calendar_.last_send_event();
  if (!send_event) {
    return;
  }

  CalendarSystemEvent event{};
  event.event_id = send_event->event_id;
  event.title = send_event->title;
  event.description = send_event->description;
  event.type = send_event->event_type;
  event.repeat_option = send_event->repeat_option;
  event.dungeon_id = send_event->dungeon_id;
  event.flags = send_event->flags;
  event.guild_id = send_event->guild_id;
  event.time = send_event->event_time;
  event.end_time = send_event->event_time;
  event.creator_guid = send_event->creator.GetRawValue();

  std::vector<CalendarSystemInvite> invites;
  invites.reserve(send_event->invites.size());
  std::vector<std::uint64_t> pending_name_queries;
  pending_name_queries.reserve(send_event->invites.size());

  CalendarContextEventInfo updated_context{};
  const CalendarContextEventInfo *current_context = CalendarSystem::Get().GetContextEvent();
  if (current_context) {
    updated_context = *current_context;
  }

  const std::uint64_t active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  for (const auto &invite : send_event->invites) {
    CalendarSystemInvite record;
    record.invite_id = invite.invite_id;
    record.event_id = send_event->event_id;
    record.invitee_guid = invite.invitee.GetRawValue();
    record.status = invite.status;
    record.rank = invite.rank;
    record.response_time = invite.status_time;
    record.can_moderate = invite.invite_type != 0;
    record.invite_type = invite.invite_type;
    record.invitee_name = ResolveCalendarInviteeName(*this, record.invitee_guid);
    record.class_id = ResolveCalendarInviteeClassId(*this, record.invitee_guid);
    if (record.invitee_name.empty()) {
      QueueCalendarInviteNameQuery(*this, record.invitee_guid, pending_name_queries);
    }
    invites.push_back(record);

    if (record.invitee_guid == active_player_guid) {
      event.self_invite_id = record.invite_id;
      event.invite_type = record.invite_type;
      if (event.creator_guid != 0 && event.creator_guid == active_player_guid) {
        event.invite_mod_status |= 0x04;
      } else if (record.rank != 0) {
        event.invite_mod_status |= 0x02;
      }
      updated_context.self_invite_id = record.invite_id;
      updated_context.invite_status = record.status;
      updated_context.invite_type = record.invite_type;
      updated_context.is_own_event =
          record.invitee_guid != 0 && record.invitee_guid == event.creator_guid;
      updated_context.is_moderator = record.rank != 0;
    }
  }

  if (active_player_guid != 0 && active_player_guid == event.creator_guid) {
    event.invite_mod_status |= 0x04;
    updated_context.is_own_event = true;
  }

  auto &calendar_system = CalendarSystem::Get();
  calendar_system.SetEventDetails(send_event->event_id, event, invites);

  CalendarDateFieldsEx event_time_fields{};
  CalendarPackedTime_UnpackToArray(send_event->event_time, event_time_fields);
  const auto apply_event_time = [&event_time_fields](CalendarContextEventInfo &context) {
    constexpr auto kUnset = std::numeric_limits<std::uint32_t>::max();
    const auto human_component = [=](const std::int32_t value,
                                     const std::uint32_t offset) -> std::uint32_t {
      return value < 0 ? kUnset : static_cast<std::uint32_t>(value) + offset;
    };
    context.month = human_component(event_time_fields.month, 1u);
    context.day = human_component(event_time_fields.day, 1u);
    context.year = human_component(event_time_fields.year, 2000u);
    context.hour = human_component(event_time_fields.hour, 0u);
    context.minute = human_component(event_time_fields.minute, 0u);
    context.weekday = event_time_fields.weekday;
    context.time_flags = event_time_fields.flags;
  };
  if (!current_context || current_context->event_id == send_event->event_id) {
    updated_context.event_id = send_event->event_id;
    updated_context.creator_guid = event.creator_guid;
    updated_context.title = send_event->title;
    updated_context.description = send_event->description;
    updated_context.event_type = send_event->event_type;
    updated_context.repeat_option = send_event->repeat_option;
    updated_context.max_invites = send_event->max_invites;
    updated_context.dungeon_id = send_event->dungeon_id;
    updated_context.flags = send_event->flags;
    apply_event_time(updated_context);
    updated_context.secondary_time_packed = send_event->unk_time;
    updated_context.local_edit = false;
    updated_context.invite_sort_criterion = 3;
    updated_context.invite_sort_reverse = false;
    calendar_system.SetContextEvent(updated_context);
  }

  if (send_event->send_type != 1 && send_event->send_type != 2) {
    CalendarContextEventInfo opened_event{};
    opened_event.event_id = send_event->event_id;
    opened_event.self_invite_id = updated_context.self_invite_id;
    opened_event.creator_guid = event.creator_guid;
    opened_event.event_type = send_event->event_type;
    opened_event.dungeon_id = event.dungeon_id;
    opened_event.flags = send_event->flags;
    opened_event.invite_status = updated_context.invite_status;
    opened_event.is_own_event = updated_context.is_own_event;
    opened_event.is_moderator = updated_context.is_moderator;
    opened_event.sequence_index = event.sequence_index;
    opened_event.sequence_total = event.sequence_total;
    opened_event.map_id = event.map_id;
    apply_event_time(opened_event);
    opened_event.secondary_time_packed = send_event->unk_time;
    calendar_system.SetOpenedEvent(opened_event);
  }

  if (pending_name_queries.empty()) {
    FinalizeCalendarInviteLookupCompletion(
        send_event->event_id, GetCalendarSendEventCompletionAction(send_event->send_type));
  } else {
    calendar_system.SetPendingInviteListNameQueries(
        send_event->event_id, pending_name_queries,
        GetCalendarSendEventCompletionAction(send_event->send_type));
  }
  ClearCalendarActionPending(calendar_system);
}

void WorldSession::FinalizeCalendarInviteLookupCompletion(
    const std::uint64_t event_id, const CalendarInviteLookupCompletionAction completion_action) {
  auto &calendar_system = CalendarSystem::Get();

  std::uint32_t sort_criterion = 3;
  bool sort_reverse = false;
  if (const auto *context = calendar_system.GetContextEvent();
      context != nullptr && context->event_id == event_id) {
    sort_criterion = context->invite_sort_criterion;
    sort_reverse = context->invite_sort_reverse;
  }
  calendar_system.SortInvitesByCriterion(event_id, static_cast<int>(sort_criterion), sort_reverse,
                                         this);

  switch (completion_action) {
  case CalendarInviteLookupCompletionAction::kUpdateInviteList:
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
    return;
  case CalendarInviteLookupCompletionAction::kUpdateInviteListWithSelection:
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST, {true});
    return;
  case CalendarInviteLookupCompletionAction::kUpdateEventNew:
    FireCalendarUiEvent(ui::game::events::CALENDAR_NEW_EVENT, {false});
    return;
  case CalendarInviteLookupCompletionAction::kUpdateEventExisting:
    FireCalendarUiEvent(ui::game::events::CALENDAR_NEW_EVENT, {true});
    return;
  case CalendarInviteLookupCompletionAction::kOpenEvent: {
    std::uint32_t event_flags = 0;
    if (const auto *event = calendar_system.GetEvent(event_id)) {
      event_flags = event->flags;
    } else if (const auto *context = calendar_system.GetContextEvent();
               context != nullptr && context->event_id == event_id) {
      event_flags = context->flags;
    }

    FireCalendarUiEvent(
        ui::game::events::CALENDAR_OPEN_EVENT,
        {CalendarSystem::GetEventTypeString(static_cast<std::uint16_t>(event_flags))});
    return;
  }
  }
}

void WorldSession::HandleCalendarCommandResult(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleCommandResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &result = calendar_.last_command_result();
  const auto display = ResolveCalendarCommandErrorDisplay(result.error);
  if (!display.has_value()) {
    return;
  }

  ApplyCalendarCommandError(CalendarSystem::Get(), result, *display);
}

void WorldSession::HandleCalendarEventInvite(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInvite(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &invite = calendar_.last_invite();
  auto &calendar_system = CalendarSystem::Get();
  const auto invitee_guid = invite.invitee.GetRawValue();
  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();

  bool fire_update_event_list = false;
  bool fire_update_invite_list = false;
  const auto *current_context = calendar_system.GetContextEvent();
  const bool context_matches_event =
      current_context != nullptr && current_context->event_id == invite.event_id;
  if (invite.invite_type == 1 && context_matches_event &&
      calendar_system.SetEventSelfInviteState(invite.event_id, invite.invite_id, invite.status)) {
    fire_update_event_list = true;
  }

  if (invitee_guid != 0 && invitee_guid == active_player_guid) {
    if (context_matches_event) {
      auto updated_context = *current_context;
      updated_context.self_invite_id = invite.invite_id;
      updated_context.invite_status = invite.status;
      calendar_system.SetContextEvent(updated_context);
    }
    if (const auto *opened_event = calendar_system.GetOpenedEvent();
        opened_event != nullptr && opened_event->event_id == invite.event_id) {
      auto updated_opened_event = *opened_event;
      updated_opened_event.self_invite_id = invite.invite_id;
      updated_opened_event.invite_status = invite.status;
      calendar_system.SetOpenedEvent(updated_opened_event);
    }
  }

  if (context_matches_event) {
    CalendarSystemInvite invite_record{};
    invite_record.invite_id = invite.invite_id;
    invite_record.event_id = invite.event_id;
    invite_record.invitee_guid = invitee_guid;
    invite_record.invitee_name = ResolveCalendarInviteeName(*this, invitee_guid);
    invite_record.class_id = ResolveCalendarInviteeClassId(*this, invitee_guid);
    invite_record.status = invite.status;
    invite_record.response_time = invite.invite_type == 1 ? invite.response_time : 0;
    invite_record.level = invite.level;
    invite_record.invite_type = invite.invite_type;
    if (invitee_guid == active_player_guid && invite.invite_type != 1) {
      invite_record.rank = 2;
    }
    calendar_system.UpsertEventInvite(invite_record);

    std::vector<std::uint64_t> pending_name_queries;
    pending_name_queries.reserve(1);
    QueueCalendarInviteNameQuery(*this, invitee_guid, pending_name_queries);
    for (const auto guid_value : pending_name_queries) {
      calendar_system.TrackPendingInviteListNameQuery(
          invite.event_id, guid_value, CalendarInviteLookupCompletionAction::kUpdateInviteList);
    }

    if (!calendar_system.HasPendingInviteListNameQueries(invite.event_id)) {
      fire_update_invite_list = true;
    }
  }

  if (fire_update_event_list) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }
  if (fire_update_invite_list) {
    FinalizeCalendarInviteListUpdate(*this, calendar_system, invite.event_id);
  }
  if (invite.pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarEventInviteAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInviteAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &alert = calendar_.last_invite_alert();
  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  if (active_player_guid == 0) {
    return;
  }

  CalendarSystemEvent event{};
  event.event_id = alert.event_id;
  event.title = alert.title;
  event.type = static_cast<std::uint8_t>(alert.type);
  event.time = alert.event_time;
  event.end_time = alert.event_time;
  event.flags = alert.flags;
  event.dungeon_id = alert.dungeon_id;
  event.creator_guid = alert.creator.GetRawValue();
  event.self_invite_id = alert.invite_id;
  event.invite_status = alert.status;
  event.pending_sender_guid = alert.sender.GetRawValue();
  if (alert.rank != 0) {
    event.invite_mod_status |= 0x02u;
  }
  if ((event.creator_guid != 0 && event.creator_guid == active_player_guid) || alert.rank == 2) {
    event.invite_mod_status |= 0x04u;
  }

  auto &calendar_system = CalendarSystem::Get();
  calendar_system.UpsertEventSummary(event);
  SyncCalendarEventAlarm(event);

  const bool is_pending_invite = alert.status == 0 && (alert.flags & 0x40u) == 0;
  const bool pending_visible = IsCalendarPendingInviteVisible(*this, alert.sender.GetRawValue());
  if (is_pending_invite && calendar_system.AddPendingInviteForEvent(
                               alert.event_id, alert.sender.GetRawValue(), pending_visible)) {
    if (pending_visible) {
      FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
    }
  }

  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
}

void WorldSession::HandleCalendarEventStatus(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventStatus(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &status = calendar_.last_event_status();
  auto &calendar_system = CalendarSystem::Get();
  const auto invitee_guid = status.invitee.GetRawValue();
  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  const bool is_active_player_invite = invitee_guid != 0 && invitee_guid == active_player_guid;

  bool fire_update_invite_list = false;
  bool fire_update_event_list = false;
  bool fire_update_pending_invites = false;
  if (const auto *event = calendar_system.GetEvent(status.event_id); event != nullptr) {
    if (is_active_player_invite) {
      const bool is_locked = (event->flags & 0x10u) != 0;
      calendar_system.SetEventInviteStatus(status.event_id, status.status);
      if (!is_locked) {
        fire_update_pending_invites =
            status.status == 0
                ? calendar_system.AddPendingInviteForEvent(
                      status.event_id, event->pending_sender_guid,
                      IsCalendarPendingInviteVisible(*this, event->pending_sender_guid))
                : calendar_system.RemovePendingInvitesForEvent(status.event_id);
      }
      fire_update_event_list = true;
    }

    fire_update_invite_list = UpdateOpenCalendarInvite(
        calendar_system, status.event_id, invitee_guid, status.status,
        status.status_time != 0 ? std::optional<std::uint32_t>{status.status_time} : std::nullopt);
  }

  if (fire_update_pending_invites) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }
  if (fire_update_event_list) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }
  if (fire_update_invite_list) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }
  if (status.pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarRaidLockoutAdded(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleRaidLockoutAdded(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &lockout = calendar_.last_lockout_added();
  auto &calendar_system = CalendarSystem::Get();
  calendar_system.SyncCurrentTime(lockout.current_time);
  const auto reset_packed_time =
      AddSecondsToPackedCalendarTime(lockout.current_time, lockout.reset_time_remaining);
  calendar_system.AddRaidLockoutEvent(BuildCalendarRaidLockoutEvent(
      dbc_, lockout.map_id, lockout.difficulty, lockout.instance_id, reset_packed_time));
  calendar_system.AddRaidInfo(BuildCalendarRaidLockoutInfo(dbc_, lockout.map_id, lockout.difficulty,
                                                   lockout.instance_id, reset_packed_time));
  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  ClearCalendarActionPending(calendar_system);
}

void WorldSession::HandleCalendarEventInviteRemoved(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInviteRemoved(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &removed = calendar_.last_invite_removed();
  if (!removed) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  if ((removed->flags & 0x400u) != 0 && calendar_system.HasEvent(removed->event_id)) {
    calendar_system.SetEventInviteStatus(removed->event_id, 7);
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }

  const auto *context_event = calendar_system.GetContextEvent();
  if (context_event != nullptr && context_event->event_id == removed->event_id &&
      calendar_system.RemoveEventInviteByGuid(removed->event_id, removed->invitee.GetRawValue())) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }

  if (removed->pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarEventInviteRemovedAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInviteRemovedAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &alert = calendar_.last_invite_removed_alert();
  if (!alert) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  const auto *context_event = calendar_system.GetContextEvent();
  const bool had_context_event =
      context_event != nullptr && context_event->event_id == alert->event_id;

  const auto *event = calendar_system.GetEvent(alert->event_id);
  const bool is_guild_signup_event =
      event != nullptr && (event->flags & 0x400u) != 0 && event->invite_type == 1;

  if (is_guild_signup_event) {
    calendar_system.SetEventInviteStatus(alert->event_id, 7);
    const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
    const bool removed_self_invite =
        had_context_event && active_player_guid != 0 &&
        calendar_system.RemoveEventInviteByGuid(alert->event_id, active_player_guid);
    if (removed_self_invite) {
      FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
    }
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  } else if (calendar_system.RemoveEventById(alert->event_id)) {
    if (had_context_event) {
      FireCalendarUiEvent(ui::game::events::CALENDAR_CLOSE_EVENT);
    }
    RemoveCalendarEventAlarm(alert->event_id);
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }

  if (CalendarInviteRemovedAlertRefreshesPendingMail(alert->status)) {
    mail_.ApplyPendingMailDelay(0.0f);
    FlushMailProtocolUpdates(
        mail_, [this](const net::wotlk::WorldPacket& packet) {
          return Send(packet);
        });
  }
  calendar_system.RemovePendingInvitesForEvent(alert->event_id);
  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
}

void WorldSession::HandleCalendarEventInviteStatusAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInviteStatusAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &alert = calendar_.last_invite_status_alert();
  if (!alert) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  const auto *event = calendar_system.GetEvent(alert->event_id);
  if (event == nullptr) {
    RequestCalendarPendingInviteCount(*this);
    return;
  }

  const bool is_locked = (event->flags & 0x10u) != 0;
  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  calendar_system.SetEventInviteStatus(alert->event_id, alert->status);

  const bool pending_membership_changed =
      !is_locked &&
      (alert->status == 0 ? calendar_system.AddPendingInviteForEvent(
                                alert->event_id, event->pending_sender_guid,
                                IsCalendarPendingInviteVisible(*this, event->pending_sender_guid))
                          : calendar_system.RemovePendingInvitesForEvent(alert->event_id));
  if (pending_membership_changed) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }
  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  if (UpdateOpenCalendarInvite(calendar_system, alert->event_id, active_player_guid,
                               alert->status)) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }
}

void WorldSession::HandleCalendarEventModeratorStatusAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventModeratorStatusAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &alert = calendar_.last_moderator_status_alert();
  if (!alert) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  if (const auto *context_event = calendar_system.GetContextEvent();
      context_event != nullptr && context_event->event_id == alert->event_id &&
      calendar_system.SetInviteModeratorRankForInvitee(alert->event_id,
                                                       alert->invitee.GetRawValue(), alert->rank)) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }

  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  if (alert->invitee.GetRawValue() != 0 && alert->invitee.GetRawValue() == active_player_guid) {
    calendar_system.SetDayEventModeratorFlag(alert->event_id, alert->rank == 1);
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }

  if (alert->pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarEventRemovedAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventRemovedAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &alert = calendar_.last_event_removed_alert();
  if (!alert) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  const auto *context_event = calendar_system.GetContextEvent();
  const bool had_context_event =
      context_event != nullptr && context_event->event_id == alert->event_id;
  if (calendar_system.RemoveEventById(alert->event_id)) {
    if (had_context_event) {
      FireCalendarUiEvent(ui::game::events::CALENDAR_CLOSE_EVENT);
    }
    RemoveCalendarEventAlarm(alert->event_id);
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }
  if (alert->pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarEventUpdatedAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventUpdatedAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &alert = calendar_.last_event_updated_alert();
  if (!alert) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  const auto *context_event = calendar_system.GetContextEvent();
  const bool had_context_event =
      context_event != nullptr && context_event->event_id == alert->event_id;
  const auto *existing_event = calendar_system.GetEvent(alert->event_id);
  if (!existing_event) {
    RequestCalendarPendingInviteCount(*this);
    if (alert->pending_action_result != 0) {
      ClearCalendarActionPending(calendar_system);
    }
    return;
  }

  const bool was_locked = (existing_event->flags & 0x10u) != 0;
  const bool had_pending_invite_status = existing_event->invite_status == 0;
  CalendarSystemEvent updated_event = *existing_event;
  updated_event.event_id = alert->event_id;
  updated_event.title = alert->title;
  updated_event.flags = alert->flags;
  updated_event.type = alert->event_type;
  updated_event.dungeon_id = static_cast<int32_t>(alert->dungeon_id);
  updated_event.time = alert->new_date;
  if (calendar_system.UpdateEventFromAlert(alert->event_id, alert->original_date, updated_event)) {
    SyncCalendarEventAlarm(updated_event);
    bool pending_membership_changed = false;
    const bool is_locked = (alert->flags & 0x10u) != 0;
    if (had_pending_invite_status && was_locked != is_locked) {
      pending_membership_changed =
          is_locked
              ? calendar_system.RemovePendingInvitesForEvent(alert->event_id)
              : calendar_system.AddPendingInviteForEvent(
                    alert->event_id, existing_event->pending_sender_guid,
                    IsCalendarPendingInviteVisible(*this, existing_event->pending_sender_guid));
    }
    if (pending_membership_changed) {
      FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
    }
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  }

  if (had_context_event) {
    if (calendar_system.UpdateContextEventFromUpdatedAlert(
            alert->event_id, alert->flags, alert->new_date,
            alert->event_type, alert->dungeon_id, alert->title,
            alert->description, alert->repeat_option,
            alert->max_invites, alert->second_packed_time)) {
      FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT);
    }
  }

  if (alert->pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarClearPendingAction(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleClearPendingAction(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  auto &calendar_system = CalendarSystem::Get();
  ClearCalendarActionPending(calendar_system);
}

void WorldSession::HandleCalendarFilterGuild(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleFilterGuild(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ApplyCalendarInviteList(*this);
  ClearCalendarActionPending(CalendarSystem::Get());
}

void WorldSession::HandleCalendarArenaTeam(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleArenaTeam(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ApplyCalendarInviteList(*this);
  ClearCalendarActionPending(CalendarSystem::Get());
}

void WorldSession::HandleCalendarRaidLockoutRemoved(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleRaidLockoutRemoved(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &lockout = calendar_.last_lockout_removed();
  if (!lockout.has_value()) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  if (lockout->map_id < 0) {
    calendar_system.RemoveAllRaidLockoutEvents();
    calendar_system.SetRaidInfoList({});
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
    ClearCalendarActionPending(calendar_system);
    return;
  }

  const auto current_time = calendar_system.GetCurrentTimePacked();
  if (current_time.has_value()) {
    const auto reset_packed_time = AddSecondsToPackedCalendarTime(*current_time, lockout->reset_time);
    const auto difficulty =
        lockout->difficulty >= 0 ? std::optional<int32_t>(lockout->difficulty) : std::nullopt;
    const auto instance_id = lockout->instance_id == std::numeric_limits<std::uint64_t>::max()
                                 ? std::nullopt
                                 : std::optional<std::uint64_t>(lockout->instance_id);
    calendar_system.RemoveRaidLockoutEvents(static_cast<std::uint32_t>(lockout->map_id), difficulty,
                                            instance_id, reset_packed_time);
    calendar_system.RemoveRaidInfo(static_cast<std::uint32_t>(lockout->map_id), difficulty,
                                   instance_id, reset_packed_time);
  }
  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  ClearCalendarActionPending(calendar_system);
}

void WorldSession::HandleCalendarRaidLockoutUpdated(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleRaidLockoutUpdated(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &lockout = calendar_.last_lockout_updated();
  if (!lockout.has_value()) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  calendar_system.SyncCurrentTime(lockout->current_time);
  const auto old_packed_time =
      AddSecondsToPackedCalendarTime(lockout->current_time, lockout->old_time);
  const auto new_packed_time =
      AddSecondsToPackedCalendarTime(lockout->current_time, lockout->new_time);
  calendar_system.UpdateRaidLockoutEventTime(lockout->map_id, lockout->difficulty, old_packed_time,
                                             new_packed_time);
  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  ClearCalendarActionPending(calendar_system);
}

void WorldSession::HandleCalendarEventInviteNotes(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInviteNotes(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &notes = calendar_.last_invite_notes();
  if (!notes) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  if (const auto *context_event = calendar_system.GetContextEvent();
      context_event != nullptr && context_event->event_id == notes->event_id &&
      calendar_system.SetInviteNotesForInvitee(notes->event_id, notes->invitee.GetRawValue(),
                                               notes->notes)) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }

  if (notes->pending_action_result != 0) {
    ClearCalendarActionPending(calendar_system);
  }
}

void WorldSession::HandleCalendarEventInviteNotesAlert(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleEventInviteNotesAlert(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &notes = calendar_.last_invite_notes_alert();
  if (!notes) {
    return;
  }

  const auto active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  if (active_player_guid == 0) {
    return;
  }

  auto &calendar_system = CalendarSystem::Get();
  if (const auto *context_event = calendar_system.GetContextEvent();
      context_event != nullptr && context_event->event_id == notes->event_id &&
      calendar_system.SetInviteNotesForInvitee(notes->event_id, active_player_guid, notes->notes)) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }
}

}

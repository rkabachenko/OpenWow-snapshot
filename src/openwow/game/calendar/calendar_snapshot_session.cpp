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

void FireCalendarUiEvent(
    const char* event_name,
    std::initializer_list<ui::game::EventArg> args = {}) {
  if (auto* ui = ui::game::runtime::WorldUiRuntimeContext::FromLua(
          ui::frame_script_events::FrameScript_GetLuaStateTyped())) {
    ui->frame_events().dispatcher().FireEventArgs(event_name, args);
  }
}

constexpr std::int64_t kCalendarEventAlarmLeadMinutes = 15;

GameTimeCallbackMoment BuildCalendarEventAlarmMoment(
    const std::uint32_t event_time) {
  const auto reminder_ns = PackedCalendarTimeToNsSince2000(event_time) -
                           kCalendarEventAlarmLeadMinutes * kCalendarNsPerMinute;
  const auto breakdown =
      ::openwow::core::ida::CalendarTimeBreakdownFromNsSince2000(reminder_ns);
  return {.minute = breakdown.minute,
          .hour = breakdown.hour,
          .weekday = breakdown.day_of_week,
          .day = breakdown.day - 1,
          .month = breakdown.month - 1,
          .year = breakdown.year - static_cast<std::int32_t>(kCalendarPackedYearBase),
          .top_bits = 0};
}

bool MatchesCalendarEventAlarmMoment(const GameTimeCallbackMoment& current,
                                     const GameTimeCallbackMoment& target) {
  return current.minute == target.minute && current.hour == target.hour &&
         current.weekday == target.weekday && current.day == target.day &&
         current.month == target.month && current.year == target.year;
}

bool IsCalendarEventAlarmStatus(const std::uint8_t status) {
  return status == 1u || status == 3u;
}

std::string ResolveRaidLockoutTitle(const data::dbc::DbcLoader *dbc, const std::uint32_t map_id) {
  if (dbc == nullptr) {
    return {};
  }
  if (const auto *map_entry = dbc->map().LookupEntry(map_id); map_entry != nullptr) {
    return std::string(map_entry->name);
  }
  return {};
}

CalendarSystemEvent BuildRaidLockoutEvent(const data::dbc::DbcLoader *dbc,
                                          const std::uint32_t map_id,
                                          const std::uint32_t difficulty,
                                          const std::uint64_t instance_id,
                                          const std::uint32_t reset_packed_time) {
  CalendarSystemEvent event{};
  event.event_id = instance_id;
  event.title = ResolveRaidLockoutTitle(dbc, map_id);
  event.dungeon_id = static_cast<std::int32_t>(difficulty);
  event.map_id = map_id;
  event.time = reset_packed_time;
  event.end_time = reset_packed_time;
  event.flags = 0x80u;
  return event;
}

CalendarRaidInfo BuildRaidLockoutInfo(const data::dbc::DbcLoader *dbc, const std::uint32_t map_id,
                                      const std::uint32_t difficulty,
                                      const std::uint64_t instance_id,
                                      const std::uint32_t reset_packed_time) {
  CalendarRaidInfo info{};
  info.map_id = map_id;
  info.instance_id = instance_id;
  const auto parts = DecodePackedCalendarTime(reset_packed_time);
  info.reset_month = parts.month;
  info.reset_day = parts.day;
  info.reset_year = parts.year;
  info.difficulty = static_cast<std::uint8_t>(difficulty);
  info.name = ResolveRaidLockoutTitle(dbc, map_id);
  if (const auto *difficulty_entry = data::DBClient_FindMapDifficulty(
          dbc, map_id, static_cast<std::uint8_t>(difficulty));
      difficulty_entry != nullptr) {
    info.max_players = difficulty_entry->max_players;
  }
  return info;
}

std::uint32_t AddSignedMinutesToPackedCalendarTime(const std::uint32_t packed_time,
                                                   const std::int64_t minutes) {
  return NsSince2000ToPackedCalendarTime(PackedCalendarTimeToNsSince2000(packed_time) +
                                         minutes * kCalendarNsPerMinute);
}

CalendarRaidResetSchedule BuildRaidResetSchedule(const data::dbc::DbcLoader *dbc,
                                                 const CalendarResetTime &reset_time,
                                                 const std::uint32_t server_time,
                                                 const std::uint32_t zone_time,
                                                 const std::uint32_t relation_time) {
  CalendarRaidResetSchedule schedule{};
  if (reset_time.map_id < 0 || reset_time.period <= 0) {
    return schedule;
  }

  const auto period_seconds = static_cast<std::int64_t>(reset_time.period);
  const auto anchor_seconds =
      static_cast<std::int64_t>(relation_time) + static_cast<std::int64_t>(reset_time.offset);
  const auto elapsed_periods =
      (static_cast<std::int64_t>(server_time) - anchor_seconds) / period_seconds;
  const auto offset_minutes =
      (anchor_seconds + period_seconds * elapsed_periods - static_cast<std::int64_t>(server_time)) /
      60LL;

  schedule.map_id = static_cast<std::uint32_t>(reset_time.map_id);
  schedule.title = ResolveRaidLockoutTitle(dbc, schedule.map_id);
  schedule.first_reset_time = AddSignedMinutesToPackedCalendarTime(zone_time, offset_minutes);
  schedule.period_minutes = static_cast<std::uint32_t>(period_seconds / 60LL);
  return schedule;
}

std::uint32_t ResolvePlayerHolidayTeamMask(const WorldSession &session,
                                           const openwow::data::dbc::DbcLoader &dbc) {
  const auto *player = session.objects().GetActivePlayer();
  if (!player) {
    return 0;
  }

  const auto *race = dbc.chr_races().LookupEntry(player->State().GetRace());
  if (!race) {
    return 0;
  }

  constexpr std::uint32_t kCalendarTeamMaskAlliance = 1u;
  constexpr std::uint32_t kCalendarTeamMaskHorde = 2u;
  return race->team_id == 0u ? kCalendarTeamMaskAlliance : kCalendarTeamMaskHorde;
}

std::uint32_t ResolveHolidayRegionBit() {
  const std::int32_t region_id = openwow::net::ClientServices::Instance().GetRegionId();
  if (region_id <= 0 || region_id >= 32) {
    return 0;
  }
  return 1u << (region_id - 1);
}

std::string ResolveHolidayText(const openwow::data::dbc::DbcLoader &dbc,
                               const std::uint32_t text_id, const bool description) {
  if (description) {
    if (const auto *entry = dbc.holiday_descriptions().LookupEntry(text_id)) {
      return std::string(entry->description);
    }
  } else if (const auto *entry = dbc.holiday_names().LookupEntry(text_id)) {
    return std::string(entry->name);
  }
  return {};
}

std::optional<std::int64_t>
ResolveHolidaySequenceDurationMinutes(const std::array<std::uint32_t, 10> &duration_hours,
                                      const std::size_t sequence_index) {
  if (sequence_index > 0 && duration_hours[sequence_index] == 0) {
    return std::nullopt;
  }

  const std::uint32_t hours =
      duration_hours[sequence_index] != 0 ? duration_hours[sequence_index] : 24u;
  return static_cast<std::int64_t>(hours) * 60LL;
}

std::int64_t ResolveHolidayRepeatStepMinutes(const std::array<std::uint32_t, 10> &duration_hours,
                                             const std::size_t sequence_index,
                                             const std::uint32_t loop_mode) {
  if (loop_mode == 0) {
    return 0;
  }

  std::int64_t repeat_hours =
      duration_hours[sequence_index] != 0 ? duration_hours[sequence_index] : 24u;
  for (std::size_t index = 0; index < duration_hours.size(); ++index) {
    if (index == sequence_index) {
      if (index == 0 && duration_hours[0] == 0) {
        repeat_hours += 24;
      }
      continue;
    }
    repeat_hours += duration_hours[index];
    if (index == 0 && duration_hours[0] == 0) {
      repeat_hours += 24;
    }
  }
  return repeat_hours * 60LL;
}

void BuildCalendarHolidaySequenceSources(WorldSession &session, const CalendarData &data,
                                         std::vector<CalendarHolidaySequenceSource> &sources,
                                         std::vector<CalendarHolidayPresentation> &presentations) {
  sources.clear();
  presentations.clear();

  const auto *dbc = session.GetDbcLoader();
  if (!dbc) {
    return;
  }

  const std::uint32_t player_team_mask = ResolvePlayerHolidayTeamMask(session, *dbc);
  if (player_team_mask == 0) {
    return;
  }
  const std::uint32_t region_bit = ResolveHolidayRegionBit();

  std::unordered_map<std::uint32_t, const CalendarHolidayEntry *> overrides;
  for (const auto &holiday : data.holidays) {
    overrides[holiday.holiday_id] = &holiday;
  }

  std::unordered_map<std::uint32_t, std::size_t> presentation_index;
  for (const auto &holiday : dbc->holidays().entries()) {
    const CalendarHolidayEntry *override_entry = nullptr;
    if (const auto it = overrides.find(holiday.id); it != overrides.end()) {
      override_entry = it->second;
    }

    const std::uint32_t selection_mask =
        override_entry ? override_entry->selection_mask : holiday.selection_mask;
    const std::uint32_t holiday_priority =
        override_entry ? override_entry->priority : holiday.priority;
    if (selection_mask != 0 && region_bit != 0 && (selection_mask & region_bit) == 0) {
      continue;
    }

    const auto &team_masks =
        override_entry ? override_entry->sequence_team_masks : holiday.sequence_team_masks;
    const std::string name = ResolveHolidayText(*dbc, holiday.holiday_name_id, false);
    const std::string description = ResolveHolidayText(*dbc, holiday.holiday_description_id, true);
    const std::string texture = override_entry ? override_entry->texture_path_override
                                               : std::string(holiday.texture_filename);

    for (std::size_t sequence_index = 0; sequence_index < team_masks.size(); ++sequence_index) {
      if ((team_masks[sequence_index] & player_team_mask) == 0) {
        continue;
      }

      const auto &duration_hours = override_entry ? override_entry->sequence_duration_hours
                                                  : holiday.sequence_duration_hours;
      const auto &packed_times = override_entry ? override_entry->occurrence_packed_times
                                                : holiday.occurrence_packed_times;
      const std::uint32_t loop_mode =
          override_entry ? override_entry->loop_mode : holiday.loop_mode;
      const auto sequence_duration_minutes =
          ResolveHolidaySequenceDurationMinutes(duration_hours, sequence_index);
      if (!sequence_duration_minutes.has_value()) {
        continue;
      }

      std::int64_t sequence_offset_minutes = 0;
      bool has_complete_prefix = true;
      for (std::size_t prefix_index = 0; prefix_index < sequence_index; ++prefix_index) {
        const auto prefix_minutes =
            ResolveHolidaySequenceDurationMinutes(duration_hours, prefix_index);
        if (!prefix_minutes.has_value()) {
          has_complete_prefix = false;
          break;
        }
        sequence_offset_minutes += *prefix_minutes;
      }
      if (!has_complete_prefix) {
        continue;
      }

      CalendarHolidaySequenceSource source{};
      source.holiday_id = holiday.id;
      source.title = name;
      source.description = description;
      source.texture = texture;
      source.flags = holiday.flags;
      source.holiday_sort_priority = holiday_priority;
      source.holiday_filter_type = static_cast<std::int32_t>(
          override_entry ? override_entry->calendar_filter_type : holiday.calendar_filter_type);
      source.occurrence_packed_times = packed_times;
      source.sequence_offset_minutes = sequence_offset_minutes;
      source.sequence_duration_minutes = *sequence_duration_minutes;
      source.repeat_step_minutes =
          ResolveHolidayRepeatStepMinutes(duration_hours, sequence_index, loop_mode);
      sources.push_back(std::move(source));

      if (presentation_index.find(holiday.id) == presentation_index.end()) {
        presentation_index[holiday.id] = presentations.size();
        presentations.push_back({
            .holiday_id = holiday.id,
            .name = name,
            .description = description,
            .texture = texture,
        });

      }
    }
  }
}

}

void WorldSession::ReplaceCalendarEventAlarms(const std::vector<CalendarSystemEvent> &events) {
  ClearCalendarEventAlarms();
  for (const auto &event : events) {
    SyncCalendarEventAlarm(event);
  }
}

void WorldSession::SyncCalendarEventAlarm(const CalendarSystemEvent &event) {
  auto& calendar_event_alarms = calendar_runtime_.alarms();
  if (event.event_id == 0 || event.time == 0) {
    RemoveCalendarEventAlarm(event.event_id);
    return;
  }

  const GameTimeCallbackMoment reminder_time = BuildCalendarEventAlarmMoment(event.time);
  if (const auto existing_it = calendar_event_alarms.find(event.event_id);
      existing_it != calendar_event_alarms.end() &&
      MatchesCalendarEventAlarmMoment(existing_it->second.reminder_time, reminder_time)) {
    return;
  }

  RemoveCalendarEventAlarm(event.event_id);
  auto [it, inserted] =
      calendar_event_alarms.emplace(event.event_id, CalendarRuntime::AlarmRegistration{
                                                         .owner = this,
                                                         .event_id = event.event_id,
                                                         .reminder_time = reminder_time,
                                                     });
  if (!inserted) {
    it->second = CalendarRuntime::AlarmRegistration{
        .owner = this,
        .event_id = event.event_id,
        .reminder_time = reminder_time,
    };
  }

  auto &registration = it->second;
  registration.handle = session_.game_time_callbacks().Register(
      registration.reminder_time, &WorldSession::DispatchCalendarEventAlarm, &registration);
  if (registration.handle == GameTimeCallbackRegistry::kInvalidHandle) {
    calendar_event_alarms.erase(it);
  }
}

void WorldSession::RemoveCalendarEventAlarm(const std::uint64_t event_id) {
  auto& calendar_event_alarms = calendar_runtime_.alarms();
  const auto it = calendar_event_alarms.find(event_id);
  if (it == calendar_event_alarms.end()) {
    return;
  }

  if (it->second.handle != GameTimeCallbackRegistry::kInvalidHandle) {
    session_.game_time_callbacks().Unregister(it->second.handle);
  }
  calendar_event_alarms.erase(it);
}

void WorldSession::ClearCalendarEventAlarms() {
  for (const auto &[event_id, registration] : calendar_runtime_.alarms()) {
    (void)event_id;
    if (registration.handle != GameTimeCallbackRegistry::kInvalidHandle) {
      session_.game_time_callbacks().Unregister(registration.handle);
    }
  }
  calendar_runtime_.Reset();
}

bool WorldSession::DispatchCalendarEventAlarm(const GameTimeCallbackMoment &current_time,
                                              void *context) {
  auto *registration = static_cast<CalendarRuntime::AlarmRegistration *>(context);
  if (registration == nullptr || registration->owner == nullptr) {
    return false;
  }

  return registration->owner->HandleCalendarEventAlarmCallback(registration->event_id,
                                                               registration->handle, current_time);
}

bool WorldSession::HandleCalendarEventAlarmCallback(
    const std::uint64_t event_id, const GameTimeCallbackRegistry::Handle expected_handle,
    const GameTimeCallbackMoment &current_time) {
  const auto& calendar_event_alarms = calendar_runtime_.alarms();
  const auto it = calendar_event_alarms.find(event_id);
  if (it == calendar_event_alarms.end() || it->second.handle != expected_handle) {
    session_.game_time_callbacks().Unregister(expected_handle);
    return false;
  }

  if (!MatchesCalendarEventAlarmMoment(current_time, it->second.reminder_time)) {
    return false;
  }

  const auto *event = CalendarSystem::Get().GetEvent(event_id);
  if (event == nullptr) {
    RemoveCalendarEventAlarm(event_id);
    return false;
  }

  if (!IsCalendarEventAlarmStatus(event->invite_status)) {
    return false;
  }

  const auto event_time = DecodePackedCalendarTime(event->time);
  FireCalendarUiEvent(
      ui::game::events::CALENDAR_EVENT_ALARM,
      {event->title, static_cast<int>(event_time.hour), static_cast<int>(event_time.minute)});
  RemoveCalendarEventAlarm(event_id);
  return true;
}

void WorldSession::HandleCalendarSendCalendar(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleSendCalendar(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &data = calendar_.calendar_data();
  const std::uint64_t active_player_guid = map_runtime_.objects().GetActivePlayerGuid().GetRawValue();
  std::vector<CalendarSystemEvent> sys_events;
  sys_events.reserve(data.events.size() + data.lockouts.size());
  std::vector<CalendarSystemEvent> alarm_events;
  alarm_events.reserve(data.events.size());
  for (const auto &ev : data.events) {
    CalendarSystemEvent se;
    se.event_id = ev.event_id;
    se.title = ev.title;
    se.type = static_cast<uint8_t>(ev.type);
    se.time = ev.event_time;
    se.end_time = ev.event_time;
    se.flags = ev.flags;
    se.dungeon_id = ev.dungeon_id;
    se.creator_guid = ev.creator.GetRawValue();
    const auto invite_it =
        std::find_if(data.invites.begin(), data.invites.end(), [&ev](const CalendarInvite &invite) {
          return invite.event_id == ev.event_id;
        });
    if (invite_it != data.invites.end()) {
      se.self_invite_id = invite_it->invite_id;
      se.invite_status = invite_it->status;
      se.invite_type = invite_it->invite_type;
      if (se.creator_guid != 0 && se.creator_guid == active_player_guid) {
        se.invite_mod_status |= 0x04;
      } else if (invite_it->rank != 0) {
        se.invite_mod_status |= 0x02;
      }
    }
    alarm_events.push_back(se);
    sys_events.push_back(std::move(se));
  }
  auto &calendar_system = CalendarSystem::Get();
  calendar_system.SyncCurrentTime(data.zone_time);
  const auto current_calendar = DecodePackedCalendarTime(data.zone_time);
  const uint32_t month = current_calendar.month;
  const uint32_t year = current_calendar.year;
  calendar_system.SetViewMonth(month, year);
  std::vector<CalendarRaidInfo> raid_infos;
  raid_infos.reserve(data.lockouts.size());
  for (const auto &lockout : data.lockouts) {
    const auto reset_packed_time =
        AddSecondsToPackedCalendarTime(data.zone_time, lockout.reset_time_remaining);
    sys_events.push_back(BuildRaidLockoutEvent(dbc_, lockout.map_id, lockout.difficulty,
                                               lockout.instance_id, reset_packed_time));
    raid_infos.push_back(BuildRaidLockoutInfo(dbc_, lockout.map_id, lockout.difficulty,
                                              lockout.instance_id, reset_packed_time));
  }
  calendar_system.ReplaceEvents(sys_events);
  calendar_system.SetRaidInfoList(raid_infos);
  std::vector<CalendarRaidResetSchedule> raid_reset_schedules;
  raid_reset_schedules.reserve(data.reset_times.size());
  for (const auto &reset_time : data.reset_times) {
    auto schedule = BuildRaidResetSchedule(dbc_, reset_time, data.server_time, data.zone_time,
                                           data.relation_time);
    if (schedule.period_minutes == 0 || schedule.first_reset_time == 0) {
      continue;
    }
    raid_reset_schedules.push_back(std::move(schedule));
  }
  calendar_system.SetRaidResetSchedules(raid_reset_schedules);

  std::vector<CalendarSystemInvite> sys_invites;
  sys_invites.reserve(data.invites.size());
  bool has_visible_pending_invite = false;
  for (const auto &inv : data.invites) {
    CalendarSystemInvite si;
    si.invite_id = inv.invite_id;
    si.event_id = inv.event_id;
    si.status = inv.status;
    si.rank = inv.rank;
    si.invite_type = inv.invite_type;
    si.sender_guid = inv.sender.GetRawValue();
    si.visible_in_pending_list = IsCalendarPendingInviteVisible(*this, si.sender_guid);
    has_visible_pending_invite |= si.visible_in_pending_list;
    sys_invites.push_back(std::move(si));
  }
  calendar_system.SetPendingInvites(sys_invites);
  ReplaceCalendarEventAlarms(alarm_events);

  std::vector<CalendarHolidaySequenceSource> holiday_sources;
  std::vector<CalendarHolidayPresentation> holiday_presentations;
  BuildCalendarHolidaySequenceSources(*this, data, holiday_sources, holiday_presentations);
  calendar_system.SetHolidaySequenceSources(holiday_sources, holiday_presentations);
  if (has_visible_pending_invite) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }

  FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_EVENT_LIST);
  ClearCalendarActionPending(calendar_system);
}

void WorldSession::HandleCalendarSendNumPending(const net::wotlk::WorldPacket &pkt) {
  if (!calendar_.HandleSendNumPending(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  auto &calendar_system = CalendarSystem::Get();
  if (calendar_system.SetPendingInviteCount(calendar_.num_pending().pending_invites)) {
    FireCalendarUiEvent(ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }
  ClearCalendarActionPending(calendar_system);
}

}

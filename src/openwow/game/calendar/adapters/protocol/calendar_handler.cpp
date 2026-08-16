
#include "openwow/game/calendar/adapters/protocol/calendar_handler.h"

#include <algorithm>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::size_t kCalendarCommandContextMaxBytesIncludingNul = 0x80u;
constexpr std::size_t kCalendarCommandPlayerNameMaxBytesIncludingNul = 0x30u;
constexpr std::size_t kCalendarTitleMaxBytesIncludingNul = 0x80u;
constexpr std::size_t kCalendarDescriptionMaxBytesIncludingNul = 0x400u;
constexpr std::size_t kCalendarInviteNotesMaxBytesIncludingNul = 0x80u;

[[nodiscard]] bool CountFitsRemaining(const std::uint32_t count,
                                      const std::size_t minimum_wire_bytes,
                                      const PacketReader &reader) {
  return minimum_wire_bytes != 0 && count <= reader.Remaining() / minimum_wire_bytes;
}

bool ReadCalendarInviteList(const std::uint8_t *data, std::size_t len,
                            std::vector<CalendarInviteListEntry> &entries) {
  PacketReader r(data, len);
  std::uint32_t count = 0;
  if (!r.ReadU32(count)) {
    return false;
  }

  if (!CountFitsRemaining(count, 2u, r)) {
    return false;
  }

  std::vector<CalendarInviteListEntry> parsed;
  parsed.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    CalendarInviteListEntry entry{};
    if (!r.ReadPackedGuid(entry.guid) || !r.ReadU8(entry.level)) {
      return false;
    }
    parsed.push_back(entry);
  }
  entries = std::move(parsed);
  return true;
}

}

std::optional<CalendarCommandErrorDisplay>
ResolveCalendarCommandErrorDisplay(const std::uint32_t error_code) {
  using Argument = CalendarCommandErrorArgument;

  switch (error_code) {
  case 1:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_GUILD_EVENTS_EXCEEDED", Argument::kNumericLimit,
        100u};
  case 2:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_EVENTS_EXCEEDED", Argument::kNumericLimit, 30u};
  case 3:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_SELF_INVITES_EXCEEDED"};
  case 4:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_OTHER_INVITES_EXCEEDED", Argument::kPlayerName};
  case 5:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_PERMISSIONS"};
  case 6:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_EVENT_INVALID"};
  case 7:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_NOT_INVITED"};
  case 8:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_INTERNAL"};
  case 9:
    return CalendarCommandErrorDisplay{"ERR_GUILD_PLAYER_NOT_IN_GUILD"};
  case 10:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_ALREADY_INVITED_TO_EVENT_S", Argument::kPlayerName};
  case 11:
    return CalendarCommandErrorDisplay{"PLAYER_NOT_FOUND"};
  case 12:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_NOT_ALLIED"};
  case 13:
    return CalendarCommandErrorDisplay{"ERR_IGNORING_YOU_S",
                                       Argument::kPlayerName};
  case 14:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_INVITES_EXCEEDED", Argument::kNumericLimit, 100u};
  case 16:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_INVALID_DATE"};
  case 17:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_INVALID_TIME"};
  case 19:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_NEEDS_TITLE"};
  case 20:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_EVENT_PASSED"};
  case 21:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_EVENT_LOCKED"};
  case 22:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_DELETE_CREATOR_FAILED"};
  case 24:
    return CalendarCommandErrorDisplay{"ERR_SYSTEM_DISABLED"};
  case 25:
    return CalendarCommandErrorDisplay{"ERR_RESTRICTED_ACCOUNT"};
  case 26:
    return CalendarCommandErrorDisplay{
        "CALENDAR_ERROR_ARENA_EVENTS_EXCEEDED"};
  case 27:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_RESTRICTED_LEVEL"};
  case 28:
    return CalendarCommandErrorDisplay{"ERR_USER_SQUELCHED"};
  case 29:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_NO_INVITE"};
  case 36:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_EVENT_WRONG_SERVER"};
  case 37:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_INVITE_WRONG_SERVER"};
  case 38:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_NO_GUILD_INVITES"};
  case 39:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_INVALID_SIGNUP"};
  case 40:
    return CalendarCommandErrorDisplay{"CALENDAR_ERROR_NO_MODERATOR"};
  default:
    return std::nullopt;
  }
}

bool CalendarHandler::HandleSendCalendar(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarData parsed{};

  std::uint32_t invite_count = 0;
  if (!r.ReadU32(invite_count))
    return false;
  if (!CountFitsRemaining(invite_count, 20u, r))
    return false;
  parsed.invites.reserve(invite_count);
  for (std::uint32_t i = 0; i < invite_count; ++i) {
    CalendarInvite inv;
    if (!r.ReadU64(inv.event_id))
      return false;
    if (!r.ReadU64(inv.invite_id))
      return false;
    if (!r.ReadU8(inv.status))
      return false;
    if (!r.ReadU8(inv.rank))
      return false;
    if (!r.ReadU8(inv.invite_type))
      return false;
    if (!r.ReadPackedGuid(inv.sender))
      return false;
    parsed.invites.push_back(std::move(inv));
  }

  std::uint32_t event_count = 0;
  if (!r.ReadU32(event_count))
    return false;
  if (!CountFitsRemaining(event_count, 26u, r))
    return false;
  parsed.events.reserve(event_count);
  for (std::uint32_t i = 0; i < event_count; ++i) {
    CalendarEvent ev;
    if (!r.ReadU64(ev.event_id))
      return false;
    if (!r.ReadCString(ev.title, kCalendarTitleMaxBytesIncludingNul))
      return false;
    if (!r.ReadU32(ev.type))
      return false;
    if (!r.ReadU32(ev.event_time))
      return false;
    if (!r.ReadU32(ev.flags))
      return false;
    if (!r.ReadI32(ev.dungeon_id))
      return false;
    if (!r.ReadPackedGuid(ev.creator))
      return false;
    parsed.events.push_back(std::move(ev));
  }

  if (!r.ReadU32(parsed.server_time))
    return false;
  if (!r.ReadU32(parsed.zone_time))
    return false;

  std::uint32_t lockout_count = 0;
  if (!r.ReadU32(lockout_count))
    return false;
  if (!CountFitsRemaining(lockout_count, 20u, r))
    return false;
  parsed.lockouts.reserve(lockout_count);
  for (std::uint32_t i = 0; i < lockout_count; ++i) {
    CalendarInstanceLockout l;
    if (!r.ReadU32(l.map_id))
      return false;
    if (!r.ReadU32(l.difficulty))
      return false;
    if (!r.ReadU32(l.reset_time_remaining))
      return false;
    if (!r.ReadU64(l.instance_id))
      return false;
    parsed.lockouts.push_back(l);
  }

  if (!r.ReadU32(parsed.relation_time))
    return false;

  std::uint32_t reset_count = 0;
  if (!r.ReadU32(reset_count))
    return false;
  if (!CountFitsRemaining(reset_count, 12u, r))
    return false;
  parsed.reset_times.reserve(reset_count);
  for (std::uint32_t i = 0; i < reset_count; ++i) {
    CalendarResetTime rt;
    if (!r.ReadI32(rt.map_id))
      return false;
    if (!r.ReadI32(rt.period))
      return false;
    if (!r.ReadI32(rt.offset))
      return false;
    parsed.reset_times.push_back(rt);
  }

  if (!r.ReadU32(parsed.holiday_count))
    return false;
  constexpr std::size_t kCalendarHolidayMinimumWireBytes =
      5u * sizeof(std::uint32_t) + 26u * sizeof(std::uint32_t) +
      10u * sizeof(std::uint32_t) + 10u * sizeof(std::uint32_t) + 1u;
  if (!CountFitsRemaining(parsed.holiday_count, kCalendarHolidayMinimumWireBytes, r))
    return false;
  parsed.holidays.reserve(parsed.holiday_count);
  for (std::uint32_t i = 0; i < parsed.holiday_count; ++i) {
    CalendarHolidayEntry holiday{};
    if (!r.ReadU32(holiday.holiday_id))
      return false;
    if (!r.ReadU32(holiday.selection_mask))
      return false;
    if (!r.ReadU32(holiday.loop_mode))
      return false;
    if (!r.ReadU32(holiday.priority))
      return false;
    if (!r.ReadU32(holiday.calendar_filter_type))
      return false;
    for (auto &packed_time : holiday.occurrence_packed_times) {
      if (!r.ReadU32(packed_time))
        return false;
    }
    for (auto &duration_hours : holiday.sequence_duration_hours) {
      if (!r.ReadU32(duration_hours))
        return false;
    }
    for (auto &team_mask : holiday.sequence_team_masks) {
      if (!r.ReadU32(team_mask))
        return false;
    }

    if (!r.ReadCString(holiday.texture_path_override, 64u))
      return false;
    parsed.holidays.push_back(std::move(holiday));
  }
  calendar_data_ = std::move(parsed);
  return true;
}

bool CalendarHandler::HandleSendEvent(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarSendEvent ev{};

  if (!r.ReadU8(ev.send_type))
    return false;
  if (!r.ReadPackedGuid(ev.creator))
    return false;
  if (!r.ReadU64(ev.event_id))
    return false;
  if (!r.ReadCString(ev.title, kCalendarTitleMaxBytesIncludingNul))
    return false;
  if (!r.ReadCString(ev.description, kCalendarDescriptionMaxBytesIncludingNul))
    return false;
  if (!r.ReadU8(ev.event_type))
    return false;
  if (!r.ReadU8(ev.repeat_option))
    return false;
  if (!r.ReadU32(ev.max_invites))
    return false;
  if (!r.ReadI32(ev.dungeon_id))
    return false;
  if (!r.ReadU32(ev.flags))
    return false;
  if (!r.ReadU32(ev.event_time))
    return false;
  if (!r.ReadU32(ev.unk_time))
    return false;
  if (!r.ReadU64(ev.guild_id))
    return false;

  std::uint32_t invite_count = 0;
  if (!r.ReadU32(invite_count))
    return false;
  if (!CountFitsRemaining(invite_count, 18u, r))
    return false;
  ev.invites.reserve(invite_count);
  for (std::uint32_t i = 0; i < invite_count; ++i) {
    CalendarSendEventInvite inv;
    if (!r.ReadPackedGuid(inv.invitee))
      return false;
    if (!r.ReadU8(inv.level))
      return false;
    if (!r.ReadU8(inv.status))
      return false;
    if (!r.ReadU8(inv.rank))
      return false;
    if (!r.ReadU8(inv.invite_type))
      return false;
    if (!r.ReadU64(inv.invite_id))
      return false;
    if (!r.ReadU32(inv.status_time))
      return false;
    if (!r.ReadCString(inv.notes, kCalendarInviteNotesMaxBytesIncludingNul))
      return false;
    ev.invites.push_back(std::move(inv));
  }

  last_send_event_ = std::move(ev);
  return true;
}

bool CalendarHandler::HandleSendNumPending(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarNumPending parsed{};
  if (!r.ReadU32(parsed.pending_invites)) {
    return false;
  }
  num_pending_ = parsed;
  return true;
}

bool CalendarHandler::HandleCommandResult(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarCommandResult result{};
  if (!r.ReadU32(result.command))
    return false;
  if (!r.ReadCString(result.command_context,
                     kCalendarCommandContextMaxBytesIncludingNul))
    return false;
  if (!r.ReadCString(result.player_name,
                     kCalendarCommandPlayerNameMaxBytesIncludingNul))
    return false;
  if (!r.ReadU32(result.error))
    return false;
  last_command_result_ = std::move(result);
  return true;
}

bool CalendarHandler::HandleEventInvite(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarEventInvite parsed{};
  if (!r.ReadPackedGuid(parsed.invitee))
    return false;
  if (!r.ReadU64(parsed.event_id))
    return false;
  if (!r.ReadU64(parsed.invite_id))
    return false;
  if (!r.ReadU8(parsed.level))
    return false;
  if (!r.ReadU8(parsed.status))
    return false;
  if (!r.ReadU8(parsed.invite_type))
    return false;
  if (parsed.invite_type == 1) {
    if (!r.ReadU32(parsed.response_time))
      return false;
  }
  if (!r.ReadU8(parsed.pending_action_result))
    return false;
  last_invite_ = parsed;
  return true;
}

bool CalendarHandler::HandleEventInviteAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarEventInviteAlert parsed{};
  if (!r.ReadU64(parsed.event_id))
    return false;
  if (!r.ReadCString(parsed.title, kCalendarTitleMaxBytesIncludingNul))
    return false;
  if (!r.ReadU32(parsed.event_time))
    return false;
  if (!r.ReadU32(parsed.flags))
    return false;
  if (!r.ReadU32(parsed.type))
    return false;
  if (!r.ReadI32(parsed.dungeon_id))
    return false;
  if (!r.ReadU64(parsed.invite_id))
    return false;
  if (!r.ReadU8(parsed.status))
    return false;
  if (!r.ReadU8(parsed.rank))
    return false;
  if (!r.ReadPackedGuid(parsed.creator))
    return false;
  if (!r.ReadPackedGuid(parsed.sender))
    return false;
  last_invite_alert_ = std::move(parsed);
  return true;
}

bool CalendarHandler::HandleEventStatus(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarEventStatus parsed{};
  if (!r.ReadPackedGuid(parsed.invitee))
    return false;
  if (!r.ReadU64(parsed.event_id))
    return false;
  if (!r.ReadU32(parsed.event_time))
    return false;
  if (!r.ReadU32(parsed.flags))
    return false;
  if (!r.ReadU8(parsed.status))
    return false;
  if (!r.ReadU8(parsed.pending_action_result))
    return false;
  if (!r.ReadU32(parsed.status_time))
    return false;
  last_event_status_ = parsed;
  return true;
}

bool CalendarHandler::HandleRaidLockoutAdded(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  CalendarRaidLockoutAdded parsed{};
  if (!r.ReadU32(parsed.current_time))
    return false;
  if (!r.ReadU32(parsed.map_id))
    return false;
  if (!r.ReadU32(parsed.difficulty))
    return false;
  if (!r.ReadU32(parsed.reset_time_remaining))
    return false;
  if (!r.ReadU64(parsed.instance_id))
    return false;
  last_lockout_added_ = parsed;
  return true;
}

void CalendarHandler::Clear() {
  calendar_data_ = {};
  num_pending_ = {};
  last_command_result_ = {};
  last_invite_ = {};
  last_invite_alert_ = {};
  last_event_status_ = {};
  last_lockout_added_ = {};
  last_invite_removed_.reset();
  last_invite_removed_alert_.reset();
  last_invite_status_alert_.reset();
  last_moderator_status_alert_.reset();
  last_event_removed_alert_.reset();
  last_event_updated_alert_.reset();
  clear_pending_action_ = false;
  invite_list_entries_.clear();
  last_lockout_removed_.reset();
  last_lockout_updated_.reset();
  last_invite_notes_.reset();
  last_invite_notes_alert_.reset();
  last_send_event_.reset();
}

bool CalendarHandler::HandleEventInviteRemoved(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_invite_removed_.reset();
  CalendarEventInviteRemoved res{};
  if (!r.ReadPackedGuid(res.invitee))
    return false;
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadU32(res.flags))
    return false;
  if (!r.ReadU8(res.pending_action_result))
    return false;
  last_invite_removed_ = res;
  return true;
}

bool CalendarHandler::HandleEventInviteRemovedAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_invite_removed_alert_.reset();
  CalendarEventInviteRemovedAlert res{};
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadU32(res.date))
    return false;
  if (!r.ReadU32(res.flags))
    return false;
  if (!r.ReadU8(res.status))
    return false;
  last_invite_removed_alert_ = res;
  return true;
}

bool CalendarHandler::HandleEventInviteStatusAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_invite_status_alert_.reset();
  CalendarEventInviteStatusAlert res{};
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadU32(res.date))
    return false;
  if (!r.ReadU32(res.flags))
    return false;
  if (!r.ReadU8(res.status))
    return false;
  last_invite_status_alert_ = res;
  return true;
}

bool CalendarHandler::HandleEventModeratorStatusAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_moderator_status_alert_.reset();
  CalendarEventModeratorStatusAlert res{};
  if (!r.ReadPackedGuid(res.invitee))
    return false;
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadU8(res.rank))
    return false;
  if (!r.ReadU8(res.pending_action_result))
    return false;
  last_moderator_status_alert_ = res;
  return true;
}

bool CalendarHandler::HandleEventRemovedAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_event_removed_alert_.reset();
  CalendarEventRemovedAlert res{};
  if (!r.ReadU8(res.pending_action_result))
    return false;
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadU32(res.date))
    return false;
  last_event_removed_alert_ = std::move(res);
  return true;
}

bool CalendarHandler::HandleEventUpdatedAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_event_updated_alert_.reset();
  CalendarEventUpdatedAlert res{};
  if (!r.ReadU8(res.pending_action_result))
    return false;
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadU32(res.original_date))
    return false;
  if (!r.ReadU32(res.flags))
    return false;
  if (!r.ReadU32(res.new_date))
    return false;
  if (!r.ReadU8(res.event_type))
    return false;
  if (!r.ReadU32(res.dungeon_id))
    return false;
  if (!r.ReadCString(res.title, kCalendarTitleMaxBytesIncludingNul))
    return false;
  if (!r.ReadCString(res.description, kCalendarDescriptionMaxBytesIncludingNul))
    return false;
  if (!r.ReadU8(res.repeat_option))
    return false;
  if (!r.ReadU32(res.max_invites))
    return false;
  if (!r.ReadU32(res.second_packed_time))
    return false;
  last_event_updated_alert_ = std::move(res);
  return true;
}

bool CalendarHandler::HandleClearPendingAction(const std::uint8_t * , std::size_t ) {
  clear_pending_action_ = true;
  return true;
}

bool CalendarHandler::HandleFilterGuild(const std::uint8_t *data, std::size_t len) {
  return ReadCalendarInviteList(data, len, invite_list_entries_);
}

bool CalendarHandler::HandleArenaTeam(const std::uint8_t *data, std::size_t len) {
  return ReadCalendarInviteList(data, len, invite_list_entries_);
}

bool CalendarHandler::HandleRaidLockoutRemoved(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_lockout_removed_.reset();
  CalendarRaidLockoutRemoved res{};
  if (!r.ReadI32(res.map_id))
    return false;
  if (!r.ReadI32(res.difficulty))
    return false;
  if (!r.ReadU32(res.reset_time))
    return false;
  if (!r.ReadU64(res.instance_id))
    return false;
  last_lockout_removed_ = res;
  return true;
}

bool CalendarHandler::HandleRaidLockoutUpdated(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_lockout_updated_.reset();
  CalendarRaidLockoutUpdated res{};
  if (!r.ReadU32(res.current_time))
    return false;
  if (!r.ReadU32(res.map_id))
    return false;
  if (!r.ReadU32(res.difficulty))
    return false;
  if (!r.ReadU32(res.old_time))
    return false;
  if (!r.ReadU32(res.new_time))
    return false;
  last_lockout_updated_ = res;
  return true;
}

bool CalendarHandler::HandleEventInviteNotes(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_invite_notes_.reset();
  CalendarEventInviteNotes res{};
  if (!r.ReadPackedGuid(res.invitee))
    return false;
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadCString(res.notes, kCalendarInviteNotesMaxBytesIncludingNul))
    return false;
  if (!r.ReadU8(res.pending_action_result))
    return false;
  last_invite_notes_ = std::move(res);
  return true;
}

bool CalendarHandler::HandleEventInviteNotesAlert(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_invite_notes_alert_.reset();
  CalendarEventInviteNotesAlert res{};
  if (!r.ReadU64(res.event_id))
    return false;
  if (!r.ReadCString(res.notes, kCalendarInviteNotesMaxBytesIncludingNul))
    return false;
  last_invite_notes_alert_ = std::move(res);
  return true;
}

}

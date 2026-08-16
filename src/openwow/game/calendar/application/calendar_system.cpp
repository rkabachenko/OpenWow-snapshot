
#include "openwow/game/calendar/calendar_system.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/calendar/adapters/protocol/calendar_date_fields_packed.h"
#include "openwow/game/calendar/calendar_time.h"
#include "openwow/game/group_system.h"
#include "openwow/game/ignore_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <limits>

namespace openwow::game {

namespace {

constexpr std::uint8_t kCalendarHolidayEventType = 5;

struct CalendarIndexDate {
  std::int32_t year = 2000;
  std::int32_t month = 1;
  std::int32_t day = 1;
};

using HolidayDateFields = std::array<std::int32_t, 6>;

struct CalendarVisibleWindow {
  HolidayDateFields start{};
  HolidayDateFields end_exclusive{};
};

void ApplyPackedTimeToContextInfo(const std::uint32_t packed_time, CalendarContextEventInfo &info) {
  CalendarDateFieldsEx fields{};
  CalendarPackedTime_UnpackToArray(packed_time, fields);
  constexpr auto kUnset = std::numeric_limits<std::uint32_t>::max();
  const auto human_component = [=](const std::int32_t value,
                                   const std::uint32_t offset) -> std::uint32_t {
    return value < 0 ? kUnset : static_cast<std::uint32_t>(value) + offset;
  };

  info.year = human_component(fields.year, 2000u);
  info.month = human_component(fields.month, 1u);
  info.day = human_component(fields.day, 1u);
  info.hour = human_component(fields.hour, 0u);
  info.minute = human_component(fields.minute, 0u);
  info.weekday = fields.weekday;
  info.time_flags = fields.flags;
}

CalendarIndexDate DecodeCalendarIndexDate(const std::uint32_t packed_time) {
  auto decode_field = [packed_time](const std::uint32_t shift, const std::uint32_t mask,
                                    const std::uint32_t sentinel) -> std::int32_t {
    const auto value = (packed_time >> shift) & mask;
    return value == sentinel ? -1 : static_cast<std::int32_t>(value);
  };

  CalendarIndexDate parts{};
  const auto packed_year = decode_field(24, 0x1Fu, 0x1Fu);
  const auto packed_month = decode_field(20, 0xFu, 0xFu);
  const auto packed_day = decode_field(14, 0x3Fu, 0x3Fu);
  parts.year =
      packed_year < 0 ? -1 : static_cast<std::int32_t>(kCalendarPackedYearBase) + packed_year;
  parts.month = packed_month < 0 ? -1 : packed_month + 1;
  parts.day = packed_day < 0 ? -1 : packed_day + 1;
  return parts;
}

HolidayDateFields DecodeHolidayPackedTime(const std::uint32_t packed_time) {
  auto decode = [packed_time](const std::uint32_t shift, const std::uint32_t mask,
                              const std::uint32_t sentinel) -> std::int32_t {
    const std::uint32_t value = (packed_time >> shift) & mask;
    return value == sentinel ? -1 : static_cast<std::int32_t>(value);
  };

  return {
      decode(0, 0x3Fu, 0x3Fu),  decode(6, 0x1Fu, 0x1Fu), decode(11, 0x7u, 0x7u),
      decode(14, 0x3Fu, 0x3Fu), decode(20, 0xFu, 0xFu),  decode(24, 0x1Fu, 0x1Fu),
  };
}

std::int64_t HolidayDateFieldsToNsSince2000(const HolidayDateFields &fields) {
  return ::openwow::core::ida::CalendarTimeNsSince2000FromFields({
      .year = 2000 + fields[5],
      .month = fields[4] + 1,
      .day = fields[3] + 1,
      .hour = fields[1],
      .minute = fields[0],
      .second = 0,
      .nanoseconds = 0,
  });
}

constexpr std::uint32_t kCalendarEventActionThrottleMs = 5000;
constexpr std::uint32_t kCalendarInviteThrottleMs = 2000;

[[nodiscard]] std::uint32_t GetCalendarThrottleTickCount32() noexcept {
  return ::openwow::core::GameClock::GetTickCount32();
}

[[nodiscard]] bool HasCalendarThrottleExpired(const std::uint32_t last_sent_ms,
                                              const std::uint32_t throttle_ms) noexcept {
  if (last_sent_ms == 0) {
    return true;
  }

  return static_cast<std::uint32_t>(GetCalendarThrottleTickCount32() - last_sent_ms) >=
         throttle_ms;
}

HolidayDateFields HolidayDateFieldsFromNsSince2000(const std::int64_t ns_since_2000) {
  const auto breakdown = ::openwow::core::ida::CalendarTimeBreakdownFromNsSince2000(ns_since_2000);
  return {
      breakdown.minute,  breakdown.hour,      breakdown.day_of_week,
      breakdown.day - 1, breakdown.month - 1, breakdown.year - 2000,
  };
}

void AddHolidayMinutes(HolidayDateFields &fields, const std::int64_t minutes) {
  constexpr std::int64_t kNsPerMinute = 60LL * 1000LL * 1000LL * 1000LL;
  fields = HolidayDateFieldsFromNsSince2000(HolidayDateFieldsToNsSince2000(fields) +
                                            minutes * kNsPerMinute);
}

void AddHolidayDays(HolidayDateFields &fields, const std::int64_t days) {
  AddHolidayMinutes(fields, days * 24LL * 60LL);
}

int CompareHolidayDates(const HolidayDateFields &lhs, const HolidayDateFields &rhs) {
  const auto lhs_ns = HolidayDateFieldsToNsSince2000(lhs);
  const auto rhs_ns = HolidayDateFieldsToNsSince2000(rhs);
  if (lhs_ns < rhs_ns) {
    return -1;
  }
  if (lhs_ns > rhs_ns) {
    return 1;
  }
  return 0;
}

std::uint32_t PackHolidayEventTime(const HolidayDateFields &fields) {
  return (static_cast<std::uint32_t>(fields[0]) & 0x3Fu) |
         ((static_cast<std::uint32_t>(fields[1]) & 0x1Fu) << 6) |
         ((static_cast<std::uint32_t>(fields[3]) & 0x3Fu) << 14) |
         ((static_cast<std::uint32_t>(fields[4]) & 0xFu) << 20) |
         ((static_cast<std::uint32_t>(fields[5]) & 0x1Fu) << 24);
}

CalendarVisibleWindow BuildCalendarVisibleWindow(const std::uint32_t view_month,
                                                 const std::uint32_t view_year) {
  CalendarVisibleWindow window{
      .start = {0, 0, -1, 0, static_cast<std::int32_t>(view_month) - 1,
                static_cast<std::int32_t>(view_year) - 2000},
      .end_exclusive = {0, 0, -1, 0, static_cast<std::int32_t>(view_month) - 1,
                        static_cast<std::int32_t>(view_year) - 2000},
  };
  AddHolidayDays(window.start, -6);
  AddHolidayDays(window.end_exclusive, 42);
  return window;
}

bool ResolveHolidayOccurrenceWindow(
    const CalendarHolidaySequenceSource &source, const HolidayDateFields &current_time,
    const CalendarVisibleWindow &window, const std::uint32_t selected_month_zero_based,
    const std::uint32_t selected_year_offset_2000, HolidayDateFields &occurrence_start,
    HolidayDateFields &occurrence_end, bool &used_weekday_adjustment) {
  used_weekday_adjustment = false;

  for (const std::uint32_t packed_time : source.occurrence_packed_times) {
    if (packed_time == 0) {
      break;
    }

    auto candidate_start = DecodeHolidayPackedTime(packed_time);
    if ((source.flags & 1u) != 0u) {
      std::int32_t local_time_fields[6] = {
          candidate_start[0], candidate_start[1], candidate_start[2],
          candidate_start[3], candidate_start[4], candidate_start[5],
      };
      ::openwow::core::ida::CalendarTimeContext time_context{};
      ::openwow::core::ida::Calendar_ConvertToLocalTime(&time_context, local_time_fields);
      candidate_start = {
          local_time_fields[0], local_time_fields[1], local_time_fields[2],
          local_time_fields[3], local_time_fields[4], local_time_fields[5],
      };
    }

    const HolidayDateFields *reference_fields = &current_time;
    std::int32_t weekday_delta = 0;
    bool weekday_adjustment = false;
    if (candidate_start[2] >= 0 && candidate_start[4] < 0 && candidate_start[3] < 0 &&
        candidate_start[5] < 0) {
      reference_fields = &window.start;
      weekday_delta = candidate_start[2] - window.start[2];
      if (weekday_delta < 0) {
        weekday_delta += 7;
      }
      weekday_adjustment = true;
    }

    if (candidate_start[0] < 0) {
      candidate_start[0] = (*reference_fields)[0];
    }
    if (candidate_start[1] < 0) {
      candidate_start[1] = (*reference_fields)[1];
    }
    if (candidate_start[3] < 0) {
      candidate_start[3] = (*reference_fields)[3];
    }
    if (candidate_start[4] < 0) {
      candidate_start[4] = (*reference_fields)[4];
    }
    if (candidate_start[5] < 0) {
      candidate_start[5] = static_cast<std::int32_t>(selected_year_offset_2000);
      if (selected_month_zero_based == 0u && candidate_start[4] == 11) {
        --candidate_start[5];
      }
    }

    if (weekday_delta != 0) {
      AddHolidayDays(candidate_start, weekday_delta);
    }
    if (source.sequence_offset_minutes != 0) {
      AddHolidayMinutes(candidate_start, source.sequence_offset_minutes);
    }

    if (source.repeat_step_minutes == 0) {
      if (CompareHolidayDates(window.end_exclusive, candidate_start) <= 0) {
        continue;
      }

      auto candidate_end = candidate_start;
      AddHolidayMinutes(candidate_end, source.sequence_duration_minutes);
      if (CompareHolidayDates(candidate_end, window.start) <= 0) {
        continue;
      }

      if (candidate_start[0] == 0 && candidate_start[1] == 0) {
        AddHolidayMinutes(candidate_start, 1);
      }
      if (candidate_end[0] == 0 && candidate_end[1] == 0) {
        AddHolidayMinutes(candidate_end, -1);
      }

      occurrence_start = candidate_start;
      occurrence_end = candidate_end;
      used_weekday_adjustment = weekday_adjustment;
      return true;
    }

    if (CompareHolidayDates(window.end_exclusive, candidate_start) <= 0) {
      return false;
    }

    while (true) {
      auto candidate_end = candidate_start;
      AddHolidayMinutes(candidate_end, source.sequence_duration_minutes);
      if (CompareHolidayDates(candidate_end, window.start) > 0) {
        if (candidate_start[0] == 0 && candidate_start[1] == 0) {
          AddHolidayMinutes(candidate_start, 1);
        }
        if (candidate_end[0] == 0 && candidate_end[1] == 0) {
          AddHolidayMinutes(candidate_end, -1);
        }

        occurrence_start = candidate_start;
        occurrence_end = candidate_end;
        used_weekday_adjustment = weekday_adjustment;
        return true;
      }

      AddHolidayMinutes(candidate_start, source.repeat_step_minutes);
      if (CompareHolidayDates(window.end_exclusive, candidate_start) <= 0) {
        return false;
      }
    }
  }

  return false;
}

int CompareCalendarDate(const CalendarIndexDate &lhs, const CalendarIndexDate &rhs) {
  if (lhs.year != rhs.year) {
    return lhs.year < rhs.year ? -1 : 1;
  }
  if (lhs.month != rhs.month) {
    return lhs.month < rhs.month ? -1 : 1;
  }
  if (lhs.day != rhs.day) {
    return lhs.day < rhs.day ? -1 : 1;
  }
  return 0;
}

void AdvanceCalendarDate(CalendarIndexDate &parts) {
  if (parts.year < 0 || parts.month < 0 || parts.day < 0) {
    return;
  }

  openwow::core::ida::CalendarDateFields fields{
      .day = parts.day - 1,
      .month = parts.month - 1,
      .year = parts.year - 2000,
  };
  ::openwow::core::ida::CalendarDateFields_AddDaysLocal(&fields, 1, false);
  parts.year = fields.year + 2000;
  parts.month = fields.month + 1;
  parts.day = fields.day + 1;
}

uint32_t SequenceDisplayRank(uint32_t sequence_index, uint32_t sequence_total) {
  if (sequence_total <= 1) {
    return 4;
  }
  if (sequence_index == 0) {
    return 1;
  }
  if (sequence_index + 1 == sequence_total) {
    return 2;
  }
  return 3;
}

std::uint32_t ComputeWeekdayIndex(const PackedCalendarTimeParts &parts) {

  static constexpr std::array<std::uint32_t, 12> kMonthOffsets = {
      0u, 3u, 2u, 5u, 0u, 3u, 5u, 1u, 4u, 6u, 2u, 4u,
  };

  std::int32_t year = static_cast<std::int32_t>(parts.year);
  if (parts.month < 3u) {
    --year;
  }

  return static_cast<std::uint32_t>((year + year / 4 - year / 100 + year / 400 +
                                     static_cast<std::int32_t>(kMonthOffsets[parts.month - 1u]) +
                                     static_cast<std::int32_t>(parts.day)) %
                                    7);
}

std::string FoldLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

constexpr std::size_t kStormCompareMaxCount = 0x7FFFFFFFu;

constexpr std::array<std::uint8_t, 9> kCalendarInviteStatusSortRank{
    9, 3, 6, 1, 7, 4, 2, 8, 5,
};

const char *TryCalendarInviteClassName(const std::uint8_t class_id) {
  switch (class_id) {
  case 1:
    return "Warrior";
  case 2:
    return "Paladin";
  case 3:
    return "Hunter";
  case 4:
    return "Rogue";
  case 5:
    return "Priest";
  case 6:
    return "Death Knight";
  case 7:
    return "Shaman";
  case 8:
    return "Mage";
  case 9:
    return "Warlock";
  case 11:
    return "Druid";
  default:
    return nullptr;
  }
}

std::string ResolveCalendarInviteSortName(const WorldSession *session,
                                          const CalendarSystemInvite &invite) {
  if (!invite.invitee_name.empty()) {
    return invite.invitee_name;
  }

  if (session == nullptr || invite.invitee_guid == 0) {
    return {};
  }

  const auto guid = ObjectGuid(invite.invitee_guid);

  if (const auto *player = session->objects().GetPlayer(guid)) {
    if (auto name = player->ResolveRetailName(*session); !name.empty()) {
      return name;
    }
  }

  if (const auto *name_info = session->query_cache().GetPlayerName(invite.invitee_guid)) {
    return name_info->name;
  }

  if (const auto *name_entry = session->objects().GetNameEntry(guid)) {
    return name_entry->name;
  }

  return {};
}

std::uint8_t ResolveCalendarInviteSortClassId(const WorldSession *session,
                                              const CalendarSystemInvite &invite) {
  if (invite.class_id != 0) {
    return invite.class_id;
  }

  if (session == nullptr || invite.invitee_guid == 0) {
    return 0;
  }

  const auto guid = ObjectGuid(invite.invitee_guid);
  if (const auto *player = session->objects().GetPlayer(guid)) {
    return player->State().GetClass();
  }

  if (const auto *name_info = session->query_cache().GetPlayerName(invite.invitee_guid)) {
    return name_info->class_id;
  }

  if (const auto *name_entry = session->objects().GetNameEntry(guid)) {
    return name_entry->class_id;
  }

  return 0;
}

bool IsCalendarInviteInCurrentGroup(const CalendarSystemInvite &invite) {
  if (invite.invitee_guid == 0) {
    return false;
  }

  auto &group_system = GroupSystem::Get();
  if (const auto local_player_guid = group_system.GetLocalPlayerGuid().GetRawValue();
      local_player_guid != 0 && local_player_guid == invite.invitee_guid) {
    return true;
  }

  const auto guid = ObjectGuid(invite.invitee_guid);
  return group_system.GetMember(guid).has_value() ||
         group_system.GetMemberByGuid(invite.invitee_guid) != nullptr;
}

struct CalendarInviteSortKey {
  std::string name;
  bool has_name = false;
  std::string class_name;
  bool has_class_name = false;
  bool in_current_group = false;
};

struct CalendarInviteSortable {
  CalendarSystemInvite invite;
  CalendarInviteSortKey key;
};

CalendarInviteSortKey BuildCalendarInviteSortKey(const WorldSession *session,
                                                 const CalendarSystemInvite &invite) {
  CalendarInviteSortKey key;
  key.name = ResolveCalendarInviteSortName(session, invite);
  key.has_name = !key.name.empty();
  if (const auto class_name =
          TryCalendarInviteClassName(ResolveCalendarInviteSortClassId(session, invite));
      class_name != nullptr) {
    key.class_name = class_name;
    key.has_class_name = true;
  }
  key.in_current_group = IsCalendarInviteInCurrentGroup(invite);
  return key;
}

int CompareCalendarInviteNames(const CalendarInviteSortKey &lhs, const CalendarInviteSortKey &rhs) {
  if (!lhs.has_name || !rhs.has_name) {
    return 0;
  }

  return core::SStrCmpUTF8NoCase(lhs.name.c_str(), rhs.name.c_str(), kStormCompareMaxCount);
}

int CompareCalendarInviteSortEntries(const CalendarInviteSortable &lhs,
                                     const CalendarInviteSortable &rhs, const int criterion,
                                     const bool reverse) {
  int result = 0;
  switch (criterion) {
  case 0:
    result = CompareCalendarInviteNames(lhs.key, rhs.key);
    break;
  case 1:
    result = static_cast<int>(lhs.invite.level) - static_cast<int>(rhs.invite.level);
    break;
  case 2:
    if (lhs.key.has_class_name && rhs.key.has_class_name) {
      result = core::SStrCmpNoCaseCollate(lhs.key.class_name.c_str(), rhs.key.class_name.c_str(),
                                          kStormCompareMaxCount);
    }
    break;
  case 3: {
    const auto lhs_rank = lhs.invite.status < kCalendarInviteStatusSortRank.size()
                              ? kCalendarInviteStatusSortRank[lhs.invite.status]
                              : std::uint8_t{0};
    const auto rhs_rank = rhs.invite.status < kCalendarInviteStatusSortRank.size()
                              ? kCalendarInviteStatusSortRank[rhs.invite.status]
                              : std::uint8_t{0};
    result = static_cast<int>(lhs_rank) - static_cast<int>(rhs_rank);
    break;
  }
  case 4:
    if (lhs.key.in_current_group != rhs.key.in_current_group) {
      result = lhs.key.in_current_group ? -1 : 1;
    }
    break;
  case 5:
    result = core::SStrCmpNoCaseCollate(lhs.invite.notes.c_str(), rhs.invite.notes.c_str(),
                                        kStormCompareMaxCount);
    break;
  default:
    break;
  }

  if (reverse) {
    result = -result;
  }
  if (criterion != 0 && result == 0) {
    result = CompareCalendarInviteNames(lhs.key, rhs.key);
  }
  return result;
}

std::string InviteSortCriterionName(const std::uint32_t criterion) {
  switch (criterion) {
  case 0:
    return "name";
  case 1:
    return "level";
  case 2:
    return "class";
  case 3:
    return "status";
  case 4:
    return "party";
  case 5:
    return "notes";
  default:
    return {};
  }
}

bool CalendarDayEventLess(const CalendarSystemEvent &lhs, const CalendarSystemEvent &rhs) {
  if (lhs.pending_invite != rhs.pending_invite) {
    return lhs.pending_invite && !rhs.pending_invite;
  }

  const bool lhs_priority = (lhs.flags & 0x543u) != 0;
  const bool rhs_priority = (rhs.flags & 0x543u) != 0;
  if (lhs_priority != rhs_priority) {
    return lhs_priority && !rhs_priority;
  }

  const bool lhs_holiday_sequence = (lhs.flags & 8u) != 0;
  const bool rhs_holiday_sequence = (rhs.flags & 8u) != 0;
  if (lhs_holiday_sequence != rhs_holiday_sequence) {
    return lhs_holiday_sequence && !rhs_holiday_sequence;
  }

  if (lhs_holiday_sequence && rhs_holiday_sequence &&
      lhs.holiday_sort_priority != rhs.holiday_sort_priority) {
    return lhs.holiday_sort_priority > rhs.holiday_sort_priority;
  }

  const uint32_t lhs_rank = SequenceDisplayRank(lhs.sequence_index, lhs.sequence_total);
  const uint32_t rhs_rank = SequenceDisplayRank(rhs.sequence_index, rhs.sequence_total);
  if (lhs_rank != rhs_rank) {
    return lhs_rank < rhs_rank;
  }

  if (lhs.time != rhs.time) {
    return lhs.time < rhs.time;
  }

  return FoldLower(lhs.title) < FoldLower(rhs.title);
}

bool IsRaidLockoutEvent(const CalendarSystemEvent &event) {
  return (event.flags & 0x80u) != 0;
}

bool IsPendingInviteVisible(const CalendarSystemInvite &invite,
                            const std::unordered_set<std::uint64_t> &ignored_guids) {
  if (invite.status != 0 || invite.sender_guid == 0) {
    return false;
  }
  return ignored_guids.find(invite.sender_guid) == ignored_guids.end();
}

bool MatchesRaidInfoDay(const CalendarRaidInfo &info, const std::optional<uint32_t> packed_time) {
  if (!packed_time.has_value()) {
    return true;
  }

  const auto parts = DecodePackedCalendarTime(*packed_time);
  return info.reset_year == parts.year && info.reset_month == parts.month &&
         info.reset_day == parts.day;
}

bool VisibilityFiltersEqual(const CalendarVisibilityFilters &lhs,
                            const CalendarVisibilityFilters &rhs) {
  return lhs.show_weekly_holidays == rhs.show_weekly_holidays &&
         lhs.show_darkmoon == rhs.show_darkmoon &&
         lhs.show_battleground_holidays == rhs.show_battleground_holidays &&
         lhs.show_lockouts == rhs.show_lockouts && lhs.show_resets == rhs.show_resets;
}

}

CalendarSystem &CalendarSystem::Get() {
  static CalendarSystem instance;
  return instance;
}

void CalendarSystem::SetHolidaySequenceSources(
    const std::vector<CalendarHolidaySequenceSource> &sources,
    const std::vector<CalendarHolidayPresentation> &presentations) {
  std::lock_guard lock(mutex_);
  holiday_sequence_sources_ = sources;
  holiday_occurrences_.clear();
  holiday_presentations_.clear();
  for (const auto &presentation : presentations) {
    holiday_presentations_[presentation.holiday_id] = presentation;
  }
  RebuildMonthEventsLocked();
}

void CalendarSystem::SetHolidayOccurrences(
    const std::vector<CalendarSystemEvent> &occurrences,
    const std::vector<CalendarHolidayPresentation> &presentations) {
  std::lock_guard lock(mutex_);
  holiday_sequence_sources_.clear();
  holiday_occurrences_ = occurrences;
  holiday_presentations_.clear();
  for (const auto &presentation : presentations) {
    holiday_presentations_[presentation.holiday_id] = presentation;
  }
  RebuildMonthEventsLocked();
}

std::optional<CalendarHolidayPresentation>
CalendarSystem::GetHolidayPresentation(const std::uint32_t holiday_id) const {
  std::lock_guard lock(mutex_);
  const auto presentation_it = holiday_presentations_.find(holiday_id);
  if (presentation_it == holiday_presentations_.end()) {
    return std::nullopt;
  }

  return presentation_it->second;
}

bool CalendarSystem::HasRequestedInitialSnapshot() const {
  std::lock_guard lock(mutex_);
  return initial_snapshot_requested_;
}

void CalendarSystem::MarkInitialSnapshotRequested() {
  std::lock_guard lock(mutex_);
  initial_snapshot_requested_ = true;
}

void CalendarSystem::SetMonthEvents(uint32_t month, uint32_t year,
                                    const std::vector<CalendarSystemEvent> &events) {
  std::lock_guard lock(mutex_);
  const MonthKey month_key{month, year};
  month_events_[month_key] = events;

  for (auto it = day_events_.begin(); it != day_events_.end();) {
    const auto key_parts = DecodePackedCalendarTime(it->first);
    if (key_parts.month == month && key_parts.year == year) {
      it = day_events_.erase(it);
      continue;
    }
    ++it;
  }

  for (const auto &source_event : events) {
    auto event = source_event;
    if (event.end_time == 0) {
      event.end_time = event.time;
    }
    day_events_[PackCalendarDayKeyFromPackedTime(event.time)].push_back(event);
    events_[event.event_id] = event;
  }

  for (auto &[date_key, day_list] : day_events_) {
    const auto key_parts = DecodePackedCalendarTime(date_key);
    if (key_parts.month == month && key_parts.year == year) {
      std::stable_sort(day_list.begin(), day_list.end(), CalendarDayEventLess);
    }
  }
}

std::vector<CalendarSystemEvent> CalendarSystem::GetMonthEvents(uint32_t month,
                                                                uint32_t year) const {
  std::lock_guard lock(mutex_);
  auto it = month_events_.find({month, year});
  if (it != month_events_.end())
    return it->second;
  return {};
}

std::vector<CalendarSystemEvent> CalendarSystem::GetDayEvents(uint32_t month, uint32_t day,
                                                              uint32_t year) const {
  std::lock_guard lock(mutex_);
  auto it = day_events_.find(PackCalendarDayKey(year, month, day));
  if (it == day_events_.end()) {
    return {};
  }
  return it->second;
}

size_t CalendarSystem::GetNumDayEvents(uint32_t month, uint32_t day, uint32_t year) const {
  std::lock_guard lock(mutex_);
  auto it = day_events_.find(PackCalendarDayKey(year, month, day));
  return it != day_events_.end() ? it->second.size() : 0;
}

void CalendarSystem::SetEventDetails(uint64_t eventId, const CalendarSystemEvent &event,
                                     const std::vector<CalendarSystemInvite> &invites) {
  std::lock_guard lock(mutex_);
  auto updated_event = event;
  updated_event.pending_invite = HasPendingInviteForEventLocked(eventId);
  if (updated_event.pending_sender_guid == 0) {
    if (const auto it = events_.find(eventId); it != events_.end()) {
      updated_event.pending_sender_guid = it->second.pending_sender_guid;
    } else if (const auto pending_it = std::find_if(pending_.begin(), pending_.end(),
                                                    [eventId](const CalendarSystemInvite &invite) {
                                                      return invite.event_id == eventId &&
                                                             invite.status == 0;
                                                    });
               pending_it != pending_.end()) {
      updated_event.pending_sender_guid = pending_it->sender_guid;
    }
  }
  events_[eventId] = std::move(updated_event);
  event_invites_[eventId] = invites;

  if (context_event_ && context_event_->event_id == eventId) {
    context_event_->selected_invitee_guid = 0;
  }
}

void CalendarSystem::UpsertEventSummary(const CalendarSystemEvent &event) {
  std::lock_guard lock(mutex_);
  auto updated_event = event;
  if (const auto it = events_.find(event.event_id); it != events_.end()) {
    if (updated_event.pending_sender_guid == 0) {
      updated_event.pending_sender_guid = it->second.pending_sender_guid;
    }
  } else if (updated_event.pending_sender_guid == 0) {
    if (const auto pending_it = std::find_if(pending_.begin(), pending_.end(),
                                             [&event](const CalendarSystemInvite &invite) {
                                               return invite.event_id == event.event_id &&
                                                      invite.status == 0;
                                             });
        pending_it != pending_.end()) {
      updated_event.pending_sender_guid = pending_it->sender_guid;
    }
  }
  updated_event.pending_invite = HasPendingInviteForEventLocked(event.event_id);
  events_[event.event_id] = std::move(updated_event);
  RebuildMonthEventsLocked();
}

const CalendarSystemEvent *CalendarSystem::GetEvent(uint64_t eventId) const {
  std::lock_guard lock(mutex_);
  auto it = events_.find(eventId);
  if (it != events_.end())
    return &it->second;
  return nullptr;
}

std::vector<CalendarSystemInvite> CalendarSystem::GetEventInvites(uint64_t eventId) const {
  std::lock_guard lock(mutex_);
  auto it = event_invites_.find(eventId);
  if (it != event_invites_.end())
    return it->second;
  return {};
}

bool CalendarSystem::UpsertEventInvite(const CalendarSystemInvite &invite) {
  if (invite.event_id == 0 || invite.invitee_guid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  auto &invites = event_invites_[invite.event_id];
  const auto it =
      std::find_if(invites.begin(), invites.end(), [&invite](const CalendarSystemInvite &existing) {
        return existing.invitee_guid == invite.invitee_guid;
      });
  if (it == invites.end()) {
    invites.push_back(invite);
    return true;
  }

  bool updated = false;
  if (it->invite_id != invite.invite_id) {
    it->invite_id = invite.invite_id;
    updated = true;
  }
  if (!invite.invitee_name.empty() && it->invitee_name != invite.invitee_name) {
    it->invitee_name = invite.invitee_name;
    updated = true;
  }
  if (it->status != invite.status) {
    it->status = invite.status;
    updated = true;
  }
  if (it->response_time != invite.response_time) {
    it->response_time = invite.response_time;
    updated = true;
  }
  if (it->level != invite.level) {
    it->level = invite.level;
    updated = true;
  }
  if (it->rank != invite.rank) {
    it->rank = invite.rank;
    updated = true;
  }
  if (it->invite_type != invite.invite_type) {
    it->invite_type = invite.invite_type;
    updated = true;
  }
  return updated;
}

bool CalendarSystem::SetEventInviteeName(uint64_t eventId, uint64_t inviteeGuid,
                                         const std::string &name) {
  if (inviteeGuid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  auto it = event_invites_.find(eventId);
  if (it == event_invites_.end()) {
    return false;
  }

  bool updated = false;
  for (auto &invite : it->second) {
    if (invite.invitee_guid != inviteeGuid || invite.invitee_name == name) {
      continue;
    }
    invite.invitee_name = name;
    updated = true;
  }

  return updated;
}

bool CalendarSystem::SetEventSelfInviteState(const std::uint64_t eventId,
                                             const std::uint64_t selfInviteId,
                                             const std::uint8_t inviteStatus) {
  std::lock_guard lock(mutex_);
  const auto it = events_.find(eventId);
  if (it == events_.end()) {
    return false;
  }

  auto &event = it->second;
  bool changed = false;
  bool rebuild_month_events = false;
  if (selfInviteId != 0 && event.self_invite_id != selfInviteId) {
    event.self_invite_id = selfInviteId;
    changed = true;
  }
  if (event.invite_status != inviteStatus) {
    event.invite_status = inviteStatus;
    changed = true;
    rebuild_month_events = true;
  }

  RefreshSelectionStateForEventLocked(event, false);
  if (rebuild_month_events) {
    RebuildMonthEventsLocked();
  }
  return changed;
}

bool CalendarSystem::SetEventInviteStatus(const std::uint64_t eventId,
                                          const std::uint8_t inviteStatus) {
  std::lock_guard lock(mutex_);
  const auto it = events_.find(eventId);
  if (it == events_.end()) {
    return false;
  }

  const bool changed = it->second.invite_status != inviteStatus;
  it->second.invite_status = inviteStatus;
  RefreshSelectionStateForEventLocked(it->second, false);
  if (changed) {
    RebuildMonthEventsLocked();
  }
  return true;
}

bool CalendarSystem::SetInviteStatusForInvitee(const std::uint64_t eventId,
                                               const std::uint64_t inviteeGuid,
                                               const std::uint8_t inviteStatus,
                                               const std::optional<uint32_t> responseTime) {
  if (inviteeGuid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto it = event_invites_.find(eventId);
  if (it == event_invites_.end()) {
    return false;
  }

  for (auto &invite : it->second) {
    if (invite.invitee_guid != inviteeGuid) {
      continue;
    }
    invite.status = inviteStatus;
    if (responseTime.has_value()) {
      invite.response_time = *responseTime;
    }
    return true;
  }
  return false;
}

bool CalendarSystem::SetInviteNotesForInvitee(const std::uint64_t eventId,
                                              const std::uint64_t inviteeGuid,
                                              const std::string &notes) {
  if (inviteeGuid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto it = event_invites_.find(eventId);
  if (it == event_invites_.end()) {
    return false;
  }

  for (auto &invite : it->second) {
    if (invite.invitee_guid != inviteeGuid) {
      continue;
    }
    invite.notes = notes;
    return true;
  }

  return false;
}

bool CalendarSystem::SetInviteModeratorRankForInvitee(const std::uint64_t eventId,
                                                      const std::uint64_t inviteeGuid,
                                                      const std::uint8_t rank) {
  if (inviteeGuid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto it = event_invites_.find(eventId);
  if (it == event_invites_.end()) {
    return false;
  }

  bool found = false;
  for (auto &invite : it->second) {
    if (invite.invitee_guid != inviteeGuid) {
      continue;
    }
    invite.rank = rank;
    found = true;
  }

  return found;
}

bool CalendarSystem::SetDayEventModeratorFlag(const std::uint64_t eventId, const bool isModerator) {
  std::lock_guard lock(mutex_);
  const auto it = events_.find(eventId);
  if (it == events_.end()) {
    return false;
  }

  auto &event = it->second;
  const auto previous_flags = event.invite_mod_status;
  if (isModerator) {
    event.invite_mod_status |= 0x02u;
  } else {
    event.invite_mod_status &= static_cast<std::uint8_t>(~0x02u);
  }

  const auto update_selection = [eventId,
                                 isModerator](std::optional<CalendarContextEventInfo> &info) {
    if (!info || info->event_id != eventId) {
      return;
    }
    info->is_moderator = isModerator;
  };
  update_selection(opened_event_);
  update_selection(context_menu_event_);
  update_selection(context_event_);
  update_selection(clipboard_event_);
  if (event.invite_mod_status != previous_flags) {
    RebuildMonthEventsLocked();
  }
  return true;
}

bool CalendarSystem::RemoveEventInviteByGuid(const uint64_t eventId, const uint64_t inviteeGuid) {
  if (inviteeGuid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto it = event_invites_.find(eventId);
  if (it == event_invites_.end()) {
    return false;
  }

  auto &invites = it->second;
  const auto invite_it = std::find_if(invites.begin(), invites.end(),
                                      [inviteeGuid](const CalendarSystemInvite &invite) {
                                        return invite.invitee_guid == inviteeGuid;
                                      });
  if (invite_it == invites.end()) {
    return false;
  }

  invites.erase(invite_it);
  return true;
}

void CalendarSystem::SetPendingInvites(const std::vector<CalendarSystemInvite> &invites) {
  std::lock_guard lock(mutex_);
  pending_.clear();
  pending_.reserve(invites.size());
  for (const auto &invite : invites) {
    if (invite.status == 0) {
      pending_.push_back(invite);
    }
  }

  for (auto &[event_id, event] : events_) {
    (void)event_id;
    event.pending_invite = false;
  }

  for (const auto &invite : invites) {
    auto event_it = events_.find(invite.event_id);
    if (event_it == events_.end()) {
      continue;
    }

    event_it->second.invite_status = invite.status;
    if (invite.status == 0 && invite.sender_guid != 0) {
      event_it->second.pending_sender_guid = invite.sender_guid;
    }
    event_it->second.pending_invite = HasPendingInviteForEventLocked(invite.event_id);
  }

  RebuildMonthEventsLocked();
}

bool CalendarSystem::SetPendingInviteCount(size_t count) {
  std::lock_guard lock(mutex_);
  if (pending_invite_count_ == count) {
    return false;
  }
  pending_invite_count_ = count;
  return true;
}

size_t CalendarSystem::GetNumPendingInvites() const {
  std::lock_guard lock(mutex_);
  const auto visible_pending = static_cast<size_t>(
      std::count_if(pending_.begin(), pending_.end(), [](const CalendarSystemInvite &invite) {
        return invite.visible_in_pending_list;
      }));
  return std::max(pending_invite_count_, visible_pending);
}

const CalendarSystemInvite *CalendarSystem::GetPendingInvite(size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= pending_.size())
    return nullptr;
  return &pending_[index];
}

bool CalendarSystem::HasPendingInviteForEvent(uint64_t eventId) const {
  std::lock_guard lock(mutex_);
  return HasPendingInviteForEventLocked(eventId);
}

size_t CalendarSystem::GetFirstPendingInviteIndex(uint32_t month, uint32_t day,
                                                  uint32_t year) const {
  std::lock_guard lock(mutex_);
  auto it = day_events_.find(PackCalendarDayKey(year, month, day));
  if (it == day_events_.end()) {
    return 0;
  }

  for (size_t index = 0; index < it->second.size(); ++index) {
    if (it->second[index].pending_invite) {
      return index + 1;
    }
  }

  return 0;
}

bool CalendarSystem::AddPendingInviteForEvent(const uint64_t eventId, const uint64_t senderGuid,
                                              const bool visibleInPendingList) {
  std::lock_guard lock(mutex_);
  const auto event_it = events_.find(eventId);
  if (event_it == events_.end()) {
    return false;
  }

  bool changed = false;
  const auto pending_it =
      std::find_if(pending_.begin(), pending_.end(), [eventId](const CalendarSystemInvite &invite) {
        return invite.event_id == eventId && invite.status == 0;
      });
  if (pending_it == pending_.end()) {
    CalendarSystemInvite invite{};
    invite.event_id = eventId;
    invite.status = 0;
    invite.sender_guid = senderGuid;
    invite.visible_in_pending_list = visibleInPendingList;
    pending_.push_back(std::move(invite));
    changed = true;
  } else {
    if (pending_it->sender_guid != senderGuid) {
      pending_it->sender_guid = senderGuid;
      changed = true;
    }
    if (pending_it->visible_in_pending_list != visibleInPendingList) {
      pending_it->visible_in_pending_list = visibleInPendingList;
      changed = true;
    }
  }

  if (event_it->second.pending_sender_guid != senderGuid) {
    event_it->second.pending_sender_guid = senderGuid;
    changed = true;
  }
  if (!event_it->second.pending_invite) {
    event_it->second.pending_invite = true;
    changed = true;
  }

  if (!changed) {
    return false;
  }

  RebuildMonthEventsLocked();
  return true;
}

bool CalendarSystem::RefreshPendingInviteVisibility(
    const std::unordered_set<uint64_t> &ignoredGuids) {
  std::lock_guard lock(mutex_);
  bool changed = false;
  for (auto &invite : pending_) {
    const bool visible = IsPendingInviteVisible(invite, ignoredGuids);
    if (invite.visible_in_pending_list == visible) {
      continue;
    }
    invite.visible_in_pending_list = visible;
    changed = true;
  }
  return changed;
}

bool CalendarSystem::RemovePendingInvitesForEvent(const uint64_t eventId) {
  std::lock_guard lock(mutex_);
  const auto original_size = pending_.size();
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                [eventId](const CalendarSystemInvite &invite) {
                                  return invite.event_id == eventId;
                                }),
                 pending_.end());
  const auto removed_pending = original_size - pending_.size();
  bool changed = removed_pending != 0;
  if (auto event_it = events_.find(eventId); event_it != events_.end()) {
    const bool has_pending_invite = HasPendingInviteForEventLocked(eventId);
    if (event_it->second.pending_invite != has_pending_invite) {
      event_it->second.pending_invite = has_pending_invite;
      changed = true;
    }
  }
  if (!changed) {
    return false;
  }

  RebuildMonthEventsLocked();
  return true;
}

bool CalendarSystem::UpdateEventFromAlert(uint64_t eventId, uint32_t old_time,
                                          const CalendarSystemEvent &updated_event) {
  std::lock_guard lock(mutex_);
  const auto it = events_.find(eventId);
  if (it == events_.end()) {
    return false;
  }

  auto &event = it->second;
  const bool had_single_day_time =
      event.end_time == 0 || event.end_time == event.time || event.end_time == old_time;

  event.title = updated_event.title;
  event.flags = updated_event.flags;
  event.type = updated_event.type;
  event.dungeon_id = updated_event.dungeon_id;
  event.time = updated_event.time;
  event.invite_type = updated_event.invite_type;
  if (had_single_day_time) {
    event.end_time = updated_event.time;
  }

  RefreshSelectionStateForEventLocked(event, true);
  RebuildMonthEventsLocked();
  return true;
}

bool CalendarSystem::HasEvent(uint64_t eventId) const {
  std::lock_guard lock(mutex_);
  return events_.find(eventId) != events_.end();
}

bool CalendarSystem::RemoveEventById(uint64_t eventId) {
  std::lock_guard lock(mutex_);
  const auto event_it = events_.find(eventId);
  if (event_it == events_.end()) {
    return false;
  }

  const bool remove_raid_info = IsRaidLockoutEvent(event_it->second);
  events_.erase(event_it);

  event_invites_.erase(eventId);
  if (pending_invite_name_query_event_id_ == eventId) {
    pending_invite_name_query_event_id_ = 0;
    pending_invite_name_query_guids_.clear();
    pending_invite_name_query_action_.reset();
  }
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                [eventId](const CalendarSystemInvite &invite) {
                                  return invite.event_id == eventId;
                                }),
                 pending_.end());
  if (remove_raid_info) {
    raid_infos_.erase(std::remove_if(raid_infos_.begin(), raid_infos_.end(),
                                     [eventId](const CalendarRaidInfo &raid) {
                                       return raid.instance_id == eventId;
                                     }),
                      raid_infos_.end());
  }

  ClearSelectionStateForEventLocked(eventId);
  RebuildMonthEventsLocked();
  return true;
}

void CalendarSystem::SetViewMonth(uint32_t month, uint32_t year) {
  std::lock_guard lock(mutex_);
  if (view_month_ == month && view_year_ == year && has_view_month_selection_) {
    return;
  }
  view_month_ = month;
  view_year_ = year;
  has_view_month_selection_ = true;
  RebuildMonthEventsLocked();
}

uint32_t CalendarSystem::GetViewMonth() const {
  std::lock_guard lock(mutex_);
  return view_month_;
}

uint32_t CalendarSystem::GetViewYear() const {
  std::lock_guard lock(mutex_);
  return view_year_;
}

bool CalendarSystem::HasViewMonthSelection() const {
  std::lock_guard lock(mutex_);
  return has_view_month_selection_;
}

void CalendarSystem::SetActionPending(bool pending) {
  std::lock_guard lock(mutex_);
  action_pending_ = pending;
}

bool CalendarSystem::IsActionPending() const {
  std::lock_guard lock(mutex_);
  return action_pending_;
}

void CalendarSystem::SyncCurrentTime(const uint32_t packedTime) {
  std::lock_guard lock(mutex_);
  current_time_anchor_ns_since_2000_ = PackedCalendarTimeToNsSince2000(packedTime);
  current_time_anchor_tick_ms_ = ::openwow::core::GameClock::GetTickCount32();
}

std::optional<uint32_t> CalendarSystem::GetCurrentTimePacked() const {
  std::lock_guard lock(mutex_);
  if (!current_time_anchor_ns_since_2000_.has_value()) {
    return std::nullopt;
  }

  constexpr std::int64_t kNsPerMillisecond = 1000LL * 1000LL;
  const auto elapsed_ms = static_cast<std::uint32_t>(::openwow::core::GameClock::GetTickCount32() -
                                                     current_time_anchor_tick_ms_);
  return NsSince2000ToPackedCalendarTime(*current_time_anchor_ns_since_2000_ +
                                         static_cast<std::int64_t>(elapsed_ms) * kNsPerMillisecond);
}

bool CalendarSystem::HasIndexedDayEventNameReference(const uint64_t playerGuid) const {
  if (playerGuid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  for (const auto &[date_key, events] : day_events_) {
    (void)date_key;
    for (const auto &event : events) {
      if (event.creator_guid == playerGuid || event.pending_sender_guid == playerGuid) {
        return true;
      }
    }
  }

  return false;
}

void CalendarSystem::ClearServerEventSummariesForNoGuild() {
  std::lock_guard lock(mutex_);

  for (auto it = events_.begin(); it != events_.end();) {
    if ((it->second.flags & 0x440u) == 0) {
      ++it;
      continue;
    }
    it = events_.erase(it);
  }

  if (clipboard_event_ && (clipboard_event_->flags & 0x440u) != 0) {
    clipboard_event_.reset();
  }

  RebuildMonthEventsLocked();
}

void CalendarSystem::Reset() {
  std::lock_guard lock(mutex_);
  initial_snapshot_requested_ = false;
  month_events_.clear();
  day_events_.clear();
  events_.clear();
  event_invites_.clear();
  holiday_sequence_sources_.clear();
  holiday_occurrences_.clear();
  holiday_presentations_.clear();
  pending_.clear();
  pending_invite_count_ = 0;
  view_month_ = 1;
  view_year_ = 2009;
  has_view_month_selection_ = false;
  action_pending_ = false;
  current_time_anchor_ns_since_2000_.reset();
  current_time_anchor_tick_ms_ = 0;

  opened_event_.reset();
  context_menu_event_.reset();
  context_event_.reset();
  raid_infos_.clear();
  raid_reset_schedules_.clear();
  visibility_filters_ = {};
  pending_invite_name_query_event_id_ = 0;
  pending_invite_name_query_guids_.clear();
  pending_invite_name_query_action_.reset();
  pending_event_list_name_query_guids_.clear();
  clipboard_event_.reset();
  last_event_action_sent_ms_ = 0;
  last_invite_sent_ms_ = 0;
  selected_invite_id_ = 0;
}

void CalendarSystem::ReplaceEvents(const std::vector<CalendarSystemEvent> &events) {
  std::lock_guard lock(mutex_);
  events_.clear();

  for (const auto &source_event : events) {
    auto event = source_event;
    if (event.end_time == 0) {
      event.end_time = event.time;
    }
    event.pending_invite = HasPendingInviteForEventLocked(event.event_id);
    events_[event.event_id] = std::move(event);
  }

  for (const auto &invite : pending_) {
    auto event_it = events_.find(invite.event_id);
    if (event_it == events_.end()) {
      continue;
    }

    if (invite.sender_guid != 0) {
      event_it->second.pending_sender_guid = invite.sender_guid;
    }
    event_it->second.invite_status = invite.status;
    if (invite.status == 0) {
      event_it->second.pending_invite = true;
    }
  }

  RebuildMonthEventsLocked();
}

void CalendarSystem::RebuildMonthEventsLocked() {
  month_events_.clear();
  day_events_.clear();

  const auto is_event_visible = [this](const CalendarSystemEvent &event) {
    if ((event.flags & 0x80u) != 0u) {
      return visibility_filters_.show_lockouts;
    }
    if ((event.flags & 0x200u) != 0u) {
      return visibility_filters_.show_resets;
    }
    if ((event.flags & 0x008u) == 0u) {
      return true;
    }

    switch (event.holiday_filter_type) {
    case 0:
      return visibility_filters_.show_weekly_holidays;
    case 1:
      return visibility_filters_.show_darkmoon;
    case 2:
      return visibility_filters_.show_battleground_holidays;
    default:
      return true;
    }
  };

  auto append_event = [this, &is_event_visible](const CalendarSystemEvent &source_event) {
    if (!is_event_visible(source_event)) {
      return;
    }

    auto event = source_event;
    if (event.end_time == 0) {
      event.end_time = event.time;
    }

    auto current = DecodeCalendarIndexDate(event.time);
    auto end = DecodeCalendarIndexDate(event.end_time);
    if (end.day < 0) {
      end.day = 31;
    }
    if (CompareCalendarDate(end, current) < 0) {
      end = current;
      event.end_time = event.time;
    }

    uint32_t sequence_total = 1;
    {
      auto probe = current;
      while (CompareCalendarDate(probe, end) < 0) {
        AdvanceCalendarDate(probe);
        ++sequence_total;
      }
    }

    for (uint32_t sequence_index = 0; sequence_index < sequence_total; ++sequence_index) {
      auto indexed_event = event;
      indexed_event.sequence_index = sequence_index;
      indexed_event.sequence_total = sequence_total;

      const auto date_key = PackCalendarDayKey(static_cast<std::uint32_t>(current.year),
                                               static_cast<std::uint32_t>(current.month),
                                               static_cast<std::uint32_t>(current.day));
      day_events_[date_key].push_back(indexed_event);
      month_events_[{static_cast<std::uint32_t>(current.month),
                     static_cast<std::uint32_t>(current.year)}]
          .push_back(indexed_event);

      if (sequence_index + 1 < sequence_total) {
        AdvanceCalendarDate(current);
      }
    }
  };

  const auto &ignore = IgnoreSystem::Get();
  for (const auto &[event_id, source_event] : events_) {
    (void)event_id;
    if (source_event.creator_guid != 0 && ignore.IsIgnored(source_event.creator_guid)) {
      continue;
    }
    append_event(source_event);
  }

  for (const auto &holiday_event : holiday_occurrences_) {
    append_event(holiday_event);
  }

  std::uint32_t base_year = view_year_;
  std::uint32_t base_month = view_month_;
  if (!has_view_month_selection_ && current_time_anchor_ns_since_2000_.has_value()) {
    const auto parts = DecodePackedCalendarTime(
        NsSince2000ToPackedCalendarTime(*current_time_anchor_ns_since_2000_));
    base_year = parts.year;
    base_month = parts.month;
  }
  const auto visible_window = BuildCalendarVisibleWindow(base_month, base_year);
  const auto window_start_ns = HolidayDateFieldsToNsSince2000(visible_window.start);
  const auto window_end_ns = HolidayDateFieldsToNsSince2000(visible_window.end_exclusive);

  if (current_time_anchor_ns_since_2000_.has_value()) {
    const auto current_time = HolidayDateFieldsFromNsSince2000(*current_time_anchor_ns_since_2000_);
    for (const auto &source : holiday_sequence_sources_) {
      HolidayDateFields occurrence_start{};
      HolidayDateFields occurrence_end{};
      bool used_weekday_adjustment = false;
      if (!ResolveHolidayOccurrenceWindow(source, current_time, visible_window, base_month - 1,
                                          base_year - 2000, occurrence_start, occurrence_end,
                                          used_weekday_adjustment)) {
        continue;
      }

      CalendarSystemEvent event{};
      event.event_id = source.holiday_id;
      event.title = source.title;
      event.description = source.description;
      event.type = kCalendarHolidayEventType;
      event.holiday_sort_priority = source.holiday_sort_priority;
      event.holiday_filter_type = source.holiday_filter_type;
      event.flags = 0x008u;

      const std::int64_t repeat_step_minutes =
          used_weekday_adjustment ? 7LL * 24LL * 60LL : source.repeat_step_minutes;
      while (CompareHolidayDates(occurrence_start, visible_window.end_exclusive) < 0) {
        event.time = PackHolidayEventTime(occurrence_start);
        event.end_time = PackHolidayEventTime(occurrence_end);
        append_event(event);
        if (repeat_step_minutes == 0) {
          break;
        }
        AddHolidayMinutes(occurrence_start, repeat_step_minutes);
        AddHolidayMinutes(occurrence_end, repeat_step_minutes);
      }
    }
  }

  for (const auto &schedule : raid_reset_schedules_) {
    if (schedule.period_minutes == 0 || schedule.first_reset_time == 0) {
      continue;
    }

    const auto period_ns =
        static_cast<std::int64_t>(schedule.period_minutes) * kCalendarNsPerMinute;
    auto occurrence_ns = PackedCalendarTimeToNsSince2000(schedule.first_reset_time);
    if (occurrence_ns < window_start_ns) {
      const auto skipped_steps = (window_start_ns - occurrence_ns) / period_ns;
      occurrence_ns += skipped_steps * period_ns;
      while (occurrence_ns < window_start_ns) {
        occurrence_ns += period_ns;
      }
    }

    while (occurrence_ns < window_end_ns) {
      CalendarSystemEvent event{};
      event.title = schedule.title;
      event.map_id = schedule.map_id;
      event.time = NsSince2000ToPackedCalendarTime(occurrence_ns);
      event.end_time = event.time;
      event.flags = 0x200u;
      append_event(event);
      occurrence_ns += period_ns;
    }
  }

  for (auto &[date_key, events] : day_events_) {
    (void)date_key;
    std::stable_sort(events.begin(), events.end(), CalendarDayEventLess);
  }
}

bool CalendarSystem::HasPendingInviteForEventLocked(uint64_t eventId) const {
  return HasPendingInviteEntryLocked(eventId);
}

bool CalendarSystem::HasPendingInviteEntryLocked(uint64_t eventId) const {
  return std::any_of(pending_.begin(), pending_.end(),
                     [eventId](const CalendarSystemInvite &invite) {
                       return invite.event_id == eventId && invite.status == 0;
                     });
}

void CalendarSystem::ClearSelectionStateForEventLocked(const std::uint64_t eventId) {
  const auto clear = [eventId](std::optional<CalendarContextEventInfo> &info) {
    if (info && info->event_id == eventId) {
      info.reset();
    }
  };

  clear(opened_event_);
  clear(context_menu_event_);
  clear(context_event_);
  clear(clipboard_event_);
}

void CalendarSystem::RefreshSelectionStateForEventLocked(const CalendarSystemEvent &event,
                                                         const bool update_title) {
  const auto refresh = [&](std::optional<CalendarContextEventInfo> &info,
                           const bool allow_title_update) {
    if (!info || info->event_id != event.event_id) {
      return;
    }

    info->creator_guid = event.creator_guid;
    if (event.self_invite_id != 0) {
      info->self_invite_id = event.self_invite_id;
    }
    info->event_type = event.type;
    info->flags = event.flags;
    info->invite_status = event.invite_status;
    info->invite_type = event.invite_type;
    info->sequence_index = event.sequence_index;
    info->sequence_total = event.sequence_total;
    info->map_id = event.map_id;
    info->dungeon_id = event.dungeon_id;
    ApplyPackedTimeToContextInfo(event.time, *info);
    if (allow_title_update) {
      info->title = event.title;
    }
  };

  refresh(opened_event_, false);
  refresh(context_menu_event_, false);
  refresh(context_event_, update_title);
  refresh(clipboard_event_, false);
}

void CalendarSystem::SetOpenedEvent(const CalendarContextEventInfo &info) {
  std::lock_guard lock(mutex_);
  opened_event_ = info;
}

void CalendarSystem::ClearOpenedEvent() {
  std::lock_guard lock(mutex_);
  opened_event_.reset();
}

const CalendarContextEventInfo *CalendarSystem::GetOpenedEvent() const {
  std::lock_guard lock(mutex_);
  return opened_event_ ? &*opened_event_ : nullptr;
}

void CalendarSystem::SetContextMenuEvent(const CalendarContextEventInfo &info) {
  std::lock_guard lock(mutex_);
  context_menu_event_ = info;
}

void CalendarSystem::ClearContextMenuEvent() {
  std::lock_guard lock(mutex_);
  context_menu_event_.reset();
}

const CalendarContextEventInfo *CalendarSystem::GetContextMenuEvent() const {
  std::lock_guard lock(mutex_);
  return context_menu_event_ ? &*context_menu_event_ : nullptr;
}

void CalendarSystem::SetContextEvent(const CalendarContextEventInfo &info) {
  std::lock_guard lock(mutex_);
  context_event_ = info;
}

void CalendarSystem::ClearContextEvent() {
  std::lock_guard lock(mutex_);
  context_event_.reset();
}

const CalendarContextEventInfo *CalendarSystem::GetContextEvent() const {
  std::lock_guard lock(mutex_);
  return context_event_ ? &*context_event_ : nullptr;
}

bool CalendarSystem::UpdateContextEventFromUpdatedAlert(
    const std::uint64_t event_id,
    const std::uint32_t flags,
    const std::uint32_t new_date,
    const std::uint8_t event_type,
    const std::uint32_t dungeon_id,
    const std::string &title,
    const std::string &description,
    const std::uint8_t repeat_option,
    const std::uint32_t max_invites,
    const std::uint32_t second_packed_time) {
  std::lock_guard lock(mutex_);
  if (!context_event_ || context_event_->event_id != event_id) {
    return false;
  }

  context_event_->flags = flags;

  context_event_->event_type = event_type;

  context_event_->dungeon_id = static_cast<std::int32_t>(dungeon_id);

  context_event_->title = title;

  context_event_->description = description;

  context_event_->repeat_option = repeat_option;

  context_event_->max_invites = max_invites;

  ApplyPackedTimeToContextInfo(new_date, *context_event_);

  context_event_->secondary_time_packed = second_packed_time;

  return true;
}

std::optional<CalendarHolidayInfo>
CalendarSystem::GetHolidayInfo(uint32_t month, uint32_t day, uint32_t year, uint32_t index) const {
  std::lock_guard lock(mutex_);
  const auto events_it = day_events_.find(PackCalendarDayKey(year, month, day));
  if (events_it == day_events_.end() || index >= events_it->second.size()) {
    return std::nullopt;
  }

  const auto &event = events_it->second[index];
  if ((event.flags & 0x008u) == 0) {
    return std::nullopt;
  }

  const auto holiday_id = static_cast<std::uint32_t>(event.event_id);
  const auto presentation_it = holiday_presentations_.find(holiday_id);
  if (presentation_it == holiday_presentations_.end()) {
    return std::nullopt;
  }

  CalendarHolidayInfo info;
  info.name = presentation_it->second.name;
  info.description = presentation_it->second.description;
  info.texture = presentation_it->second.texture;
  info.start_time = event.time;
  info.end_time = event.end_time;
  return info;
}

std::string CalendarSystem::GetHolidayName(const std::uint32_t holiday_id) const {
  std::lock_guard lock(mutex_);
  const auto presentation_it = holiday_presentations_.find(holiday_id);
  if (presentation_it == holiday_presentations_.end()) {
    return {};
  }

  return presentation_it->second.name;
}

std::string CalendarSystem::GetHolidayTexture(const std::uint32_t holiday_id) const {
  std::lock_guard lock(mutex_);
  const auto presentation_it = holiday_presentations_.find(holiday_id);
  if (presentation_it == holiday_presentations_.end()) {
    return {};
  }

  return presentation_it->second.texture;
}

void CalendarSystem::SetRaidInfoList(const std::vector<CalendarRaidInfo> &raids) {
  std::lock_guard lock(mutex_);
  raid_infos_ = raids;
}

std::optional<CalendarRaidInfo> CalendarSystem::GetRaidInfo(uint32_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= raid_infos_.size())
    return std::nullopt;
  return raid_infos_[index];
}

size_t CalendarSystem::GetNumRaidInfo() const {
  std::lock_guard lock(mutex_);
  return raid_infos_.size();
}

void CalendarSystem::SetRaidResetSchedules(
    const std::vector<CalendarRaidResetSchedule> &schedules) {
  std::lock_guard lock(mutex_);
  raid_reset_schedules_ = schedules;
  RebuildMonthEventsLocked();
}

void CalendarSystem::AddRaidInfo(const CalendarRaidInfo &raid) {
  std::lock_guard lock(mutex_);
  for (auto &existing : raid_infos_) {
    if (existing.map_id != raid.map_id || existing.difficulty != raid.difficulty ||
        existing.instance_id != raid.instance_id) {
      continue;
    }
    existing = raid;
    return;
  }
  raid_infos_.push_back(raid);
}

bool CalendarSystem::RemoveRaidInfo(const uint32_t mapId, const std::optional<int32_t> difficulty,
                                    const std::optional<uint64_t> instanceId,
                                    const std::optional<uint32_t> resetPackedTime) {
  std::lock_guard lock(mutex_);
  const auto original_size = raid_infos_.size();
  raid_infos_.erase(std::remove_if(raid_infos_.begin(), raid_infos_.end(),
                                   [=](const CalendarRaidInfo &raid) {
                                     if (raid.map_id != mapId) {
                                       return false;
                                     }
                                     if (difficulty.has_value() &&
                                         raid.difficulty != static_cast<uint8_t>(*difficulty)) {
                                       return false;
                                     }
                                     if (instanceId.has_value() &&
                                         raid.instance_id != *instanceId) {
                                       return false;
                                     }
                                     return MatchesRaidInfoDay(raid, resetPackedTime);
                                   }),
                    raid_infos_.end());
  return raid_infos_.size() != original_size;
}

bool CalendarSystem::AddRaidLockoutEvent(const CalendarSystemEvent &event) {
  if (!IsRaidLockoutEvent(event)) {
    return false;
  }

  std::lock_guard lock(mutex_);
  auto updated_event = event;
  if (updated_event.end_time == 0) {
    updated_event.end_time = updated_event.time;
  }
  events_[updated_event.event_id] = std::move(updated_event);
  RebuildMonthEventsLocked();
  return true;
}

bool CalendarSystem::RemoveRaidLockoutEvents(const uint32_t mapId,
                                             const std::optional<int32_t> difficulty,
                                             const std::optional<uint64_t> instanceId,
                                             const std::optional<uint32_t> resetPackedTime) {
  std::lock_guard lock(mutex_);
  const auto original_size = events_.size();
  for (auto it = events_.begin(); it != events_.end();) {
    const auto &event = it->second;
    if (!IsRaidLockoutEvent(event) || event.map_id != mapId ||
        (difficulty.has_value() && event.dungeon_id != *difficulty) ||
        (instanceId.has_value() && event.event_id != *instanceId) ||
        (resetPackedTime.has_value() && !SamePackedCalendarDay(event.time, *resetPackedTime))) {
      ++it;
      continue;
    }
    it = events_.erase(it);
  }

  if (events_.size() == original_size) {
    return false;
  }

  RebuildMonthEventsLocked();
  return true;
}

bool CalendarSystem::RemoveAllRaidLockoutEvents() {
  std::lock_guard lock(mutex_);
  const auto original_size = events_.size();
  for (auto it = events_.begin(); it != events_.end();) {
    if (!IsRaidLockoutEvent(it->second)) {
      ++it;
      continue;
    }
    it = events_.erase(it);
  }
  if (events_.size() == original_size) {
    return false;
  }

  RebuildMonthEventsLocked();
  return true;
}

bool CalendarSystem::UpdateRaidLockoutEventTime(const uint32_t mapId, const uint32_t difficulty,
                                                const uint32_t oldPackedTime,
                                                const uint32_t newPackedTime) {
  if (SamePackedCalendarDay(oldPackedTime, newPackedTime)) {
    return false;
  }

  std::lock_guard lock(mutex_);
  bool updated = false;
  const auto new_parts = DecodePackedCalendarTime(newPackedTime);
  for (auto &[event_id, event] : events_) {
    (void)event_id;
    if (!IsRaidLockoutEvent(event) || event.map_id != mapId ||
        event.dungeon_id != static_cast<int32_t>(difficulty) ||
        !SamePackedCalendarDay(event.time, oldPackedTime)) {
      continue;
    }
    event.time = newPackedTime;
    event.end_time = newPackedTime;
    updated = true;
  }

  for (auto &raid_info : raid_infos_) {
    if (raid_info.map_id != mapId ||
        raid_info.difficulty != static_cast<std::uint8_t>(difficulty) ||
        !MatchesRaidInfoDay(raid_info, oldPackedTime)) {
      continue;
    }
    raid_info.reset_month = new_parts.month;
    raid_info.reset_day = new_parts.day;
    raid_info.reset_year = new_parts.year;
    updated = true;
  }

  if (!updated) {
    return false;
  }

  RebuildMonthEventsLocked();
  return true;
}

void CalendarSystem::SetVisibilityFilters(const CalendarVisibilityFilters &filters) {
  std::lock_guard lock(mutex_);
  if (VisibilityFiltersEqual(visibility_filters_, filters)) {
    return;
  }
  visibility_filters_ = filters;
  RebuildMonthEventsLocked();
}

void CalendarSystem::ApplyInviteSortRequest(uint32_t criterion, bool toggle_reverse) {
  std::lock_guard lock(mutex_);
  if (!context_event_) {
    return;
  }

  if (context_event_->invite_sort_criterion == criterion) {
    if (toggle_reverse) {
      context_event_->invite_sort_reverse = !context_event_->invite_sort_reverse;
    }
    return;
  }

  context_event_->invite_sort_criterion = criterion;
  context_event_->invite_sort_reverse = false;
}

std::string CalendarSystem::GetSortCriterion() const {
  std::lock_guard lock(mutex_);
  if (!context_event_) {
    return {};
  }
  return InviteSortCriterionName(context_event_->invite_sort_criterion);
}

bool CalendarSystem::GetSortReverse() const {
  std::lock_guard lock(mutex_);
  return context_event_ ? context_event_->invite_sort_reverse : false;
}

bool CalendarSystem::SortInvitesByCriterion(uint64_t eventId, int criterion, bool reverse,
                                            const WorldSession *session) {
  std::lock_guard lock(mutex_);
  auto it = event_invites_.find(eventId);
  if (it == event_invites_.end())
    return false;

  auto &invites = it->second;

  std::vector<uint64_t> before;
  before.reserve(invites.size());
  for (const auto &inv : invites)
    before.push_back(inv.invite_id);

  std::vector<CalendarInviteSortable> sortable_invites;
  sortable_invites.reserve(invites.size());
  for (auto &invite : invites) {
    auto key = BuildCalendarInviteSortKey(session, invite);
    sortable_invites.push_back(CalendarInviteSortable{std::move(invite), std::move(key)});
  }

  std::sort(
      sortable_invites.begin(), sortable_invites.end(),
      [criterion, reverse](const CalendarInviteSortable &lhs, const CalendarInviteSortable &rhs) {
        return CompareCalendarInviteSortEntries(lhs, rhs, criterion, reverse) < 0;
      });

  for (std::size_t index = 0; index < sortable_invites.size(); ++index) {
    invites[index] = std::move(sortable_invites[index].invite);
  }

  for (size_t i = 0; i < invites.size(); ++i) {
    if (invites[i].invite_id != before[i])
      return true;
  }
  return false;
}

void CalendarSystem::SetPendingInviteListNameQueries(
    uint64_t eventId, const std::vector<uint64_t> &guids,
    CalendarInviteLookupCompletionAction completion_action) {
  std::lock_guard lock(mutex_);
  pending_invite_name_query_event_id_ = eventId;
  pending_invite_name_query_action_ = completion_action;
  pending_invite_name_query_guids_.clear();
  for (const auto guid : guids) {
    if (guid != 0) {
      pending_invite_name_query_guids_.insert(guid);
    }
  }
  if (pending_invite_name_query_guids_.empty()) {
    pending_invite_name_query_event_id_ = 0;
    pending_invite_name_query_action_.reset();
  }
}

void CalendarSystem::TrackPendingInviteListNameQuery(
    const uint64_t eventId, const uint64_t guid,
    const CalendarInviteLookupCompletionAction completion_action) {
  if (eventId == 0 || guid == 0) {
    return;
  }

  std::lock_guard lock(mutex_);
  if (pending_invite_name_query_event_id_ != 0 && pending_invite_name_query_event_id_ != eventId) {
    pending_invite_name_query_guids_.clear();
  }

  pending_invite_name_query_event_id_ = eventId;
  pending_invite_name_query_action_ = completion_action;
  pending_invite_name_query_guids_.insert(guid);
}

void CalendarSystem::ClearPendingInviteListNameQueries() {
  std::lock_guard lock(mutex_);
  pending_invite_name_query_event_id_ = 0;
  pending_invite_name_query_guids_.clear();
  pending_invite_name_query_action_.reset();
}

bool CalendarSystem::HasPendingInviteListNameQueries(const uint64_t eventId) const {
  std::lock_guard lock(mutex_);
  return pending_invite_name_query_event_id_ == eventId &&
         !pending_invite_name_query_guids_.empty();
}

std::optional<CalendarPendingInviteNameQueryResolution>
CalendarSystem::ResolvePendingInviteListNameQuery(uint64_t guid) {
  std::lock_guard lock(mutex_);
  if (pending_invite_name_query_guids_.erase(guid) == 0) {
    return std::nullopt;
  }

  CalendarPendingInviteNameQueryResolution resolution;
  resolution.event_id = pending_invite_name_query_event_id_;
  resolution.completion_action = pending_invite_name_query_action_.value_or(
      CalendarInviteLookupCompletionAction::kUpdateInviteList);
  resolution.completed = pending_invite_name_query_guids_.empty();
  if (resolution.completed) {
    pending_invite_name_query_event_id_ = 0;
    pending_invite_name_query_action_.reset();
  }
  return resolution;
}

void CalendarSystem::TrackPendingEventListNameQuery(const uint64_t guid) {
  if (guid == 0) {
    return;
  }

  std::lock_guard lock(mutex_);
  pending_event_list_name_query_guids_.insert(guid);
}

bool CalendarSystem::ResolvePendingEventListNameQuery(const uint64_t guid) {
  if (guid == 0) {
    return false;
  }

  std::lock_guard lock(mutex_);
  return pending_event_list_name_query_guids_.erase(guid) != 0;
}

void CalendarSystem::SetClipboardEvent(const CalendarContextEventInfo &info) {
  std::lock_guard lock(mutex_);
  clipboard_event_ = info;
}

void CalendarSystem::ClearClipboard() {
  std::lock_guard lock(mutex_);
  clipboard_event_.reset();
}

const CalendarContextEventInfo *CalendarSystem::GetClipboardEvent() const {
  std::lock_guard lock(mutex_);
  return clipboard_event_ ? &*clipboard_event_ : nullptr;
}

bool CalendarSystem::HasClipboardEvent() const {
  std::lock_guard lock(mutex_);
  return clipboard_event_ != std::nullopt && clipboard_event_->event_id != 0;
}

void CalendarSystem::MarkEventActionSent() {
  std::lock_guard lock(mutex_);
  last_event_action_sent_ms_ = GetCalendarThrottleTickCount32();
}

bool CalendarSystem::CanSendEventAction() const {
  std::lock_guard lock(mutex_);
  return !action_pending_ &&
         HasCalendarThrottleExpired(last_event_action_sent_ms_, kCalendarEventActionThrottleMs);
}

void CalendarSystem::MarkInviteSent() {
  std::lock_guard lock(mutex_);
  last_invite_sent_ms_ = GetCalendarThrottleTickCount32();
}

bool CalendarSystem::CanSendInvite(const bool bypass_cooldown) const {
  std::lock_guard lock(mutex_);
  return !action_pending_ &&
         (bypass_cooldown ||
          HasCalendarThrottleExpired(last_invite_sent_ms_, kCalendarInviteThrottleMs));
}

MonthInfo CalendarSystem::GetMonthInfo(uint32_t month, uint32_t year) {
  MonthInfo info;

  openwow::core::ida::CalendarDateFields first_day{
      .day = 0,
      .month = static_cast<std::int32_t>(month),
      .year = static_cast<std::int32_t>(year) - 2000,
  };
  ::openwow::core::ida::CalendarDateFields_AddDaysLocal(&first_day, 0, false);
  info.firstWeekday = static_cast<uint32_t>(first_day.weekday);

  openwow::core::ida::CalendarDateFields last_day{
      .day = 0,
      .month = static_cast<std::int32_t>(month == 11 ? 0 : month + 1),
      .year = static_cast<std::int32_t>(year) - 2000 + (month == 11 ? 1 : 0),
  };
  ::openwow::core::ida::CalendarDateFields_AddDaysLocal(&last_day, -1, false);
  info.numDays = static_cast<uint32_t>(last_day.day + 1);

  return info;
}

void CalendarSystem::SetEventInviteStatus(uint32_t inviteIndex, uint32_t status) {
  std::lock_guard lock(mutex_);

  if (!context_event_)
    return;

  const uint64_t event_id = context_event_->event_id;
  auto it = event_invites_.find(event_id);
  if (it == event_invites_.end())
    return;

  auto &invites = it->second;
  if (inviteIndex >= invites.size())
    return;

  if (context_event_->flags & 0x40u) {
    if (!context_event_->is_moderator && !context_event_->is_own_event) {
      return;
    }
  }

  invites[inviteIndex].status = static_cast<uint8_t>(status);
}

std::string CalendarSystem::FormatCalendarDateTime(uint32_t packedTime, bool use24Hour) {
  static constexpr std::array<const char *, 12> kMonthKeys = {
      "FULLDATE_MONTH_JANUARY", "FULLDATE_MONTH_FEBRUARY", "FULLDATE_MONTH_MARCH",
      "FULLDATE_MONTH_APRIL",   "FULLDATE_MONTH_MAY",      "FULLDATE_MONTH_JUNE",
      "FULLDATE_MONTH_JULY",    "FULLDATE_MONTH_AUGUST",   "FULLDATE_MONTH_SEPTEMBER",
      "FULLDATE_MONTH_OCTOBER", "FULLDATE_MONTH_NOVEMBER", "FULLDATE_MONTH_DECEMBER",
  };
  static constexpr std::array<const char *, 7> kWeekdayKeys = {
      "WEEKDAY_SUNDAY",   "WEEKDAY_MONDAY", "WEEKDAY_TUESDAY",  "WEEKDAY_WEDNESDAY",
      "WEEKDAY_THURSDAY", "WEEKDAY_FRIDAY", "WEEKDAY_SATURDAY",
  };

  const auto parts = DecodePackedCalendarTime(packedTime);
  auto &localization = Localization::Get();

  const auto month_index = std::min<std::size_t>(parts.month - 1u, kMonthKeys.size() - 1u);
  const auto weekday_index =
      std::min<std::size_t>(ComputeWeekdayIndex(parts), kWeekdayKeys.size() - 1u);
  const std::string full_date = localization.FormatString(
      localization.GetString("FULLDATE", "FULLDATE"),
      {
          localization.GetString(kWeekdayKeys[weekday_index], kWeekdayKeys[weekday_index]),
          localization.GetString(kMonthKeys[month_index], kMonthKeys[month_index]),
          std::to_string(parts.day),
          std::to_string(parts.year),
          std::to_string(parts.month),
      });

  std::string time_text;
  if (use24Hour) {
    time_text = localization.FormatString(
        localization.GetString("TIME_TWENTYFOURHOURS", "TIME_TWENTYFOURHOURS"),
        {std::to_string(parts.hour), std::to_string(parts.minute)});
  } else {
    std::uint32_t display_hour = parts.hour;
    const char *time_key = "TIME_TWELVEHOURAM";
    if (display_hour == 0u) {
      display_hour = 12u;
    } else if (display_hour == 12u) {
      time_key = "TIME_TWELVEHOURPM";
    } else if (display_hour > 12u) {
      display_hour -= 12u;
      time_key = "TIME_TWELVEHOURPM";
    }

    time_text =
        localization.FormatString(localization.GetString(time_key, time_key),
                                  {std::to_string(display_hour), std::to_string(parts.minute)});
  }

  return localization.FormatString(localization.GetString("FULLDATE_AND_TIME", "FULLDATE_AND_TIME"),
                                   {full_date, time_text});
}

const char *CalendarSystem::GetEventTypeString(uint16_t flags) {
  if (flags & 0x01)
    return "PLAYER";
  if (flags & 0x40)
    return "GUILD_ANNOUNCEMENT";
  if (flags & 0x400)
    return "GUILD_EVENT";
  if (flags & 0x04)
    return "SYSTEM";
  if (flags & 0x08)
    return "HOLIDAY";
  if (flags & 0x80)
    return "RAID_LOCKOUT";
  if (flags & 0x200)
    return "RAID_RESET";
  if (flags & 0x02)
    return "PLAYER";
  if (flags & 0x100)
    return "PLAYER";
  return "";
}

const char *CalendarSystem::GetHolidayPhaseString(uint8_t flags, int sequenceIndex,
                                                  uint32_t sequenceTotal) {
  if ((flags & 8) == 0 || sequenceTotal <= 1)
    return "";
  if (sequenceIndex == 0)
    return "START";
  if (sequenceIndex == static_cast<int>(sequenceTotal) - 1)
    return "END";
  return "ONGOING";
}

const char *CalendarSystem::GetInviteModStatusString(uint8_t modStatus) {
  if (modStatus & 4)
    return "CREATOR";
  if (modStatus & 2)
    return "MODERATOR";
  return "";
}

bool CalendarSystem::IsGuildSignupEvent(uint32_t eventFlags, uint8_t typeFlags) {
  return (eventFlags & 0x400) != 0 && (typeFlags & 8) != 0;
}

void CalendarSystem::SelectInviteByIndex(const uint64_t event_id, const std::size_t invite_index) {
  std::lock_guard lock(mutex_);

  const auto it = event_invites_.find(event_id);
  if (it == event_invites_.end() || invite_index >= it->second.size()) {
    return;
  }

  selected_invite_id_ = it->second[invite_index].invite_id;
  if (context_event_ && context_event_->event_id == event_id) {
    context_event_->selected_invitee_guid = it->second[invite_index].invitee_guid;
  }
}

int CalendarSystem::GetSelectedInviteIndex() const {
  std::lock_guard lock(mutex_);

  if (!context_event_)
    return -1;

  const std::uint64_t selected_invitee_guid = context_event_->selected_invitee_guid;
  if (selected_invitee_guid == 0)
    return -1;

  const uint64_t event_id = context_event_->event_id;
  auto it = event_invites_.find(event_id);
  if (it == event_invites_.end())
    return -1;

  const auto &invites = it->second;
  for (size_t i = 0; i < invites.size(); ++i) {
    if (invites[i].invitee_guid == selected_invitee_guid) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void CalendarSystem::SetSelectedInviteId(uint64_t invite_id) {
  std::lock_guard lock(mutex_);
  selected_invite_id_ = invite_id;
  if (invite_id == 0 && context_event_) {
    context_event_->selected_invitee_guid = 0;
  }
}

uint64_t CalendarSystem::GetSelectedInviteId() const {
  std::lock_guard lock(mutex_);
  return selected_invite_id_;
}

bool SyncActivePlayerCalendarGuildState(WorldSession &session, const std::uint32_t guild_id) {
  if (guild_id != 0) {
    session.interaction().SendCalendarGetCalendar();
    return false;
  }

  CalendarSystem::Get().ClearServerEventSummariesForNoGuild();
  return true;
}

}

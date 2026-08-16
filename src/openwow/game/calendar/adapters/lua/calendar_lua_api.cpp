
#include "openwow/game/calendar/adapters/lua/calendar_lua_api.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/calendar/adapters/protocol/calendar_date_fields_packed.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/localization.h"
#include "openwow/game/name_validation.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/game/error_message.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_result_capacity.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {

static void FireCalendarPermissionError(lua_State *L);
constexpr std::size_t kCalendarEventInfoResultCount = 25;

struct CalendarDate {
  int weekday = 1;
  int month = 1;
  int day = 1;
  int year = 2000;
};

static bool IsCalendarDateWithinClientRange(const CalendarDate &target_date);
static bool IsCalendarCreatableDateValid(lua_State *L, const CalendarDate &target_date);
void FireCalendarError(lua_State *L, const char *error_token,
                       std::optional<int> format_argument = std::nullopt);
void FireCalendarError(lua_State *L, const char *error_token, std::string_view format_argument);

[[nodiscard]] static CalendarDate GetFallbackLocalCalendarDate() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  return {
      .weekday = local.tm_wday + 1,
      .month = local.tm_mon + 1,
      .day = local.tm_mday,
      .year = local.tm_year + 1900,
  };
}

[[nodiscard]] static std::optional<CalendarDate> GetCurrentGameCalendarDate(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (!session) {
    return std::nullopt;
  }

  const std::uint32_t packed_time = session->session().game_time().packed_time;
  return CalendarDate{
      .weekday = static_cast<int>(((packed_time >> 11) & 0x7u) + 1u),
      .month = static_cast<int>(((packed_time >> 20) & 0xFu) + 1u),
      .day = static_cast<int>(((packed_time >> 14) & 0x3Fu) + 1u),
      .year = static_cast<int>(((packed_time >> 24) & 0x1Fu) + 2000u),
  };
}

[[nodiscard]] static CalendarDate GetCurrentCalendarDate(lua_State *L) {
  if (const auto date = GetCurrentGameCalendarDate(L)) {
    return *date;
  }
  return GetFallbackLocalCalendarDate();
}

[[nodiscard]] static CalendarDate OffsetCalendarDateByDays(const CalendarDate &date,
                                                           const int day_offset) {
  ::openwow::core::ida::CalendarDateFields fields{
      .day = date.day - 1,
      .month = date.month - 1,
      .year = date.year - 2000,
  };
  ::openwow::core::ida::CalendarDateFields_AddDaysLocal(&fields, day_offset, false);
  return {
      .weekday = fields.weekday + 1,
      .month = fields.month + 1,
      .day = fields.day + 1,
      .year = fields.year + 2000,
  };
}

[[nodiscard]] static CalendarDate GetCalendarMaxCreateDate(const CalendarDate &current_date) {

  const int zero_based_limit_month = (current_date.month - 1) + 13;
  const CalendarDate limit_month_start{
      .month = (zero_based_limit_month % 12) + 1,
      .day = 1,
      .year = current_date.year + (zero_based_limit_month / 12),
  };
  CalendarDate maximum_date = OffsetCalendarDateByDays(limit_month_start, -1);
  maximum_date.year = std::min(maximum_date.year, 2031);
  return maximum_date;
}

static int WeekdayForDate(const int month, const int day, const int year) {
  ::openwow::core::ida::CalendarDateFields fields{
      .day = day - 1,
      .month = month - 1,
      .year = year - 2000,
  };
  ::openwow::core::ida::CalendarDateFields_AddDaysLocal(&fields, 0, false);
  return fields.weekday + 1;
}

static uint32_t PackedTimeHour(uint32_t packed_time) {
  return (packed_time >> 6) & 0x1F;
}

static uint32_t PackedTimeMinute(uint32_t packed_time) {
  return packed_time & 0x3F;
}

struct CalendarPackedDateTime {
  uint32_t year = 2000;
  uint32_t month = 1;
  uint32_t day = 1;
  uint32_t hour = 0;
  uint32_t minute = 0;
  int32_t weekday = -1;
  int32_t flags = 0;
};

struct CalendarInviteResponseTime {
  int weekday = 0;
  int month = 0;
  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
};

static int DecodePackedCalendarFieldWithSentinel(const uint32_t packed_time, const uint32_t shift,
                                                 const uint32_t mask, const uint32_t sentinel) {
  const int value = static_cast<int>((packed_time >> shift) & mask);
  return value == static_cast<int>(sentinel) ? -1 : value;
}

static CalendarPackedDateTime DecodePackedCalendarDateTime(const uint32_t packed_time) {
  ::openwow::game::CalendarDateFieldsEx fields{};
  ::openwow::game::CalendarPackedTime_UnpackToArray(packed_time, fields);
  constexpr auto kUnset = std::numeric_limits<std::uint32_t>::max();
  const auto human_component = [=](const std::int32_t value,
                                   const std::uint32_t offset) -> std::uint32_t {
    return value < 0 ? kUnset : static_cast<std::uint32_t>(value) + offset;
  };
  return {
      .year = human_component(fields.year, 2000u),
      .month = human_component(fields.month, 1u),
      .day = human_component(fields.day, 1u),
      .hour = human_component(fields.hour, 0u),
      .minute = human_component(fields.minute, 0u),
      .weekday = fields.weekday,
      .flags = fields.flags,
  };
}

static CalendarInviteResponseTime DecodePackedCalendarInviteResponseTime(
    const uint32_t packed_time) {
  const int minute = DecodePackedCalendarFieldWithSentinel(packed_time, 0, 0x3Fu, 0x3Fu);
  const int hour = DecodePackedCalendarFieldWithSentinel(packed_time, 6, 0x1Fu, 0x1Fu);
  const int weekday = DecodePackedCalendarFieldWithSentinel(packed_time, 11, 0x7u, 0x7u);
  const int day = DecodePackedCalendarFieldWithSentinel(packed_time, 14, 0x3Fu, 0x3Fu);
  const int month = DecodePackedCalendarFieldWithSentinel(packed_time, 20, 0xFu, 0xFu);
  const int year = DecodePackedCalendarFieldWithSentinel(packed_time, 24, 0x1Fu, 0x1Fu);

  return {
      .weekday = weekday + 1,
      .month = month + 1,
      .day = day + 1,
      .year = year + 2000,
      .hour = hour,
      .minute = minute,
  };
}

static void PushCalendarInviteResponseTime(lua_State *L, const CalendarInviteResponseTime &value) {
  lua_pushnumber(L, value.weekday);
  lua_pushnumber(L, value.month);
  lua_pushnumber(L, value.day);
  lua_pushnumber(L, value.year);
  lua_pushnumber(L, value.hour);
  lua_pushnumber(L, value.minute);
}

static void PushZeroCalendarInviteResponseTime(lua_State *L) {
  for (int i = 0; i < 6; ++i) {
    lua_pushnumber(L, 0);
  }
}

static uint32_t PackCalendarDateTime(const CalendarPackedDateTime &value) {
  constexpr auto kUnset = std::numeric_limits<std::uint32_t>::max();
  const auto zero_based = [=](const std::uint32_t component,
                              const std::uint32_t offset) -> std::uint32_t {
    return component == kUnset ? kUnset : component - offset;
  };
  const std::uint32_t minute = value.minute;
  const std::uint32_t hour = value.hour;
  const std::uint32_t weekday = static_cast<std::uint32_t>(value.weekday);
  const std::uint32_t day = zero_based(value.day, 1u);
  const std::uint32_t month = zero_based(value.month, 1u);
  const std::uint32_t year = zero_based(value.year, 2000u);
  const std::uint32_t flags = static_cast<std::uint32_t>(value.flags);
  return (minute & 0x3fu) | ((hour & 0x1fu) << 6) | ((weekday & 0x7u) << 11) |
         ((day & 0x3fu) << 14) | ((month & 0xfu) << 20) | ((year & 0x1fu) << 24) |
         ((flags & 0x3u) << 29);
}

static ::openwow::game::CalendarDateFieldsEx
ToCalendarDateFields(const CalendarPackedDateTime &value) {
  constexpr auto kUnset = std::numeric_limits<std::uint32_t>::max();
  const auto zero_based = [=](const std::uint32_t component,
                              const std::uint32_t offset) -> std::int32_t {
    if (component == kUnset) {
      return -1;
    }
    return static_cast<std::int32_t>(component - offset);
  };
  return {
      .minute = zero_based(value.minute, 0u),
      .hour = zero_based(value.hour, 0u),
      .weekday = value.weekday,
      .day = zero_based(value.day, 1u),
      .month = zero_based(value.month, 1u),
      .year = zero_based(value.year, 2000u),
      .flags = value.flags,
  };
}

[[nodiscard]] static bool IsCalendarContextEventAtOrBeforeCurrentTime(
    const ::openwow::game::CalendarSystem &calendar,
    const ::openwow::game::CalendarContextEventInfo &context) {
  const auto current_time = calendar.GetCurrentTimePacked();
  if (!current_time.has_value()) {
    return false;
  }

  const auto event_time = ToCalendarDateFields({
      .year = context.year,
      .month = context.month,
      .day = context.day,
      .hour = context.hour,
      .minute = context.minute,
      .weekday = context.weekday,
      .flags = context.time_flags,
  });
  const auto current = ToCalendarDateFields(DecodePackedCalendarDateTime(*current_time));
  return ::openwow::game::CalendarDateFields_LessOrEqual(event_time, current);
}

static int CompareCalendarDate(const CalendarDate &lhs, const CalendarDate &rhs) {
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

[[nodiscard]] static CalendarDate BuildCalendarDate(const std::uint32_t month,
                                                    const std::uint32_t day,
                                                    const std::uint32_t year) {
  return {
      .month = static_cast<int>(month),
      .day = static_cast<int>(day),
      .year = static_cast<int>(year),
  };
}

static void ApplyPackedEventTimeToContext(const ::openwow::game::CalendarSystemEvent &event,
                                          ::openwow::game::CalendarContextEventInfo &info) {
  const CalendarPackedDateTime date_time = DecodePackedCalendarDateTime(event.time);
  info.month = date_time.month;
  info.day = date_time.day;
  info.year = date_time.year;
  info.hour = date_time.hour;
  info.minute = date_time.minute;
  info.weekday = date_time.weekday;
  info.time_flags = date_time.flags;
}

struct CalendarResolvedMonthInfo {
  int month = 1;
  int year = 2004;
  int num_days = 30;
  int first_weekday = 1;
};

struct CalendarRelativeDayLookup {
  int month = 1;
  int day = 1;
  int year = 2004;
};

static CalendarDate
GetCalendarRelativeMonthBaseDate(lua_State *L, const ::openwow::game::CalendarSystem &calendar) {
  if (calendar.HasViewMonthSelection()) {
    return {
        .month = static_cast<int>(calendar.GetViewMonth()),
        .year = static_cast<int>(calendar.GetViewYear()),
    };
  }

  return GetCurrentCalendarDate(L);
}

[[nodiscard]] static CalendarResolvedMonthInfo BuildCalendarMonthInfo(const int month,
                                                                      const int year) {
  const auto month_info = ::openwow::game::CalendarSystem::GetMonthInfo(
      static_cast<uint32_t>(month - 1), static_cast<uint32_t>(year));
  return {
      .month = month,
      .year = year,
      .num_days = static_cast<int>(month_info.numDays),
      .first_weekday = static_cast<int>(month_info.firstWeekday) + 1,
  };
}

static CalendarResolvedMonthInfo
ResolveCalendarRelativeMonthInfo(lua_State *L,
                                 const ::openwow::game::CalendarSystem &calendar,
                                 const int month_offset) {
  const CalendarDate base_date = GetCalendarRelativeMonthBaseDate(L, calendar);

  int year = base_date.year + month_offset / 12;
  int month_index = (base_date.month - 1) + month_offset % 12;
  if (month_index < 0) {
    month_index += 12;
    --year;
  } else if (month_index >= 12) {
    month_index -= 12;
    ++year;
  }

  if (year > 2030) {
    year = 2030;
  }
  if (year < 2004) {
    year = 2004;
  }
  if (year == 2004 && month_index < 10) {
    month_index = 10;
  }

  return BuildCalendarMonthInfo(month_index + 1, year);
}

static bool ResolveCalendarRelativeDayLookup(lua_State *L,
                                             const ::openwow::game::CalendarSystem &calendar,
                                             const int month_offset, const std::uint32_t day,
                                             CalendarRelativeDayLookup &lookup) {
  if (month_offset < -1 || month_offset > 1) {
    return false;
  }
  if (day == 0 || day > 31) {
    return false;
  }

  const CalendarResolvedMonthInfo target_month =
      ResolveCalendarRelativeMonthInfo(L, calendar, month_offset);
  const CalendarDate target_date{
      .weekday = target_month.first_weekday,
      .month = target_month.month,
      .day = static_cast<int>(day),
      .year = target_month.year,
  };
  if (!IsCalendarDateWithinClientRange(target_date)) {
    return false;
  }

  lookup = {
      .month = target_month.month,
      .day = static_cast<int>(day),
      .year = target_month.year,
  };
  return true;
}

static CalendarResolvedMonthInfo ResolveCalendarAbsoluteMonthInfo(const int requested_month,
                                                                  int requested_year,
                                                                  const int current_year) {

  const auto wrap_i32_subtract = [](const int value, const std::uint32_t amount) {
    const std::uint32_t wrapped = static_cast<std::uint32_t>(value) - amount;
    const std::int64_t signed_value =
        wrapped <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
            ? static_cast<std::int64_t>(wrapped)
            : static_cast<std::int64_t>(wrapped) - (std::int64_t{1} << 32);
    return static_cast<std::int32_t>(signed_value);
  };

  int requested_internal_year = wrap_i32_subtract(requested_year, 2000u);
  const int current_internal_year = current_year - 2000;
  requested_internal_year =
      std::clamp(requested_internal_year, current_internal_year - 5, current_internal_year + 5);
  requested_internal_year = std::clamp(requested_internal_year, 4, 30);

  const int zero_based_month = wrap_i32_subtract(requested_month, 1u);
  const int wrapped_month = zero_based_month % 12;
  const int month_index = wrapped_month < 0 ? -wrapped_month : wrapped_month;
  return BuildCalendarMonthInfo(month_index + 1, requested_internal_year + 2000);
}

[[nodiscard]] static int TruncateLuaNumberToCalendarI32(const lua_Number value) {

  if (!std::isfinite(value) ||
      value < static_cast<lua_Number>(std::numeric_limits<std::int32_t>::min()) ||
      value > static_cast<lua_Number>(std::numeric_limits<std::int32_t>::max())) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(std::trunc(value));
}

[[nodiscard]] static int GetOptionalCalendarMonthOffset(lua_State *L, const int index) {
  if (!lua_isnumber(L, index)) {
    return 0;
  }
  return TruncateLuaNumberToCalendarI32(lua_tonumber(L, index));
}

[[nodiscard]] static std::uint32_t TruncateLuaNumberToCalendarU32(const lua_Number value) {

  if (std::isnan(value)) {
    return 0x80000000u;
  }
  if (value <= 0.0) {
    return 0;
  }
  constexpr auto kMax = static_cast<lua_Number>(std::numeric_limits<std::uint32_t>::max());
  if (value >= kMax) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(std::trunc(value));
}

[[nodiscard]] static std::uint32_t GetOptionalCalendarUnsignedArgumentOrZero(lua_State *L,
                                                                             const int index) {
  if (!lua_isnumber(L, index)) {
    return 0;
  }
  return TruncateLuaNumberToCalendarU32(lua_tonumber(L, index));
}

[[nodiscard]] static int RequireCalendarMonthOffset(lua_State *L, const int index) {
  if (!lua_isnumber(L, index)) {
    luaL_error(L, "Usage: CalendarSetMonth(offsetMonths)");
  }
  return TruncateLuaNumberToCalendarI32(lua_tonumber(L, index));
}

static bool IsCalendarDateWithinClientRange(const CalendarDate &target_date) {
  static constexpr CalendarDate kMinimumDate{
      .weekday = 4,
      .month = 11,
      .day = 24,
      .year = 2004,
  };
  static constexpr CalendarDate kMaximumDate{
      .weekday = 3,
      .month = 12,
      .day = 31,
      .year = 2030,
  };

  if (CompareCalendarDate(target_date, kMinimumDate) < 0) {
    return false;
  }

  return CompareCalendarDate(target_date, kMaximumDate) <= 0;
}

static bool IsCalendarCreatableDateValid(lua_State *L, const CalendarDate &target_date) {
  if (!IsCalendarDateWithinClientRange(target_date)) {
    return false;
  }

  const std::optional<CalendarDate> current_date = GetCurrentGameCalendarDate(L);
  if (!current_date.has_value() || CompareCalendarDate(target_date, *current_date) < 0) {
    return false;
  }

  const CalendarDate maximum_date = GetCalendarMaxCreateDate(*current_date);
  return CompareCalendarDate(target_date, maximum_date) <= 0;
}

static const char *CalendarTypeFromFlags(uint32_t flags) {
  return ::openwow::game::CalendarSystem::GetEventTypeString(
      static_cast<std::uint16_t>(flags));
}

static std::string GetCalendarGlobalString(lua_State *L, const char *key) {
  lua_getglobal(L, key);
  std::string value;
  if (lua_isstring(L, -1)) {
    value = lua_tostring(L, -1);
  }
  lua_pop(L, 1);
  if (!value.empty()) {
    return value;
  }
  return ::openwow::game::Localization::Get().GetString(key, key);
}

template <std::size_t N>
int PushCalendarGlobalStringList(lua_State *L,
                                 const std::array<std::string_view, N> &global_string_keys) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, N, "calendar strings");
  for (const std::string_view key : global_string_keys) {
    const std::string value = GetCalendarGlobalString(L, key.data());
    lua_pushlstring(L, value.data(), value.size());
  }
  return result_count;
}

template <std::size_t N>
int PushCalendarLocalizedStringList(lua_State *L,
                                    const std::array<std::string_view, N> &global_string_keys) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, N, "localized calendar strings");
  for (const std::string_view key : global_string_keys) {
    const std::string key_string(key);
    const std::string value =
        ::openwow::game::Localization::Get().GetString(key_string, key_string);
    lua_pushlstring(L, value.data(), value.size());
  }
  return result_count;
}

static openwow::ui::game::runtime::WorldUiRuntimeContext *GetCalendarGameUiManager(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.world_ui_runtime_context");
  auto *manager = static_cast<openwow::ui::game::runtime::WorldUiRuntimeContext *>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return manager;
}

static void FireCalendarUiEvent(lua_State *L, const char *event_name,
                                std::initializer_list<EventArg> args = {}) {
  if (auto *manager = GetCalendarGameUiManager(L)) {
    manager->frame_events().dispatcher().FireEventArgs(event_name, args);
  }
}

static void SetCalendarActionPending(lua_State *L, const bool pending) {
  ::openwow::game::CalendarSystem::Get().SetActionPending(pending);
  FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_ACTION_PENDING, {pending});
}

static bool EqualsAsciiNoCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

struct CalendarInvitePresentation {
  std::string name;
  std::uint8_t class_id = 0;
};

static std::string ResolveCalendarPlayerName(const ::openwow::game::WorldSession &session,
                                             const std::uint64_t guid_value) {
  if (guid_value == 0) {
    return {};
  }

  const auto guid = ::openwow::game::ObjectGuid(guid_value);

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

static std::string ResolveCalendarEventListPlayerName(
    ::openwow::game::WorldSession &session, const std::uint64_t guid_value) {
  const std::string resolved_name = ResolveCalendarPlayerName(session, guid_value);
  if (!resolved_name.empty() || guid_value == 0) {
    return resolved_name;
  }

  ::openwow::game::CalendarSystem::Get().TrackPendingEventListNameQuery(guid_value);
  (void)session.query_cache().RequestNameQuery(guid_value);
  return {};
}

static CalendarInvitePresentation
ResolveCalendarInvitePresentation(const ::openwow::game::WorldSession &session,
                                  const ::openwow::game::CalendarSystemInvite &invite) {
  CalendarInvitePresentation presentation;
  if (invite.invitee_guid == 0) {
    return presentation;
  }

  const auto guid = ::openwow::game::ObjectGuid(invite.invitee_guid);

  if (const auto *player = session.objects().GetPlayer(guid)) {
    presentation.class_id = player->State().GetClass();
    if (auto name = player->ResolveRetailName(session); !name.empty()) {
      presentation.name = name;
      return presentation;
    }
  }

  if (const auto *name_info = session.query_cache().GetPlayerName(invite.invitee_guid)) {
    presentation.name = name_info->name;
    presentation.class_id = name_info->class_id;
    return presentation;
  }

  if (const auto *name_entry = session.objects().GetNameEntry(guid)) {
    presentation.name = name_entry->name;
    presentation.class_id = name_entry->class_id;
  }

  return presentation;
}

constexpr std::size_t kCalendarInviteeNameMaxBytesIncludingNul = 0x30u;
constexpr std::size_t kCalendarInviteeNameMaxBytes =
    kCalendarInviteeNameMaxBytesIncludingNul - 1u;

[[nodiscard]] static std::string TruncateCalendarInviteeName(std::string_view name) {
  if (name.size() <= kCalendarInviteeNameMaxBytes) {
    return std::string(name);
  }

  std::size_t copied_bytes = 0;
  while (copied_bytes < name.size() && copied_bytes < kCalendarInviteeNameMaxBytes) {
    const auto lead = static_cast<unsigned char>(name[copied_bytes]);
    std::size_t code_point_bytes = 1;
    if ((lead & 0x80u) == 0u) {
      code_point_bytes = 1;
    } else if ((lead & 0xE0u) == 0xC0u) {
      code_point_bytes = 2;
    } else if ((lead & 0xF0u) == 0xE0u) {
      code_point_bytes = 3;
    } else if ((lead & 0xF8u) == 0xF0u) {
      code_point_bytes = 4;
    }

    if (copied_bytes + code_point_bytes > kCalendarInviteeNameMaxBytes) {
      break;
    }
    copied_bytes += code_point_bytes;
  }
  return std::string(name.substr(0, copied_bytes));
}

[[nodiscard]] static std::string NormalizeCalendarInviteeName(std::string_view raw_name) {
  std::string normalized(raw_name);
  if (normalized.empty()) {
    return normalized;
  }

  std::string capitalized;
  if (::openwow::game::CapitalizeName(normalized, capitalized) && !capitalized.empty()) {
    return TruncateCalendarInviteeName(capitalized);
  }

  return TruncateCalendarInviteeName(normalized);
}

static bool HasResolvedCalendarInviteeName(
    const ::openwow::game::WorldSession &session,
    const std::vector<::openwow::game::CalendarSystemInvite> &invites,
    std::string_view invitee_name) {
  for (const auto &invite : invites) {
    if (invite.invitee_guid == 0) {
      continue;
    }

    const std::string resolved_name = ResolveCalendarPlayerName(session, invite.invitee_guid);
    if (resolved_name.empty()) {
      continue;
    }

    if (resolved_name == invitee_name) {
      return true;
    }
  }

  return false;
}

enum class NewCalendarCreatorInviteMode : std::uint8_t {
  kNone,
  kPlayerEvent,
  kGuildEvent,
};

static const char *ClassFileName(std::uint8_t class_id) {
  switch (class_id) {
  case 1:
    return "WARRIOR";
  case 2:
    return "PALADIN";
  case 3:
    return "HUNTER";
  case 4:
    return "ROGUE";
  case 5:
    return "PRIEST";
  case 6:
    return "DEATHKNIGHT";
  case 7:
    return "SHAMAN";
  case 8:
    return "MAGE";
  case 9:
    return "WARLOCK";
  case 11:
    return "DRUID";
  default:
    return "";
  }
}

static ::openwow::game::CalendarSystemInvite
BuildCreatorInvite(const ::openwow::game::CGPlayer_C &player,
                   NewCalendarCreatorInviteMode invite_mode) {
  ::openwow::game::CalendarSystemInvite invite{};
  invite.event_id = 0;
  invite.invitee_guid = player.GetGuid().GetRawValue();
  invite.invitee_name = player.GetName();
  invite.status = 3;
  invite.rank = 2;
  invite.can_moderate = true;
  invite.level = static_cast<std::uint8_t>(player.State().GetLevel());
  invite.class_id = player.State().GetClass();
  invite.invite_type = invite_mode == NewCalendarCreatorInviteMode::kGuildEvent ? 1 : 0;
  return invite;
}

static bool SeedNewCalendarContextEvent(
    lua_State *L, uint32_t flags, bool require_guild_membership,
    NewCalendarCreatorInviteMode invite_mode = NewCalendarCreatorInviteMode::kNone) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!player) {
    return false;
  }

  if (require_guild_membership && player->GetGuildID() == 0) {
    FireCalendarError(L, "ERR_GUILD_PLAYER_NOT_IN_GUILD");
    return false;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  calendar.ClearPendingInviteListNameQueries();
  calendar.ClearOpenedEvent();

  ::openwow::game::CalendarSystemEvent event{};
  event.event_id = 0;
  event.creator_guid = player->GetGuid().GetRawValue();
  event.creator_name = player->GetName();
  event.flags = flags;

  std::vector<::openwow::game::CalendarSystemInvite> invites;
  if (invite_mode != NewCalendarCreatorInviteMode::kNone) {
    invites.push_back(BuildCreatorInvite(*player, invite_mode));
  }
  calendar.SetEventDetails(0, event, invites);
  calendar.SetSelectedInviteId(0);

  ::openwow::game::CalendarContextEventInfo info{};
  info.event_id = 0;
  info.creator_guid = player->GetGuid().GetRawValue();
  info.is_own_event = true;
  info.is_moderator = true;
  info.invite_status = 0;
  info.invite_type = 0;
  info.flags = flags;
  info.max_invites = 100;
  info.invite_sort_criterion = 3;
  info.local_edit = true;

  constexpr auto kUnsetCalendarComponent = std::numeric_limits<std::uint32_t>::max();
  info.month = kUnsetCalendarComponent;
  info.day = kUnsetCalendarComponent;
  info.year = kUnsetCalendarComponent;
  info.hour = kUnsetCalendarComponent;
  info.minute = kUnsetCalendarComponent;
  info.weekday = -1;
  info.time_flags = 0;
  info.secondary_time_packed = 0x1fffffffu;

  info.self_invite_id = 1;
  calendar.SetContextEvent(info);
  return true;
}

static const char *SequenceTypeFromEvent(const ::openwow::game::CalendarSystemEvent &event) {
  return ::openwow::game::CalendarSystem::GetHolidayPhaseString(
      static_cast<uint8_t>(event.flags), event.sequence_index, event.sequence_total);
}

static int CalendarDifficultyFromEvent(const ::openwow::game::CalendarSystemEvent &event) {
  if ((event.flags & 0x280u) == 0) {
    return 0;
  }
  return static_cast<int>(event.dungeon_id) + 1;
}

static int CalendarInviteTypeFromEvent(const ::openwow::game::CalendarSystemEvent &event) {
  return ::openwow::game::CalendarSystem::IsGuildSignupEvent(
             event.flags, event.invite_type != 0 ? 0x08u : 0x00u)
             ? 2
             : 1;
}

constexpr std::uint32_t kGuildLeaderCalendarRight = 0x00100000u;
constexpr std::uint32_t kCalendarFlagPlayerEvent = 0x01u;
constexpr std::uint32_t kCalendarFlagLocked = 0x10u;
constexpr std::uint32_t kCalendarFlagAutoApprove = 0x20u;
constexpr std::uint32_t kCalendarFlagGuildAnnouncement = 0x40u;
constexpr std::uint32_t kCalendarFlagRaidLockout = 0x80u;
constexpr std::uint32_t kCalendarFlagGuildEvent = 0x400u;
constexpr std::uint32_t kCalendarComplaintBlockedFlags = 0x6CCu;
constexpr std::uint32_t kCalendarEventTextureFlagsMask = 0x543u;
constexpr std::uint32_t kCalendarInviteStatusCount = 9u;
constexpr std::uint8_t kCalendarInviteStatusNotSignedUp = 7u;
constexpr std::array<std::string_view, 5> kCalendarEventTypeGlobalKeys = {
    "CALENDAR_TYPE_RAID",    "CALENDAR_TYPE_DUNGEON", "CALENDAR_TYPE_PVP",
    "CALENDAR_TYPE_MEETING", "CALENDAR_TYPE_OTHER",
};
constexpr std::array<std::string_view, 4> kCalendarRepeatOptionGlobalKeys = {
    "CALENDAR_REPEAT_NEVER",
    "CALENDAR_REPEAT_WEEKLY",
    "CALENDAR_REPEAT_BIWEEKLY",
    "CALENDAR_REPEAT_MONTHLY",
};
constexpr std::array<std::string_view, kCalendarInviteStatusCount> kCalendarInviteStatusGlobalKeys =
    {
        "CALENDAR_STATUS_INVITED",   "CALENDAR_STATUS_ACCEPTED",     "CALENDAR_STATUS_DECLINED",
        "CALENDAR_STATUS_CONFIRMED", "CALENDAR_STATUS_OUT",          "CALENDAR_STATUS_STANDBY",
        "CALENDAR_STATUS_SIGNEDUP",  "CALENDAR_STATUS_NOT_SIGNEDUP", "CALENDAR_STATUS_TENTATIVE",
};

struct CalendarTextureOption {
  std::string display_name;
  std::string texture_path;
  std::uint32_t record_id = 0;
  std::uint32_t expansion_level = 0;
  std::string difficulty_name;
};

using CalendarTextureOptions = std::vector<CalendarTextureOption>;

constexpr std::uint32_t kCalendarDungeonTextureListType = 1u;
constexpr std::uint32_t kCalendarRaidTextureListType = 2u;

const ::openwow::data::dbc::MapDifficultyEntry *
LookupCalendarMapDifficulty(const ::openwow::data::dbc::DbcLoader &dbc, const std::uint32_t map_id,
                            const std::uint32_t difficulty) {
  return ::openwow::data::DBClient_FindMapDifficulty(&dbc, map_id, difficulty);
}

std::int32_t CalendarFactionForRace(const std::uint8_t race_id) {
  switch (race_id) {
  case 1:
  case 3:
  case 4:
  case 7:
  case 11:
    return -1;
  case 2:
  case 5:
  case 6:
  case 8:
  case 10:
    return 1;
  default:
    return 0;
  }
}

const ::openwow::game::CGUnit_C *GetActiveCalendarPlayer(const lua_State *L) {
  const auto *session = GetWorldSession(const_cast<lua_State *>(L));
  return session != nullptr ? session->objects().GetActivePlayer() : nullptr;
}

std::optional<std::uint32_t>
GetCalendarTextureListTypeForRawEventType(const std::uint8_t event_type) {
  switch (event_type) {
  case 0:
    return kCalendarRaidTextureListType;
  case 1:
    return kCalendarDungeonTextureListType;
  default:
    return std::nullopt;
  }
}

std::optional<std::uint32_t>
GetCalendarTextureListTypeForLuaEventType(const lua_Integer event_type) {
  switch (event_type) {
  case 1:
    return kCalendarRaidTextureListType;
  case 2:
    return kCalendarDungeonTextureListType;
  default:
    return std::nullopt;
  }
}

CalendarTextureOptions BuildCalendarTextureOptions(const lua_State *L,
                                                   const std::uint32_t texture_list_type) {

  const auto *dbc = GetDbcLoader(const_cast<lua_State *>(L));
  const auto *player = GetActiveCalendarPlayer(L);
  if (dbc == nullptr || player == nullptr) {
    return {};
  }

  const auto active_faction = CalendarFactionForRace(player->State().GetRace());
  CalendarTextureOptions options;
  for (const auto &dungeon : dbc->lfg_dungeons()) {
    if (dungeon.type_id != texture_list_type) {
      continue;
    }
    if (dungeon.faction >= 0 && dungeon.faction != active_faction) {
      continue;
    }

    CalendarTextureOption option{};
    option.display_name = std::string(dungeon.name);
    option.texture_path = std::string(dungeon.texture_filename);
    option.record_id = dungeon.id;
    option.expansion_level = dungeon.expansion_level;

    if (texture_list_type == kCalendarRaidTextureListType) {
      if (const auto *difficulty =
              LookupCalendarMapDifficulty(*dbc, dungeon.map_id, dungeon.difficulty);
          difficulty != nullptr) {
        option.difficulty_name = std::string(difficulty->difficulty_string);
      }
    }

    options.push_back(std::move(option));
  }

  std::sort(options.begin(), options.end(),
            [](const CalendarTextureOption &left, const CalendarTextureOption &right) {
              if (left.expansion_level != right.expansion_level) {
                return left.expansion_level > right.expansion_level;
              }
              return ::openwow::core::SStrCmpNoCaseCollate(
                         left.display_name.c_str(), right.display_name.c_str(),
                         std::numeric_limits<std::size_t>::max()) < 0;
            });
  return options;
}

CalendarTextureOptions GetCalendarTextureOptionsForRawEventType(const lua_State *L,
                                                                const std::uint8_t event_type) {
  const auto texture_list_type = GetCalendarTextureListTypeForRawEventType(event_type);
  return texture_list_type.has_value() ? BuildCalendarTextureOptions(L, *texture_list_type)
                                       : CalendarTextureOptions{};
}

CalendarTextureOptions GetCalendarTextureOptionsForLuaEventType(const lua_State *L,
                                                                const lua_Integer event_type) {
  const auto texture_list_type = GetCalendarTextureListTypeForLuaEventType(event_type);
  return texture_list_type.has_value() ? BuildCalendarTextureOptions(L, *texture_list_type)
                                       : CalendarTextureOptions{};
}

std::uint32_t ResolveCalendarTextureRecordId(const CalendarTextureOptions &options,
                                             const lua_Integer texture_index) {
  if (texture_index <= 0) {
    return 0;
  }

  const auto zero_based_index = static_cast<std::size_t>(texture_index - 1);
  if (zero_based_index >= options.size()) {
    return 0;
  }

  return options[zero_based_index].record_id;
}

lua_Integer FindCalendarTextureSelectionIndex(const CalendarTextureOptions &options,
                                              const std::uint32_t record_id) {
  const auto it = std::find_if(
      options.begin(), options.end(),
      [record_id](const CalendarTextureOption &option) { return option.record_id == record_id; });
  if (it == options.end()) {
    return 0;
  }

  return static_cast<lua_Integer>(std::distance(options.begin(), it)) + 1;
}

struct CalendarResolvedHolidayDisplay {
  std::optional<std::string> name;
  std::optional<std::string> description;
  std::string texture;
};

std::optional<std::string> ResolveHolidayText(const ::openwow::data::dbc::DbcLoader &dbc,
                                              const std::uint32_t holiday_id,
                                              const bool description) {
  const auto *holiday = dbc.holidays().LookupEntry(holiday_id);
  if (holiday == nullptr) {
    return std::nullopt;
  }

  if (description) {
    const auto *entry = dbc.holiday_descriptions().LookupEntry(holiday->holiday_description_id);
    if (entry == nullptr) {
      return std::nullopt;
    }
    return std::string(entry->description);
  }

  const auto *entry = dbc.holiday_names().LookupEntry(holiday->holiday_name_id);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return std::string(entry->name);
}

std::string ResolveHolidayTextureFromDbc(const ::openwow::data::dbc::DbcLoader &dbc,
                                         const std::uint32_t holiday_id) {
  const auto *holiday = dbc.holidays().LookupEntry(holiday_id);
  if (holiday == nullptr) {
    return {};
  }

  return std::string(holiday->texture_filename);
}

CalendarResolvedHolidayDisplay ResolveCalendarHolidayDisplay(
    lua_State *L, const ::openwow::game::CalendarSystem &calendar,
    const std::uint32_t holiday_id) {
  CalendarResolvedHolidayDisplay display{};
  if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
    display.name = ResolveHolidayText(*dbc, holiday_id, false);
    display.description = ResolveHolidayText(*dbc, holiday_id, true);
    display.texture = ResolveHolidayTextureFromDbc(*dbc, holiday_id);
  }

  if (const auto presentation = calendar.GetHolidayPresentation(holiday_id); presentation) {
    display.texture = presentation->texture;
  }

  return display;
}

std::string ResolveCalendarDayEventTexture(lua_State *L,
                                           const ::openwow::game::CalendarSystem &calendar,
                                           const ::openwow::game::CalendarSystemEvent &event) {
  if ((event.flags & 0x008u) != 0) {
    return ResolveCalendarHolidayDisplay(L, calendar,
                                         static_cast<std::uint32_t>(event.event_id))
        .texture;
  }

  if ((event.flags & kCalendarEventTextureFlagsMask) == 0 || event.dungeon_id < 0) {
    return {};
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return {};
  }

  const auto *texture_entry =
      dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(event.dungeon_id));
  if (texture_entry == nullptr) {
    return {};
  }

  return std::string(texture_entry->texture_filename);
}

std::string ResolveCalendarDayEventInviterName(
    lua_State *L, const ::openwow::game::CalendarSystemEvent &event) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || event.pending_sender_guid == 0) {
    return {};
  }

  return ResolveCalendarEventListPlayerName(*session, event.pending_sender_guid);
}

std::uint64_t GetActiveCalendarPlayerGuid(const ::openwow::game::WorldSession &session) {
  return session.objects().GetActivePlayerGuid().GetRawValue();
}

void PushDefaultCalendarEventInfo(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushstring(L, "");

  for (int index = 0; index < 16; ++index) {
    lua_pushnumber(L, 0.0);
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushstring(L, "");
}

bool HasGuildLeaderCalendarRight(const ::openwow::game::WorldSession &session) {
  return session.guild().has_permissions() &&
         (static_cast<std::uint32_t>(session.guild().permissions().flags) &
          kGuildLeaderCalendarRight) != 0;
}

bool HasCalendarEventModeratorRights(const ::openwow::game::CalendarContextEventInfo &context,
                                     const ::openwow::game::CalendarSystemEvent *event,
                                     std::uint64_t active_player_guid) {
  return context.is_own_event || context.is_moderator ||
         (active_player_guid != 0 &&
          ((context.creator_guid != 0 && context.creator_guid == active_player_guid) ||
           (event != nullptr && event->creator_guid == active_player_guid)));
}

bool CanEditCalendarContextEvent(const ::openwow::game::WorldSession &session,
                                 const ::openwow::game::CalendarSystem &calendar,
                                 const ::openwow::game::CalendarContextEventInfo &context) {
  if ((context.flags & kCalendarFlagGuildAnnouncement) != 0) {
    return HasGuildLeaderCalendarRight(session);
  }

  return HasCalendarEventModeratorRights(context, calendar.GetEvent(context.event_id),
                                         GetActiveCalendarPlayerGuid(session));
}

bool CanEditCalendarEventBuffer(const ::openwow::game::CalendarContextEventInfo &context,
                                const std::uint64_t active_player_guid) {
  return context.self_invite_id != 0 ||
         (active_player_guid != 0 && context.creator_guid != 0 &&
          context.creator_guid == active_player_guid);
}

std::uint64_t ResolveCalendarEventBufferEditorInviteId(
    const ::openwow::game::CalendarContextEventInfo &context,
    const std::uint64_t active_player_guid) {
  return CanEditCalendarEventBuffer(context, active_player_guid) ? context.self_invite_id : 0;
}

bool UsesCalendarRemoveEventDeletePath(const ::openwow::game::WorldSession &session,
                                       const ::openwow::game::CalendarContextEventInfo &context,
                                       const std::uint64_t active_player_guid) {
  if ((context.flags & kCalendarFlagGuildAnnouncement) != 0) {
    return HasGuildLeaderCalendarRight(session);
  }

  return CanEditCalendarEventBuffer(context, active_player_guid);
}

bool CanMutateCalendarEventBuffer(const ::openwow::game::WorldSession &session,
                                  const ::openwow::game::CalendarContextEventInfo &context) {
  if ((context.flags & kCalendarFlagGuildAnnouncement) != 0) {
    return HasGuildLeaderCalendarRight(session);
  }

  const auto *active_player = session.objects().GetActivePlayer();
  const auto active_player_guid = active_player != nullptr
                                      ? active_player->GetGuid().GetRawValue()
                                      : 0;
  return CanEditCalendarEventBuffer(context, active_player_guid);
}

::openwow::game::CalendarContextEventInfo *TryGetEditableCalendarEventBufferContext(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return nullptr;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  auto *context =
      const_cast<::openwow::game::CalendarContextEventInfo *>(calendar.GetContextEvent());
  if (context == nullptr) {
    return nullptr;
  }

  return CanMutateCalendarEventBuffer(*session, *context) ? context : nullptr;
}

std::string SanitizeCalendarEventText(const std::string_view text,
                                      const std::size_t max_codepoints_with_nul,
                                      const std::size_t max_bytes_with_nul,
                                      const bool truncate_at_line_breaks) {
  if (max_bytes_with_nul <= 1 || max_codepoints_with_nul <= 1) {
    return {};
  }

  std::string sanitized;
  sanitized.reserve(std::min(text.size(), max_bytes_with_nul - 1));
  for (const char ch : text) {
    if (sanitized.size() >= max_bytes_with_nul - 1) {
      break;
    }
    if (ch != '|') {
      sanitized.push_back(ch);
    }
  }

  if (truncate_at_line_breaks) {
    for (std::size_t index = 0; index < sanitized.size(); ++index) {
      const char ch = sanitized[index];
      const bool escaped_newline =
          (ch == '\\' || ch == '|') && index + 1 < sanitized.size() && sanitized[index + 1] == 'n';
      if (ch == '\r' || ch == '\n' || escaped_newline) {
        sanitized.resize(index);
        break;
      }
    }
  }

  std::size_t codepoints = 0;
  for (std::size_t index = 0; index < sanitized.size(); ++index) {
    if ((static_cast<unsigned char>(sanitized[index]) & 0xC0u) != 0x80u) {
      ++codepoints;
      if (codepoints == max_codepoints_with_nul) {
        sanitized.resize(index);
        break;
      }
    }
  }
  return sanitized;
}

void ApplyCalendarEventTextEdit(std::string &target, bool &settings_changed, const char *raw_text,
                                const std::size_t max_codepoints_with_nul,
                                const std::size_t max_bytes_with_nul,
                                const bool truncate_at_line_breaks) {
  if (raw_text == nullptr) {
    return;
  }

  if (std::string_view(raw_text) == target) {
    return;
  }

  target = SanitizeCalendarEventText(raw_text, max_codepoints_with_nul, max_bytes_with_nul,
                                     truncate_at_line_breaks);
  settings_changed = true;
}

bool IsValidCalendarEditTime(const std::uint32_t hour, const std::uint32_t minute) {
  return hour < 24 && minute < 60;
}

struct CalendarDateEdit {
  std::uint32_t attempted_month = 0;
  std::uint32_t attempted_day = 0;
  std::uint32_t attempted_year = 0;
  std::uint32_t stored_year = 0;
  bool valid = false;
};

CalendarDateEdit DecodeCalendarDateEdit(const std::uint32_t month, const std::uint32_t day,
                                        const std::uint32_t year) {
  CalendarDateEdit edit{
      .attempted_month = month - 1u,
      .attempted_day = day - 1u,
      .attempted_year = year - 2000u,
  };
  edit.stored_year = edit.attempted_year;
  if (edit.stored_year >= 2000u) {
    edit.stored_year -= 2000u;
  }
  edit.valid = edit.attempted_month < 12u && edit.attempted_day < 32u &&
               edit.stored_year < 32u;
  return edit;
}

std::uint32_t CalendarHumanFieldToInternal(const std::uint32_t value,
                                           const std::uint32_t offset) {
  return value == std::numeric_limits<std::uint32_t>::max() ? value : value - offset;
}

bool TryApplyCalendarDateEdit(std::uint32_t &stored_month, std::uint32_t &stored_day,
                              std::uint32_t &stored_year, bool &settings_changed,
                              const std::uint32_t month, const std::uint32_t day,
                              const std::uint32_t year) {
  const auto edit = DecodeCalendarDateEdit(month, day, year);

  if (edit.attempted_month != CalendarHumanFieldToInternal(stored_month, 1u) ||
      edit.attempted_day != CalendarHumanFieldToInternal(stored_day, 1u) ||
      edit.attempted_year != CalendarHumanFieldToInternal(stored_year, 2000u)) {
    settings_changed = true;
  }
  if (!edit.valid) {
    return false;
  }

  stored_month = edit.attempted_month + 1u;
  stored_day = edit.attempted_day + 1u;
  stored_year = edit.stored_year + 2000u;
  return true;
}

bool TryApplyCalendarPackedDateEdit(std::uint32_t &packed_time, bool &settings_changed,
                                    const std::uint32_t month, const std::uint32_t day,
                                    const std::uint32_t year) {
  ::openwow::game::CalendarDateFieldsEx fields{};
  ::openwow::game::CalendarPackedTime_UnpackToArray(packed_time, fields);
  const auto edit = DecodeCalendarDateEdit(month, day, year);
  if (edit.attempted_month != static_cast<std::uint32_t>(fields.month) ||
      edit.attempted_day != static_cast<std::uint32_t>(fields.day) ||
      edit.attempted_year != static_cast<std::uint32_t>(fields.year)) {
    settings_changed = true;
  }
  if (!edit.valid) {
    return false;
  }

  fields.month = static_cast<std::int32_t>(edit.attempted_month);
  fields.day = static_cast<std::int32_t>(edit.attempted_day);
  fields.year = static_cast<std::int32_t>(edit.stored_year);
  packed_time = ::openwow::game::CalendarDateFields_PackFromArray(fields);
  return true;
}

bool TryApplyCalendarPackedTimeEdit(std::uint32_t &packed_time, bool &settings_changed,
                                    const std::uint32_t hour, const std::uint32_t minute) {
  ::openwow::game::CalendarDateFieldsEx fields{};
  ::openwow::game::CalendarPackedTime_UnpackToArray(packed_time, fields);
  if (hour != static_cast<std::uint32_t>(fields.hour) ||
      minute != static_cast<std::uint32_t>(fields.minute)) {
    settings_changed = true;
  }
  if (!::openwow::game::CalendarDateFields_SetTime(fields, hour, minute)) {
    return false;
  }

  packed_time = ::openwow::game::CalendarDateFields_PackFromArray(fields);
  return true;
}

bool TryApplyCalendarTimeEdit(std::uint32_t &stored_hour, std::uint32_t &stored_minute,
                              bool &settings_changed, const std::uint32_t hour,
                              const std::uint32_t minute) {
  if (hour != stored_hour || minute != stored_minute) {
    settings_changed = true;
  }
  if (!IsValidCalendarEditTime(hour, minute)) {
    return false;
  }

  stored_hour = hour;
  stored_minute = minute;
  return true;
}

bool TrySetCurrentCalendarEventFlag(lua_State *L, const std::uint32_t flag_mask,
                                    const bool enabled) {
  auto *context = TryGetEditableCalendarEventBufferContext(L);
  if (context == nullptr) {
    return false;
  }

  const auto old_flags = context->flags;
  if (enabled) {
    context->flags |= flag_mask;
  } else {
    context->flags &= ~flag_mask;
  }
  if (context->flags != old_flags) {
    context->settings_changed = true;
  }
  return true;
}

std::vector<::openwow::game::CalendarSystemInvite>
GetContextInvites(::openwow::game::CalendarSystem &calendar,
                  const ::openwow::game::CalendarContextEventInfo &context) {
  return calendar.GetEventInvites(context.event_id);
}

void PersistContextInvites(::openwow::game::CalendarSystem &calendar,
                           const ::openwow::game::CalendarContextEventInfo &context,
                           std::vector<::openwow::game::CalendarSystemInvite> invites) {
  ::openwow::game::CalendarSystemEvent event{};
  if (const auto *stored = calendar.GetEvent(context.event_id)) {
    event = *stored;
  } else {
    event.event_id = context.event_id;
    event.flags = context.flags;
  }
  calendar.SetEventDetails(context.event_id, event, invites);
}

bool ValidateInviteModeratorSelection(
    lua_State *L, const ::openwow::game::CalendarContextEventInfo &context,
    const std::vector<::openwow::game::CalendarSystemInvite> &invites,
    std::size_t invite_index) {
  if (invite_index >= invites.size()) {
    return false;
  }
  if ((context.flags & kCalendarFlagGuildEvent) != 0 && !invites[invite_index].can_moderate) {
    if (L != nullptr) {
      FireCalendarError(L, "CALENDAR_ERROR_NO_MODERATOR");
    }
    return false;
  }
  return true;
}

std::uint64_t ResolveSelfInviteId(const ::openwow::game::CalendarContextEventInfo &context,
                                  const std::vector<::openwow::game::CalendarSystemInvite> &invites,
                                  std::uint64_t active_player_guid) {
  if (context.self_invite_id != 0) {
    return context.self_invite_id;
  }
  if (active_player_guid == 0) {
    return 0;
  }
  for (const auto &invite : invites) {
    if (invite.invitee_guid == active_player_guid) {
      return invite.invite_id;
    }
  }
  return 0;
}

std::uint32_t ReadCalendarInviteIndexArg(lua_State *L) {
  return TruncateLuaNumberToCalendarU32(luaL_checknumber(L, 1)) - 1u;
}

bool CanRemoveCalendarInvite(const ::openwow::game::CalendarContextEventInfo &context,
                             const ::openwow::game::CalendarSystemInvite &invite,
                             const std::uint64_t active_player_guid) {
  return active_player_guid == 0 || invite.invitee_guid == active_player_guid ||
         CanEditCalendarEventBuffer(context, active_player_guid);
}

int RemoveCalendarInviteByIndex(lua_State *L, ::openwow::game::WorldSession *session,
                                const ::openwow::game::CalendarContextEventInfo &context,
                                const std::size_t invite_index) {
  auto &calendar = ::openwow::game::CalendarSystem::Get();
  auto invites = GetContextInvites(calendar, context);
  if (invite_index >= invites.size()) {
    return 0;
  }

  const auto invite = invites[invite_index];
  if (invite.rank == 2) {
    FireCalendarError(L, "CALENDAR_DELETE_CREATOR_FAILED");
    return 0;
  }

  const auto *active_player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  const auto active_player_guid =
      active_player != nullptr ? active_player->GetGuid().GetRawValue() : 0;
  if (!CanRemoveCalendarInvite(context, invite, active_player_guid)) {
    FireCalendarPermissionError(L);
    return 0;
  }

  if (context.local_edit) {
    invites.erase(invites.begin() + invite_index);
    PersistContextInvites(calendar, context, std::move(invites));
    FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
    return 0;
  }

  if (session == nullptr) {
    return 0;
  }

  const auto self_invite_id = context.self_invite_id;
  session->interaction().SendCalendarEventRemoveInvite(invite.invitee_guid, context.event_id,
                                                       invite.invite_id, self_invite_id);
  SetCalendarActionPending(L, true);
  return 0;
}

int ToggleCalendarInviteModeratorStatus(lua_State *L, const std::uint8_t rank) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  const auto *context = calendar.GetContextEvent();
  if (!context) {
    return 0;
  }

  const auto invite_index = ReadCalendarInviteIndexArg(L);

  if (!CanMutateCalendarEventBuffer(*session, *context)) {
    return 0;
  }

  if (!context->local_edit && calendar.IsActionPending()) {
    return 0;
  }

  auto invites = GetContextInvites(calendar, *context);
  if (!ValidateInviteModeratorSelection(L, *context, invites, invite_index)) {
    return 0;
  }
  if (invites[invite_index].rank == rank) {
    return 0;
  }

  if (context->local_edit) {
    invites[invite_index].rank = rank;
    PersistContextInvites(calendar, *context, invites);
    FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
    return 0;
  }

  const auto self_invite_id = context->self_invite_id;
  session->interaction().SendCalendarEventModeratorStatus(
      invites[invite_index].invitee_guid, context->event_id, invites[invite_index].invite_id,
      self_invite_id, rank);
  return 0;
}

const ::openwow::game::CalendarSystemInvite *
FindCalendarInviteById(const std::vector<::openwow::game::CalendarSystemInvite> &invites,
                       const std::uint64_t invite_id) {
  for (const auto &invite : invites) {
    if (invite.invite_id == invite_id) {
      return &invite;
    }
  }
  return nullptr;
}

const ::openwow::game::CalendarSystemInvite *
FindCalendarInviteByInviteeGuid(const std::vector<::openwow::game::CalendarSystemInvite> &invites,
                                const std::uint64_t invitee_guid) {
  for (const auto &invite : invites) {
    if (invite.invitee_guid == invitee_guid) {
      return &invite;
    }
  }
  return nullptr;
}

bool HasCalendarInviteForGuid(const std::vector<::openwow::game::CalendarSystemInvite> &invites,
                              const std::uint64_t invitee_guid) {
  if (invitee_guid == 0) {
    return false;
  }

  return std::any_of(invites.begin(), invites.end(),
                     [invitee_guid](const ::openwow::game::CalendarSystemInvite &invite) {
                       return invite.invitee_guid == invitee_guid;
                     });
}

bool IsGuildSignupEventForPlayer(
    const ::openwow::game::CalendarContextEventInfo &context,
    const ::openwow::game::CalendarSystemEvent &event,
    const ::openwow::game::CGPlayer_C &player) {

  return (context.flags & kCalendarFlagGuildEvent) != 0 &&
         event.guild_id == player.GetGuildID();
}

bool SendCurrentCalendarEventRsvp(lua_State *L, const std::uint32_t status) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return false;
  }

  auto *player = session->objects().GetActivePlayer();
  if (!player) {
    return false;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending()) {
    return false;
  }

  const auto *context = calendar.GetContextEvent();
  if (!context || context->local_edit) {
    return false;
  }

  const auto *event = calendar.GetEvent(context->event_id);
  if (!event || IsGuildSignupEventForPlayer(*context, *event, *player)) {
    return false;
  }

  const auto invites = calendar.GetEventInvites(context->event_id);

  const auto *self_invite =
      FindCalendarInviteByInviteeGuid(invites, player->GetGuid().GetRawValue());
  if (!self_invite) {
    return false;
  }

  if ((context->flags & kCalendarFlagGuildEvent) != 0 && self_invite->invite_type == 1) {
    return false;
  }

  session->interaction().SendCalendarEventRsvp(context->event_id, self_invite->invite_id, status);
  SetCalendarActionPending(L, true);
  return true;
}

bool SendCurrentCalendarGuildSignup(lua_State *L, const std::uint8_t tentative) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return false;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending()) {
    return false;
  }

  const auto *context = calendar.GetContextEvent();
  if (!context || context->local_edit || (context->flags & kCalendarFlagGuildEvent) == 0) {
    return false;
  }

  const auto invites = calendar.GetEventInvites(context->event_id);
  if (const auto *active_player = session->objects().GetActivePlayer();
      active_player != nullptr &&
      HasCalendarInviteForGuid(invites, active_player->GetGuid().GetRawValue())) {
    return false;
  }

  session->interaction().SendCalendarEventSignUp(context->event_id, tentative);
  SetCalendarActionPending(L, true);
  return true;
}

bool SendCurrentCalendarTentative(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return false;
  }

  const auto *active_player = session->objects().GetActivePlayer();
  if (!active_player) {
    return false;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  const auto *context = calendar.GetContextEvent();
  if (!context) {
    return false;
  }

  if (const auto *event = calendar.GetEvent(context->event_id);
      event != nullptr && (context->flags & kCalendarFlagGuildEvent) != 0 &&
      event->guild_id == active_player->GetGuildID()) {
    const auto invites = calendar.GetEventInvites(context->event_id);
    if (HasCalendarInviteForGuid(invites, active_player->GetGuid().GetRawValue())) {
      return false;
    }

    return SendCurrentCalendarGuildSignup(L, 1);
  }

  return SendCurrentCalendarEventRsvp(L, 8);
}

bool ResolveCalendarDayEventFromCoordinates(lua_State *L,
                                            const ::openwow::game::CalendarSystem &calendar,
                                            const int month_offset, const std::uint32_t day,
                                            const std::uint32_t event_index,
                                            ::openwow::game::CalendarSystemEvent &event) {
  CalendarRelativeDayLookup lookup{};
  if (!ResolveCalendarRelativeDayLookup(L, calendar, month_offset, day, lookup)) {
    return false;
  }

  const auto events = calendar.GetDayEvents(static_cast<std::uint32_t>(lookup.month),
                                            static_cast<std::uint32_t>(lookup.day),
                                            static_cast<std::uint32_t>(lookup.year));
  if (event_index >= events.size()) {
    return false;
  }

  event = events[static_cast<std::size_t>(event_index)];
  return true;
}

bool ParseCalendarRelativeDayEventCoordinates(lua_State *L, const char *usage, int &month_offset,
                                              std::uint32_t &day,
                                              std::uint32_t &event_index) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    luaL_error(L, usage);
    return false;
  }

  month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  event_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3)) - 1u;
  return true;
}

::openwow::game::CalendarContextEventInfo
BuildCalendarDayEventSelection(const ::openwow::game::CalendarSystemEvent &event) {
  ::openwow::game::CalendarContextEventInfo info{};
  info.event_id = event.event_id;
  info.self_invite_id = event.self_invite_id;
  info.creator_guid = event.creator_guid;
  info.event_type = event.type;
  info.dungeon_id = event.dungeon_id;
  info.flags = event.flags;
  info.invite_status = event.invite_status;
  info.invite_type = event.invite_type;
  info.is_own_event = (event.invite_mod_status & 0x04u) != 0;
  info.is_moderator = (event.invite_mod_status & 0x02u) != 0;
  info.sequence_index = event.sequence_index;
  info.sequence_total = event.sequence_total;
  info.map_id = event.map_id;
  ApplyPackedEventTimeToContext(event, info);
  return info;
}

bool ResolveCalendarSelectionIndex(const ::openwow::game::CalendarSystem &calendar,
                                   const ::openwow::game::CalendarContextEventInfo &selection,
                                   int &month_offset, int &day, int &event_index) {
  if (selection.month == 0 || selection.day == 0 || selection.year == 0) {
    return false;
  }

  month_offset =
      (static_cast<int>(selection.year) - static_cast<int>(calendar.GetViewYear())) * 12 +
      (static_cast<int>(selection.month) - static_cast<int>(calendar.GetViewMonth()));
  if (month_offset < -1 || month_offset > 1) {
    return false;
  }

  const auto events = calendar.GetDayEvents(selection.month, selection.day, selection.year);
  if (events.empty()) {
    return false;
  }

  const auto matches = [&selection](const ::openwow::game::CalendarSystemEvent &event) {
    if (selection.event_id != 0) {
      return event.event_id == selection.event_id;
    }

    if (selection.map_id != event.map_id) {
      return false;
    }
    if ((selection.flags & kCalendarFlagRaidLockout) != 0 &&
        event.dungeon_id != selection.dungeon_id) {
      return false;
    }
    return true;
  };

  for (std::size_t index = 0; index < events.size(); ++index) {
    if (matches(events[index])) {
      day = static_cast<int>(selection.day);
      event_index = static_cast<int>(index) + 1;
      return true;
    }
  }

  return false;
}

bool ResolveCalendarContextEventOperationTarget(lua_State *L,
                                                const char *usage,
                                                ::openwow::game::CalendarContextEventInfo &target) {
  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
    const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
    const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
    const auto event_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3)) - 1u;
    ::openwow::game::CalendarSystemEvent event{};
    if (!ResolveCalendarDayEventFromCoordinates(L, calendar, month_offset, day, event_index,
                                                event)) {
      return false;
    }
    target = BuildCalendarDayEventSelection(event);

    target.self_invite_id = event.self_invite_id;
    target.invite_type = event.invite_type;
    return true;
  }

  const auto *current = calendar.GetContextMenuEvent();
  if (current == nullptr) {
    luaL_error(L, usage);
    return false;
  }

  target = *current;
  if (const auto *event = calendar.GetEvent(target.event_id)) {
    ApplyPackedEventTimeToContext(*event, target);
  }

  return true;
}

bool SendCalendarContextInviteRsvp(lua_State *L, const char *usage, const std::uint32_t status) {
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(L, usage, target)) {
    return false;
  }
  if (!session) {
    return false;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending() ||
      ::openwow::game::CalendarSystem::IsGuildSignupEvent(
          target.flags, target.invite_type != 0 ? 0x08u : 0x00u)) {
    return false;
  }

  session->interaction().SendCalendarEventRsvp(target.event_id, target.self_invite_id, status);
  SetCalendarActionPending(L, true);
  return true;
}

bool SendCalendarContextGuildSignUp(lua_State *L,
                                    ::openwow::game::WorldSession &session,
                                    const ::openwow::game::CalendarContextEventInfo &target,
                                    const std::uint8_t tentative) {
  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending() || (target.flags & kCalendarFlagGuildEvent) == 0 ||
      target.invite_status != kCalendarInviteStatusNotSignedUp) {
    return false;
  }

  if (session.objects().GetActivePlayer() == nullptr) {
    return false;
  }

  session.interaction().SendCalendarEventSignUp(target.event_id, tentative);
  SetCalendarActionPending(L, true);
  return true;
}

bool CanEditCalendarContextSelection(const ::openwow::game::WorldSession *session,
                                     const ::openwow::game::CalendarContextEventInfo &context) {
  if ((context.flags & (kCalendarFlagGuildAnnouncement | kCalendarFlagGuildEvent)) != 0) {

    const auto *active_player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
    return active_player != nullptr && active_player->GetGuildID() != 0 &&
           HasGuildLeaderCalendarRight(*session);
  }

  return context.is_own_event || context.is_moderator;
}

std::uint64_t ResolveCalendarComplaintCreatorGuid(
    const ::openwow::game::CalendarSystem &calendar,
    const ::openwow::game::CalendarContextEventInfo &context) {
  if (context.creator_guid != 0) {
    return context.creator_guid;
  }

  if (const auto *event = calendar.GetEvent(context.event_id)) {
    return event->creator_guid;
  }

  return 0;
}

std::uint32_t ResolveCalendarComplaintFlags(const ::openwow::game::CalendarSystem &calendar,
                                            const ::openwow::game::CalendarContextEventInfo &context) {
  if (context.flags != 0) {
    return context.flags;
  }

  if (const auto *event = calendar.GetEvent(context.event_id)) {
    return event->flags;
  }

  return 0;
}

bool CanComplainAboutCalendarContextEvent(const ::openwow::game::WorldSession &session,
                                          const ::openwow::game::CalendarSystem &calendar,
                                          const ::openwow::game::CalendarContextEventInfo &context) {

  if (calendar.IsActionPending()) {
    return false;
  }

  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  if (session.feature_status().complaint_status == 0) {
    return false;
  }

  if ((ResolveCalendarComplaintFlags(calendar, context) & kCalendarComplaintBlockedFlags) != 0) {
    return false;
  }

  const auto creator_guid = ResolveCalendarComplaintCreatorGuid(calendar, context);
  if (creator_guid == active_player->GetGuid().GetRawValue()) {
    return false;
  }

  return !session.social().HasContact(::openwow::game::ObjectGuid(creator_guid));
}

void FireCalendarError(lua_State *L, const char *error_token,
                       std::optional<int> format_argument) {
  std::string message = GetCalendarGlobalString(L, error_token);
  if (format_argument.has_value()) {
    char formatted[1024];
    ::openwow::core::SStrPrintf(formatted, sizeof(formatted), message.c_str(), *format_argument);
    message = formatted;
  }
  FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_ERROR, {message});
  SetCalendarActionPending(L, false);
}

void FireCalendarError(lua_State *L, const char *error_token, std::string_view format_argument) {
  std::string message = GetCalendarGlobalString(L, error_token);
  const std::string format_string(format_argument);
  char formatted[1024];
  ::openwow::core::SStrPrintf(formatted, sizeof(formatted), message.c_str(),
                              format_string.c_str());
  FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_ERROR, {formatted});
  SetCalendarActionPending(L, false);
}

void FireCalendarPermissionError(lua_State *L) {
  FireCalendarError(L, "CALENDAR_ERROR_PERMISSIONS");
}

bool TrySendCalendarAddEvent(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return false;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending()) {
    return false;
  }
  if (!SecureExecution::Get().IsSecure(L) && !calendar.CanSendEventAction()) {
    FireCalendarError(L, "CALENDAR_ERROR_EVENT_THROTTLED");
    return false;
  }

  const auto *context = calendar.GetContextEvent();
  if (context == nullptr || !context->local_edit) {
    FireCalendarError(L, "CALENDAR_ERROR_INTERNAL");
    return false;
  }

  const CalendarDate target_date = BuildCalendarDate(context->month, context->day, context->year);
  if (!IsCalendarCreatableDateValid(L, target_date)) {
    return false;
  }

  if (context->title.empty()) {
    FireCalendarError(L, "CALENDAR_ERROR_NEEDS_TITLE");
    return false;
  }

  if ((context->flags & (kCalendarFlagGuildAnnouncement | kCalendarFlagGuildEvent)) != 0) {
    if (const auto *active_player = session->objects().GetActivePlayer();
        active_player != nullptr && active_player->GetGuildID() == 0) {
      FireCalendarError(L, "ERR_GUILD_PLAYER_NOT_IN_GUILD");
      return false;
    }
  } else {
    if (context->max_invites == 0) {
      return false;
    }
    if (context->max_invites > 100) {
      FireCalendarError(L, "CALENDAR_ERROR_INVITES_EXCEEDED", 100);
      return false;
    }
  }

  constexpr auto kUnsetCalendarComponent = std::numeric_limits<std::uint32_t>::max();
  if (context->hour == kUnsetCalendarComponent ||
      context->minute == kUnsetCalendarComponent) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_TIME");
    return false;
  }
  if (context->month == kUnsetCalendarComponent || context->day == kUnsetCalendarComponent ||
      context->year == kUnsetCalendarComponent) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_DATE");
    return false;
  }

  const CalendarPackedDateTime target_time{
      .year = context->year,
      .month = context->month,
      .day = context->day,
      .hour = context->hour,
      .minute = context->minute,
      .weekday = context->weekday,
      .flags = context->time_flags,
  };
  const auto current_time =
      DecodePackedCalendarDateTime(session->session().game_time().packed_time);
  if (::openwow::game::CalendarDateFields_LessOrEqual(ToCalendarDateFields(target_time),
                                                       ToCalendarDateFields(current_time))) {
    FireCalendarError(L, "CALENDAR_ERROR_EVENT_TIME_PASSED");
    return false;
  }

  std::vector<::openwow::net::wotlk::CalendarAddEventInvite> invites;
  if ((context->flags & kCalendarFlagGuildAnnouncement) == 0) {
    const auto stored_invites = GetContextInvites(calendar, *context);
    invites.reserve(stored_invites.size());
    for (const auto &invite : stored_invites) {
      invites.push_back({
          .invitee = ::openwow::game::ObjectGuid(invite.invitee_guid),
          .status = invite.status,
          .moderator_status = invite.rank,
      });
    }
  }

  session->interaction().SendCalendarAddEvent(
      context->title, context->description, context->event_type, context->repeat_option,
      context->max_invites, context->dungeon_id,
      PackCalendarDateTime(target_time), context->secondary_time_packed,
      context->flags, invites);
  calendar.MarkEventActionSent();
  SetCalendarActionPending(L, true);
  return true;
}

bool TryRunCalendarAddEvent(lua_State *L) {
  if (!GameUI_CanPerformProtectedAction(openwow::ui::game::protected_action_kind::kCalendar)) {
    return false;
  }
  return TrySendCalendarAddEvent(L);
}

bool CanApplyInviteStatusChange(const ::openwow::game::CalendarContextEventInfo &context,
                                const std::vector<::openwow::game::CalendarSystemInvite> &invites,
                                std::size_t invite_index, std::uint32_t status,
                                std::uint64_t active_player_guid) {

  if (!HasCalendarEventModeratorRights(context, nullptr, active_player_guid) ||
      invite_index >= invites.size() || status >= kCalendarInviteStatusCount || status == 0 ||
      status == 6 || status == 7) {
    return false;
  }
  if ((context.flags & kCalendarFlagGuildEvent) != 0 && (status == 1 || status == 2)) {
    return false;
  }
  return status != invites[invite_index].status;
}

int LuaOpenCalendar(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (cal.HasRequestedInitialSnapshot()) {
    return 0;
  }

  session->interaction().SendCalendarGetCalendar();
  return 0;
}

int LuaCalendarGetNumDayEvents(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CalendarGetNumDayEvents([-1,0,1], monthDay)");
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));

  CalendarRelativeDayLookup lookup{};
  const size_t count = ResolveCalendarRelativeDayLookup(L, cal, month_offset, day, lookup)
                           ? cal.GetNumDayEvents(static_cast<uint32_t>(lookup.month),
                                                 static_cast<uint32_t>(lookup.day),
                                                 static_cast<uint32_t>(lookup.year))
                           : 0;
  lua_pushnumber(L, static_cast<lua_Integer>(count));
  return 1;
}

int LuaCalendarGetDayEvent(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();

  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarGetDayEvent([-1.0.1], monthDay, index)", month_offset, day,
          event_index)) {
    return 0;
  }

  ::openwow::game::CalendarSystemEvent ev{};
  if (!ResolveCalendarDayEventFromCoordinates(L, cal, month_offset, day, event_index, ev)) {

    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushstring(L, "");
    lua_pushstring(L, "");
    lua_pushnumber(L, 0);
    lua_pushstring(L, "");
    lua_pushstring(L, "");
    lua_pushnumber(L, 0);
    lua_pushstring(L, "");
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushstring(L, "");
    return 15;
  }

  uint32_t hour = PackedTimeHour(ev.time);
  uint32_t minute = PackedTimeMinute(ev.time);
  std::string title;
  std::string difficulty_name;
  const bool is_holiday = (ev.flags & 0x008u) != 0;
  std::optional<std::string> holiday_title;
  if (is_holiday) {
    holiday_title =
        ResolveCalendarHolidayDisplay(L, cal, static_cast<std::uint32_t>(ev.event_id)).name;
  } else if ((ev.flags & 0x280u) != 0) {
    if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
      if (const auto *map_entry = dbc->map().LookupEntry(ev.map_id); map_entry != nullptr) {
        title = std::string(map_entry->name);
      }
      if (ev.dungeon_id >= 0) {
        if (const auto *map_difficulty = LookupCalendarMapDifficulty(
                *dbc, ev.map_id, static_cast<std::uint32_t>(ev.dungeon_id));
            map_difficulty != nullptr) {
          difficulty_name = std::string(map_difficulty->difficulty_string);
        }
      }
    }
  } else {
    title = ev.title;
    ::openwow::game::ChatFrame_MatureLanguageFilter(title, false);
  }
  std::string texture = ResolveCalendarDayEventTexture(L, cal, ev);
  const std::string invited_by = ResolveCalendarDayEventInviterName(L, ev);

  if (is_holiday) {
    if (holiday_title) {
      lua_pushstring(L, holiday_title->c_str());
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushstring(L, title.c_str());
  }
  lua_pushnumber(L, static_cast<lua_Number>(hour));
  lua_pushnumber(L, static_cast<lua_Number>(minute));
  lua_pushstring(L, CalendarTypeFromFlags(ev.flags));
  lua_pushstring(L, SequenceTypeFromEvent(ev));
  lua_pushnumber(L, static_cast<lua_Number>(ev.type + 1));
  lua_pushstring(L, texture.c_str());
  lua_pushstring(L, ::openwow::game::CalendarSystem::GetInviteModStatusString(
                        ev.invite_mod_status));
  lua_pushnumber(L, static_cast<lua_Number>(ev.invite_status + 1));
  lua_pushstring(L, invited_by.c_str());
  lua_pushnumber(L, CalendarDifficultyFromEvent(ev));
  lua_pushnumber(L, CalendarInviteTypeFromEvent(ev));
  lua_pushnumber(L, static_cast<lua_Number>(ev.sequence_index + 1));
  lua_pushnumber(L, static_cast<lua_Number>(ev.sequence_total));
  lua_pushstring(L, difficulty_name.c_str());
  return 15;
}

int LuaCalendarGetMonth(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const int offset = GetOptionalCalendarMonthOffset(L, 1);
  const CalendarResolvedMonthInfo resolved =
      ResolveCalendarRelativeMonthInfo(L, cal, offset);
  lua_pushinteger(L, resolved.month);
  lua_pushinteger(L, resolved.year);
  lua_pushinteger(L, resolved.num_days);
  lua_pushinteger(L, resolved.first_weekday);
  return 4;
}

int LuaCalendarSetMonth(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const int offset = RequireCalendarMonthOffset(L, 1);
  const CalendarResolvedMonthInfo resolved =
      ResolveCalendarRelativeMonthInfo(L, cal, offset);
  cal.SetViewMonth(static_cast<uint32_t>(resolved.month),
                   static_cast<uint32_t>(resolved.year));
  return 0;
}

int LuaCalendarGetMinDate(lua_State *L) {

  constexpr int kMinMonth = 11;
  constexpr int kMinDay = 24;
  constexpr int kMinYear = 2004;
  int weekday = WeekdayForDate(kMinMonth, kMinDay, kMinYear);
  lua_pushnumber(L, weekday);
  lua_pushnumber(L, kMinMonth);
  lua_pushnumber(L, kMinDay);
  lua_pushnumber(L, kMinYear);
  return 4;
}

int LuaCalendarGetMaxDate(lua_State *L) {

  int weekday = WeekdayForDate(12, 31, 2030);
  lua_pushnumber(L, weekday);
  lua_pushnumber(L, 12);
  lua_pushnumber(L, 31);
  lua_pushnumber(L, 2030);
  return 4;
}

int LuaCalendarGetDate(lua_State *L) {

  const CalendarDate date = GetCurrentCalendarDate(L);
  lua_pushnumber(L, date.weekday);
  lua_pushnumber(L, date.month);
  lua_pushnumber(L, date.day);
  lua_pushnumber(L, date.year);
  return 4;
}

int LuaCalendarGetNumPendingInvites(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  lua_pushnumber(L, static_cast<lua_Integer>(cal.GetNumPendingInvites()));
  return 1;
}

int LuaCalendarGetAbsMonth(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarGetAbsMonth(month[, year])");
    return 0;
  }
  const int month = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  int year;
  if (lua_isnumber(L, 2)) {
    year = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 2));
  } else if (cal.HasViewMonthSelection()) {
    year = static_cast<int>(cal.GetViewYear());
  } else {
    year = GetCurrentCalendarDate(L).year;
  }
  const auto resolved =
      ResolveCalendarAbsoluteMonthInfo(month, year, GetCurrentCalendarDate(L).year);
  lua_pushinteger(L, resolved.month);
  lua_pushinteger(L, resolved.year);
  lua_pushinteger(L, resolved.num_days);
  lua_pushinteger(L, resolved.first_weekday);
  return 4;
}

int LuaCalendarSetAbsMonth(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarSetAbsMonth(month[, year])");
    return 0;
  }
  const int month = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  int year;
  if (lua_isnumber(L, 2)) {
    year = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 2));
  } else if (cal.HasViewMonthSelection()) {
    year = static_cast<int>(cal.GetViewYear());
  } else {
    year = GetCurrentCalendarDate(L).year;
  }
  const auto resolved =
      ResolveCalendarAbsoluteMonthInfo(month, year, GetCurrentCalendarDate(L).year);
  cal.SetViewMonth(static_cast<uint32_t>(resolved.month), static_cast<uint32_t>(resolved.year));
  return 0;
}

int LuaCalendarCanAddEvent(lua_State *L) {
  const auto &cal = ::openwow::game::CalendarSystem::Get();
  lua_pushboolean(L, cal.CanSendEventAction() ? 1 : 0);
  return 1;
}

int LuaCalendarNewEvent(lua_State *L) {

  if (SeedNewCalendarContextEvent(L, 0x001u, false,
                                  NewCalendarCreatorInviteMode::kPlayerEvent)) {
    FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_INVITE_LIST, {true});
  }
  return 0;
}

int LuaCalendarNewGuildAnnouncement(lua_State *L) {

  SeedNewCalendarContextEvent(L, 0x040u, true);
  return 0;
}

int LuaCalendarNewGuildEvent(lua_State *L) {

  SeedNewCalendarContextEvent(L, 0x400u, true, NewCalendarCreatorInviteMode::kGuildEvent);
  return 0;
}

int LuaCalendarGetEventInfo(lua_State *L) {

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kCalendarEventInfoResultCount, "calendar event values");
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    PushDefaultCalendarEventInfo(L);
    return result_count;
  }

  auto *session = GetWorldSession(L);
  const auto active_player_guid =
      session != nullptr ? session->objects().GetActivePlayerGuid().GetRawValue() : 0;

  std::string title = ctx->title;
  std::string desc = ctx->description;
  ::openwow::game::ChatFrame_MatureLanguageFilter(title, false);
  ::openwow::game::ChatFrame_MatureLanguageFilter(desc, false);
  std::string creator;
  uint8_t event_type = ctx->event_type;
  uint8_t repeat_option = ctx->repeat_option;
  uint32_t max_size = ctx->max_invites;
  uint32_t texture_index = 0;
  uint32_t month = ctx->month;
  uint32_t day = ctx->day;
  uint32_t year = ctx->year;
  uint32_t hour = ctx->hour;
  uint32_t minute = ctx->minute;
  uint32_t flags = ctx->flags;

  texture_index = static_cast<std::uint32_t>(FindCalendarTextureSelectionIndex(
      GetCalendarTextureOptionsForRawEventType(L, event_type),
      static_cast<std::uint32_t>(ctx->dungeon_id)));

  if (session != nullptr) {
    creator = ResolveCalendarEventListPlayerName(*session, ctx->creator_guid);
  }

  const int weekday = ctx->weekday < 0 ? 0 : ctx->weekday + 1;
  ::openwow::game::CalendarDateFieldsEx lockout{};
  ::openwow::game::CalendarPackedTime_UnpackToArray(ctx->secondary_time_packed, lockout);
  const auto display_calendar_field = [](const std::int32_t value,
                                         const std::int32_t offset) -> lua_Number {
    return value < 0 ? 0.0 : static_cast<lua_Number>(value + offset);
  };

  double invite_status = 0.0;
  double mod_status = 0.0;
  const auto invites = cal.GetEventInvites(ctx->event_id);
  const auto invite_it =
      std::find_if(invites.begin(), invites.end(),
                   [active_player_guid](const ::openwow::game::CalendarSystemInvite &invite) {
                     return active_player_guid != 0 && invite.invitee_guid == active_player_guid;
                   });
  if (invite_it != invites.end()) {
    invite_status = static_cast<double>(invite_it->status + 1);
    mod_status = static_cast<double>(invite_it->rank + 1);
  } else if ((flags & 0x400u) != 0) {
    invite_status = 8.0;
    mod_status = 2.0;
  }

  lua_pushstring(L, title.c_str());
  lua_pushstring(L, desc.c_str());
  lua_pushstring(L, creator.c_str());

  lua_pushnumber(L, event_type);
  lua_pushnumber(L, repeat_option);
  lua_pushnumber(L, max_size);
  lua_pushnumber(L, texture_index);
  lua_pushnumber(L, weekday);
  lua_pushnumber(L, month);
  lua_pushnumber(L, day);
  lua_pushnumber(L, year);
  lua_pushnumber(L, hour);
  lua_pushnumber(L, minute);
  lua_pushnumber(L, display_calendar_field(lockout.weekday, 1));
  lua_pushnumber(L, display_calendar_field(lockout.month, 1));
  lua_pushnumber(L, display_calendar_field(lockout.day, 1));
  lua_pushnumber(L, display_calendar_field(lockout.year, 2000));
  lua_pushnumber(L, display_calendar_field(lockout.hour, 0));
  lua_pushnumber(L, display_calendar_field(lockout.minute, 0));
  if ((flags & kCalendarFlagLocked) != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  if ((flags & kCalendarFlagAutoApprove) != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  if (cal.HasPendingInviteForEvent(ctx->event_id)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, invite_status);
  lua_pushnumber(L, mod_status);
  lua_pushstring(L, ::openwow::game::CalendarSystem::GetEventTypeString(
                        static_cast<std::uint16_t>(flags)));
  return result_count;
}

int LuaCalendarAddEvent(lua_State *L) {

  TryRunCalendarAddEvent(L);
  return 0;
}

int LuaCalendarCloseEvent([[maybe_unused]] lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  cal.ClearOpenedEvent();
  return 0;
}

int LuaCalendarContextDeselectEvent([[maybe_unused]] lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  cal.ClearContextMenuEvent();
  return 0;
}

int LuaCalendarContextEventClipboard([[maybe_unused]] lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  lua_pushboolean(L, cal.HasClipboardEvent() ? 1 : 0);
  return 1;
}

int LuaCalendarContextEventComplain(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto &calendar = ::openwow::game::CalendarSystem::Get();
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: CalendarContextEventComplain([-1.0.1], monthDay, index)", target)) {
    return 0;
  }
  if (!session) {
    return 0;
  }

  if (calendar.IsActionPending()) {
    return 0;
  }

  const auto *active_player = session->objects().GetActivePlayer();
  if (active_player == nullptr || !CanComplainAboutCalendarContextEvent(*session, calendar, target)) {
    return 0;
  }

  const auto active_player_guid = active_player->GetGuid().GetRawValue();
  const auto creator_guid = ResolveCalendarComplaintCreatorGuid(calendar, target);
  if (session->social().HasRecentComplaintGuid(creator_guid)) {
    FireCalendarError(L, "COMPLAINT_ADDED");

    session->interaction().SendCalendarEventRemoveInvite(active_player_guid, target.event_id,
                                                         target.self_invite_id, 0);
  } else {
    session->social().RememberRecentComplaintGuid(creator_guid);
    session->interaction().SendCalendarComplain(creator_guid, target.event_id,
                                                target.self_invite_id);
  }

  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarContextEventCopy(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto &cal = ::openwow::game::CalendarSystem::Get();

  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: Script_CalendarContextEventCopy([-1.0.1], monthDay, index)",
          target)) {
    return 0;
  }

  if (!CanEditCalendarContextSelection(session, target)) {
    FireCalendarPermissionError(L);
    return 0;
  }

  cal.SetClipboardEvent(target);
  return 0;
}

int LuaCalendarContextEventGetCalendarType(lua_State *L) {
  const auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
    const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
    const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
    const auto event_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3)) - 1u;

    ::openwow::game::CalendarSystemEvent event{};
    if (!ResolveCalendarDayEventFromCoordinates(L, calendar, month_offset, day, event_index,
                                                event)) {
      FrameScript_PushNil(L);
      return 1;
    }

    lua_pushstring(L, CalendarTypeFromFlags(event.flags));
    return 1;
  }

  const auto *selected = calendar.GetContextMenuEvent();
  if (!selected) {
    luaL_error(L, "Usage: CalendarContextEventGetCalendarType([-1.0.1], monthDay, index)");
    return 0;
  }

  lua_pushstring(L, CalendarTypeFromFlags(selected->flags));
  return 1;
}

int LuaCalendarContextEventPaste(lua_State *L) {

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    luaL_error(L, "Usage: Script_CalendarContextEventPaste([-1.0.1], monthDay)");
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (!cal.HasClipboardEvent()) {
    return 0;
  }
  if (cal.IsActionPending()) {
    return 0;
  }

  if (!cal.CanSendEventAction()) {
    FireCalendarError(L, "CALENDAR_ERROR_EVENT_THROTTLED");
    return 0;
  }

  const auto *clipboard = cal.GetClipboardEvent();
  if (clipboard == nullptr || clipboard->event_id == 0) {
    return 0;
  }

  const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  const auto target_day_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2)) - 1u;
  const CalendarResolvedMonthInfo target_month =
      ResolveCalendarRelativeMonthInfo(L, cal, month_offset);
  if (target_day_index >= static_cast<std::uint32_t>(target_month.num_days)) {
    return 0;
  }

  const CalendarDate target_date{
      .weekday = 1,
      .month = static_cast<int>(target_month.month),
      .day = static_cast<int>(target_day_index + 1u),
      .year = static_cast<int>(target_month.year),
  };
  if (!IsCalendarCreatableDateValid(L, target_date)) {
    return 0;
  }

  auto event_time = DecodePackedCalendarDateTime(
      cal.GetCurrentTimePacked().value_or(session->session().game_time().packed_time));
  event_time.year = target_month.year;
  event_time.month = target_month.month;
  event_time.day = target_day_index + 1u;

  event_time.weekday =
      WeekdayForDate(static_cast<int>(event_time.month), static_cast<int>(event_time.day),
                     static_cast<int>(event_time.year)) -
      1;

  session->interaction().SendCalendarCopyEvent(clipboard->event_id, clipboard->self_invite_id,
                                               PackCalendarDateTime(event_time));
  cal.MarkEventActionSent();
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarContextEventRemove(lua_State *L) {
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: Script_CalendarContextEventRemove([-1.0.1], monthDay, index)",
          target)) {
    return 0;
  }

  if (!CanEditCalendarContextSelection(session, target)) {
    FireCalendarPermissionError(L);
    return 0;
  }
  if (!session) {
    return 0;
  }

  if (::openwow::game::CalendarSystem::Get().IsActionPending()) {
    return 0;
  }

  if (session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  session->interaction().SendCalendarRemoveEvent(target.event_id, target.self_invite_id,
                                                 target.flags);
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarContextEventSignUp(lua_State *L) {
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: CalendarContextEventSignUp([-1.0.1], monthDay, index)", target)) {
    return 0;
  }
  if (!session) {
    return 0;
  }

  SendCalendarContextGuildSignUp(L, *session, target, 0);
  return 0;
}

int LuaCalendarContextInviteAvailable(lua_State *L) {
  SendCalendarContextInviteRsvp(L,
                                "Usage: CalendarContextInviteAvailable([-1.0.1], monthDay, "
                                "index)",
                                1);
  return 0;
}

int LuaCalendarContextInviteModeratorStatus(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {

    if (::openwow::game::CalendarSystem::Get().GetContextMenuEvent() == nullptr) {
      luaL_error(L, "Usage: CalendarContextInviteModeratorStatus([-1.0.1], monthDay, index)");
      return 0;
    }
    lua_pushnil(L);
    return 1;
  }

  const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  const auto event_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3)) - 1u;
  const auto &calendar = ::openwow::game::CalendarSystem::Get();
  ::openwow::game::CalendarSystemEvent event{};
  if (ResolveCalendarDayEventFromCoordinates(L, calendar, month_offset, day, event_index, event)) {
    if ((event.invite_mod_status & 0x04u) != 0) {
      lua_pushstring(L, "CREATOR");
      return 1;
    }
    if ((event.invite_mod_status & 0x02u) != 0) {
      lua_pushstring(L, "MODERATOR");
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaCalendarContextInviteRemove(lua_State *L) {
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: CalendarContextInviteRemove([-1.0.1], monthDay, index)", target)) {
    return 0;
  }

  if ((target.flags & (kCalendarFlagPlayerEvent | kCalendarFlagGuildEvent)) == 0) {
    return 0;
  }
  if (!session) {
    return 0;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending() || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto active_player_guid = GetActiveCalendarPlayerGuid(*session);
  if (active_player_guid == 0) {
    return 0;
  }

  session->interaction().SendCalendarEventRemoveInvite(active_player_guid, target.event_id,
                                                       target.self_invite_id, 0);
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarContextInviteTentative(lua_State *L) {
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: Script_CalendarContextInviteTentative([-1.0.1], monthDay, index)",
          target)) {
    return 0;
  }
  if (!session) {
    return 0;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending()) {
    return 0;
  }

  const bool is_guild_signup = ::openwow::game::CalendarSystem::IsGuildSignupEvent(
      target.flags, target.invite_type != 0 ? 0x08u : 0x00u);
  if (is_guild_signup) {
    if (!SendCalendarContextGuildSignUp(L, *session, target, 1)) {
      return 0;
    }
  } else {
    session->interaction().SendCalendarEventRsvp(target.event_id, target.self_invite_id, 8);
    SetCalendarActionPending(L, true);
  }
  return 0;
}

int LuaCalendarContextSelectEvent(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    luaL_error(L, "Usage: CalendarContextSelectEvent([-1.0.1], monthDay, index)");
    return 0;
  }
  const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  const auto event_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3)) - 1u;

  auto &cal = ::openwow::game::CalendarSystem::Get();
  ::openwow::game::CalendarSystemEvent event{};
  if (ResolveCalendarDayEventFromCoordinates(L, cal, month_offset, day, event_index, event)) {
    cal.SetContextMenuEvent(BuildCalendarDayEventSelection(event));
  }
  return 0;
}

int LuaCalendarEventAvailable(lua_State *L) {
  SendCurrentCalendarEventRsvp(L, 1);
  return 0;
}

int LuaCalendarEventCanEdit(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  const auto *session = GetWorldSession(L);
  const bool can_edit =
      ctx != nullptr && session != nullptr && CanMutateCalendarEventBuffer(*session, *ctx);

  lua_pushboolean(L, can_edit ? 1 : 0);
  return 1;
}

int LuaCalendarEventClearAutoApprove(lua_State *L) {
  TrySetCurrentCalendarEventFlag(L, kCalendarFlagAutoApprove, false);
  return 0;
}

int LuaCalendarEventClearLocked(lua_State *L) {
  TrySetCurrentCalendarEventFlag(L, kCalendarFlagLocked, false);
  return 0;
}

int LuaCalendarEventClearModerator(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventClearModerator(index)");
    return 0;
  }
  return ToggleCalendarInviteModeratorStatus(L, 0);
}

int LuaCalendarEventDecline(lua_State *L) {
  SendCurrentCalendarEventRsvp(L, 2);
  return 0;
}

int LuaCalendarEventSetAutoApprove(lua_State *L) {
  TrySetCurrentCalendarEventFlag(L, kCalendarFlagAutoApprove, true);
  return 0;
}

int LuaCalendarEventSetDate(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    luaL_error(L, "Usage: CalendarEventSetDate(month, day, year)");
    return 0;
  }

  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_DATE");
    return 0;
  }

  const auto month = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  const auto year = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3));
  if (!TryApplyCalendarDateEdit(ctx->month, ctx->day, ctx->year, ctx->settings_changed,
                                month, day, year)) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_DATE");
  }
  return 0;
}

int LuaCalendarEventSetSize(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return 0;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  auto *context =
      const_cast<::openwow::game::CalendarContextEventInfo *>(calendar.GetContextEvent());
  if (context == nullptr) {
    return 0;
  }

  const auto requested_size = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1));
  const auto *session = GetWorldSession(L);
  if ((context->flags & kCalendarFlagGuildAnnouncement) != 0) {
    if (session == nullptr || !HasGuildLeaderCalendarRight(*session)) {
      return 0;
    }
  } else {
    const auto active_player_guid = session ? GetActiveCalendarPlayerGuid(*session) : 0;
    if (!CanEditCalendarEventBuffer(*context, active_player_guid)) {
      return 0;
    }
  }

  if ((context->flags & (kCalendarFlagGuildAnnouncement | kCalendarFlagGuildEvent)) != 0 ||
      requested_size <= 1 || requested_size == context->max_invites) {
    return 0;
  }

  context->max_invites = requested_size;
  context->settings_changed = true;
  return 0;
}

int LuaCalendarEventSetDescription(lua_State *L) {
  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx) {
    return 0;
  }

  const char *desc = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
  ApplyCalendarEventTextEdit(ctx->description, ctx->settings_changed, desc, 256, 1024, false);
  return 0;
}

int LuaCalendarEventSetLocked(lua_State *L) {
  TrySetCurrentCalendarEventFlag(L, kCalendarFlagLocked, true);
  return 0;
}

int LuaCalendarEventSetModerator(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventSetModerator(index)");
    return 0;
  }
  return ToggleCalendarInviteModeratorStatus(L, 1);
}

int LuaCalendarEventSetRepeatOption(lua_State *L) {
  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx || !lua_isnumber(L, 1)) {
    return 0;
  }

  const auto option = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1)) - 1u;
  if (option >= 4u) {
    return 0;
  }

  const auto repeat_option = static_cast<std::uint8_t>(option);
  if (repeat_option != ctx->repeat_option) {
    ctx->repeat_option = repeat_option;
    ctx->settings_changed = true;
  }
  return 0;
}

int LuaCalendarEventSetTextureID(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventSetTextureID(textureIndex)");
    return 0;
  }

  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx) {
    return 0;
  }

  const auto texture_index =
      static_cast<lua_Integer>(TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1)));
  const auto texture_options = GetCalendarTextureOptionsForRawEventType(L, ctx->event_type);
  const auto new_texture_id = ResolveCalendarTextureRecordId(texture_options, texture_index);
  if (new_texture_id == 0) {
    return 0;
  }

  if (static_cast<std::int32_t>(new_texture_id) != ctx->dungeon_id) {
    ctx->dungeon_id = static_cast<std::int32_t>(new_texture_id);
    ctx->settings_changed = true;
  }
  return 0;
}

int LuaCalendarEventSetTime(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    luaL_error(L, "Usage: CalendarEventSetTime(hour, minute)");
    return 0;
  }

  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_TIME");
    return 0;
  }

  const auto hour = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1));
  const auto minute = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  if (!TryApplyCalendarTimeEdit(ctx->hour, ctx->minute, ctx->settings_changed, hour, minute)) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_TIME");
  }
  return 0;
}

int LuaCalendarEventSetTitle(lua_State *L) {
  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx) {
    return 0;
  }

  const char *title = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
  ApplyCalendarEventTextEdit(ctx->title, ctx->settings_changed, title, 32, 128, true);
  return 0;
}

int LuaCalendarEventSetType(lua_State *L) {
  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx || !lua_isnumber(L, 1)) {
    return 0;
  }

  const auto event_type = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1)) - 1u;
  if (event_type >= 5u) {
    return 0;
  }

  const auto new_event_type = static_cast<std::uint8_t>(event_type);
  if (new_event_type != ctx->event_type) {
    ctx->event_type = new_event_type;
    ctx->settings_changed = true;
  }
  return 0;
}

int LuaCalendarEventSignUp(lua_State *L) {
  SendCurrentCalendarGuildSignup(L, 0);
  return 0;
}

int LuaCalendarEventSortInvites(lua_State *L) {

  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: CalendarEventSortInvites(\"criteria\", reverse)");
    return 0;
  }
  const char *criteria_str = lua_tostring(L, 1);

  int criterion = 3;
  if (EqualsAsciiNoCase(criteria_str, "name")) {
    criterion = 0;
  } else if (EqualsAsciiNoCase(criteria_str, "level")) {
    criterion = 1;
  } else if (EqualsAsciiNoCase(criteria_str, "class")) {
    criterion = 2;
  } else if (EqualsAsciiNoCase(criteria_str, "status")) {
    criterion = 3;
  } else if (EqualsAsciiNoCase(criteria_str, "party")) {
    criterion = 4;
  } else if (EqualsAsciiNoCase(criteria_str, "notes")) {
    criterion = 5;
  }

  auto *session = GetWorldSession(L);
  auto &cal = ::openwow::game::CalendarSystem::Get();
  cal.ApplyInviteSortRequest(static_cast<std::uint32_t>(criterion),
                             ScriptReadBoolArgOrDefault(L, 2, true));

  const bool reverse = cal.GetSortReverse();
  const auto *ctx = cal.GetContextEvent();
  if (ctx) {
    cal.SortInvitesByCriterion(ctx->event_id, criterion, reverse, session);
    FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
  }
  return 0;
}

int LuaCalendarEventTentative(lua_State *L) {

  SendCurrentCalendarTentative(L);
  return 0;
}

int LuaCalendarEventInvite(lua_State *L) {
  const char *raw_name = lua_tostring(L, 1);
  if (raw_name == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (cal.IsActionPending()) {
    return 0;
  }

  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    return 0;
  }

  const auto *active_player = session->objects().GetActivePlayer();
  const auto active_player_guid = active_player != nullptr
                                      ? active_player->GetGuid().GetRawValue()
                                      : 0;
  if (!CanEditCalendarEventBuffer(*ctx, active_player_guid)) {
    return 0;
  }

  if (!cal.CanSendInvite(false)) {
    FireCalendarError(L, "CALENDAR_ERROR_INVITE_THROTTLED");
    return 0;
  }

  if ((ctx->flags & kCalendarFlagGuildAnnouncement) != 0) {
    FireCalendarError(L, "CALENDAR_ERROR_INVITES_DISABLED");
    return 0;
  }

  const auto invites = GetContextInvites(cal, *ctx);
  if (invites.size() >= 100u) {
    FireCalendarError(L, "CALENDAR_ERROR_INVITES_EXCEEDED", 100);
    return 0;
  }

  const std::string invitee_name = NormalizeCalendarInviteeName(raw_name);
  if (HasResolvedCalendarInviteeName(*session, invites, invitee_name)) {
    FireCalendarError(L, "CALENDAR_ERROR_ALREADY_INVITED_TO_EVENT_S", invitee_name);
    return 0;
  }

  if (!session->interaction().SendCalendarEventInvite(
          ctx->event_id, ctx->self_invite_id, invitee_name, ctx->local_edit,
          (ctx->flags & kCalendarFlagGuildEvent) != 0)) {
    return 0;
  }
  cal.MarkInviteSent();
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarEventRemoveInvite(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventRemoveInvite(index)");
    return 0;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending()) {
    return 0;
  }

  const auto *context = calendar.GetContextEvent();
  if (context == nullptr) {
    return 0;
  }

  return RemoveCalendarInviteByIndex(L, session, *context, ReadCalendarInviteIndexArg(L));
}

int LuaCalendarEventSelectInvite(lua_State *L) {

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CalendarEventSelectInvite(index)");
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx)
    return 0;

  const auto invite_index =
      static_cast<std::uint32_t>(TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1))) - 1u;
  cal.SelectInviteByIndex(ctx->event_id, static_cast<std::size_t>(invite_index));
  return 0;
}

int LuaCalendarIsActionPending(lua_State *L) {

  lua_pushboolean(L, ::openwow::game::CalendarSystem::Get().IsActionPending() ? 1 : 0);
  return 1;
}

int LuaCalendarMassInviteArenaTeam(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CalendarNewArenaTeamEvent(index)");
  }

  const auto team_index = GetOptionalCalendarUnsignedArgumentOrZero(L, 1) - 1u;

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (calendar.IsActionPending()) {
    return 0;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (!player || team_index >= 3u) {
    return 0;
  }

  const auto team_info = player->GetArenaTeamInfo(static_cast<std::uint8_t>(team_index));
  if (team_info.team_id == 0) {
    return 0;
  }

  const auto *context = calendar.GetContextEvent();
  if (!context || !context->local_edit || (context->flags & 0x440u) != 0) {
    return 0;
  }

  session->interaction().SendCalendarArenaTeam(team_info.team_id);
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarDefaultGuildFilter(lua_State *L) {
  if (GetActiveCalendarPlayer(L) == nullptr) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 3;
  }

  constexpr std::array<std::uint32_t, 3> kDefaultLevelsByExpansion = {
      60u,
      70u,
      80u,
  };

  const auto expansion_level =
      openwow::net::ClientServices::Instance().GetExpansionLevel();
  const auto default_level =
      expansion_level < kDefaultLevelsByExpansion.size()
          ? kDefaultLevelsByExpansion[expansion_level]
          : 0u;

  lua_pushnumber(L, static_cast<lua_Number>(default_level));
  lua_pushnumber(L, static_cast<lua_Number>(default_level));
  lua_pushnumber(
      L, static_cast<lua_Number>(::openwow::game::GuildSystem::Get().GetNumRanks()));
  return 3;
}

int LuaCalendarMassInviteGuild(lua_State *L) {
  if (!lua_isnumber(L, 1) && !lua_isnumber(L, 2) && !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: CalendarMassInviteGuild(minLevel, maxLevel, minRank)");
  }

  const auto min_level = GetOptionalCalendarUnsignedArgumentOrZero(L, 1);
  const auto max_level = GetOptionalCalendarUnsignedArgumentOrZero(L, 2);
  const auto min_rank = GetOptionalCalendarUnsignedArgumentOrZero(L, 3) - 1u;

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (cal.IsActionPending()) {
    return 0;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (!player) {
    return 0;
  }
  if (player->GetGuildID() == 0) {
    FireCalendarError(L, "ERR_GUILD_PLAYER_NOT_IN_GUILD");
    return 0;
  }

  const auto *ctx = cal.GetContextEvent();
  if (!ctx || !ctx->local_edit || (ctx->flags & 0x440u) != 0) {
    return 0;
  }

  session->interaction().SendCalendarGuildFilter(min_level, max_level, min_rank);
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarGetEventIndex(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *opened = cal.GetOpenedEvent();

  int month_offset = 0;
  int day = 0;
  int event_index = 0;
  if (opened != nullptr &&
      ResolveCalendarSelectionIndex(cal, *opened, month_offset, day, event_index)) {
    lua_pushnumber(L, month_offset);
    lua_pushnumber(L, day);
    lua_pushnumber(L, event_index);
    return 3;
  }

  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return 3;
}

int LuaCalendarOpenEvent(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    luaL_error(L, "Usage: CalendarOpenEvent([-1,0,1], monthDay, index)");
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (cal.IsActionPending()) {
    return 0;
  }

  const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  const auto event_index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3)) - 1u;

  ::openwow::game::CalendarSystemEvent event{};
  if (!ResolveCalendarDayEventFromCoordinates(L, cal, month_offset, day, event_index, event)) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if ((event.flags & 0x543u) != 0) {
    if (!session) {
      return 0;
    }
    session->interaction().SendCalendarGetEvent(event.event_id);
    SetCalendarActionPending(L, true);
    return 0;
  }

  cal.SetOpenedEvent(BuildCalendarDayEventSelection(event));
  FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_OPEN_EVENT,
                      {::openwow::game::CalendarSystem::GetEventTypeString(
                          static_cast<std::uint16_t>(event.flags))});
  return 0;
}

int LuaCalendarRemoveEvent(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx || ctx->local_edit || cal.IsActionPending()) {
    return 0;
  }

  const auto active_player_guid = GetActiveCalendarPlayerGuid(*session);
  const auto editor_invite_id =
      ResolveCalendarEventBufferEditorInviteId(*ctx, active_player_guid);

  if (UsesCalendarRemoveEventDeletePath(*session, *ctx, active_player_guid)) {
    if (!CanEditCalendarEventBuffer(*ctx, active_player_guid)) {
      FireCalendarPermissionError(L);
      return 0;
    }

    const bool uses_guild_calendar =
        (ctx->flags & (kCalendarFlagGuildAnnouncement | kCalendarFlagGuildEvent)) != 0;
    session->interaction().SendCalendarRemoveEventBuffer(ctx->event_id, editor_invite_id,
                                                         uses_guild_calendar);
    SetCalendarActionPending(L, true);
    return 0;
  }

  if (active_player_guid == 0) {
    return 0;
  }

  const auto invites = GetContextInvites(cal, *ctx);
  const auto *invite = FindCalendarInviteByInviteeGuid(invites, active_player_guid);
  if (!invite) {
    return 0;
  }
  if (invite->rank == 2) {
    FireCalendarError(L, "CALENDAR_DELETE_CREATOR_FAILED");
    return 0;
  }

  session->interaction().SendCalendarEventRemoveInvite(active_player_guid, ctx->event_id,
                                                       invite->invite_id, editor_invite_id);
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarUpdateEvent(lua_State *L) {
  if (!GameUI_CanPerformProtectedAction(openwow::ui::game::protected_action_kind::kCalendar)) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  if (cal.IsActionPending()) {
    return 0;
  }

  auto *ctx = const_cast<::openwow::game::CalendarContextEventInfo *>(cal.GetContextEvent());
  if (!ctx || ctx->local_edit) {
    return 0;
  }

  const auto active_player_guid = GetActiveCalendarPlayerGuid(*session);
  if (!UsesCalendarRemoveEventDeletePath(*session, *ctx, active_player_guid)) {
    FireCalendarPermissionError(L);
    return 0;
  }

  if (!ctx->settings_changed) {
    return 0;
  }

  ctx->settings_changed = false;
  if (IsCalendarContextEventAtOrBeforeCurrentTime(cal, *ctx)) {
    FireCalendarError(L, "CALENDAR_ERROR_EVENT_PASSED");
    return 0;
  }

  session->interaction().SendCalendarUpdateEvent(
      ctx->event_id, ctx->self_invite_id, ctx->title, ctx->description, ctx->event_type,
      ctx->repeat_option, ctx->max_invites, ctx->dungeon_id,
      PackCalendarDateTime({
          .year = ctx->year,
          .month = ctx->month,
          .day = ctx->day,
          .hour = ctx->hour,
          .minute = ctx->minute,
          .weekday = ctx->weekday,
          .flags = ctx->time_flags,
      }),
      ctx->secondary_time_packed, ctx->flags);
  SetCalendarActionPending(L, true);
  return 0;
}

int LuaCalendarEventGetCalendarType(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    FrameScript_PushNil(L);
    return 1;
  }

  lua_pushstring(L, CalendarTypeFromFlags(ctx->flags));
  return 1;
}

int LuaCalendarEventGetInvite(lua_State *L) {

  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventGetInvite(index)");
    return 0;
  }

  const auto index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1)) - 1u;
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    lua_pushnil(L);
    lua_pushinteger(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushinteger(L, 0);
    lua_pushnil(L);
    return 9;
  }
  auto invites = cal.GetEventInvites(ctx->event_id);
  if (index >= invites.size()) {
    lua_pushnil(L);
    lua_pushinteger(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    lua_pushinteger(L, 0);
    lua_pushnil(L);
    return 9;
  }
  const auto &inv = invites[index];
  const auto *session = GetWorldSession(L);
  const CalendarInvitePresentation presentation =
      session != nullptr ? ResolveCalendarInvitePresentation(*session, inv)
                          : CalendarInvitePresentation{};
  const char *class_name = presentation.class_id != 0 ? ClassName(presentation.class_id) : "";
  const char *class_file = presentation.class_id != 0 ? ClassFileName(presentation.class_id) : "";
  const bool invite_is_mine =
      session && inv.invitee_guid != 0 &&
      inv.invitee_guid == session->objects().GetActivePlayerGuid().GetRawValue();

  const char *mod_status = "";
  if (inv.rank == 1) {
    mod_status = "MODERATOR";
  } else if (inv.rank == 2) {
    mod_status = "CREATOR";
  }

  lua_pushstring(L, presentation.name.c_str());
  lua_pushinteger(L, inv.level);
  lua_pushstring(L, class_name);
  lua_pushstring(L, class_file);
  lua_pushinteger(L, inv.status + 1);
  lua_pushstring(L, mod_status);
  lua_pushboolean(L, invite_is_mine ? 1 : 0);
  lua_pushinteger(L, inv.invite_type + 1);
  lua_pushstring(L, inv.notes.c_str());
  return 9;
}

int LuaCalendarEventGetInviteResponseTime(lua_State *L) {

  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventGetInviteResponseTime(index)");
    return 0;
  }

  const auto index = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1)) - 1u;
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  const auto invites =
      ctx ? cal.GetEventInvites(ctx->event_id) : std::vector<::openwow::game::CalendarSystemInvite>{};
  const bool valid = ctx && index < invites.size();
  if (!valid) {
    PushZeroCalendarInviteResponseTime(L);
    return 6;
  }

  const auto packed_time = invites[index].response_time;
  if (packed_time == 0) {
    PushZeroCalendarInviteResponseTime(L);
    return 6;
  }

  PushCalendarInviteResponseTime(L, DecodePackedCalendarInviteResponseTime(packed_time));
  return 6;
}

int LuaCalendarEventGetInviteSortCriterion(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const std::string criterion = cal.GetSortCriterion();
  lua_pushstring(L, criterion.c_str());
  lua_pushboolean(L, cal.GetSortReverse() ? 1 : 0);
  return 2;
}

int LuaCalendarEventGetNumInvites(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    lua_pushnumber(L, 0);
    return 1;
  }
  auto invites = cal.GetEventInvites(ctx->event_id);
  lua_pushnumber(L, static_cast<lua_Number>(invites.size()));
  return 1;
}

int LuaCalendarEventGetSelectedInvite(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, cal.GetSelectedInviteIndex() + 1);
  return 1;
}

int LuaCalendarEventGetStatusOptions(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventGetStatusOptions(index)");
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx) {
    return 0;
  }

  const auto invite_index = ReadCalendarInviteIndexArg(L);
  const auto *session = GetWorldSession(L);
  const auto *active_player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  const auto active_player_guid =
      active_player != nullptr ? active_player->GetGuid().GetRawValue() : 0;
  if (!HasCalendarEventModeratorRights(*ctx, nullptr, active_player_guid)) {
    FireCalendarPermissionError(L);
    return 0;
  }

  const auto invites = GetContextInvites(cal, *ctx);
  (void)openwow::ui::ReserveLuaResultCapacity(
      L, kCalendarInviteStatusCount, 2u,
      "calendar invite status values");
  int count = 0;
  for (std::uint32_t status = 0; status < kCalendarInviteStatusCount; ++status) {
    if (!CanApplyInviteStatusChange(*ctx, invites, invite_index, status, active_player_guid)) {
      continue;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(status + 1u));
    const std::string label = ::openwow::game::Localization::Get().GetString(
        std::string(kCalendarInviteStatusGlobalKeys[status]),
        std::string(kCalendarInviteStatusGlobalKeys[status]));
    lua_pushlstring(L, label.data(), label.size());
    count += 2;
  }
  return count;
}

int LuaCalendarEventGetTextures(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventGetTextures(eventType)");
    return 0;
  }

  const auto event_type =
      static_cast<lua_Integer>(TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1)));
  const auto texture_options = GetCalendarTextureOptionsForLuaEventType(L, event_type);
  if (texture_options.empty()) {
    return 0;
  }

  (void)openwow::ui::ReserveLuaResultCapacity(
      L, texture_options.size(), 4u, "calendar texture values");
  int result_count = 0;
  for (const auto &option : texture_options) {
    lua_pushstring(L, option.display_name.c_str());
    lua_pushstring(L, option.texture_path.c_str());
    lua_pushnumber(L, static_cast<lua_Number>(option.expansion_level));
    lua_pushstring(L, option.difficulty_name.c_str());
    result_count += 4;
  }
  return result_count;
}

int LuaCalendarEventGetTypes(lua_State *L) {

  return PushCalendarLocalizedStringList(L, kCalendarEventTypeGlobalKeys);
}

int LuaCalendarEventHasPendingInvite(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  lua_pushboolean(L, ctx != nullptr && cal.HasPendingInviteForEvent(ctx->event_id) ? 1 : 0);
  return 1;
}

int LuaCalendarGetMaxCreateDate(lua_State *L) {

  const CalendarDate maximum_date = GetCalendarMaxCreateDate(GetCurrentCalendarDate(L));
  lua_pushnumber(L, maximum_date.weekday);
  lua_pushnumber(L, maximum_date.month);
  lua_pushnumber(L, maximum_date.day);
  lua_pushnumber(L, maximum_date.year);
  return 4;
}

int LuaCalendarGetMonthNames(lua_State *L) {
  static constexpr std::array<std::string_view, 12> kMonthGlobalKeys = {
      "MONTH_JANUARY",   "MONTH_FEBRUARY", "MONTH_MARCH",    "MONTH_APRIL",
      "MONTH_MAY",       "MONTH_JUNE",     "MONTH_JULY",     "MONTH_AUGUST",
      "MONTH_SEPTEMBER", "MONTH_OCTOBER",  "MONTH_NOVEMBER", "MONTH_DECEMBER",
  };

  return PushCalendarGlobalStringList(L, kMonthGlobalKeys);
}

int LuaCalendarGetWeekdayNames(lua_State *L) {
  static constexpr std::array<std::string_view, 7> kWeekdayGlobalKeys = {
      "WEEKDAY_SUNDAY",   "WEEKDAY_MONDAY", "WEEKDAY_TUESDAY",  "WEEKDAY_WEDNESDAY",
      "WEEKDAY_THURSDAY", "WEEKDAY_FRIDAY", "WEEKDAY_SATURDAY",
  };

  return PushCalendarGlobalStringList(L, kWeekdayGlobalKeys);
}

int LuaCalendarContextEventCanComplain(lua_State *L) {
  auto &calendar = ::openwow::game::CalendarSystem::Get();
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: CalendarContextEventCanComplain([-1.0.1], monthDay, index)",
          target)) {
    lua_pushboolean(L, 0);
    return 1;
  }

  lua_pushboolean(
      L, session != nullptr && CanComplainAboutCalendarContextEvent(*session, calendar, target)
             ? 1
             : 0);
  return 1;
}

int LuaCalendarContextEventCanEdit(lua_State *L) {
  auto *session = GetWorldSession(L);
  ::openwow::game::CalendarContextEventInfo target{};
  if (!ResolveCalendarContextEventOperationTarget(
          L, "Usage: CalendarContextEventCanEdit([-1.0.1], monthDay, index)",
          target)) {
    lua_pushboolean(L, 0);
    return 1;
  }

  lua_pushboolean(L, CanEditCalendarContextSelection(session, target) ? 1 : 0);
  return 1;
}

int LuaCalendarContextInviteDecline(lua_State *L) {
  SendCalendarContextInviteRsvp(
      L, "Usage: CalendarContextInviteDecline([-1.0.1], monthDay, index)", 2);
  return 0;
}

int LuaCalendarContextInviteIsPending(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {

    if (::openwow::game::CalendarSystem::Get().GetContextMenuEvent() != nullptr) {
      lua_pushboolean(L, 0);
      return 1;
    }
    luaL_error(L, "Usage: CalendarContextInviteIsPending([-1.0.1], monthDay, index)");
    return 0;
  }

  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarContextInviteIsPending([-1.0.1], monthDay, index)", month_offset,
          day, event_index)) {
    return 0;
  }

  const auto &calendar = ::openwow::game::CalendarSystem::Get();
  ::openwow::game::CalendarSystemEvent event{};
  const bool has_event =
      ResolveCalendarDayEventFromCoordinates(L, calendar, month_offset, day, event_index, event);
  lua_pushboolean(L, has_event && event.pending_invite);
  return 1;
}

int LuaCalendarContextInviteStatus(lua_State *L) {
  const auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    if (const auto *selected = calendar.GetContextMenuEvent(); selected != nullptr) {
      lua_pushnumber(L, static_cast<lua_Number>(selected->invite_status + 1u));
      return 1;
    }
    luaL_error(L, "Usage: CalendarContextInviteStatus([-1.0.1], monthDay, index)");
    return 0;
  }

  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarContextInviteStatus([-1.0.1], monthDay, index)", month_offset, day,
          event_index)) {
    return 0;
  }

  ::openwow::game::CalendarSystemEvent event{};
  if (!ResolveCalendarDayEventFromCoordinates(L, calendar, month_offset, day, event_index,
                                              event)) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(event.invite_status + 1));
  return 1;
}

int LuaCalendarContextInviteType(lua_State *L) {
  const auto &calendar = ::openwow::game::CalendarSystem::Get();
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    if (const auto *selected = calendar.GetContextMenuEvent(); selected != nullptr) {
      const bool is_signup =
          (selected->flags & kCalendarFlagGuildEvent) != 0 && selected->invite_type != 0;
      lua_pushnumber(L, is_signup ? 2.0 : 1.0);
      return 1;
    }
    luaL_error(L, "Usage: CalendarContextInviteType([-1.0.1], monthDay, index)");
    return 0;
  }

  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarContextInviteType([-1.0.1], monthDay, index)", month_offset, day,
          event_index)) {
    return 0;
  }

  ::openwow::game::CalendarSystemEvent event{};
  if (!ResolveCalendarDayEventFromCoordinates(L, calendar, month_offset, day, event_index,
                                              event)) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, CalendarInviteTypeFromEvent(event));
  return 1;
}

int LuaCalendarEventCanModerate(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: CalendarEventCanModerate(index)");
    return 0;
  }

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  const auto invite_index = ReadCalendarInviteIndexArg(L);
  const auto invites =
      ctx ? GetContextInvites(cal, *ctx) : std::vector<::openwow::game::CalendarSystemInvite>{};
  lua_pushboolean(
      L, ctx != nullptr && ValidateInviteModeratorSelection(L, *ctx, invites, invite_index) ? 1
                                                                                            : 0);
  return 1;
}

int LuaCalendarEventSetLockoutDate(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    luaL_error(L, "Usage: CalendarEventSetLockoutDate(month, day, year)");
    return 0;
  }

  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx || (ctx->flags & 0x440u) != 0) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_DATE");
    return 0;
  }

  const auto month = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  const auto year = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 3));
  if (!TryApplyCalendarPackedDateEdit(ctx->secondary_time_packed, ctx->settings_changed, month,
                                      day, year)) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_DATE");
  }
  return 0;
}

int LuaCalendarEventSetStatus(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    luaL_error(L, "Usage: CalendarEventSetStatus(index, status)");
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  if (!ctx)
    return 0;

  const auto invite_index = ReadCalendarInviteIndexArg(L);
  const auto status = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2)) - 1u;
  const auto active_player_guid = GetActiveCalendarPlayerGuid(*session);

  if ((ctx->flags & kCalendarFlagGuildAnnouncement) != 0) {
    if (!HasGuildLeaderCalendarRight(*session)) {
      return 0;
    }
  } else if (!CanEditCalendarEventBuffer(*ctx, active_player_guid)) {
    return 0;
  }

  if (!ctx->local_edit && cal.IsActionPending()) {
    return 0;
  }

  auto invites = GetContextInvites(cal, *ctx);
  if (!CanApplyInviteStatusChange(*ctx, invites, invite_index, status, active_player_guid)) {
    return 0;
  }

  if (ctx->local_edit) {
    invites[invite_index].status = static_cast<std::uint8_t>(status);
    PersistContextInvites(cal, *ctx, invites);
    FireCalendarUiEvent(L, ::openwow::ui::game::events::CALENDAR_UPDATE_INVITE_LIST);
    return 0;
  }

  const auto self_invite_id = ResolveSelfInviteId(*ctx, invites, active_player_guid);
  if (self_invite_id == 0) {
    return 0;
  }

  session->interaction().SendCalendarEventStatus(invites[invite_index].invitee_guid, ctx->event_id,
                                                 invites[invite_index].invite_id, self_invite_id,
                                                 status);
  return 0;
}

int LuaCalendarGetDayEventSequenceInfo(lua_State *L) {

  auto &cal = ::openwow::game::CalendarSystem::Get();

  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarGetDayEventSequenceInfo([-1.0.1], monthDay, index)", month_offset, day,
          event_index)) {
    return 0;
  }

  ::openwow::game::CalendarSystemEvent ev{};
  if (ResolveCalendarDayEventFromCoordinates(L, cal, month_offset, day, event_index, ev)) {
    lua_pushnumber(L, static_cast<lua_Number>(ev.sequence_index));
    lua_pushnumber(L, static_cast<lua_Number>(ev.sequence_total));
    lua_pushstring(L, SequenceTypeFromEvent(ev));
    return 3;
  }
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushstring(L, "");
  return 3;
}

int LuaCalendarGetHolidayInfo(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarGetHolidayInfo([-1,0,1], monthDay, index)", month_offset, day,
          event_index)) {
    return 0;
  }

  ::openwow::game::CalendarSystemEvent event{};
  if (ResolveCalendarDayEventFromCoordinates(L, cal, month_offset, day, event_index, event) &&
      (event.flags & 0x008u) != 0) {
    const auto display =
        ResolveCalendarHolidayDisplay(L, cal, static_cast<std::uint32_t>(event.event_id));
    if (display.name && display.description) {
      lua_pushstring(L, display.name->c_str());
      lua_pushstring(L, display.description->c_str());
      lua_pushstring(L, display.texture.c_str());
      return 3;
    }
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  return 3;
}

int LuaCalendarGetRaidInfo(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();

  int month_offset = 0;
  std::uint32_t day = 0;
  std::uint32_t event_index = 0;
  if (!ParseCalendarRelativeDayEventCoordinates(
          L, "Usage: CalendarGetRaidInfo([-1,0,1], monthDay, index)", month_offset, day,
          event_index)) {
    return 0;
  }

  ::openwow::game::CalendarSystemEvent ev{};
  if (ResolveCalendarDayEventFromCoordinates(L, cal, month_offset, day, event_index, ev)) {

    std::string name;
    if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
      if (const auto *map_entry = dbc->map().LookupEntry(ev.map_id); map_entry != nullptr) {
        name = std::string(map_entry->name);
      }
    }
    lua_pushstring(L, name.c_str());

    lua_pushstring(L, CalendarTypeFromFlags(ev.flags));

    lua_pushnumber(L, static_cast<lua_Number>(ev.event_id));

    lua_pushnumber(L, static_cast<lua_Number>(PackedTimeHour(ev.time)));
    lua_pushnumber(L, static_cast<lua_Number>(PackedTimeMinute(ev.time)));

    lua_pushnumber(L, static_cast<lua_Number>(ev.dungeon_id + 1));

    std::string difficulty_name;
    if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
      if (ev.dungeon_id >= 0) {
        if (const auto *map_difficulty = LookupCalendarMapDifficulty(
                *dbc, ev.map_id, static_cast<std::uint32_t>(ev.dungeon_id));
            map_difficulty != nullptr) {
          difficulty_name = std::string(map_difficulty->difficulty_string);
        }
      }
    }
    lua_pushstring(L, difficulty_name.c_str());
    return 7;
  }

  lua_pushnil(L);
  lua_pushstring(L, "");
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushstring(L, "");
  return 7;
}

int LuaCalendarContextGetEventIndex(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *selected = cal.GetContextMenuEvent();

  int month_offset = 0;
  int day = 0;
  int event_index = 0;
  if (selected != nullptr &&
      ResolveCalendarSelectionIndex(cal, *selected, month_offset, day, event_index)) {
    lua_pushnumber(L, month_offset);
    lua_pushnumber(L, day);
    lua_pushnumber(L, event_index);
    return 3;
  }

  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return 3;
}

int LuaCalendarGetMinHistoryDate(lua_State *L) {
  const CalendarDate minimum_history_date =
      OffsetCalendarDateByDays(GetCurrentCalendarDate(L), -14);
  lua_pushnumber(L, minimum_history_date.weekday);
  lua_pushnumber(L, minimum_history_date.month);
  lua_pushnumber(L, minimum_history_date.day);
  lua_pushnumber(L, minimum_history_date.year);
  return 4;
}

int LuaCalendarEventIsModerator([[maybe_unused]] lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();

  const bool is_moderator = ctx != nullptr && cal.GetEvent(ctx->event_id) != nullptr;
  lua_pushboolean(L, is_moderator ? 1 : 0);
  return 1;
}

int LuaCalendarEventGetRepeatOptions(lua_State *L) {
  return PushCalendarLocalizedStringList(L, kCalendarRepeatOptionGlobalKeys);
}

int LuaCalendarEventHaveSettingsChanged(lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  const auto *ctx = cal.GetContextEvent();
  bool changed = ctx && ctx->settings_changed;
  lua_pushboolean(L, changed ? 1 : 0);
  return 1;
}

int LuaCalendarCanSendInvite([[maybe_unused]] lua_State *L) {
  auto &cal = ::openwow::game::CalendarSystem::Get();
  lua_pushboolean(L, cal.CanSendInvite(false) ? 1 : 0);
  return 1;
}

int LuaCalendarGetFirstPendingInvite(lua_State *L) {
  if (lua_gettop(L) < 2) {
    luaL_error(L, "Usage: CalendarGetDay([-1,0,1], monthDay)");
    return 0;
  }
  const int month_offset = TruncateLuaNumberToCalendarI32(lua_tonumber(L, 1));
  const auto day = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));

  auto &cal = ::openwow::game::CalendarSystem::Get();
  CalendarRelativeDayLookup lookup{};
  const size_t result = ResolveCalendarRelativeDayLookup(L, cal, month_offset, day, lookup)
                            ? cal.GetFirstPendingInviteIndex(static_cast<uint32_t>(lookup.month),
                                                             static_cast<uint32_t>(lookup.day),
                                                             static_cast<uint32_t>(lookup.year))
                            : 0;
  lua_pushnumber(L, static_cast<lua_Number>(result));
  return 1;
}

int LuaCalendarEventSetLockoutTime(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    luaL_error(L, "Usage: CalendarEvenSettLockoutTime(hour, minute)");
    return 0;
  }

  auto *ctx = TryGetEditableCalendarEventBufferContext(L);
  if (!ctx || (ctx->flags & 0x440u) != 0) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_TIME");
    return 0;
  }

  const auto hour = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 1));
  const auto minute = TruncateLuaNumberToCalendarU32(lua_tonumber(L, 2));
  if (!TryApplyCalendarPackedTimeEdit(ctx->secondary_time_packed, ctx->settings_changed, hour,
                                      minute)) {
    FireCalendarError(L, "CALENDAR_ERROR_INVALID_TIME");
  }
  return 0;
}

}


#pragma once

#include <array>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <string>

#include "openwow/runtime/time/game_time.h"
#include "openwow/net/serialization/cdatastore_vtable.h"

namespace openwow::game {

struct CalendarDateFieldsEx {
  std::int32_t minute  = -1;
  std::int32_t hour    = -1;
  std::int32_t weekday = -1;
  std::int32_t day     = -1;
  std::int32_t month   = -1;
  std::int32_t year    = -1;
  std::int32_t flags   = -1;
};

struct CalendarDateFieldsFull {
  std::int32_t minute  = -1;
  std::int32_t hour    = -1;
  std::int32_t weekday = -1;
  std::int32_t day     = -1;
  std::int32_t month   = -1;
  std::int32_t year    = -1;
  std::int32_t aux0    =  0;
  std::int32_t aux1    =  0;
};
static_assert(sizeof(CalendarDateFieldsFull) == 32,
              "Must be exactly 32 bytes (8 DWORDs) per IDA 0x576AE0 rep-movsd");

inline CalendarDateFieldsFull& Calendar_ResetDateFields(
    CalendarDateFieldsFull& fields) noexcept {
  fields.minute  = -1;
  fields.hour    = -1;
  fields.weekday = -1;
  fields.day     = -1;
  fields.month   = -1;
  fields.year    = -1;
  fields.aux0    =  0;
  fields.aux1    =  0;
  return fields;
}

[[nodiscard]] inline std::int32_t CalendarDateFields_GetMinuteOfDay(
    const CalendarDateFieldsEx& fields) noexcept {
  return fields.hour >= 0 && fields.minute >= 0
      ? fields.minute + fields.hour * 60
      : 0;
}

inline void CalendarDateFields_SetMinuteOfDay(
    CalendarDateFieldsEx& fields,
    const std::int32_t minute_of_day) noexcept {
  fields.hour = minute_of_day / 60;
  fields.minute = minute_of_day % 60;
}

[[nodiscard]] inline bool CalendarDateFields_SetTime(
    CalendarDateFieldsEx& fields,
    const std::uint32_t hour,
    const std::uint32_t minute) noexcept {
  if (minute >= 60 || hour >= 24) {
    return false;
  }
  fields.hour = static_cast<std::int32_t>(hour);
  fields.minute = static_cast<std::int32_t>(minute);
  return true;
}

[[nodiscard]] inline bool CalendarDateFields_SetDate(
    CalendarDateFieldsEx& fields,
    const std::uint32_t month,
    const std::uint32_t day,
    std::uint32_t year) noexcept {
  if (month >= 12 || day >= 32) {
    return false;
  }
  if (year >= 2000) {
    year -= 2000;
  }
  if (year >= 32) {
    return false;
  }
  fields.month = static_cast<std::int32_t>(month);
  fields.day = static_cast<std::int32_t>(day);
  fields.year = static_cast<std::int32_t>(year);
  return true;
}

[[nodiscard]] inline std::int32_t CalendarDateFields_DayCount(
    const CalendarDateFieldsEx& fields) {
  if (fields.year < 0 || fields.month < 0 || fields.day < 0) {
    return 0;
  }
  std::tm value{};
  value.tm_year = fields.year % 100 + 100;
  value.tm_mon = fields.month;
  value.tm_mday = fields.day + 1;
  value.tm_isdst = -1;
  return static_cast<std::int32_t>(std::mktime(&value) / 86400);
}

[[nodiscard]] inline std::string CalendarDateFields_Format(
    const CalendarDateFieldsEx& fields) {
  const auto has_any_masked_value =
      (static_cast<std::uint32_t>(fields.minute) & 0x3Fu) != 0
      || (static_cast<std::uint32_t>(fields.hour) & 0x1Fu) != 0
      || (static_cast<std::uint32_t>(fields.weekday) & 0x7u) != 0
      || (static_cast<std::uint32_t>(fields.day) & 0x3Fu) != 0
      || (static_cast<std::uint32_t>(fields.month) & 0xFu) != 0
      || (static_cast<std::uint32_t>(fields.year) & 0x1Fu) != 0
      || (static_cast<std::uint32_t>(fields.flags) & 0x3u) != 0;
  if (!has_any_masked_value) {
    return "Not Set";
  }

  constexpr std::array<const char*, 7> kWeekdays{
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char year[20]{};
  char month[8]{};
  char day[8]{};
  char weekday[8]{};
  char hour[8]{};
  char minute[8]{};
  if (fields.year < 0) {
    std::snprintf(year, sizeof(year), "A");
  } else {
    std::snprintf(year, sizeof(year), "%i", fields.year + 2000);
  }
  if (fields.month < 0) {
    std::snprintf(month, sizeof(month), "A");
  } else {
    std::snprintf(month, sizeof(month), "%i", fields.month + 1);
  }
  if (fields.day < 0) {
    std::snprintf(day, sizeof(day), "A");
  } else {
    std::snprintf(day, sizeof(day), "%i", fields.day + 1);
  }
  if (fields.weekday < 0) {
    std::snprintf(weekday, sizeof(weekday), "Any");
  } else {
    const auto index = static_cast<std::size_t>(fields.weekday);
    std::snprintf(weekday, sizeof(weekday), "%s",
                  index < kWeekdays.size() ? kWeekdays[index] : "Any");
  }
  if (fields.hour < 0) {
    std::snprintf(hour, sizeof(hour), "A");
  } else {
    std::snprintf(hour, sizeof(hour), "%i", fields.hour);
  }
  if (fields.minute < 0) {
    std::snprintf(minute, sizeof(minute), "A");
  } else {
    std::snprintf(minute, sizeof(minute), "%2.2i", fields.minute);
  }

  char formatted[128]{};
  std::snprintf(formatted, sizeof(formatted), "%s/%s/%s (%s) %s:%s",
                month, day, year, weekday, hour, minute);
  return formatted;
}

inline void CalendarPackedTime_UnpackFields(
    const std::uint32_t packed,
    std::int32_t* out_minute,
    std::int32_t* out_hour,
    std::int32_t* out_weekday,
    std::int32_t* out_day,
    std::int32_t* out_month,
    std::int32_t* out_year,
    std::int32_t* out_flags) noexcept {
  if (out_minute) {
    const std::int32_t v = static_cast<std::int32_t>(packed & 0x3Fu);
    *out_minute = (v == 63) ? -1 : v;
  }
  if (out_hour) {
    const std::int32_t v = static_cast<std::int32_t>((packed >> 6) & 0x1Fu);
    *out_hour = (v == 31) ? -1 : v;
  }
  if (out_weekday) {
    const std::int32_t v = static_cast<std::int32_t>((packed >> 11) & 0x7u);
    *out_weekday = (v == 7) ? -1 : v;
  }
  if (out_day) {
    const std::int32_t v = static_cast<std::int32_t>((packed >> 14) & 0x3Fu);
    *out_day = (v == 63) ? -1 : v;
  }
  if (out_month) {
    const std::int32_t v = static_cast<std::int32_t>((packed >> 20) & 0xFu);
    *out_month = (v == 15) ? -1 : v;
  }
  if (out_year) {
    const std::int32_t v =
        static_cast<std::int32_t>((packed >> 24) & 0x1Fu);
    *out_year = (v == 31) ? -1 : v;
  }
  if (out_flags) {
    const std::int32_t v = static_cast<std::int32_t>((packed >> 29) & 0x3u);
    *out_flags = (v == 3) ? -1 : v;
  }
}

inline void CalendarPackedTime_UnpackToArray(
    const std::uint32_t packed,
    CalendarDateFieldsEx& out) noexcept {
  CalendarPackedTime_UnpackFields(
      packed,
      &out.minute, &out.hour, &out.weekday,
      &out.day, &out.month, &out.year, &out.flags);
}

inline void CalendarPackedTime_UnpackToDateFields(
    const std::uint32_t packed,
    ::openwow::core::ida::CalendarDateFields& out) noexcept {
  std::int32_t flags_ignored = 0;
  CalendarPackedTime_UnpackFields(
      packed,
      &out.minute, &out.hour, &out.weekday,
      &out.day, &out.month, &out.year, &flags_ignored);
}

[[nodiscard]] inline std::uint32_t CalendarDateFields_PackFromArray(
    const CalendarDateFieldsEx& fields) noexcept {
  return static_cast<std::uint32_t>(fields.minute & 0x3F)
       | (static_cast<std::uint32_t>(fields.hour & 0x1F) << 6)
       | (static_cast<std::uint32_t>(fields.weekday & 0x7) << 11)
       | (static_cast<std::uint32_t>(fields.day & 0x3F) << 14)
       | (static_cast<std::uint32_t>(fields.month & 0xF) << 20)
       | (static_cast<std::uint32_t>(fields.year & 0x1F) << 24)
       | (static_cast<std::uint32_t>(fields.flags & 0x3) << 29);
}

[[nodiscard]] inline std::uint32_t CalendarDateFields_Pack(
    const std::int32_t minute,
    const std::int32_t hour,
    const std::int32_t weekday,
    const std::int32_t day,
    const std::int32_t month,
    const std::int32_t year,
    const std::int32_t flags) noexcept {
  return static_cast<std::uint32_t>(minute & 0x3F)
       | (static_cast<std::uint32_t>(hour & 0x1F) << 6)
       | (static_cast<std::uint32_t>(weekday & 0x7) << 11)
       | (static_cast<std::uint32_t>(day & 0x3F) << 14)
       | (static_cast<std::uint32_t>(month & 0xF) << 20)
       | (static_cast<std::uint32_t>(year & 0x1F) << 24)
       | (static_cast<std::uint32_t>(flags & 0x3) << 29);
}

#pragma pack(push, 1)
struct CalendarCompactTime {
  std::uint16_t year   = 0;
  std::uint8_t  month  = 0;
  std::uint8_t  day    = 0;
  std::uint8_t  hour   = 0;
  std::uint8_t  minute = 0;
  std::uint8_t  pad    = 0;
};
#pragma pack(pop)

inline void CalendarPackedTime_UnpackToCompact(
    const std::uint32_t packed,
    CalendarCompactTime& out) noexcept {
  std::int32_t minute_raw = -1, hour_raw = -1, weekday_raw = 0;
  std::int32_t day_raw = -1, month_raw = -1, year_raw = -1;
  std::int32_t flags_raw = 0;
  CalendarPackedTime_UnpackFields(
      packed,
      &minute_raw, &hour_raw, &weekday_raw,
      &day_raw, &month_raw, &year_raw, &flags_raw);

  out.year   = static_cast<std::uint16_t>(year_raw + 2000);
  out.month  = static_cast<std::uint8_t>(month_raw + 1);
  out.day    = static_cast<std::uint8_t>(day_raw + 1);
  out.hour   = static_cast<std::uint8_t>(hour_raw);
  out.minute = static_cast<std::uint8_t>(minute_raw);
  out.pad    = 0;
}

inline void CDataStore_ReadCalendarDateFields(
    ::openwow::net::CDataStore& store,
    CalendarDateFieldsEx& out) noexcept {
  std::uint32_t packed = 0;
  ::openwow::net::CDataStore_GetUInt32(store, packed);
  CalendarPackedTime_UnpackFields(
      packed,
      &out.minute, &out.hour, &out.weekday,
      &out.day, &out.month, &out.year, &out.flags);
}

[[nodiscard]] inline int CalendarDateFields_CompareMinute(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (a.minute > b.minute) return 1;
  return (a.minute >= b.minute) ? 0 : -1;
}

[[nodiscard]] inline int CalendarDateFields_CompareHour(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (a.hour > b.hour) return 1;
  return (a.hour >= b.hour) ? 0 : -1;
}

[[nodiscard]] inline int CalendarDateFields_CompareWeekday(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (a.weekday > b.weekday) return 1;
  return (a.weekday >= b.weekday) ? 0 : -1;
}

[[nodiscard]] inline int CalendarDateFields_CompareDay(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (a.day > b.day) return 1;
  return (a.day >= b.day) ? 0 : -1;
}

[[nodiscard]] inline int CalendarDateFields_CompareMonth(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (a.month > b.month) return 1;
  return (a.month >= b.month) ? 0 : -1;
}

[[nodiscard]] inline bool CalendarDateFields_Matches(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (b.year >= 0 && a.year >= 0 && b.year != a.year) return false;
  if (b.month >= 0 && a.month >= 0 && b.month != a.month) return false;
  if (b.day >= 0 && a.day >= 0 && b.day != a.day) return false;
  if (b.weekday >= 0 && a.weekday >= 0 && b.weekday != a.weekday) return false;
  if (b.hour >= 0 && a.hour >= 0 && b.hour != a.hour) return false;
  if (b.minute < 0 || a.minute < 0 || b.minute == a.minute) return true;
  return false;
}

[[nodiscard]] inline bool CalendarDateFields_LessThan(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (&a == &b) return false;

  if (b.year >= 0 && a.year >= 0) {
    if (a.year > b.year) return false;
    if (a.year < b.year) return true;
  }

  if (b.month >= 0 && a.month >= 0) {
    int cmp = CalendarDateFields_CompareMonth(a, b);
    if (cmp != 0) return cmp < 0;
  }

  if (b.day >= 0 && a.day >= 0) {
    int cmp = CalendarDateFields_CompareDay(a, b);
    if (cmp != 0) return cmp < 0;
  }

  if (b.weekday >= 0 && a.weekday >= 0) {
    int cmp = CalendarDateFields_CompareWeekday(a, b);
    if (cmp != 0) return cmp < 0;
  }

  if (b.hour >= 0 && a.hour >= 0) {
    int cmp = CalendarDateFields_CompareHour(a, b);
    if (cmp != 0) return cmp < 0;
  }

  if (b.minute >= 0 && a.minute >= 0) {
    int cmp = CalendarDateFields_CompareMinute(a, b);
    if (cmp != 0) return cmp < 0;
  }
  return false;
}

[[nodiscard]] inline bool CalendarDateFields_GreaterThan(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  if (&a == &b) return false;
  if (b.year >= 0 && a.year >= 0) {
    if (a.year > b.year) return true;
    if (a.year < b.year) return false;
  }
  if (b.month >= 0 && a.month >= 0) {
    int cmp = CalendarDateFields_CompareMonth(a, b);
    if (cmp != 0) return cmp > 0;
  }
  if (b.day >= 0 && a.day >= 0) {
    int cmp = CalendarDateFields_CompareDay(a, b);
    if (cmp != 0) return cmp > 0;
  }
  if (b.weekday >= 0 && a.weekday >= 0) {
    int cmp = CalendarDateFields_CompareWeekday(a, b);
    if (cmp != 0) return cmp > 0;
  }
  if (b.hour >= 0 && a.hour >= 0) {
    int cmp = CalendarDateFields_CompareHour(a, b);
    if (cmp != 0) return cmp > 0;
  }
  if (b.minute >= 0 && a.minute >= 0) {
    int cmp = CalendarDateFields_CompareMinute(a, b);
    if (cmp != 0) return cmp > 0;
  }
  return false;
}

[[nodiscard]] inline bool CalendarDateFields_LessOrEqual(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  return CalendarDateFields_Matches(a, b) || CalendarDateFields_LessThan(a, b);
}

[[nodiscard]] inline bool CalendarDateFields_GreaterOrEqual(
    const CalendarDateFieldsEx& a, const CalendarDateFieldsEx& b) noexcept {
  return CalendarDateFields_Matches(a, b) ||
         CalendarDateFields_GreaterThan(a, b);
}

[[nodiscard]] inline bool CalendarDateFields_IsInRange(
    const CalendarDateFieldsEx& value,
    const CalendarDateFieldsEx& range_start,
    const CalendarDateFieldsEx& range_end) noexcept {
  if (CalendarDateFields_Matches(range_start, range_end) ||
      CalendarDateFields_LessThan(range_start, range_end)) {
    return (CalendarDateFields_Matches(value, range_start) ||
            CalendarDateFields_GreaterThan(value, range_start)) &&
           CalendarDateFields_LessThan(value, range_end);
  }
  return (CalendarDateFields_Matches(value, range_start) ||
          CalendarDateFields_GreaterThan(value, range_start)) ||
         CalendarDateFields_LessThan(value, range_end);
}

}

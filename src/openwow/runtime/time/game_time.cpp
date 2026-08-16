
#include "openwow/runtime/time/game_time.h"

#include "openwow/runtime/time/game_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

namespace openwow::core::ida {

namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000LL;
constexpr std::int64_t kSecondsPerDay = 86'400LL;
constexpr std::uint64_t kMinConvertibleFileTimeTicks = 0x0077A5D6AA8BF851ULL;
constexpr std::uint64_t kMaxConvertibleFileTimeTicks = 0x030701FFA04E87AEULL;
constexpr std::uint64_t kFileTimeTicksToGameEpochBiasNs = 0x5143382561530000ULL;
constexpr std::uint64_t kNanosecondsPerFileTimeTick = 100ULL;

struct CanonicalCalendarTimeFields {
    std::int32_t year = 0;
    std::int32_t month = 0;
    std::int32_t day = 0;
    std::int32_t hour = 0;
    std::int32_t minute = 0;
    std::int32_t second = 0;
    std::int32_t nanoseconds = 0;
};

constexpr CanonicalCalendarTimeFields TruncateCalendarTimeFields(
    const CalendarTimeFields& fields) noexcept {
    return {
        static_cast<std::int32_t>(static_cast<std::uint16_t>(fields.year)),
        static_cast<std::int32_t>(static_cast<std::uint16_t>(fields.month)),
        static_cast<std::int32_t>(static_cast<std::uint16_t>(fields.day)),
        static_cast<std::int32_t>(static_cast<std::uint16_t>(fields.hour)),
        static_cast<std::int32_t>(static_cast<std::uint16_t>(fields.minute)),
        static_cast<std::int32_t>(static_cast<std::uint16_t>(fields.second)),
        fields.nanoseconds,
    };
}

constexpr bool IsLeapYear(const std::int32_t year) noexcept {
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

constexpr std::int32_t DaysInMonth(const std::int32_t year,
                                   const std::int32_t month) noexcept {
    switch (month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
        return 31;
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;
    case 2:
        return IsLeapYear(year) ? 29 : 28;
    default:
        return 0;
    }
}

constexpr bool IsValidCalendarTime(
    const CanonicalCalendarTimeFields& fields) noexcept {
    if (fields.month < 1 || fields.month > 12) {
        return false;
    }
    if (fields.day < 1 || fields.day > DaysInMonth(fields.year, fields.month)) {
        return false;
    }
    if (fields.hour < 0 || fields.hour > 23) {
        return false;
    }
    if (fields.minute < 0 || fields.minute > 59) {
        return false;
    }
    if (fields.second < 0 || fields.second > 59) {
        return false;
    }
    return true;
}

constexpr int CompareCalendarTime(const CanonicalCalendarTimeFields& lhs,
                                  const CanonicalCalendarTimeFields& rhs) noexcept {
    if (lhs.year != rhs.year) {
        return lhs.year < rhs.year ? -1 : 1;
    }
    if (lhs.month != rhs.month) {
        return lhs.month < rhs.month ? -1 : 1;
    }
    if (lhs.day != rhs.day) {
        return lhs.day < rhs.day ? -1 : 1;
    }
    if (lhs.hour != rhs.hour) {
        return lhs.hour < rhs.hour ? -1 : 1;
    }
    if (lhs.minute != rhs.minute) {
        return lhs.minute < rhs.minute ? -1 : 1;
    }
    if (lhs.second != rhs.second) {
        return lhs.second < rhs.second ? -1 : 1;
    }
    return 0;
}

constexpr std::int64_t DaysFromCivil(std::int32_t year,
                                     std::uint32_t month,
                                     std::uint32_t day) noexcept {
    year -= month <= 2;
    const std::int32_t era = (year >= 0 ? year : year - 399) / 400;
    const std::uint32_t yoe = static_cast<std::uint32_t>(year - era * 400);
    const std::int32_t shifted_month =
        static_cast<std::int32_t>(month) + (month > 2 ? -3 : 9);
    const std::uint32_t doy =
        (153 * static_cast<std::uint32_t>(shifted_month) + 2) / 5 + day - 1;
    const std::uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097
         + static_cast<std::int64_t>(doe) - 719468;
}

constexpr std::int64_t kEpochDaysSinceCivilZero = DaysFromCivil(2000, 1, 1);

constexpr CanonicalCalendarTimeFields kMinConvertibleCalendarTime{
    1707, 9, 22, 0, 12, 44, 0,
};
constexpr CanonicalCalendarTimeFields kMaxConvertibleCalendarTime{
    2292, 4, 10, 23, 47, 16, 0,
};

constexpr std::int64_t WrapAddInt64AndInt32(std::int64_t lhs,
                                            std::int32_t rhs) noexcept {
    return static_cast<std::int64_t>(
        static_cast<std::uint64_t>(lhs)
        + static_cast<std::uint64_t>(static_cast<std::int64_t>(rhs)));
}

constexpr std::int32_t WrapAddInt32(const std::int32_t lhs,
                                    const std::int32_t rhs) noexcept {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
}

constexpr std::int32_t WrapSubInt32(const std::int32_t lhs,
                                    const std::int32_t rhs) noexcept {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
}

constexpr std::int32_t WrapMulInt32(const std::int32_t lhs,
                                    const std::int32_t rhs) noexcept {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(static_cast<std::int64_t>(lhs) * rhs));
}

constexpr std::int64_t FloorDiv(const std::int64_t value,
                                const std::int64_t divisor) noexcept {
    std::int64_t quotient = value / divisor;
    if ((value % divisor) < 0) {
        --quotient;
    }
    return quotient;
}

constexpr std::int64_t PositiveModulo(const std::int64_t value,
                                      const std::int64_t divisor) noexcept {
    const std::int64_t remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

struct CivilDate {
    std::int32_t year = 0;
    std::uint32_t month = 0;
    std::uint32_t day = 0;
};

constexpr CivilDate CivilFromDays(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::uint32_t doe = static_cast<std::uint32_t>(z - era * 146097);
    const std::uint32_t yoe =
        (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int32_t year =
        static_cast<std::int32_t>(yoe) + static_cast<std::int32_t>(era * 400);
    const std::uint32_t doy =
        doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::uint32_t mp = (5 * doy + 2) / 153;
    const std::uint32_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::uint32_t month = mp < 10 ? mp + 3 : mp - 9;
    year += month <= 2;
    return {year, month, day};
}

}

namespace {

[[nodiscard]] std::uint32_t TickNow(
    const TickCountProvider tick_count_provider) noexcept {
    return tick_count_provider
        ? tick_count_provider()
        : openwow::core::GameClock::GetTickCount32();
}

void CopyDateTuple(GameTimeData& destination,
                   const GameTimeData& source) noexcept {
    destination.minute = source.minute;
    destination.hour = source.hour;
    destination.weekday = source.weekday;
    destination.day = source.day;
    destination.month = source.month;
    destination.year = source.year;
    destination.field_24 = source.field_24;
    destination.tz_offset = source.tz_offset;
}

void SetMinuteOfDay(GameTimeData& data, const std::int32_t minute_of_day) {
    data.hour = minute_of_day / 60;
    data.minute = minute_of_day % 60;
}

void AddDays(GameTimeData& data,
             const std::int32_t day_delta,
             const bool preserve_time_of_day) {
    CalendarDateFields fields{
        .minute = data.minute,
        .hour = data.hour,
        .weekday = data.weekday,
        .day = data.day,
        .month = data.month,
        .year = data.year,
    };
    CalendarDateFields_AddDaysLocal(&fields, day_delta, preserve_time_of_day);
    data.minute = fields.minute;
    data.hour = fields.hour;
    data.weekday = fields.weekday;
    data.day = fields.day;
    data.month = fields.month;
    data.year = fields.year;
}

[[nodiscard]] std::int32_t DayCount(const GameTimeData& data) {
    if (data.year < 0 || data.month < 0 || data.day < 0) {
        return 0;
    }

    std::tm value{};
    value.tm_year = data.year % 100 + 100;
    value.tm_mon = data.month;
    value.tm_mday = data.day + 1;
    value.tm_isdst = -1;
    return static_cast<std::int32_t>(std::mktime(&value) / kSecondsPerDay);
}

[[nodiscard]] GameTimeData ApplyIncomingOffsets(
    const GameTimeData& clock,
    const GameTimeData& incoming) {
    GameTimeData adjusted = incoming;
    if (clock.time_offset_minutes != 0) {
        std::int32_t minute_of_day = WrapAddInt32(
            GameTime_GetMinuteOfDay(adjusted), clock.time_offset_minutes);
        if (minute_of_day < 0) {
            minute_of_day = WrapAddInt32(minute_of_day, 1440);
        } else {
            minute_of_day %= 1440;
        }
        SetMinuteOfDay(adjusted, minute_of_day);
    }
    if (clock.day_offset != 0) {
        AddDays(adjusted, clock.day_offset, false);
    }
    return adjusted;
}

void AdvanceOneMinute(GameTimeData& data,
                      const GameTimeMinuteObserver observer,
                      void* const observer_context,
                      const TickCountProvider tick_count_provider) {
    const std::int32_t minute_of_day =
        (GameTime_GetMinuteOfDay(data) + 1) % 1440;
    SetMinuteOfDay(data, minute_of_day);
    if (minute_of_day == 0) {
        AddDays(data, 1, false);
    }

    data.total_minutes_advanced =
        WrapAddInt32(data.total_minutes_advanced, 1);
    if (observer) {
        observer(data, observer_context);
    }
    data.start_tick_count = TickNow(tick_count_provider);
    data.is_running = 1;
    data.base_time_of_day_minutes =
        static_cast<float>(minute_of_day) + data.fractional_minute;
}

}

GameTimeData GameTimeData_FromPacked(const std::uint32_t packed_time,
                                     const std::uint32_t timezone_hint) noexcept {
    const auto decode = [packed_time](const std::uint32_t shift,
                                      const std::uint32_t mask) {
        const std::uint32_t value = (packed_time >> shift) & mask;
        return value == mask ? -1 : static_cast<std::int32_t>(value);
    };

    GameTimeData result{};
    result.minute = decode(0, 0x3Fu);
    result.hour = decode(6, 0x1Fu);
    result.weekday = decode(11, 0x7u);
    result.day = decode(14, 0x3Fu);
    result.month = decode(20, 0xFu);
    result.year = decode(24, 0x1Fu);
    result.field_24 = decode(29, 0x3u);
    result.tz_offset = static_cast<std::int32_t>(timezone_hint);
    return result;
}

std::uint32_t GameTimeData_ToPacked(const GameTimeData& data) noexcept {
    return (static_cast<std::uint32_t>(data.minute) & 0x3Fu)
        | (static_cast<std::uint32_t>(data.hour) & 0x1Fu) << 6
        | (static_cast<std::uint32_t>(data.weekday) & 0x7u) << 11
        | (static_cast<std::uint32_t>(data.day) & 0x3Fu) << 14
        | (static_cast<std::uint32_t>(data.month) & 0xFu) << 20
        | (static_cast<std::uint32_t>(data.year) & 0x1Fu) << 24
        | (static_cast<std::uint32_t>(data.field_24) & 0x3u) << 29;
}

std::int32_t GameTime_GetMinuteOfDay(const GameTimeData& data) noexcept {
    if (data.hour < 0 || data.minute < 0) {
        return 0;
    }
    return data.minute + data.hour * 60;
}

float GameTime_SetSpeed(GameTimeData& data,
                        const float requested_speed) noexcept {
    const float previous = data.time_speed;
    if (std::isnan(requested_speed)
        || requested_speed > kRetailMaximumGameSpeed) {
        data.time_speed = kRetailMaximumGameSpeed;
    } else if (requested_speed < kRetailMinimumGameSpeed) {
        data.time_speed = kRetailMinimumGameSpeed;
    } else {
        data.time_speed = requested_speed;
    }
    return previous;
}

void GameTime_Set(GameTimeData& local,
                  const GameTimeData& server_time,
                  const bool notify_current_minute,
                  const GameTimeMinuteObserver observer,
                  void* const observer_context,
                  const TickCountProvider tick_count_provider) {
    const GameTimeData adjusted = ApplyIncomingOffsets(local, server_time);
    CopyDateTuple(local, adjusted);

    if (!notify_current_minute) {
        return;
    }

    if (local.minute == 0) {
        local.minute = 59;
        if (local.hour == 0) {
            local.hour = 23;
            AddDays(local, -1, false);
        } else {
            --local.hour;
        }
    } else {
        --local.minute;
    }
    AdvanceOneMinute(local, observer, observer_context, tick_count_provider);
}

void GameTime_Sync(GameTimeData& local,
                   const GameTimeData& server_time,
                   const bool force,
                   const GameTimeMinuteObserver observer,
                   void* const observer_context,
                   const TickCountProvider tick_count_provider) {
    const GameTimeData adjusted = ApplyIncomingOffsets(local, server_time);
    const std::int32_t day_delta =
        WrapSubInt32(DayCount(adjusted), DayCount(local));
    const std::int32_t minute_delta = WrapSubInt32(
        WrapAddInt32(WrapMulInt32(day_delta, 1440),
                     GameTime_GetMinuteOfDay(adjusted)),
        GameTime_GetMinuteOfDay(local)) % 1440;

    std::int32_t differential = 0;
    if (!force && minute_delta < 1) {
        differential = -minute_delta;
    } else if (minute_delta > 0) {
        for (std::int32_t i = 0; i < minute_delta; ++i) {
            AdvanceOneMinute(local, observer, observer_context,
                             tick_count_provider);
        }
    }

    CopyDateTuple(local, adjusted);
    if (differential == 0) {
        return;
    }

    local.deferred_minutes += static_cast<std::uint32_t>(differential);
    for (std::int32_t i = 0; i < differential; ++i) {
        AdvanceOneMinute(local, observer, observer_context,
                         tick_count_provider);
    }
}

void GameTime_Sync(GameTimeData* const local,
                   const std::int32_t server_time[8],
                   const bool force) {
    if (!local || !server_time) {
        return;
    }
    GameTimeData incoming{};
    incoming.minute = server_time[0];
    incoming.hour = server_time[1];
    incoming.weekday = server_time[2];
    incoming.day = server_time[3];
    incoming.month = server_time[4];
    incoming.year = server_time[5];
    incoming.field_24 = server_time[6];
    incoming.tz_offset = server_time[7];
    GameTime_Sync(*local, incoming, force);
}

void GameTime_Advance(GameTimeData& data,
                      const float elapsed_seconds,
                      const GameTimeMinuteObserver observer,
                      void* const observer_context,
                      const TickCountProvider tick_count_provider) {
    data.fractional_minute += elapsed_seconds * data.time_speed;

    if (data.deferred_minutes != 0 && data.fractional_minute >= 1.0f) {
        const auto available = data.fractional_minute
            >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(data.fractional_minute);
        const std::uint32_t consumed =
            std::min(data.deferred_minutes, available);
        data.deferred_minutes -= consumed;
        data.fractional_minute -= static_cast<float>(consumed);
    }

    while (data.fractional_minute >= 1.0f) {
        data.fractional_minute -= 1.0f;
        AdvanceOneMinute(data, observer, observer_context,
                         tick_count_provider);
    }
}

double GameTime_GetNormalizedTimeOfDay(
    const GameTimeData* const data,
    const TickCountProvider tick_count_provider) noexcept {
    if (!data) {
        return 0.0;
    }

    std::uint32_t elapsed_ms = 0;
    if (data->is_running) {

        (void)TickNow(tick_count_provider);
        elapsed_ms = TickNow(tick_count_provider) - data->start_tick_count;
    }

    float time_of_day_minutes =
        (static_cast<float>(elapsed_ms) / 1000.0f) * data->time_speed
        + data->base_time_of_day_minutes;
    while (time_of_day_minutes > 1440.0f) {
        time_of_day_minutes -= 1440.0f;
    }
    return static_cast<double>(time_of_day_minutes / 1440.0f);
}

std::int64_t TimeNsSince2000FromFileTimeTicks(
    const std::uint64_t filetime_ticks) noexcept {
    if (filetime_ticks <= kMinConvertibleFileTimeTicks) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (filetime_ticks > kMaxConvertibleFileTimeTicks) {
        return std::numeric_limits<std::int64_t>::max();
    }

    return static_cast<std::int64_t>(
        filetime_ticks * kNanosecondsPerFileTimeTick
        + kFileTimeTicksToGameEpochBiasNs);
}

CalendarTimeBreakdown CalendarTimeBreakdownFromNsSince2000(
    const std::int64_t time_ns_since_2000) noexcept {
    CalendarTimeBreakdown breakdown{};
    breakdown.nanoseconds = static_cast<std::int32_t>(
        PositiveModulo(time_ns_since_2000, kNanosecondsPerSecond));

    const std::int64_t whole_seconds =
        FloorDiv(time_ns_since_2000, kNanosecondsPerSecond);
    const std::int64_t day_delta =
        FloorDiv(whole_seconds, kSecondsPerDay);
    const std::int32_t seconds_of_day = static_cast<std::int32_t>(
        PositiveModulo(whole_seconds, kSecondsPerDay));
    const CivilDate civil = CivilFromDays(kEpochDaysSinceCivilZero + day_delta);

    breakdown.year = civil.year;
    breakdown.month = static_cast<std::int32_t>(civil.month);
    breakdown.day = static_cast<std::int32_t>(civil.day);
    breakdown.hour = seconds_of_day / 3600;
    breakdown.minute = (seconds_of_day / 60) % 60;
    breakdown.second = seconds_of_day % 60;
    breakdown.day_of_week = static_cast<std::int32_t>(
        PositiveModulo(day_delta + 6, 7));
    breakdown.day_of_year = static_cast<std::int32_t>(
        DaysFromCivil(breakdown.year,
                      static_cast<std::uint32_t>(breakdown.month),
                      static_cast<std::uint32_t>(breakdown.day))
        - DaysFromCivil(breakdown.year, 1, 1));
    return breakdown;
}

std::int64_t CalendarTimeNsSince2000FromFields(
    const CalendarTimeFields& input_fields) {
    const CanonicalCalendarTimeFields fields =
        TruncateCalendarTimeFields(input_fields);
    if (!IsValidCalendarTime(fields)) {
        return std::numeric_limits<std::int64_t>::min();
    }

    if (CompareCalendarTime(fields, kMinConvertibleCalendarTime) < 0) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (CompareCalendarTime(fields, kMaxConvertibleCalendarTime) > 0) {
        return WrapAddInt64AndInt32(std::numeric_limits<std::int64_t>::max(),
                                    fields.nanoseconds);
    }

    const std::int64_t day_delta =
        DaysFromCivil(fields.year,
                      static_cast<std::uint32_t>(fields.month),
                      static_cast<std::uint32_t>(fields.day))
        - kEpochDaysSinceCivilZero;
    const std::int64_t whole_seconds =
        day_delta * kSecondsPerDay
        + static_cast<std::int64_t>(fields.hour) * 3600
        + static_cast<std::int64_t>(fields.minute) * 60
        + static_cast<std::int64_t>(fields.second);
    const std::int64_t base_nanoseconds =
        whole_seconds * kNanosecondsPerSecond;
    return WrapAddInt64AndInt32(base_nanoseconds, fields.nanoseconds);
}

bool Calendar_LocalTime32ToTmOrZero(
    const std::int32_t time32_seconds,
    std::tm& out_tm) noexcept {
    const std::time_t seconds = static_cast<std::time_t>(time32_seconds);
#if defined(_WIN32)
    if (localtime_s(&out_tm, &seconds) == 0) {
        return true;
    }
#else
    if (localtime_r(&seconds, &out_tm) != nullptr) {
        return true;
    }
#endif

    out_tm = {};
    return false;
}

void CalendarDateFields_AddDaysLocal(
    CalendarDateFields* fields,
    const std::int32_t day_delta,
    const bool preserve_time_of_day) {
    if (!fields || fields->year < 0 || fields->month < 0 || fields->day < 0) {
        return;
    }

    std::tm tm_value{};
    tm_value.tm_year = fields->year + 100;
    tm_value.tm_mon = fields->month;
    tm_value.tm_mday = fields->day + 1;
    tm_value.tm_isdst = -1;
    if (preserve_time_of_day) {
        tm_value.tm_hour = fields->hour;
        tm_value.tm_min = fields->minute;
    }

    const auto base_seconds = static_cast<std::int32_t>(std::mktime(&tm_value));
    auto localtime_input = WrapAddInt32(base_seconds, WrapMulInt32(day_delta, 24 * 60 * 60));
    if (!preserve_time_of_day) {
        localtime_input = WrapAddInt32(localtime_input, 60 * 60);
    }

    if (!Calendar_LocalTime32ToTmOrZero(localtime_input, tm_value)) {
        tm_value = {};
    }
    fields->weekday = tm_value.tm_wday;
    fields->day = tm_value.tm_mday - 1;
    fields->month = tm_value.tm_mon;
    fields->year = tm_value.tm_year - 100;
    if (preserve_time_of_day) {
        fields->minute = tm_value.tm_min;
        fields->hour = tm_value.tm_hour;
    }
}

void CalendarDateFields_AddMinutes(
    CalendarDateFields* fields,
    const std::int32_t minute_delta) {
    int32_t total;
    if (fields->hour >= 0 && fields->minute >= 0) {
        total = fields->minute + 60 * fields->hour;
    } else {
        total = 0;
    }

    total += minute_delta;
    const int32_t total_copy = total;

    if (total_copy / 1440 != 0) {
        total += -1440 * (total_copy / 1440);
        CalendarDateFields_AddDaysLocal(fields, total_copy / 1440, false);
    }

    if (total < 0) {
        CalendarDateFields_AddDaysLocal(fields, -1, false);
        total += 1440;
    }

    fields->minute = total % 60;
    fields->hour = total / 60;
}

void CalendarDateFields_AddMinutesLocal(
    CalendarDateFields* fields,
    const std::int32_t minute_delta) {
    const int32_t saved_minute = fields->minute;
    const int32_t saved_hour = fields->hour;

    const int32_t full_days = minute_delta / 1440;
    if (full_days > 0) {
        CalendarDateFields_AddDaysLocal(fields, full_days, true);
    }

    const int32_t remaining = minute_delta - full_days * 1440;
    CalendarDateFields_AddMinutes(fields, remaining);

    int32_t expected = (saved_minute + remaining + 60 * saved_hour) % 1440;

    int32_t actual;
    if (fields->hour < 0 || fields->minute < 0) {
        actual = 0;
    } else {
        actual = fields->minute + 60 * fields->hour;
    }

    if (expected == actual) {
        return;
    }

    if (fields->hour < 0 || fields->minute < 0) {
        actual = 0;
    } else {
        actual = fields->minute + 60 * fields->hour;
    }

    if ((actual - expected + 1440) % 1440 == 60) {

        if (fields->hour < 0 || fields->minute < 0 ||
            (fields->minute + 60 * fields->hour) < 60) {
            CalendarDateFields_AddDaysLocal(fields, -1, false);
        }

        fields->minute = expected % 60;
        fields->hour = expected / 60;
    } else {

        CalendarDateFields_AddMinutes(fields, 60);
    }
}

static int32_t s_cached_tz_delta = 0;

void Calendar_ConvertToLocalTime(const CalendarTimeContext* ctx, int32_t date[6]) {
    if (!ctx || ctx->tz_offset == 0) return;

    if (s_cached_tz_delta == 0) {
        std::time_t now = std::time(nullptr);
        std::tm* gm = std::gmtime(&now);
        std::time_t utc_epoch = std::mktime(gm);
        s_cached_tz_delta = static_cast<int32_t>(now + ctx->tz_offset * 3600 - utc_epoch);
        if (s_cached_tz_delta == 0) return;
    }

    std::tm tm_val = {};
    tm_val.tm_sec  = 0;
    tm_val.tm_min  = date[0];
    tm_val.tm_hour = date[1];
    tm_val.tm_mday = date[3] + 1;
    tm_val.tm_mon  = date[4];
    tm_val.tm_year = date[5] + 100;
    tm_val.tm_isdst = -1;

    const auto event_time = WrapAddInt32(
        static_cast<std::int32_t>(std::mktime(&tm_val)),
        s_cached_tz_delta);

    std::tm local_tm{};
    if (Calendar_LocalTime32ToTmOrZero(event_time, local_tm)) {
        date[0] = local_tm.tm_min;
        date[1] = local_tm.tm_hour;
        date[2] = -1;
        date[3] = local_tm.tm_mday - 1;
        date[4] = local_tm.tm_mon;
        date[5] = local_tm.tm_year - 100;
    } else {
        date[0] = 0;
        date[1] = 0;
        date[2] = -1;
        date[3] = -1;
        date[4] = 0;
        date[5] = -100;
    }
}

}

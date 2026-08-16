
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <type_traits>

namespace openwow::core::ida {

inline constexpr float kRetailMinimumGameSpeed = 1.0f / 60.0f;
inline constexpr float kRetailMaximumGameSpeed = 60.0f;

struct GameTimeData {

    int32_t minute    = -1;
    int32_t hour      = -1;
    int32_t weekday   = -1;
    int32_t day       = -1;
    int32_t month     = -1;
    int32_t year      = -1;
    int32_t field_24  = 0;
    int32_t tz_offset = 0;

    int32_t field_32  = 0;
    int32_t time_offset_minutes = 0;
    int32_t day_offset = 0;
    int32_t total_minutes_advanced = 0;
    float   time_speed = kRetailMinimumGameSpeed;
    float   fractional_minute = 0.0f;
    uint32_t deferred_minutes = 0;

    uint32_t start_tick_count = 0;
    uint8_t  is_running = 0;
    uint8_t  padding_65[3] = {};
    float    base_time_of_day_minutes = 0.0f;
    int32_t  field_72 = 0;
    int32_t  field_76 = 0;
};

static_assert(std::is_standard_layout_v<GameTimeData>);
static_assert(offsetof(GameTimeData, time_speed) == 0x30);
static_assert(offsetof(GameTimeData, deferred_minutes) == 0x38);
static_assert(offsetof(GameTimeData, start_tick_count) == 0x3C);
static_assert(offsetof(GameTimeData, is_running) == 0x40);
static_assert(offsetof(GameTimeData, base_time_of_day_minutes) == 0x44);
static_assert(sizeof(GameTimeData) == 0x50);

using TickCountProvider = uint32_t(*)();
using GameTimeMinuteObserver = void (*)(const GameTimeData& current_time,
                                        void* context);

[[nodiscard]] GameTimeData GameTimeData_FromPacked(
    uint32_t packed_time,
    uint32_t timezone_hint = 0) noexcept;

[[nodiscard]] uint32_t GameTimeData_ToPacked(
    const GameTimeData& data) noexcept;

[[nodiscard]] int32_t GameTime_GetMinuteOfDay(
    const GameTimeData& data) noexcept;

[[nodiscard]] float GameTime_SetSpeed(GameTimeData& data,
                                      float requested_speed) noexcept;

void GameTime_Set(GameTimeData& local,
                  const GameTimeData& server_time,
                  bool notify_current_minute,
                  GameTimeMinuteObserver observer = nullptr,
                  void* observer_context = nullptr,
                  TickCountProvider tick_count_provider = nullptr);

void GameTime_Sync(GameTimeData& local,
                   const GameTimeData& server_time,
                   bool force,
                   GameTimeMinuteObserver observer = nullptr,
                   void* observer_context = nullptr,
                   TickCountProvider tick_count_provider = nullptr);

void GameTime_Sync(GameTimeData* local,
                   const int32_t server_time[8],
                   bool force);

void GameTime_Advance(GameTimeData& data,
                      float elapsed_seconds,
                      GameTimeMinuteObserver observer = nullptr,
                      void* observer_context = nullptr,
                      TickCountProvider tick_count_provider = nullptr);

[[nodiscard]] double GameTime_GetNormalizedTimeOfDay(
    const GameTimeData* data,
    TickCountProvider tick_count_provider = nullptr) noexcept;

[[nodiscard]] int64_t TimeNsSince2000FromFileTimeTicks(
    std::uint64_t filetime_ticks) noexcept;

struct CalendarTimeBreakdown {
    int32_t year = 0;
    int32_t month = 0;
    int32_t day = 0;
    int32_t hour = 0;
    int32_t minute = 0;
    int32_t second = 0;
    int32_t nanoseconds = 0;
    int32_t day_of_week = 0;
    int32_t day_of_year = 0;
};

[[nodiscard]] CalendarTimeBreakdown CalendarTimeBreakdownFromNsSince2000(
    int64_t time_ns_since_2000) noexcept;

struct CalendarTimeFields {
    int32_t year        = 0;
    int32_t month       = 0;
    int32_t day         = 0;
    int32_t hour        = 0;
    int32_t minute      = 0;
    int32_t second      = 0;
    int32_t nanoseconds = 0;
};

[[nodiscard]] int64_t CalendarTimeNsSince2000FromFields(
    const CalendarTimeFields& fields);

struct CalendarDateFields {
    int32_t minute = -1;
    int32_t hour = -1;
    int32_t weekday = -1;
    int32_t day = -1;
    int32_t month = -1;
    int32_t year = -1;
};

[[nodiscard]] bool Calendar_LocalTime32ToTmOrZero(
    int32_t time32_seconds,
    std::tm& out_tm) noexcept;

void CalendarDateFields_AddDaysLocal(
    CalendarDateFields* fields,
    int32_t day_delta,
    bool preserve_time_of_day);

void CalendarDateFields_AddMinutes(
    CalendarDateFields* fields,
    int32_t minute_delta);

void CalendarDateFields_AddMinutesLocal(
    CalendarDateFields* fields,
    int32_t minute_delta);

struct CalendarTimeContext {
    int32_t tz_offset = 0;
};

void Calendar_ConvertToLocalTime(const CalendarTimeContext* ctx, int32_t date[6]);

}

#pragma once

#include "openwow/game/game_time_callback_registry.h"
#include "openwow/game/calendar/calendar_system.h"

#include <cstdint>
#include <unordered_map>

namespace openwow::data::dbc { class DbcLoader; }

namespace openwow::game {

class WorldSession;

CalendarSystemEvent BuildCalendarRaidLockoutEvent(
    const data::dbc::DbcLoader* dbc, std::uint32_t map_id,
    std::uint32_t difficulty, std::uint64_t instance_id,
    std::uint32_t reset_packed_time);
CalendarRaidInfo BuildCalendarRaidLockoutInfo(
    const data::dbc::DbcLoader* dbc, std::uint32_t map_id,
    std::uint32_t difficulty, std::uint64_t instance_id,
    std::uint32_t reset_packed_time);

class CalendarRuntime final {
 public:
  struct AlarmRegistration {
    WorldSession* owner{nullptr};
    std::uint64_t event_id{0};
    GameTimeCallbackMoment reminder_time{};
    GameTimeCallbackRegistry::Handle handle{
        GameTimeCallbackRegistry::kInvalidHandle};
  };

  using AlarmMap = std::unordered_map<std::uint64_t, AlarmRegistration>;

  [[nodiscard]] AlarmMap& alarms() noexcept { return alarms_; }
  [[nodiscard]] const AlarmMap& alarms() const noexcept { return alarms_; }
  void Reset() noexcept { alarms_.clear(); }

 private:
  AlarmMap alarms_;
};

}

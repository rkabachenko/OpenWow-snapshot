#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

class GameTimeSystem {
 public:

  void SetServerTime(std::uint32_t hour, std::uint32_t minute);
  [[nodiscard]] std::uint32_t GetHour() const;
  [[nodiscard]] std::uint32_t GetMinute() const;

  [[nodiscard]] float GetTimeOfDay() const;

  [[nodiscard]] bool IsDay() const;

  [[nodiscard]] bool IsNight() const;

  [[nodiscard]] bool IsDawn() const;

  [[nodiscard]] bool IsDusk() const;

  [[nodiscard]] float GetDayNightFactor() const;

  void SetGameSpeed(float multiplier);
  [[nodiscard]] float GetGameSpeed() const;

  [[nodiscard]] std::string GetFormattedTime() const;

  [[nodiscard]] std::string GetMilitaryTime() const;

  void SetServerTimestamp(std::uint64_t unix_time);
  [[nodiscard]] std::uint64_t GetServerTimestamp() const;
  [[nodiscard]] std::uint64_t GetLocalTimestamp() const;

  void Update(float dt);
  void Reset();

 private:
  std::uint32_t hour_{0};
  std::uint32_t minute_{0};
  float fractional_seconds_{0.0f};
  float game_speed_{1.0f};
  std::uint64_t server_timestamp_{0};
};

}

#include "openwow/game/game_time.h"
#include "openwow/game/time_of_day_windows.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace openwow::game {

void GameTimeSystem::SetServerTime(std::uint32_t hour, std::uint32_t minute) {
  hour_ = hour % 24;
  minute_ = minute % 60;
  fractional_seconds_ = 0.0f;
}

std::uint32_t GameTimeSystem::GetHour() const { return hour_; }
std::uint32_t GameTimeSystem::GetMinute() const { return minute_; }

float GameTimeSystem::GetTimeOfDay() const {
  return static_cast<float>(hour_) +
         static_cast<float>(minute_) / 60.0f +
         fractional_seconds_ / 3600.0f;
}

bool GameTimeSystem::IsDay() const {
  return IsObservedDaytime(GetTimeOfDay());
}

bool GameTimeSystem::IsNight() const { return IsObservedNighttime(GetTimeOfDay()); }

bool GameTimeSystem::IsDawn() const {
  float t = GetTimeOfDay();
  return t >= 5.0f && t < 7.0f;
}

bool GameTimeSystem::IsDusk() const {
  float t = GetTimeOfDay();
  return t >= 19.0f && t < 21.0f;
}

float GameTimeSystem::GetDayNightFactor() const {

  constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
  float t = GetTimeOfDay();
  return 0.5f * (1.0f + std::sin((t / 24.0f - 0.25f) * kTwoPi));
}

void GameTimeSystem::SetGameSpeed(float multiplier) {
  game_speed_ = multiplier > 0.0f ? multiplier : 1.0f;
}

float GameTimeSystem::GetGameSpeed() const { return game_speed_; }

std::string GameTimeSystem::GetFormattedTime() const {
  std::uint32_t h12 = hour_ % 12;
  if (h12 == 0) h12 = 12;
  const char* ampm = (hour_ < 12) ? "AM" : "PM";

  std::ostringstream oss;
  oss << h12 << ":" << std::setfill('0') << std::setw(2) << minute_ << " " << ampm;
  return oss.str();
}

std::string GameTimeSystem::GetMilitaryTime() const {
  std::ostringstream oss;
  oss << std::setfill('0') << std::setw(2) << hour_ << ":"
      << std::setfill('0') << std::setw(2) << minute_;
  return oss.str();
}

void GameTimeSystem::SetServerTimestamp(std::uint64_t unix_time) {
  server_timestamp_ = unix_time;
}

std::uint64_t GameTimeSystem::GetServerTimestamp() const {
  return server_timestamp_;
}

std::uint64_t GameTimeSystem::GetLocalTimestamp() const {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

void GameTimeSystem::Update(float dt) {

  float game_seconds = dt * game_speed_;
  fractional_seconds_ += game_seconds;

  while (fractional_seconds_ >= 60.0f) {
    fractional_seconds_ -= 60.0f;
    ++minute_;
    if (minute_ >= 60) {
      minute_ = 0;
      ++hour_;
      if (hour_ >= 24) {
        hour_ = 0;
      }
    }
  }
}

void GameTimeSystem::Reset() {
  hour_ = 0;
  minute_ = 0;
  fractional_seconds_ = 0.0f;
  game_speed_ = 1.0f;
  server_timestamp_ = 0;
}

}

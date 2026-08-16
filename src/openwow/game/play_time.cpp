#include "openwow/game/play_time.h"

#include <sstream>

namespace openwow::game {

void PlayTimeTracker::SetTotalTime(std::uint32_t seconds) {
  total_time_ = seconds;
}

std::uint32_t PlayTimeTracker::GetTotalTime() const { return total_time_; }

void PlayTimeTracker::SetLevelTime(std::uint32_t seconds) {
  level_time_ = seconds;
}

std::uint32_t PlayTimeTracker::GetLevelTime() const { return level_time_; }

float PlayTimeTracker::GetSessionTime() const { return session_time_; }

void PlayTimeTracker::Update(float dt) {
  session_time_ += dt;

  total_time_ += static_cast<std::uint32_t>(dt);
  level_time_ += static_cast<std::uint32_t>(dt);
}

std::string PlayTimeTracker::FormatTime(std::uint32_t seconds) {
  std::uint32_t d = GetDays(seconds);
  std::uint32_t h = GetHours(seconds);
  std::uint32_t m = GetMinutes(seconds);

  std::ostringstream oss;
  bool need_comma = false;

  if (d > 0) {
    oss << d << (d == 1 ? " day" : " days");
    need_comma = true;
  }
  if (h > 0 || d > 0) {
    if (need_comma) oss << ", ";
    oss << h << (h == 1 ? " hour" : " hours");
    need_comma = true;
  }
  if (need_comma) oss << ", ";
  oss << m << (m == 1 ? " minute" : " minutes");

  return oss.str();
}

std::string PlayTimeTracker::GetTotalTimeFormatted() const {
  return FormatTime(total_time_);
}

std::string PlayTimeTracker::GetLevelTimeFormatted() const {
  return FormatTime(level_time_);
}

std::string PlayTimeTracker::GetSessionTimeFormatted() const {
  return FormatTime(static_cast<std::uint32_t>(session_time_));
}

std::uint32_t PlayTimeTracker::GetDays(std::uint32_t seconds) {
  return seconds / 86400;
}

std::uint32_t PlayTimeTracker::GetHours(std::uint32_t seconds) {
  return (seconds % 86400) / 3600;
}

std::uint32_t PlayTimeTracker::GetMinutes(std::uint32_t seconds) {
  return (seconds % 3600) / 60;
}

void PlayTimeTracker::ResetLevelTime() { level_time_ = 0; }

float PlayTimeTracker::GetAverageTimePerLevel() const {
  if (level_ <= 1) return static_cast<float>(total_time_);
  return static_cast<float>(total_time_) / static_cast<float>(level_ - 1);
}

void PlayTimeTracker::SetLevel(std::uint32_t level) {
  level_ = (level > 0) ? level : 1;
}

void PlayTimeTracker::Reset() {
  total_time_ = 0;
  level_time_ = 0;
  session_time_ = 0.0f;
  level_ = 1;
}

}

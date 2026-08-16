#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

class PlayTimeTracker {
 public:

  void SetTotalTime(std::uint32_t seconds);
  [[nodiscard]] std::uint32_t GetTotalTime() const;

  void SetLevelTime(std::uint32_t seconds);
  [[nodiscard]] std::uint32_t GetLevelTime() const;

  [[nodiscard]] float GetSessionTime() const;

  void Update(float dt);

  [[nodiscard]] static std::string FormatTime(std::uint32_t seconds);

  [[nodiscard]] std::string GetTotalTimeFormatted() const;
  [[nodiscard]] std::string GetLevelTimeFormatted() const;
  [[nodiscard]] std::string GetSessionTimeFormatted() const;

  [[nodiscard]] static std::uint32_t GetDays(std::uint32_t seconds);
  [[nodiscard]] static std::uint32_t GetHours(std::uint32_t seconds);
  [[nodiscard]] static std::uint32_t GetMinutes(std::uint32_t seconds);

  void ResetLevelTime();

  [[nodiscard]] float GetAverageTimePerLevel() const;

  void SetLevel(std::uint32_t level);

  void Reset();

 private:
  std::uint32_t total_time_{0};
  std::uint32_t level_time_{0};
  float session_time_{0.0f};
  std::uint32_t level_{1};
};

}

#pragma once

#include <chrono>
#include <cstdint>

namespace openwow::game {

struct FrameStats {
  float update_ms  = 0.0f;
  float render_ms  = 0.0f;
  float network_ms = 0.0f;
  float ui_ms      = 0.0f;
  float total_ms   = 0.0f;
};

class FrameTimer {
 public:
  static FrameTimer& Get();

  void Tick();

  [[nodiscard]] float GetDeltaTime() const { return delta_time_; }

  [[nodiscard]] float GetElapsedTime() const { return elapsed_time_; }

  [[nodiscard]] std::uint64_t GetFrameCount() const { return frame_count_; }

  [[nodiscard]] float GetFPS() const { return current_fps_; }

  [[nodiscard]] float GetAverageFPS() const { return average_fps_; }

  void SetTargetFPS(std::uint32_t fps);
  [[nodiscard]] std::uint32_t GetTargetFPS() const { return target_fps_; }

  void SetServerTime(std::uint32_t packed);

  [[nodiscard]] std::uint32_t GetServerTime() const { return server_time_; }

  [[nodiscard]] std::uint32_t GetGameMinuteOfDay() const;

  [[nodiscard]] std::uint32_t GetGameHour() const;

  [[nodiscard]] std::uint32_t GetGameMinute() const;

  void SetFrameStats(const FrameStats& stats);
  [[nodiscard]] const FrameStats& GetFrameStats() const { return frame_stats_; }

  void SetPaused(bool paused);
  [[nodiscard]] bool IsPaused() const { return paused_; }

  void Reset();

 private:
  FrameTimer();

  std::chrono::steady_clock::time_point last_tick_{};
  bool first_tick_ = true;
  float delta_time_  = 0.0f;
  float elapsed_time_ = 0.0f;
  std::uint64_t frame_count_ = 0;

  float fps_accumulator_ = 0.0f;
  std::uint32_t fps_frame_count_ = 0;
  float current_fps_ = 0.0f;
  float average_fps_ = 0.0f;

  std::uint32_t target_fps_ = 60;

  std::uint32_t server_time_  = 0;
  float         server_time_frac_ = 0.0f;

  FrameStats frame_stats_;

  bool paused_ = false;
};

}

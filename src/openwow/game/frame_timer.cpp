#include "openwow/game/frame_timer.h"

#include <algorithm>
#include <thread>

namespace openwow::game {

FrameTimer& FrameTimer::Get() {
  static FrameTimer instance;
  return instance;
}

FrameTimer::FrameTimer() { Reset(); }

void FrameTimer::Tick() {
  const auto now = std::chrono::steady_clock::now();

  if (first_tick_) {
    last_tick_ = now;
    first_tick_ = false;
    delta_time_ = 0.0f;
    return;
  }

  const auto raw_dt = std::chrono::duration<float>(now - last_tick_).count();
  last_tick_ = now;

  delta_time_ = std::clamp(raw_dt, 0.0f, 0.25f);

  if (paused_) {
    delta_time_ = 0.0f;
    return;
  }

  elapsed_time_ += delta_time_;
  ++frame_count_;

  fps_accumulator_ += delta_time_;
  ++fps_frame_count_;
  if (fps_accumulator_ >= 1.0f) {
    current_fps_ = static_cast<float>(fps_frame_count_) / fps_accumulator_;

    if (average_fps_ <= 0.0f) {
      average_fps_ = current_fps_;
    } else {
      average_fps_ = average_fps_ * 0.9f + current_fps_ * 0.1f;
    }
    fps_accumulator_ = 0.0f;
    fps_frame_count_ = 0;
  }

  server_time_frac_ += delta_time_;
  while (server_time_frac_ >= 60.0f) {
    server_time_frac_ -= 60.0f;

    std::uint32_t minute = server_time_ & 0x3F;
    std::uint32_t hour   = (server_time_ >> 6) & 0x1F;
    ++minute;
    if (minute >= 60) {
      minute = 0;
      ++hour;
      if (hour >= 24) {
        hour = 0;

      }
    }
    server_time_ = (server_time_ & ~0x7FFu) | (hour << 6) | minute;
  }

  if (target_fps_ > 0) {
    const float target_frame_time = 1.0f / static_cast<float>(target_fps_);
    const float sleep_time = target_frame_time - raw_dt;
    if (sleep_time > 0.001f) {
      std::this_thread::sleep_for(
          std::chrono::microseconds(static_cast<long long>(sleep_time * 1e6)));
    }
  }
}

void FrameTimer::SetTargetFPS(std::uint32_t fps) {
  target_fps_ = fps;
}

void FrameTimer::SetServerTime(std::uint32_t packed) {
  server_time_ = packed;
  server_time_frac_ = 0.0f;
}

std::uint32_t FrameTimer::GetGameMinuteOfDay() const {
  const std::uint32_t minute = server_time_ & 0x3F;
  const std::uint32_t hour   = (server_time_ >> 6) & 0x1F;
  return hour * 60 + minute;
}

std::uint32_t FrameTimer::GetGameHour() const {
  return (server_time_ >> 6) & 0x1F;
}

std::uint32_t FrameTimer::GetGameMinute() const {
  return server_time_ & 0x3F;
}

void FrameTimer::SetFrameStats(const FrameStats& stats) {
  frame_stats_ = stats;
}

void FrameTimer::SetPaused(bool paused) {
  if (paused && !paused_) {

  } else if (!paused && paused_) {

    last_tick_ = std::chrono::steady_clock::now();
  }
  paused_ = paused;
}

void FrameTimer::Reset() {
  first_tick_ = true;
  delta_time_  = 0.0f;
  elapsed_time_ = 0.0f;
  frame_count_ = 0;
  fps_accumulator_ = 0.0f;
  fps_frame_count_ = 0;
  current_fps_ = 0.0f;
  average_fps_ = 0.0f;
  target_fps_ = 60;
  server_time_ = 0;
  server_time_frac_ = 0.0f;
  frame_stats_ = {};
  paused_ = false;
}

}

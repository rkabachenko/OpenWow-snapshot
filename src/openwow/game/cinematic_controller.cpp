
#include "openwow/game/cinematic_controller.h"

#include <algorithm>

namespace openwow::game {

void CinematicController::Play(std::uint32_t cinematicId,
                               const std::string& name, float duration,
                               bool skippable) {
  cinematicId_ = cinematicId;
  name_ = name;
  duration_ = duration;
  currentTime_ = 0.0f;
  skippable_ = skippable;
  state_ = CinematicDisplayState::Playing;
}

void CinematicController::Stop() {
  state_ = CinematicDisplayState::Idle;
  currentTime_ = 0.0f;
  cinematicId_ = 0;
  name_.clear();
  duration_ = 0.0f;
  skippable_ = true;
}

void CinematicController::Pause() {
  if (state_ == CinematicDisplayState::Playing) {
    state_ = CinematicDisplayState::Paused;
  }
}

void CinematicController::Resume() {
  if (state_ == CinematicDisplayState::Paused) {
    state_ = CinematicDisplayState::Playing;
  }
}

bool CinematicController::Skip() {
  if (state_ == CinematicDisplayState::Idle ||
      state_ == CinematicDisplayState::Finished) {
    return false;
  }
  if (!skippable_) return false;
  currentTime_ = duration_;
  state_ = CinematicDisplayState::Finished;
  return true;
}

CinematicDisplayState CinematicController::GetState() const { return state_; }

float CinematicController::GetCurrentTime() const { return currentTime_; }

float CinematicController::GetDuration() const { return duration_; }

float CinematicController::GetProgress() const {
  if (duration_ <= 0.0f) return 0.0f;
  return std::clamp(currentTime_ / duration_, 0.0f, 1.0f);
}

std::string CinematicController::GetCinematicName() const { return name_; }

bool CinematicController::IsPlaying() const {
  return state_ == CinematicDisplayState::Playing;
}

bool CinematicController::IsSkippable() const { return skippable_; }

std::optional<CinematicInfo> CinematicController::GetInfo() const {
  if (state_ == CinematicDisplayState::Idle) return std::nullopt;
  CinematicInfo info;
  info.cinematicId = cinematicId_;
  info.name = name_;
  info.duration = duration_;
  info.currentTime = currentTime_;
  info.state = state_;
  info.isSkippable = skippable_;
  return info;
}

void CinematicController::Update(float dt) {
  if (state_ != CinematicDisplayState::Playing) return;
  currentTime_ += dt;
  if (currentTime_ >= duration_) {
    currentTime_ = duration_;
    state_ = CinematicDisplayState::Finished;
  }
}

void CinematicController::Reset() {
  state_ = CinematicDisplayState::Idle;
  cinematicId_ = 0;
  name_.clear();
  duration_ = 0.0f;
  currentTime_ = 0.0f;
  skippable_ = true;
}

}

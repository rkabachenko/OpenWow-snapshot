
#include "openwow/game/target_cast_bar.h"

#include <algorithm>
#include <cstdio>

namespace openwow::game {

const std::string TargetCastBar::kEmptyString;

void TargetCastBar::SetCast(const TargetCastBarInfo& info) {
  info_    = info;
  elapsed_ = 0.0f;
}

void TargetCastBar::ClearCast() {
  info_.reset();
  elapsed_ = 0.0f;
}

std::optional<TargetCastBarInfo> TargetCastBar::GetInfo() const {
  return info_;
}

bool TargetCastBar::IsActive() const {
  return info_.has_value();
}

float TargetCastBar::GetProgress(double currentTime) const {
  if (!info_ || info_->castTime <= 0.0f) return 0.0f;

  float elapsed = static_cast<float>(currentTime) - info_->startTime;
  elapsed = std::clamp(elapsed, 0.0f, info_->castTime);
  float ratio = elapsed / info_->castTime;

  return info_->isChanneling ? (1.0f - ratio) : ratio;
}

float TargetCastBar::GetTimeRemaining(double currentTime) const {
  if (!info_) return 0.0f;
  float elapsed = static_cast<float>(currentTime) - info_->startTime;
  return std::max(0.0f, info_->castTime - elapsed);
}

bool TargetCastBar::IsCasting() const {
  return info_.has_value() && !info_->isChanneling;
}

bool TargetCastBar::IsChanneling() const {
  return info_.has_value() && info_->isChanneling;
}

bool TargetCastBar::IsInterruptible() const {
  return info_.has_value() && !info_->isUninterruptible;
}

const std::string& TargetCastBar::GetSpellName() const {
  return info_ ? info_->spellName : kEmptyString;
}

void TargetCastBar::Update(float dt) {
  if (!info_) return;

  elapsed_ += dt;
  info_->currentTime += dt;

  if (elapsed_ >= info_->castTime) {
    info_.reset();
    elapsed_ = 0.0f;
  }
}

void TargetCastBar::SetTarget(const ObjectGuid& guid) {
  target_guid_ = guid;
}

ObjectGuid TargetCastBar::GetTarget() const {
  return target_guid_;
}

void TargetCastBar::Reset() {
  info_.reset();
  target_guid_ = ObjectGuid{};
  elapsed_     = 0.0f;
  failed_      = false;
}

void TargetCastBar::OnCastFailed() {
  if (!info_) return;
  failed_ = true;

}

void TargetCastBar::OnCastPushback(float pushbackSeconds) {
  if (!info_) return;

  info_->castTime += pushbackSeconds;

  if (info_->castTime < 0.0f) info_->castTime = 0.0f;

  if (info_->isChanneling) {
    float remaining = info_->castTime - elapsed_;
    remaining -= pushbackSeconds;
    if (remaining < 0.0f) remaining = 0.0f;
    info_->castTime = elapsed_ + remaining;
  }
}

std::uint32_t TargetCastBar::GetBarColor() const {
  if (failed_) {
    return 0xFFFF0000;
  }
  if (!info_) {
    return 0xFF777777;
  }
  if (info_->isChanneling) {

    return 0xFF00CCCC;
  }
  if (info_->isUninterruptible) {

    return 0xFFB0B0B0;
  }

  return 0xFFFFB200;
}

std::string TargetCastBar::FormatTimeRemaining(float seconds) {
  if (seconds <= 0.0f) return "0.0s";
  char buf[32];
  if (seconds >= 60.0f) {
    int m = static_cast<int>(seconds) / 60;
    float s = seconds - static_cast<float>(m * 60);
    std::snprintf(buf, sizeof(buf), "%dm %.0fs", m, s);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1fs", seconds);
  }
  return std::string(buf);
}

ObjectGuid TargetCastBar::GetCasterGuid() const {
  if (!info_) return ObjectGuid{};
  return info_->casterGuid;
}

const std::string& TargetCastBar::GetSpellIcon() const {
  return info_ ? info_->spellIcon : kEmptyString;
}

float TargetCastBar::GetCastTime() const {
  return info_ ? info_->castTime : 0.0f;
}

bool TargetCastBar::HasFailed() const {
  return failed_;
}

}

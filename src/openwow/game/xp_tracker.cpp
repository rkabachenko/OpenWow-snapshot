#include "openwow/game/xp_tracker.h"

#include <algorithm>

namespace openwow::game {

void XPTracker::SetCurrentXP(std::uint32_t xp) { currentXP_ = xp; }
std::uint32_t XPTracker::GetCurrentXP() const { return currentXP_; }

void XPTracker::SetMaxXP(std::uint32_t xp) { maxXP_ = (xp > 0) ? xp : 1; }
std::uint32_t XPTracker::GetMaxXP() const { return maxXP_; }

void XPTracker::SetLevel(std::uint32_t level) { level_ = level; }
std::uint32_t XPTracker::GetLevel() const { return level_; }

float XPTracker::GetProgress() const {
  if (maxXP_ == 0) return 0.0f;
  return std::clamp(static_cast<float>(currentXP_) /
                        static_cast<float>(maxXP_),
                    0.0f, 1.0f);
}

std::string XPTracker::GetProgressText() const {
  return std::to_string(currentXP_) + " / " + std::to_string(maxXP_);
}

std::uint32_t XPTracker::GetXPToLevel() const {
  if (currentXP_ >= maxXP_) return 0;
  return maxXP_ - currentXP_;
}

void XPTracker::AddXPGain(std::uint32_t amount, std::string source,
                          XPSourceType type) {
  recentGains_.push_back(XPSource{amount, std::move(source), type});
  sessionXP_ += amount;

  if (type == XPSourceType::Kill) {
    killXPTotal_ += amount;
    ++killCount_;
  } else if (type == XPSourceType::Quest) {
    questXPTotal_ += amount;
    ++questCount_;
  }
}

const std::vector<XPSource>& XPTracker::GetRecentGains() const {
  return recentGains_;
}

std::uint32_t XPTracker::GetSessionXP() const { return sessionXP_; }

float XPTracker::GetSessionTime() const { return sessionTime_; }

float XPTracker::GetXPPerHour() const {
  if (sessionTime_ < 1.0f) return 0.0f;
  return static_cast<float>(sessionXP_) / (sessionTime_ / 3600.0f);
}

float XPTracker::GetEstimatedTimeToLevel() const {
  float rate = GetXPPerHour();
  if (rate <= 0.0f) return 0.0f;
  std::uint32_t remaining = GetXPToLevel();

  return (static_cast<float>(remaining) / rate) * 3600.0f;
}

void XPTracker::SetRestedXP(std::uint32_t xp) { restedXP_ = xp; }
std::uint32_t XPTracker::GetRestedXP() const { return restedXP_; }

float XPTracker::GetRestedPercent() const {
  if (maxXP_ == 0) return 0.0f;
  return std::clamp(static_cast<float>(restedXP_) /
                        static_cast<float>(maxXP_),
                    0.0f, 1.0f);
}

bool XPTracker::IsRested() const { return restedXP_ > 0; }

void XPTracker::SetMaxLevel(bool maxLevel) { maxLevel_ = maxLevel; }
bool XPTracker::IsMaxLevel() const { return maxLevel_; }

std::uint32_t XPTracker::GetKillsToLevel() const {
  if (killCount_ == 0) return 0;
  std::uint32_t avg = killXPTotal_ / killCount_;
  if (avg == 0) return 0;
  std::uint32_t remaining = GetXPToLevel();
  return (remaining + avg - 1) / avg;
}

std::uint32_t XPTracker::GetQuestsToLevel() const {
  if (questCount_ == 0) return 0;
  std::uint32_t avg = questXPTotal_ / questCount_;
  if (avg == 0) return 0;
  std::uint32_t remaining = GetXPToLevel();
  return (remaining + avg - 1) / avg;
}

void XPTracker::Update(float dt) { sessionTime_ += dt; }

void XPTracker::Reset() {
  currentXP_ = 0;
  maxXP_ = 1;
  level_ = 1;
  restedXP_ = 0;
  maxLevel_ = false;
  recentGains_.clear();
  sessionXP_ = 0;
  sessionTime_ = 0.0f;
  killXPTotal_ = 0;
  killCount_ = 0;
  questXPTotal_ = 0;
  questCount_ = 0;
}

}

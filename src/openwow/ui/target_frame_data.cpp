#include "openwow/ui/target_frame_data.h"

#include <cmath>

namespace openwow::ui {

void TargetFrameProvider::SetTarget(const TargetFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  target_ = data;
}

std::optional<TargetFrameData> TargetFrameProvider::GetTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return target_;
}

bool TargetFrameProvider::HasTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return target_.has_value();
}

void TargetFrameProvider::ClearTarget() {
  std::lock_guard<std::mutex> lock(mutex_);
  target_.reset();
}

void TargetFrameProvider::SetFocus(const TargetFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  focus_ = data;
}

std::optional<TargetFrameData> TargetFrameProvider::GetFocus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return focus_;
}

bool TargetFrameProvider::HasFocus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return focus_.has_value();
}

void TargetFrameProvider::ClearFocus() {
  std::lock_guard<std::mutex> lock(mutex_);
  focus_.reset();
}

void TargetFrameProvider::SetTargetOfTarget(const TargetFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  targetOfTarget_ = data;
}

std::optional<TargetFrameData> TargetFrameProvider::GetTargetOfTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return targetOfTarget_;
}

bool TargetFrameProvider::HasTargetOfTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return targetOfTarget_.has_value();
}

std::uint32_t TargetFrameProvider::GetReactionColor(TargetReaction reaction) {

  switch (reaction) {
    case TargetReaction::Hostile:    return 0xFFFF0000;
    case TargetReaction::Unfriendly: return 0xFFFF8000;
    case TargetReaction::Neutral:    return 0xFFFFFF00;
    case TargetReaction::Friendly:   return 0xFF00FF00;
  }
  return 0xFFFFFFFF;
}

std::uint32_t TargetFrameProvider::GetLevelColor(std::int32_t playerLevel,
                                                  std::int32_t targetLevel) {

  if (targetLevel < 0) return 0xFFFF0000;

  std::int32_t diff = targetLevel - playerLevel;

  if (diff >= 5)  return 0xFFFF0000;
  if (diff >= 3)  return 0xFFFF8000;
  if (diff >= -2) return 0xFFFFFF00;
  if (diff >= -8) return 0xFF00FF00;
  return 0xFF808080;
}

std::string TargetFrameProvider::GetCreatureTypeString(CreatureType type) {
  switch (type) {
    case CreatureType::Normal:    return "Normal";
    case CreatureType::Elite:     return "Elite";
    case CreatureType::RareElite: return "Rare Elite";
    case CreatureType::WorldBoss: return "Boss";
    case CreatureType::Rare:      return "Rare";
  }
  return "Unknown";
}

bool TargetFrameProvider::IsTargetAttackable() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!target_) return false;

  return target_->reaction == TargetReaction::Hostile ||
         target_->reaction == TargetReaction::Unfriendly;
}

std::uint32_t TargetFrameProvider::GetTargetClassColor() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!target_) return 0xFFFFFFFF;
  return GetClassColor(target_->classId);
}

std::uint32_t TargetFrameProvider::GetClassColor(std::uint8_t classId) {

  switch (classId) {
    case 1:  return 0xFFC79C6E;
    case 2:  return 0xFFF58CBA;
    case 3:  return 0xFFABD473;
    case 4:  return 0xFFFFF569;
    case 5:  return 0xFFFFFFFF;
    case 6:  return 0xFFC41F3B;
    case 7:  return 0xFF0070DE;
    case 8:  return 0xFF69CCF0;
    case 9:  return 0xFF9482C9;
    case 11: return 0xFFFF7D0A;
  }
  return 0xFFFFFFFF;
}

void TargetFrameProvider::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  target_.reset();
  focus_.reset();
  targetOfTarget_.reset();
}

}

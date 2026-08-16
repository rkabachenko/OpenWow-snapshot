
#include "openwow/game/deserter_tracker.h"

#include <algorithm>

namespace openwow::game {

namespace {

constexpr float kMaxLFGDuration = 1800.0f;
constexpr float kMaxBGDuration  =  900.0f;

float ClampDuration(DeserterType type, float duration) {
  const float cap = (type == DeserterType::LFG) ? kMaxLFGDuration
                                                  : kMaxBGDuration;
  return std::clamp(duration, 0.0f, cap);
}

}

void DeserterTracker::AddDeserter(DeserterType type, float duration,
                                  const std::string& reason) {
  std::lock_guard lock(mutex_);
  auto key = static_cast<uint8_t>(type);

  const float clamped = ClampDuration(type, duration);

  auto it = deserters_.find(key);
  if (it != deserters_.end() && it->second.expiryTime > clamped) {

    if (!reason.empty()) it->second.reason = reason;
    return;
  }

  deserters_[key] = DeserterEntry{type, clamped, reason};
}

bool DeserterTracker::HasDeserter(DeserterType type) const {
  std::lock_guard lock(mutex_);
  auto it = deserters_.find(static_cast<uint8_t>(type));
  return it != deserters_.end() && it->second.expiryTime > 0.0f;
}

float DeserterTracker::GetRemainingTime(DeserterType type) const {
  std::lock_guard lock(mutex_);
  auto it = deserters_.find(static_cast<uint8_t>(type));
  if (it == deserters_.end()) return 0.0f;
  return std::max(0.0f, it->second.expiryTime);
}

std::optional<DeserterEntry> DeserterTracker::GetDeserter(
    DeserterType type) const {
  std::lock_guard lock(mutex_);
  auto it = deserters_.find(static_cast<uint8_t>(type));
  if (it == deserters_.end()) return std::nullopt;
  return it->second;
}

void DeserterTracker::RemoveDeserter(DeserterType type) {
  std::lock_guard lock(mutex_);
  deserters_.erase(static_cast<uint8_t>(type));
}

std::vector<DeserterEntry> DeserterTracker::GetAllDeserters() const {
  std::lock_guard lock(mutex_);
  std::vector<DeserterEntry> result;
  result.reserve(deserters_.size());
  for (const auto& [_, entry] : deserters_) {
    result.push_back(entry);
  }
  return result;
}

void DeserterTracker::Update(float dt) {
  std::lock_guard lock(mutex_);

  std::vector<uint8_t> expired;
  for (auto& [key, entry] : deserters_) {
    entry.expiryTime -= dt;
    if (entry.expiryTime <= 0.0f) {
      entry.expiryTime = 0.0f;
      expired.push_back(key);
    }
  }

  for (const auto key : expired) {
    deserters_.erase(key);
  }
}

bool DeserterTracker::IsLFGLocked() const {
  return HasDeserter(DeserterType::LFG);
}

bool DeserterTracker::IsBGLocked() const {
  return HasDeserter(DeserterType::Battleground);
}

void DeserterTracker::Reset() {
  std::lock_guard lock(mutex_);
  deserters_.clear();
}

}

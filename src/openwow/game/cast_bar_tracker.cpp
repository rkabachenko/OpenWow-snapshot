#include "openwow/game/cast_bar_tracker.h"

#include <algorithm>

namespace openwow::game {

float CastBarInfo::GetProgress(std::uint32_t current_time) const {
  if (duration == 0) return 1.0f;

  if (is_channeling) {

    if (current_time >= end_time) return 0.0f;
    if (current_time <= start_time) return 1.0f;
    float elapsed = static_cast<float>(current_time - start_time);
    return 1.0f - (elapsed / static_cast<float>(duration));
  } else {

    if (current_time >= end_time) return 1.0f;
    if (current_time <= start_time) return 0.0f;
    float elapsed = static_cast<float>(current_time - start_time);
    return elapsed / static_cast<float>(duration);
  }
}

float CastBarInfo::GetRemainingSeconds(std::uint32_t current_time) const {
  if (current_time >= end_time) return 0.0f;
  return static_cast<float>(end_time - current_time) / 1000.0f;
}

bool CastBarInfo::IsComplete(std::uint32_t current_time) const {
  return current_time >= end_time;
}

CastBarTracker& CastBarTracker::Get() {
  static CastBarTracker instance;
  return instance;
}

void CastBarTracker::StartCast(const ObjectGuid& caster,
                               std::uint32_t spell_id,
                               const std::string& name,
                               std::uint32_t cast_time,
                               std::uint32_t start_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& info = casts_[caster.GetRawValue()];
  info = CastBarInfo{};
  info.spell_id = spell_id;
  info.spell_name = name;
  info.is_casting = true;
  info.is_channeling = false;
  info.start_time = start_time;
  info.end_time = start_time + cast_time;
  info.duration = cast_time;
  info.result = CastBarInfo::Result::kNone;
}

void CastBarTracker::StartChannel(const ObjectGuid& caster,
                                  std::uint32_t spell_id,
                                  const std::string& name,
                                  std::uint32_t duration,
                                  std::uint32_t start_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& info = casts_[caster.GetRawValue()];
  info = CastBarInfo{};
  info.spell_id = spell_id;
  info.spell_name = name;
  info.is_casting = false;
  info.is_channeling = true;
  info.start_time = start_time;
  info.end_time = start_time + duration;
  info.duration = duration;
  info.result = CastBarInfo::Result::kNone;
}

void CastBarTracker::InterruptCast(const ObjectGuid& caster) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = casts_.find(caster.GetRawValue());
  if (it != casts_.end()) {
    it->second.is_casting = false;
    it->second.is_channeling = false;
    it->second.result = CastBarInfo::Result::kInterrupted;
  }
}

void CastBarTracker::FinishCast(const ObjectGuid& caster) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = casts_.find(caster.GetRawValue());
  if (it != casts_.end()) {
    it->second.is_casting = false;
    it->second.is_channeling = false;
    it->second.result = CastBarInfo::Result::kSuccess;
  }
}

void CastBarTracker::FailCast(const ObjectGuid& caster) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = casts_.find(caster.GetRawValue());
  if (it != casts_.end()) {
    it->second.is_casting = false;
    it->second.is_channeling = false;
    it->second.result = CastBarInfo::Result::kFailed;
  }
}

const CastBarInfo& CastBarTracker::GetCastInfo(
    const ObjectGuid& caster) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = casts_.find(caster.GetRawValue());
  if (it != casts_.end()) return it->second;
  return empty_info_;
}

bool CastBarTracker::IsCasting(const ObjectGuid& caster) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = casts_.find(caster.GetRawValue());
  if (it != casts_.end()) return it->second.is_casting;
  return false;
}

bool CastBarTracker::IsChanneling(const ObjectGuid& caster) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = casts_.find(caster.GetRawValue());
  if (it != casts_.end()) return it->second.is_channeling;
  return false;
}

const CastBarInfo& CastBarTracker::GetPlayerCast() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (player_guid_.IsEmpty()) return empty_info_;
  auto it = casts_.find(player_guid_.GetRawValue());
  if (it != casts_.end()) return it->second;
  return empty_info_;
}

const CastBarInfo& CastBarTracker::GetTargetCast() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (target_guid_.IsEmpty()) return empty_info_;
  auto it = casts_.find(target_guid_.GetRawValue());
  if (it != casts_.end()) return it->second;
  return empty_info_;
}

void CastBarTracker::SetPlayerGuid(const ObjectGuid& guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  player_guid_ = guid;
}

void CastBarTracker::SetTargetGuid(const ObjectGuid& guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  target_guid_ = guid;
}

void CastBarTracker::Update(std::uint32_t current_time) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto it = casts_.begin(); it != casts_.end();) {
    auto& info = it->second;
    bool expired = false;

    if (info.result != CastBarInfo::Result::kNone) {

      expired = true;
    } else if (!info.is_casting && !info.is_channeling) {
      expired = true;
    } else if (current_time > info.end_time + 2000) {

      expired = true;
    }

    if (expired) {
      it = casts_.erase(it);
    } else {
      ++it;
    }
  }
}

void CastBarTracker::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  casts_.clear();
  player_guid_ = ObjectGuid{};
  target_guid_ = ObjectGuid{};
}

}

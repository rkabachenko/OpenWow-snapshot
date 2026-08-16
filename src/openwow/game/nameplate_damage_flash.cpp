
#include "openwow/game/nameplate_damage_flash.h"

namespace openwow::game {

NameplateDamageFlashState& NameplateDamageFlashState::Get() {
  static NameplateDamageFlashState state;
  return state;
}

void NameplateDamageFlashState::Trigger(const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return;
  }

  std::lock_guard lock(mutex_);
  remaining_ms_by_guid_[guid.GetRawValue()] = kFlashDurationMs;
}

void NameplateDamageFlashState::Update(const float dt_seconds) {
  if (dt_seconds <= 0.0f) {
    return;
  }

  const auto elapsed_ms = static_cast<std::int32_t>(dt_seconds * 1000.0f);
  if (elapsed_ms <= 0) {
    return;
  }

  std::lock_guard lock(mutex_);
  for (auto it = remaining_ms_by_guid_.begin(); it != remaining_ms_by_guid_.end();) {
    it->second -= elapsed_ms;
    if (it->second <= 0) {
      it = remaining_ms_by_guid_.erase(it);
    } else {
      ++it;
    }
  }
}

bool NameplateDamageFlashState::IsActive(const ObjectGuid guid) const {
  if (guid.IsEmpty()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto it = remaining_ms_by_guid_.find(guid.GetRawValue());
  return it != remaining_ms_by_guid_.end() && it->second > 0;
}

void NameplateDamageFlashState::Reset() {
  std::lock_guard lock(mutex_);
  remaining_ms_by_guid_.clear();
}

}

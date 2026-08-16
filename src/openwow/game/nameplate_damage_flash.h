
#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace openwow::game {

class NameplateDamageFlashState {
 public:
  static constexpr std::int32_t kFlashDurationMs = 5000;
  static constexpr std::uint32_t kFlashColorArgb = 0xFFFF0000u;

  static NameplateDamageFlashState& Get();

  void Trigger(ObjectGuid guid);
  void Update(float dt_seconds);
  [[nodiscard]] bool IsActive(ObjectGuid guid) const;
  void Reset();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, std::int32_t> remaining_ms_by_guid_;
};

}

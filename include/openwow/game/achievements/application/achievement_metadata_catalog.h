#pragma once

#include "openwow/game/achievements/model/achievement_state_types.h"

#include <cstdint>

namespace openwow::game {

class AchievementMetadataCatalog {
 public:
  virtual ~AchievementMetadataCatalog() = default;

  [[nodiscard]] virtual bool Contains(AchievementId achievement_id) const = 0;
  [[nodiscard]] virtual std::uint32_t OrderInGroup(
      AchievementId achievement_id) const = 0;
};

}

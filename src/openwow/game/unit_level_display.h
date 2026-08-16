#pragma once

#include "openwow/game/inebriation.h"
#include "openwow/game/objects/cgplayer.h"

#include <algorithm>

namespace openwow::game {

inline int ApplyHostileDrunkLevelMask(const CGPlayer_C& viewer,
                                      const CGUnit_C& target,
                                      const int displayed_level) {
  if (displayed_level <= 0) {
    return displayed_level;
  }

  if (viewer.GetGuid() == target.GetGuid()) {
    return displayed_level;
  }

  if (!viewer.HasActiveInebriation() ||
      !viewer.Interaction().CanAttackSpellTarget(target)) {
    return displayed_level;
  }

  const auto inebriation = GetEffectiveInebriationValue(
      viewer.GetDrunkState(), viewer.GetFakeInebriation());
  const float rounded_penalty =
      (static_cast<float>(inebriation) / 100.0f) * 5.0f + 0.5f;
  const int penalty = static_cast<int>(rounded_penalty);
  return std::max(displayed_level - penalty, 1);
}

}

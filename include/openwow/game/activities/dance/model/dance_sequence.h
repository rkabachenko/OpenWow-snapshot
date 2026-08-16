#pragma once

#include "openwow/game/activities/dance/model/dance_types.h"

#include <cstddef>
#include <vector>

namespace openwow::game {

struct DanceSequence final {
  std::vector<DanceMoveId> steps;
  DanceSequencePosition start_position;

  [[nodiscard]] std::size_t StepCount() const {
    return steps.size();
  }
};

}

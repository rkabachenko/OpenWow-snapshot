#pragma once

#include "openwow/game/activities/dance/model/dance_types.h"

#include <string>

namespace openwow::game {

struct KnownDanceEntry final {
  std::string name;
  DanceId dance_id;
  DanceSequenceId sequence_id;
};

}

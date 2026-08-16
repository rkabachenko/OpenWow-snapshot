#pragma once

#include "openwow/game/activities/dance/model/dance_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace openwow::game {

struct DanceEmoteAnimationAction final {
  DanceEmoteAnimationId animation_id;
};

struct DanceAnimationDataAction final {
  DanceAnimationDataId animation_id;
};

struct DanceSoundAction final {
  DanceSoundKitId sound_kit_id;
};

struct DanceDelayAction final {
  DanceDelayDuration duration;
};

struct DanceRepeatPreviousAction final {
  DanceDelayDuration duration;
};

struct UnsupportedDanceMoveAction final {};

using DanceMoveAction =
    std::variant<DanceEmoteAnimationAction, DanceAnimationDataAction,
                 DanceSoundAction, DanceDelayAction,
                 DanceRepeatPreviousAction, UnsupportedDanceMoveAction>;

struct DanceMoveRecord final {
  DanceMoveId id;
  DanceMoveAction action;
  DanceMoveId fallback_step_id;
  DanceClassMask required_classes;
  std::optional<DanceLearnedMoveIndex> required_learned_move;
  std::string name;
};

[[nodiscard]] bool IsDanceDelayAction(const DanceMoveAction& action);

class DanceMoveCatalog final {
 public:
  explicit DanceMoveCatalog(std::vector<DanceMoveRecord> records);

  [[nodiscard]] const DanceMoveRecord* Lookup(DanceMoveId id) const;

 private:
  std::vector<DanceMoveRecord> records_;
};

}

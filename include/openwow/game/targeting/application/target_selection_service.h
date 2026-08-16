#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {
class TargetingSystem;
class WorldSession;

namespace targeting {

enum class TargetClearMode : std::uint8_t {
  kLocalOnly,
  kNotifyServer,
};

enum class TargetSelectionResult : std::uint8_t {
  kSelected,
  kCleared,
  kUnchanged,
  kRejected,
  kNoTargetingOwner,
  kNoActivePlayer,
  kObjectNotFound,
};

class TargetSelectionService {
public:
  explicit TargetSelectionService(TargetingSystem &targeting) noexcept;

  TargetSelectionResult ClearTarget(
      WorldSession &session, ObjectGuid expected_target = {},
      TargetClearMode mode = TargetClearMode::kNotifyServer);
  TargetSelectionResult SelectTarget(WorldSession &session, ObjectGuid target);
  TargetSelectionResult SelectTargetIfNone(WorldSession &session,
                                           ObjectGuid target);

private:
  TargetingSystem &targeting_;
};

}
}

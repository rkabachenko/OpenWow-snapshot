#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {
class CGPlayer_C;
class CGUnit_C;
class TargetingSystem;
class WorldSession;

namespace targeting::ui {

enum class TargetInteractionResult : std::uint8_t {
  kInteracted,
  kNoActivePlayer,
  kObjectNotFound,
};

class TargetInteractionController {
public:
  explicit TargetInteractionController(TargetingSystem &targeting) noexcept;

  TargetInteractionResult InteractWithTarget(WorldSession &session,
                                              ObjectGuid target);

private:
  TargetingSystem &targeting_;
};

bool TryCastHeldItemSpellOnOwnedPet(WorldSession &session,
                                    const CGPlayer_C &player,
                                    const CGUnit_C &target);

void HandleHeldCursorLeftClickOnPlayer(WorldSession &session,
                                       const CGUnit_C &player,
                                       const CGUnit_C &target);

}
}

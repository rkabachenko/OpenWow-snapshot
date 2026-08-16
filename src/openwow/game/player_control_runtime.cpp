#include "openwow/game/player_control_runtime.h"

#include "openwow/game/objects/cgunit.h"
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"

#include <utility>

namespace openwow::game {

const CGUnit_C *ResolveEffectiveMovingUnit(const WorldSession &session) {
  const auto active_mover_guid =
      session.player_control_runtime().ActiveMoverGuid();
  return active_mover_guid.IsEmpty()
             ? session.objects().GetLocalPlayerTyped()
             : session.objects().GetUnit(active_mover_guid);
}

CGUnit_C *ResolveEffectiveMovingUnit(WorldSession &session) {
  return const_cast<CGUnit_C *>(ResolveEffectiveMovingUnit(
      static_cast<const WorldSession &>(session)));
}

void SynchronizeUnitBoundWorldCamera(
    const CGUnit_C &unit, openwow::world::WorldCamera *const camera) {
  if (camera != nullptr &&
      camera->bound_object() == unit.GetGuid().GetRawValue()) {

    const auto target_position = unit.GetPosition();
    camera->SetTarget(target_position.x, target_position.y, target_position.z);
  }
}

void PlayerControlRuntime::SetCombatFocusGuid(const std::uint64_t guid,
                                              const bool force) {
  if (combat_focus_guid == 0u || combat_focus_guid == guid ||
      !combat_focus_locked || force) {
    combat_focus_guid = guid;
    combat_focus_locked = force;
  }
}

void PlayerControlRuntime::ClearDestroyedActiveMover(
    const std::uint64_t destroyed_guid) noexcept {
  if (active_mover_guid == destroyed_guid) {
    active_mover_guid = 0u;
  }
}

}

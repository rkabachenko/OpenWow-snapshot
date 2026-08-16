#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {

class CGUnit_C;
class ObjectManager;
class UnitMissileTrajectory_C;
class WorldSession;

[[nodiscard]] CGUnit_C *ResolveEffectiveMovingUnit(WorldSession &session);
[[nodiscard]] const CGUnit_C *ResolveEffectiveMovingUnit(
    const WorldSession &session);

struct PlayerControlRuntime final {
  PlayerControlRuntime() = default;

  std::uint64_t active_mover_guid{0};
  std::uint32_t movement_interaction_flags{0};

  std::uint64_t combat_focus_guid{0};
  bool combat_focus_locked{false};
  std::uint32_t combat_focus_enabled{1};

  void InstallInitialActiveMover(WorldSession &session, ObjectManager &objects,
                                 UnitMissileTrajectory_C &missile_trajectory,
                                 std::uint64_t mover_guid);
  void SetActiveMover(WorldSession &session, ObjectManager &objects,
                      UnitMissileTrajectory_C &missile_trajectory,
                      std::uint64_t new_guid);
  [[nodiscard]] std::uint32_t
  GetActiveMoverAutoAttackType(const ObjectManager &objects) const;
  [[nodiscard]] ObjectGuid ActiveMoverGuid() const {
    return ObjectGuid(active_mover_guid);
  }
  void ClearDestroyedActiveMover(std::uint64_t destroyed_guid) noexcept;
  void SetCombatFocusGuid(std::uint64_t guid, bool force);
};

}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

void SynchronizeUnitBoundWorldCamera(
    const CGUnit_C &unit, openwow::world::WorldCamera *camera);

}

#include "openwow/game/combat/application/client_control_transition.h"

#include "openwow/game/objects/cgunit.h"
#include "openwow/game/world_session.h"

namespace openwow::game::combat {
namespace {

AutoAttackActivity ObserveAutoAttackActivity(const CGUnit_C& unit) {
  return unit.Movement().CanControlCharacter() ? AutoAttackActivity::Active
                                    : AutoAttackActivity::Inactive;
}

}

AutoAttackActivityChange ApplyClientControlPermission(
    WorldSession& session, CGUnit_C& unit,
    const ClientControlPermission permission) {
  const auto previous = ObserveAutoAttackActivity(unit);
  unit.Movement().SetClientControlState(
      session.world_camera(), permission == ClientControlPermission::Granted);
  return {
      .previous = previous,
      .current = ObserveAutoAttackActivity(unit),
  };
}

}

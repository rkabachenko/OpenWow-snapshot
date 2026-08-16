#pragma once

#include "openwow/game/combat/model/auto_attack_activity.h"

namespace openwow::game {

class CGUnit_C;
class WorldSession;

namespace combat {

[[nodiscard]] AutoAttackActivityChange ApplyClientControlPermission(
    WorldSession& session, CGUnit_C& unit,
    ClientControlPermission permission);

}
}

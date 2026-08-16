#pragma once

#include "openwow/game/combat/model/auto_attack_activity.h"

namespace openwow::game {
class WorldSession;
}

namespace openwow::game::combat::ui {

void PresentAutoAttackActivityChange(
    WorldSession& session,
    AutoAttackActivityChange change);

}

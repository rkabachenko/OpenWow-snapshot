
#include "openwow/game/attack_range_raycast_state.h"

namespace openwow::game {

void AttackRangeRaycastState::EnsureInitialised() {
    if (initialised_)
        return;

    facet_array_.clear();
    facet_array_.reserve(256);

    k_array_.clear();
    k_array_.reserve(256);

    initialised_ = true;
}

void AttackRangeRaycastState::Cleanup() {
    k_array_.clear();
    k_array_.shrink_to_fit();

    facet_array_.clear();
    facet_array_.shrink_to_fit();

    initialised_ = false;
}

void AttackRangeRaycastState::ResetKArrayCount() {
    k_array_.clear();
}

AttackRangeRaycastState& GetAttackRangeRaycastState() {
    static AttackRangeRaycastState instance;
    return instance;
}

}

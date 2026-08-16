
#include "openwow/game/spell_chain_jump.h"

#include "openwow/game/objects/cgunit.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace openwow::game {

float ChainSpellTargetSelector::ComputeJumpMultiplier(
    std::uint32_t jump_index,
    const ChainSpellParams& params) {
  if (jump_index == 0) {
    return 1.0f;
  }

  return std::pow(params.damage_mult_per_jump,
                  static_cast<float>(jump_index));
}

ChainSpellResult ChainSpellTargetSelector::ProcessServerChainTargets(
    const std::vector<ObjectGuid>& server_targets,
    const ChainSpellParams& params) {
  ChainSpellResult result;

  for (std::size_t i = 0; i < server_targets.size(); ++i) {
    ChainJumpResult jump;
    jump.target_guid = server_targets[i];
    jump.current_jump = static_cast<std::uint8_t>(i);
    jump.damage_multiplier = ComputeJumpMultiplier(
        static_cast<std::uint32_t>(i), params);
    jump.hit = true;
    result.targets.push_back(jump);
    result.bounces.push_back(jump);
  }

  return result;
}

ChainSpellResult ChainSpellTargetSelector::SelectChainTargets(
    const CGUnit_C& caster,
    const ObjectGuid& initial_target,
    const ChainSpellParams& params,
    const WorldSession& ,
    bool ,
    TargetSelectionMode ) {
  ChainSpellResult result;

  if (initial_target.IsEmpty() || params.max_jumps == 0) {
    return result;
  }

  {
    ChainJumpResult initial;
    initial.target_guid = initial_target;
    initial.current_jump = 0;
    initial.damage_multiplier = 1.0f;
    result.targets.push_back(initial);
    result.bounces.push_back(initial);
  }

  (void)caster;
  return result;
}

}

#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_effect_types.h"

#include <cstdint>
#include <vector>

namespace openwow::game {

class CGUnit_C;
class WorldSession;

struct ChainJumpResult {
  ObjectGuid target_guid;
  bool hit                                   = true;
  std::uint8_t current_jump                  = 0;
  float damage_multiplier                    = 1.0f;
};

struct ChainSpellResult {

  std::vector<ChainJumpResult> targets;

  std::vector<ChainJumpResult> bounces;

  std::uint32_t target_count() const { return static_cast<std::uint32_t>(targets.size()); }
};

class ChainSpellTargetSelector {
 public:
  ChainSpellTargetSelector() = default;

  ChainSpellResult SelectChainTargets(
      const CGUnit_C& caster,
      const ObjectGuid& initial_target,
      const ChainSpellParams& params,
      const WorldSession& session,
      bool is_harmful,
      TargetSelectionMode selection_mode = TargetSelectionMode::kNearest);

  static ChainSpellResult ProcessServerChainTargets(
      const std::vector<ObjectGuid>& server_targets,
      const ChainSpellParams& params);

  static float ComputeJumpMultiplier(std::uint32_t jump_index,
                                      const ChainSpellParams& params);

  static constexpr float kDefaultBounceDistance = 10.0f;

  static constexpr float kDefaultDamagePerJump = 0.70f;
  static constexpr float kDefaultHealPerJump   = 0.50f;
};

}

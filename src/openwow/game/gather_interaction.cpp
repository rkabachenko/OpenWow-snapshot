#include "openwow/game/gather_interaction.h"

namespace openwow::game {

void GatherInteraction::TryGatherOrApproach(
    const std::uint64_t ,
    const std::uint64_t target_guid,
    const bool approach_if_out_of_range,
    const bool set_auto_loot) {
  std::uint32_t spell_id = 0;
  if (delegates_.get_gather_spell_id) {
    spell_id = delegates_.get_gather_spell_id(target_guid);
  }

  float min_range = 0.0f;
  float max_range = 0.0f;
  if (delegates_.get_range_window) {
    delegates_.get_range_window(spell_id, target_guid, &min_range, &max_range);
  }

  Position player_pos = {0.0f, 0.0f, 0.0f};
  Position target_pos = {0.0f, 0.0f, 0.0f};
  if (delegates_.get_player_position) {
    player_pos = delegates_.get_player_position(0);
  }
  if (delegates_.get_target_position) {
    target_pos = delegates_.get_target_position(target_guid);
  }

  const float dx = player_pos[0] - target_pos[0];
  const float dy = player_pos[1] - target_pos[1];
  const float dz = player_pos[2] - target_pos[2];
  const float dist_sq = dx * dx + dy * dy + dz * dz;

  if (IsOutOfGatherRange(dist_sq, max_range) && approach_if_out_of_range) {
    if (delegates_.initiate_action) {
      delegates_.initiate_action(target_guid, kActionTypeObjectInteract);
    }
  } else {
    if (delegates_.cast_gather_spell) {
      delegates_.cast_gather_spell(spell_id, target_guid);
    }
  }

  if (set_auto_loot) {
    bool auto_loot_enabled = false;
    if (delegates_.is_auto_loot_toggled) {
      auto_loot_enabled = delegates_.is_auto_loot_toggled();
    }
    if (delegates_.set_pending_auto_loot) {
      delegates_.set_pending_auto_loot(auto_loot_enabled);
    }
  }
}

}


#include "openwow/game/unit_animation_resolution.h"

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <array>
#include <cstring>

namespace openwow::game::unit_animation_resolution {

using openwow::data::dbc::AnimationDataEntry;
using openwow::data::dbc::DbcStore;

bool AnimationDataDB_FindByBehaviorAndTier(
    const DbcStore<AnimationDataEntry>& animation_db,
    const std::uint32_t behavior_id,
    const std::uint32_t variation_tier,
    std::uint32_t& out_sequence_id) {

  const auto& entries = animation_db.entries();
  const auto entry_count = static_cast<std::uint32_t>(entries.size());

  if (behavior_id >= entry_count) {
    return false;
  }

  if (variation_tier == 0 && behavior_id < entry_count) {
    const auto& direct = entries[behavior_id];
    if (direct.behavior_id == behavior_id && direct.behavior_tier == 0) {
      out_sequence_id = direct.id;
      return true;
    }
  }

  const auto* sparse_entry = animation_db.LookupEntry(behavior_id);
  if (sparse_entry != nullptr && sparse_entry->behavior_tier != 0) {
    out_sequence_id = behavior_id;
    return true;
  }

  for (const auto& entry : entries) {
    if (entry.behavior_id == behavior_id &&
        entry.behavior_tier == variation_tier) {
      out_sequence_id = entry.id;
      return true;
    }
  }

  return false;
}

bool AnimationDataDB_FindDefaultForVariation(
    const DbcStore<AnimationDataEntry>& animation_db,
    const std::function<bool(std::uint32_t)>& model_supports_animation,
    const std::uint32_t variation_tier,
    std::uint32_t& out_sequence_id) {

  const auto& entries = animation_db.entries();
  const auto entry_count = static_cast<std::uint32_t>(entries.size());

  if (entry_count == 0) {
    return false;
  }

  std::uint32_t candidate_id = 0;

  if (variation_tier == 0 && !entries.empty() &&
      entries[0].behavior_id == 0 && entries[0].behavior_tier == 0) {
    candidate_id = entries[0].id;
  } else {
    const auto* sparse_entry = animation_db.LookupEntry(0);
    if (sparse_entry != nullptr && sparse_entry->behavior_tier != 0) {
      candidate_id = 0;
    } else {
      bool found = false;
      for (const auto& entry : entries) {
        if (entry.behavior_id == 0 &&
            entry.behavior_tier == variation_tier) {
          candidate_id = entry.id;
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    }
  }

  if (!model_supports_animation || !model_supports_animation(candidate_id)) {
    return false;
  }

  out_sequence_id = candidate_id;
  return true;
}

std::uint32_t AnimationData_GetBehaviorId(
    const DbcStore<AnimationDataEntry>& animation_db,
    const std::uint32_t animation_id) {

  const auto* entry = animation_db.LookupEntry(animation_id);
  if (entry != nullptr) {
    return entry->behavior_id;
  }
  return kMaxAnimationId;
}

}

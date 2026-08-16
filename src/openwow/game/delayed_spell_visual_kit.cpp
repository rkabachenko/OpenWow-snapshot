
#include "openwow/game/delayed_spell_visual_kit.h"

#include <algorithm>

namespace openwow::game {

DelayedSpellVisualKit& AddDelayedSpellVisualKit(
    std::vector<DelayedSpellVisualKit>& kits,
    std::uint32_t spell_id,
    std::uint32_t visual_rec,
    std::uint32_t effect_index,
    const DelayedVisualKitPosition* position,
    std::uint32_t pos_param_a,
    std::uint32_t pos_param_b,
    bool flag_a,
    bool flag_b,
    std::uint32_t timestamp,
    std::uint32_t visual_type,
    std::uint32_t expire_time) {

  auto it = std::find_if(kits.begin(), kits.end(),
                         [](const DelayedSpellVisualKit& e) {
                           return !e.IsActive();
                         });

  if (it == kits.end()) {
    kits.emplace_back();
    it = kits.end() - 1;
  }

  DelayedSpellVisualKit& entry = *it;

  entry.spell_id          = spell_id;
  entry.spell_visual_rec_ptr = visual_rec;
  entry.effect_index      = effect_index;

  if (position) {

    entry.position_x     = position->x;
    entry.position_y     = position->y;
    entry.position_z     = position->z;
    entry.flags         |= DelayedSpellVisualKit::kFlagHasPosition;
    entry.position_param_a = pos_param_a;
    entry.position_param_b = pos_param_b;
  } else {

    entry.position_x     = 0.0f;

    entry.position_y     = 0.0f;

    entry.position_z     = 0.0f;

    entry.flags         &= ~DelayedSpellVisualKit::kFlagHasPosition;

    entry.position_param_a = 0;

    entry.position_param_b = 0;

  }

  entry.timestamp = timestamp;

  std::uint32_t computed_flags =
      DelayedSpellVisualKit::kFlagActive
      | (flag_b ? DelayedSpellVisualKit::kFlagParamB : 0u)
      | (flag_a ? DelayedSpellVisualKit::kFlagParamA : 0u);

  entry.flags = (entry.flags & ~0x0Eu) | computed_flags;

  entry.visual_type = visual_type;
  entry.expire_time = expire_time;

  return entry;
}

}

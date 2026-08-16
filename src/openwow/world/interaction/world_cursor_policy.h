#pragma once

#include <cstdint>

namespace openwow::world {

struct CursorInfoContext {
  bool active_player_present = false;
  bool normal_focus_enables_terrain_pick = false;
  bool normal_focus_enables_liquid_pick = false;
  bool has_held_cursor_payload = false;
  bool has_active_targeting_spell = false;
  std::uint32_t targeting_flags = 0;
  bool targets_terrain_and_liquid = false;
  bool has_spell_target_mask = false;
  bool allows_creature_type_12 = false;
  bool cursor_requires_unit_target = false;
  bool cursor_supports_auto_target = false;
  bool cursor_has_area_target_flag = false;
};

[[nodiscard]] int BuildWorldCursorIntersectionMask(
    const CursorInfoContext& context);

}

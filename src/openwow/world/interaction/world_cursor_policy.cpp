#include "openwow/world/interaction/world_cursor_policy.h"

namespace openwow::world {

int BuildWorldCursorIntersectionMask(const CursorInfoContext& context) {
  if (context.has_active_targeting_spell) {
    int flags = 0;
    if (context.targets_terrain_and_liquid) {
      flags = 0x3;
    }
    if ((context.targeting_flags & 0x4800u) != 0) {
      flags |= 0x4;
    }
    if (context.has_spell_target_mask) {
      flags |= 0x18;
      if (context.cursor_requires_unit_target) {
        flags |= 0x20;
      }
      if ((context.targeting_flags & 0x408u) != 0) {
        flags |= 0x10000;
      }
      if ((context.targeting_flags & 0x500u) != 0) {
        flags |= 0x40000;
      }
      if ((context.targeting_flags & 0x480u) != 0) {
        flags |= 0x80000;
      }
      if ((context.targeting_flags & 0x404u) != 0) {
        flags |= 0x20000;
      }
      if (context.cursor_supports_auto_target) {
        flags |= 0x200040;
      }
      if ((context.targeting_flags & 0x8600u) == 0) {
        flags |= 0x100000;
      }
      if (context.allows_creature_type_12) {
        flags |= 0x400000;
      }
      if (context.cursor_has_area_target_flag) {
        flags |= 0x800000;
      }
    }
    if ((context.targeting_flags & 0x8000u) != 0) {
      flags |= 0x40040;
    }
    if ((context.targeting_flags & 0x200u) != 0) {
      flags |= 0x80040;
    }
    return flags;
  }

  int flags = 0;
  if (context.normal_focus_enables_terrain_pick &&
      !context.has_held_cursor_payload) {
    flags = context.normal_focus_enables_liquid_pick ? 0x3 : 0x1;
  }
  if (context.active_player_present) {
    flags |= 0x5C;
  }
  return flags;
}

}

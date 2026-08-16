#include "openwow/game/spell_visual_m2_event.h"

#include "openwow/game/objects/cgunit.h"
#include "openwow/game/world_session.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kTriggeredEventSound = 0x444E5324u;
constexpr std::uint32_t kTriggeredEventHit = 0x54494824u;

}

bool DispatchSpellVisualM2Event(
    const render::m2::M2TriggeredEvent& event,
    const SpellVisualM2EventHandlers& handlers) {
  switch (event.identifier) {
    case kTriggeredEventSound:
      if (!handlers.sound) {
        return false;
      }
      handlers.sound(event);
      return true;
    case kTriggeredEventHit:
      if (!handlers.hit) {
        return false;
      }
      handlers.hit();
      return true;
    default:
      return false;
  }
}

void RefreshSpellVisualM2HitReaction(CGUnit_C& unit,
                                     const WorldSession& session) {
  const auto animation_group =
      unit.Mount().HasMountedDisplay(unit) ? 9 : 8;
  unit.Animation().UpdateStandAnimation(
      session, animation_group, unit.Animation().GetStandState());
}

}

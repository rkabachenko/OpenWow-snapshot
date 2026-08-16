#pragma once

#include <cstdint>
#include <functional>

#include "openwow/render/m2/m2_public_types.h"

namespace openwow::game {

class CGUnit_C;
class WorldSession;

struct SpellVisualM2EventHandlers {
  std::function<void(const render::m2::M2TriggeredEvent&)> sound;
  std::function<void()> hit;
};

[[nodiscard]] bool DispatchSpellVisualM2Event(
    const render::m2::M2TriggeredEvent& event,
    const SpellVisualM2EventHandlers& handlers);

void RefreshSpellVisualM2HitReaction(CGUnit_C& unit,
                                     const WorldSession& session);

}

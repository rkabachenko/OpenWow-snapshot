#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;

void SpellC_OnCastComplete(WorldSession& session, std::uintptr_t spell_entry,
                           std::uintptr_t spell_rec);

}

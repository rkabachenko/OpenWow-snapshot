#pragma once

#include "openwow/game/spell_runtime_values.h"

#include <cstdint>

namespace openwow::data::dbc {
struct SpellEntry;
}

namespace openwow::game {

class CGUnit_C;
class WorldSession;

int HandleSpellDelayPacket(WorldSession& session, std::uintptr_t a1,
                           std::uintptr_t a2, std::uintptr_t a3,
                           std::uintptr_t data_store);
int HandleChannelStartPacket(const WorldSession& session, std::uintptr_t a1,
                             std::uintptr_t a2, std::uintptr_t a3,
                             std::uintptr_t data_store);
int HandleChannelUpdatePacket(WorldSession& session, std::uintptr_t a1,
                              std::uintptr_t a2, std::uintptr_t a3,
                              std::uintptr_t data_store);
[[nodiscard]] ChannelUpdateTransition PrepareChannelUpdate(
    CGUnit_C& unit, const data::dbc::SpellEntry& spell,
    std::uint64_t caster_guid, std::int32_t time_remaining,
    std::uint32_t current_time);
void CompleteChannelUpdate(WorldSession& session, CGUnit_C& unit,
                           std::uint32_t spell_id,
                           ChannelUpdateTransition transition);

}

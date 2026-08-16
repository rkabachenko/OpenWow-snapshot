#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace openwow::game {

class WorldSession;
class CGUnit_C;
struct SpellGoVisualData;
struct DestLocSpellCast;

void QueueSpellStartVisual(WorldSession& session, ObjectGuid caster,
                           std::uint32_t spell_id);
void QueueSpellStopVisual(WorldSession& session, ObjectGuid caster,
                          std::uint32_t spell_id);

void QueueSpellGoVisual(
    WorldSession& session, ObjectGuid caster, std::uint32_t spell_id,
    std::span<const ObjectGuid> hit_targets,
    const std::optional<std::array<float, 3>>& destination,
    const SpellGoVisualData& visual_data);

void QueueChannelStartVisual(WorldSession& session, ObjectGuid caster,
                             std::uint32_t spell_id);
void QueueChannelStopVisual(WorldSession& session, ObjectGuid caster,
                            std::uint32_t spell_id);

void QueueDestLocSpellCastVisual(WorldSession& session,
                                 const DestLocSpellCast& record);

}

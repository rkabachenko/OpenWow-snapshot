#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_target_validation.h"

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
struct SpellVisualEntry;
}

namespace openwow::game {

class CGItem_C;
class CGObject_C;
class CGPlayer_C;
class CGUnit_C;
class ItemDefinitions;
class ObjectManager;
class PlayerInventoryReplica;
class SpellCastRuntime;
class WorldSession;
enum class SpellHelpfulHarmfulDisposition : std::uint8_t;
struct Position;
struct SpellGroundClickData;

[[nodiscard]] std::optional<SpellTargetRangeWindow>
QueryActiveGroundClickRangeWindow(const WorldSession& session,
                                  const SpellCastRuntime& spells);

[[nodiscard]] Position ResolveGroundClickWorldPosition(
    const WorldSession& session, const SpellGroundClickData& click);

[[nodiscard]] bool HandleSpellVisualTrigger(
    WorldSession& session, std::uint64_t guid, std::uint32_t kit_id);

[[nodiscard]] bool HandleSpellVisualTriggerWithTarget(
    WorldSession& session, std::uint64_t guid, std::uint32_t kit_id);

bool BuildSpellTooltipHasModifier(const WorldSession& session,
                                   std::uintptr_t spell_rec,
                                   std::uintptr_t modifier_type);

bool HasNonStanceShapeshiftEffect(const data::dbc::SpellEntry& spell,
                                  const data::dbc::DbcLoader& dbc);

bool CanStopChanneling(const WorldSession& session);

void ProcessSpellMissileEffects(
    WorldSession& session, std::uintptr_t caster, std::uintptr_t spell_entry,
    std::uintptr_t spell_rec, std::uintptr_t visual_rec,
    std::uintptr_t visual_kit, std::uintptr_t target_list,
    float travel_time_seconds);

void InitTargetPointArray(std::uintptr_t array_ptr, std::uintptr_t data,
                           std::uint32_t count, bool zero_fill);

void ResizeTargetPointArray(std::uintptr_t array_ptr, std::uint32_t new_count);

bool CheckCooldownStartsOnEvent(const WorldSession& session,
                                 const data::dbc::SpellEntry& spell,
                                 ObjectGuid item_guid);

SpellHelpfulHarmfulDisposition GetHelpfulHarmfulDisposition(
    const data::dbc::SpellEntry& spell);

[[nodiscard]] int ComputeReagentCastCount(
    const PlayerInventoryReplica& inventory,
    const data::dbc::SpellEntry& spell);

[[nodiscard]] int GetReagentCastCount(const WorldSession& session,
                                      std::uint32_t spell_id);

bool CursorRequiresUnitTarget(const SpellCastRuntime& spells);

bool CursorSupportsAutoTarget(const SpellCastRuntime& spells);

bool CursorHasAreaTargetFlag(const SpellCastRuntime& spells);

[[nodiscard]] bool ActiveTargetingSpellAllowsTapped(
    const WorldSession& session, const SpellCastRuntime& spells);

}

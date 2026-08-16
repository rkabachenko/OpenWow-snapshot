#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_runtime_values.h"

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
class WorldSession;
enum class SpellCastResult : std::uint8_t;

bool IsTargetInRange(const WorldSession& session, const CGObject_C& target,
                     std::uint32_t spell_id,
                     bool* out_of_range, const CGUnit_C& caster);

bool HasActiveInterruptibleCast(const WorldSession& session,
                                const ObjectGuid& caster_guid);

bool IsValidSpellTarget(const WorldSession& session,
                         std::uintptr_t spell_rec,
                         std::uintptr_t target_unit,
                         std::uint32_t target_mask, std::uintptr_t caster);

SpellGroundClickValidation ValidateSpellGroundClickPosition(
    const class WorldSession& session,
    const SpellGroundClickData& click,
    bool allow_range_validation);

void UseSpellAction(const WorldSession& session, std::uintptr_t action_data,
                    std::uint32_t spell_id);

[[nodiscard]] bool CGUnit_C__HasCompatibleStunAura(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out);

[[nodiscard]] bool CGUnit_C__HasCompatiblePacifyAura(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out);

[[nodiscard]] bool CGUnit_C__HasCompatibleSilenceAura(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out);

[[nodiscard]] bool CGUnit_C__HasCompatibleFearAura(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out);

[[nodiscard]] bool CGUnit_C__HasCompatibleConfuseAura(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out);

[[nodiscard]] bool CGUnit_C__HasCompatibleCharmAura(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t* blocking_mechanic_out);

[[nodiscard]] SpellCastResult ValidateSpellCasterControlState(
    const CGUnit_C& caster,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry& spell,
    const ObjectGuid& active_player_guid,
    std::uint32_t* blocking_mechanic_out = nullptr);

bool ValidateAllTargets(const WorldSession& session,
                         std::uintptr_t caster, std::uintptr_t spell_rec,
                         std::uint32_t target_count,
                         std::uintptr_t target_list,
                         std::uint32_t* out_error, bool has_item_target);

namespace detail {

[[nodiscard]] const std::optional<SpellActionInvocation> &LastUseSpellActionInvocation();

void ClearLastUseSpellActionInvocation();

[[nodiscard]] std::uint32_t GetLastCastFailureReasonForTests();

void SetLastCastFailureReasonForTests(std::uint32_t reason);

}

}

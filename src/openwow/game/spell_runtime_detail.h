#pragma once

#include "openwow/game/spell_runtime_values.h"

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
}

namespace openwow::game {

class CGObject_C;
class CGUnit_C;
class SpellCastRuntime;
class WorldSession;

inline constexpr std::uint32_t kMeleeAttackCastFailureReasonResetSentinel = 187;
inline constexpr std::uint32_t kSpellEffectSummon = 28;
inline constexpr std::uint32_t kSpellEffectTradeSkill = 47;
inline constexpr std::uint32_t kSpellEffectAttack = 78;

std::uint32_t ResolveActiveTargetingSpellId(const SpellCastRuntime& runtime);
bool WriteTargetRangeWindow(const WorldSession& session, std::uint32_t spell_id,
                            const CGUnit_C& caster,
                            const CGObject_C* pending_target, float* out_min,
                            float* out_max);
bool IsCompanionSpellRecord(const data::dbc::SpellEntry& spell,
                            const data::dbc::DbcLoader& dbc_loader);
bool HasMatchingVehicleOverrideState(const data::dbc::SpellEntry& spell,
                                     const CGUnit_C& unit);
bool HasActiveMatchingAura(const data::dbc::SpellEntry& spell,
                           const CGUnit_C& unit);
bool HasEquivalentMountedAura(const data::dbc::SpellEntry& spell,
                              const data::dbc::DbcLoader& dbc_loader,
                              const CGUnit_C& unit);

}

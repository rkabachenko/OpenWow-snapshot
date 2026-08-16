#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/spell_public_values.h"
#include "openwow/game/spell_runtime_values.h"

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
struct SpellVisualEntry;
}

namespace openwow::game {

class CGUnit_C;

[[nodiscard]] bool HasMatchingAllowOnlyAbilityAuraForSpell(
    const CGUnit_C& unit,
    const data::dbc::SpellEntry& spell);

[[nodiscard]] bool HasMatchingIgnoreShapeshiftAuraForSpell(
    const CGUnit_C& unit,
    const data::dbc::SpellEntry& spell);

class CGItem_C;
class CGObject_C;
class CGPlayer_C;
class ItemDefinitions;
class ObjectManager;
class PlayerInventoryReplica;
class WorldSession;

int SendCastSpell(WorldSession& session, std::uintptr_t spell_entry,
                  bool skip_visual);

struct SpellRequirementValidation {
  SpellCastResult result = SpellCastResult::kSuccess;
  std::int32_t extra1 = -1;
  std::int32_t extra2 = -1;

  [[nodiscard]] bool IsSuccess() const {
    return result == SpellCastResult::kSuccess;
  }
};

[[nodiscard]] SpellRequirementValidation ValidateSpellRequirementsDetailed(
    const WorldSession& session, std::uintptr_t caster,
    std::uintptr_t spell_rec, std::uintptr_t target_guid_ptr,
    std::uintptr_t item_template, bool force);

bool ValidateSpellRequirements(const WorldSession& session,
                                std::uintptr_t caster,
                                std::uintptr_t spell_rec,
                                std::uintptr_t target_guid_ptr,
                                std::uintptr_t item_template, bool force);

bool TryAssignTarget(WorldSession& session, std::uintptr_t target_unit,
                     std::uintptr_t spell_entry);

}

#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
struct SpellRangeEntry;
}

namespace openwow::game {

class CGUnit_C;
class CGObject_C;
class WorldSession;
enum class SpellCastResult : std::uint8_t;

enum class SpellTargetResult : std::uint8_t {
  kValid = 0,
  kInvalidTarget,
  kTargetIsDead,
  kTargetIsAlive,
  kTargetIsFriendly,
  kTargetIsHostile,
  kTargetNotInParty,
  kTargetNotInRaid,
  kTargetNotPlayer,
  kTargetNotNpc,
  kOutOfRange,
  kTooClose,

  kWrongCreatureType,
  kSelfOnly,
  kTargetImmune,
  kTargetNotSelf,
};

struct SpellUnitTargetResolution {
  SpellTargetResult result = SpellTargetResult::kInvalidTarget;
  ObjectGuid resolved_target;
  std::uint32_t consumed_target_mask = 0;
  std::uint32_t packet_target_mask = 0;

  [[nodiscard]] bool IsValid() const {
    return result == SpellTargetResult::kValid;
  }
};

[[nodiscard]] const char* SpellTargetResultToString(SpellTargetResult r);

[[nodiscard]] SpellCastResult SpellTargetResultToCastResult(
    SpellTargetResult result);

enum class UnitRelation : std::uint8_t {
  kFriendly = 0,
  kHostile  = 1,
  kNeutral  = 2,
};

struct UnitTargetInfo {
  ObjectGuid guid;
  bool is_dead          = false;
  bool is_player        = true;
  bool is_self          = false;
  bool is_in_party      = false;
  bool is_in_raid       = false;
  bool is_immune        = false;
  UnitRelation relation = UnitRelation::kFriendly;
  std::uint32_t creature_type = 0;
  float distance        = 0.0f;

};

struct SpellTargetRangeWindow {
  float min_range = 0.0f;
  float max_range = 0.0f;
};

struct SpellTargetRequirements {
  std::uint32_t spell_id = 0;
  std::uint32_t target_mask = 0;
  std::uint32_t attributes_ex = 0;
  std::uint32_t attributes_ex2 = 0;
  std::uint32_t attributes_ex4 = 0;
  std::uint32_t attributes_ex5 = 0;
  std::uint32_t attributes_ex6 = 0;

  bool self_only             = false;
  bool targets_friendly      = false;
  bool targets_hostile       = false;
  bool targets_dead          = false;
  bool targets_alive         = true;
  bool requires_party        = false;
  bool requires_raid         = false;
  bool requires_player       = false;
  bool requires_npc          = false;

  std::uint32_t creature_type_mask = 0;

  float min_range = 0.0f;
  float max_range = 0.0f;
};

class SpellTargetValidator {
 public:

  [[nodiscard]] static SpellTargetResult Validate(
      const SpellTargetRequirements& req,
      const UnitTargetInfo& target);

  [[nodiscard]] static SpellTargetResult ValidateRelation(
      const SpellTargetRequirements& req,
      const UnitTargetInfo& target);

  [[nodiscard]] static SpellTargetResult ValidateAliveState(
      const SpellTargetRequirements& req,
      const UnitTargetInfo& target);

  [[nodiscard]] static SpellTargetResult ValidateRange(
      const SpellTargetRequirements& req,
      const UnitTargetInfo& target);

  [[nodiscard]] static SpellTargetResult ValidateCreatureType(
      const SpellTargetRequirements& req,
      const UnitTargetInfo& target);

  [[nodiscard]] static std::uint32_t GetSpellTargetCreatureTypeId(
      const WorldSession& session,
      const CGUnit_C& target);

  [[nodiscard]] static SpellTargetRangeWindow GetTargetRangeWindow(
      const data::dbc::SpellEntry& spell,
      const data::dbc::SpellRangeEntry* range_entry,
      const CGUnit_C& caster,
      const CGUnit_C& target,
      bool use_friendly_range,
      const WorldSession* session = nullptr);

  [[nodiscard]] static SpellTargetRangeWindow GetUntargetedRangeWindow(
      const data::dbc::SpellEntry& spell,
      const data::dbc::SpellRangeEntry* range_entry,
      const CGUnit_C& caster,
      bool use_friendly_range,
      const WorldSession* session = nullptr);

  [[nodiscard]] static bool IsTargetInRange(
      const CGObject_C& caster,
      const CGObject_C& target,
      const SpellTargetRangeWindow& window,
      bool* out_of_range = nullptr);

  [[nodiscard]] static std::uint32_t BuildTargetMask(
      const data::dbc::SpellEntry& spell,
      const data::dbc::DbcLoader* dbc = nullptr);

  [[nodiscard]] static SpellTargetRequirements BuildRequirements(
      const data::dbc::SpellEntry& spell,
      bool use_friendly_range,
      float min_range,
      float max_range);

  [[nodiscard]] static SpellTargetResult ValidateUnitTarget(
      const WorldSession& session,
      const data::dbc::DbcLoader& dbc,
      std::uint32_t spell_id,
      const CGUnit_C& caster,
      const CGUnit_C& target,
      bool check_range = true);

  [[nodiscard]] static SpellUnitTargetResolution ResolveUnitTarget(
      const WorldSession& session,
      const data::dbc::DbcLoader& dbc,
      std::uint32_t spell_id,
      const CGUnit_C& caster,
      const CGUnit_C& target,
      std::uint32_t target_mask,
      bool check_range = true);

  [[nodiscard]] static SpellTargetResult ValidateUnitTarget(
      const WorldSession& session,
      const data::dbc::DbcLoader& dbc,
      std::uint32_t spell_id,
      const CGUnit_C& target,
      bool check_range = true);

  [[nodiscard]] static SpellTargetRequirements BuildRequirements(
      std::uint32_t spell_id,
      std::uint32_t attributes,
      std::uint32_t attributes_ex,
      std::uint32_t attributes_ex2,
      std::uint32_t targets,
      std::uint32_t target_creature_type,
      float min_range,
      float max_range);

  [[nodiscard]] static bool CanAssistSpellTarget(
      const CGUnit_C& caster,
      const CGUnit_C& target,
      const WorldSession& session,
      bool use_alt_check = false);

  [[nodiscard]] static bool CanAttackSpellTarget(
      const CGUnit_C& caster,
      const CGUnit_C& target,
      const WorldSession& session);

  [[nodiscard]] static const CGUnit_C* ResolveRedirectedTarget(
      const data::dbc::SpellEntry* spell,
      std::uint32_t target_mask,
      const CGUnit_C& target,
      const CGUnit_C& caster,
      const WorldSession& session);
};

}

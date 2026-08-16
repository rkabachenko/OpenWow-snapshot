#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/objects/cgunit.h"

#include <array>
#include <cstdint>
#include <vector>

namespace openwow::data::dbc {
struct SpellEntry;
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

enum class AuraStackingRule : std::uint8_t {
  kDefault          = 0,
  kExclusive        = 1,
  kExclusiveSameCaster  = 2,
  kExclusiveSameEffect  = 3,
  kCrowdControl     = 4,
};

enum class AuraApplyResult : std::uint8_t {
  kSuccess           = 0,
  kAlreadyApplied    = 1,
  kExclusiveConflict = 2,
  kImmunity          = 3,
  kMissed            = 4,
  kResisted          = 5,
  kStackFull         = 6,
  kInvalidTarget     = 7,
};

struct AuraApplicationRequest {
  std::uint32_t spell_id{0};
  ObjectGuid caster_guid;
  std::uint8_t stack_count{1};
  std::int32_t duration{-1};
  std::int32_t remaining{-1};
  std::array<std::int32_t, 3> effect_amounts{};
  bool is_harmful{false};
};

struct AuraApplicationState {
  std::uint32_t spell_id{0};
  ObjectGuid caster_guid;
  std::uint8_t current_stacks{0};
  std::int32_t duration{-1};
  std::int32_t remaining{-1};
  std::array<std::int32_t, 3> effect_amounts{};
  AuraStackingRule stacking_rule{AuraStackingRule::kDefault};
  bool is_harmful{false};
  bool active{false};
};

inline constexpr std::uint32_t kAuraTypeNone                = 0;
inline constexpr std::uint32_t kAuraTypeBindSight           = 1;
inline constexpr std::uint32_t kAuraTypeModPossessPet       = 2;
inline constexpr std::uint32_t kAuraTypePeriodicDamage      = 3;
inline constexpr std::uint32_t kAuraTypeDummy               = 4;
inline constexpr std::uint32_t kAuraTypeModConfuse          = 5;
inline constexpr std::uint32_t kAuraTypeModCharm            = 6;
inline constexpr std::uint32_t kAuraTypeModFear             = 7;
inline constexpr std::uint32_t kAuraTypePeriodicHeal        = 8;
inline constexpr std::uint32_t kAuraTypeModAttackSpeed      = 9;
inline constexpr std::uint32_t kAuraTypeModThreat           = 10;
inline constexpr std::uint32_t kAuraTypeModTaunt            = 11;
inline constexpr std::uint32_t kAuraTypeModStun             = 12;
inline constexpr std::uint32_t kAuraTypeModDamageDone       = 13;
inline constexpr std::uint32_t kAuraTypeModDamageTaken      = 14;
inline constexpr std::uint32_t kAuraTypeDamageShield        = 15;
inline constexpr std::uint32_t kAuraTypeModStealth          = 16;
inline constexpr std::uint32_t kAuraTypeModDetect           = 17;
inline constexpr std::uint32_t kAuraTypeModInvisibility     = 18;
inline constexpr std::uint32_t kAuraTypeModParrySkill       = 19;
inline constexpr std::uint32_t kAuraTypeModBlockSkill       = 20;
inline constexpr std::uint32_t kAuraTypeModHitChance        = 21;
inline constexpr std::uint32_t kAuraTypePeriodicTriggerSpell = 22;
inline constexpr std::uint32_t kAuraTypeModCritPercent      = 23;
inline constexpr std::uint32_t kAuraTypeModSpellCritPercent = 24;
inline constexpr std::uint32_t kAuraTypeModSilence          = 25;
inline constexpr std::uint32_t kAuraTypeModCastingSpeed     = 26;
inline constexpr std::uint32_t kAuraTypeModPacify           = 27;
inline constexpr std::uint32_t kAuraTypeModPacifySilence    = 60;
inline constexpr std::uint32_t kAuraTypePeriodicManaLeech   = 29;
inline constexpr std::uint32_t kAuraTypeModCritDmgPercent   = 31;
inline constexpr std::uint32_t kAuraTypeModRangedAttackPower = 32;
inline constexpr std::uint32_t kAuraTypeModMeleeAttackPower  = 33;
inline constexpr std::uint32_t kAuraTypeModResistance        = 34;
inline constexpr std::uint32_t kAuraTypeModSkill             = 35;
inline constexpr std::uint32_t kAuraTypeModShapeshift        = 36;
inline constexpr std::uint32_t kAuraTypeModImmunity          = 37;
inline constexpr std::uint32_t kAuraTypeSchoolAbsorb        = 38;
inline constexpr std::uint32_t kAuraTypeModProcTrigger       = 40;
inline constexpr std::uint32_t kAuraTypeModPowerRegen        = 41;
inline constexpr std::uint32_t kAuraTypeModDrainPower        = 42;
inline constexpr std::uint32_t kAuraTypeModDamagePercent     = 43;
inline constexpr std::uint32_t kAuraTypeModHitChancePct      = 45;
inline constexpr std::uint32_t kAuraTypeAddCasterHitChance   = 48;
inline constexpr std::uint32_t kAuraTypeModInvisibilityDetect = 49;

class AuraApplication {
 public:
  AuraApplication() = default;
  ~AuraApplication() = default;

  static AuraApplication& Get();

  [[nodiscard]] AuraApplyResult CanApplyAura(
      const CGUnit_C& target,
      const data::dbc::SpellEntry& spell,
      const AuraApplicationRequest& request,
      const data::dbc::DbcLoader& dbc,
      std::uint32_t* out_reason = nullptr) const;

  AuraApplyResult ApplyAura(
      WorldSession& session, CGUnit_C& unit,
      const data::dbc::SpellEntry& spell,
      const AuraApplicationRequest& request,
      const data::dbc::DbcLoader& dbc);

  bool RemoveAura(CGUnit_C& unit, std::uint32_t spell_id);

  [[nodiscard]] static AuraStackingRule ResolveStackingRule(
      const data::dbc::SpellEntry& spell,
      std::size_t effect_index);

  [[nodiscard]] bool HasStackingConflict(
      const CGUnit_C& target,
      const data::dbc::SpellEntry& spell,
      const AuraApplicationRequest& request,
      const data::dbc::DbcLoader& dbc) const;

  [[nodiscard]] static bool IsUnitImmuneToAuraType(
      const CGUnit_C& unit,
      std::uint32_t aura_type);

  [[nodiscard]] static bool IsHarmfulAura(
      const data::dbc::SpellEntry& spell);

  [[nodiscard]] static std::int32_t GetSpellDuration(
      const data::dbc::SpellEntry& spell,
      const data::dbc::DbcLoader& dbc,
      std::uint32_t caster_level = 0);

  void Clear();

 private:

  struct PendingAuraChange {
    std::uint32_t spell_id{0};
    ObjectGuid target_guid;
    bool applied{false};
  };

  std::vector<PendingAuraChange> pending_changes_;

  void ApplyEffectAura(WorldSession& session, CGUnit_C& unit,
                       const data::dbc::SpellEntry& spell,
                       std::size_t effect_index,
                       std::int32_t base_points);

  void RemoveEffectAura(CGUnit_C& unit,
                        const data::dbc::SpellEntry& spell,
                        std::size_t effect_index);
};

}

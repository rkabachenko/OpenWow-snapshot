
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include "openwow/game/combat_log.h"
#include "openwow/game/object_guid.h"

namespace openwow::game {

namespace ThreatStanceMultiplier {
inline constexpr float kBattleStance      = 1.00f;
inline constexpr float kDefensiveStance   = 1.30f;
inline constexpr float kBerserkerStance   = 0.80f;
inline constexpr float kBearForm          = 1.30f;
inline constexpr float kCatForm           = 1.00f;
inline constexpr float kMoonkinForm       = 1.00f;
inline constexpr float kPaladinRighteousFury = 1.60f;
inline constexpr float kDeathKnightFrostPresence = 1.80f;
inline constexpr float kDeathKnightBloodPresence  = 0.80f;
inline constexpr float kNormal            = 1.00f;
}

namespace ThreatTalentBonus {
inline constexpr float kDefiance          = 0.15f;
inline constexpr float kFeralInstinct     = 0.15f;
inline constexpr float kSavageDefense     = 0.05f;
inline constexpr float kImprovedRighteousFury = 0.50f;
inline constexpr float kFrostPresenceMastery   = 0.10f;
inline constexpr float kBurningAdrenaline     = 0.50f;
inline constexpr float kMisdirection          = 1.00f;
inline constexpr float kTricksOfTheTrade      = 1.00f;
inline constexpr float kVigilance             = 1.00f;
}

namespace ThreatDecayRate {
inline constexpr float kMeleeRange      = 0.00f;
inline constexpr float kRangedDecay     = 0.05f;
inline constexpr float kMaxRangeDecay   = 0.20f;
inline constexpr float kThreatWipeInterval = 5.0f;
}

namespace AggroThreshold {
inline constexpr float kMeleeAggro    = 1.10f;
inline constexpr float kRangedAggro   = 1.30f;
}

enum class ThreatModifierSource : std::uint8_t {
  Stance,
  Talent,
  Buff,
  Debuff,
  Aura,
  SpellEffect,
  ItemSetBonus,
  Enchant,
};

struct ThreatModifier {
  ThreatModifierSource source{ThreatModifierSource::Aura};
  std::uint32_t spell_id{0};
  float multiplier{1.0f};
  float additive{0.0f};
  bool is_active{true};
  bool applies_to_all{true};
  std::uint32_t school_mask{0};
  float duration_remaining{-1.0f};
};

struct SimThreatEntry {
  ObjectGuid unit_guid;
  ObjectGuid target_guid;
  float threat_value{0.0f};
  float base_threat{0.0f};
  float modified_threat{0.0f};
  bool is_taunted{false};
  float taunt_until{0.0f};

  float last_update_time{0.0f};
  bool in_melee_range{true};
};

struct CombatLogEntryBuildResult {
  CombatLogEntry entry;
  bool success{false};
};

struct RawDamageEvent {
  ObjectGuid source;
  ObjectGuid target;
  std::uint32_t spell_id{0};
  std::string spell_name;
  std::uint32_t school_mask{0};
  std::int32_t amount{0};
  std::int32_t overkill{0};
  std::int32_t resisted{0};
  std::int32_t blocked{0};
  std::int32_t absorbed{0};
  bool critical{false};
  bool glancing{false};
  bool crushing{false};
};

struct RawHealEvent {
  ObjectGuid source;
  ObjectGuid target;
  std::uint32_t spell_id{0};
  std::string spell_name;
  std::int32_t amount{0};
  std::int32_t overheal{0};
  std::int32_t absorbed{0};
  bool critical{false};
};

struct RawEnergizeEvent {
  ObjectGuid source;
  ObjectGuid target;
  std::uint32_t spell_id{0};
  std::string spell_name;
  std::uint32_t power_type{0};
  std::int32_t amount{0};
  bool periodic{false};
};

struct RawMissEvent {
  ObjectGuid source;
  ObjectGuid target;
  std::uint32_t spell_id{0};
  std::string spell_name;
  std::string miss_type;
  bool is_periodic{false};
  bool is_swing{false};
};

struct RawEnvironmentalEvent {
  ObjectGuid target;
  std::uint8_t env_type{0};
  std::int32_t amount{0};
  std::int32_t absorbed{0};
  std::int32_t resisted{0};
};

struct RawDamageShieldEvent {
  ObjectGuid caster;
  ObjectGuid attacker;
  std::uint32_t spell_id{0};
  std::string spell_name;
  std::int32_t damage{0};
  std::int32_t absorbed{0};
  std::int32_t resisted{0};
};

struct RawDamageSplitEvent {
  ObjectGuid source;
  ObjectGuid target;
  std::uint32_t spell_id{0};
  std::string spell_name;
  std::int32_t damage{0};
  std::int32_t overkill{0};
  std::uint32_t school_mask{0};
  std::int32_t absorbed{0};
  std::int32_t resisted{0};
  std::int32_t blocked{0};
  bool critical{false};
};

class UnitCombatLog {
 public:
  static UnitCombatLog& Get();

  void AddThreat(const ObjectGuid& unit_guid, const ObjectGuid& target_guid,
                 float base_threat, std::uint32_t school_mask = 0);

  void RemoveThreat(const ObjectGuid& unit_guid, const ObjectGuid& target_guid);

  void RemoveUnitFromAllThreats(const ObjectGuid& unit_guid);

  void ModifyThreat(const ObjectGuid& unit_guid, const ObjectGuid& target_guid,
                    float delta);

  [[nodiscard]] float GetThreat(const ObjectGuid& unit_guid,
                                const ObjectGuid& target_guid) const;

  [[nodiscard]] float GetThreatPercent(const ObjectGuid& unit_guid,
                                       const ObjectGuid& target_guid) const;

  [[nodiscard]] ObjectGuid GetHighestThreatUnit(const ObjectGuid& target_guid) const;

  [[nodiscard]] float GetHighestThreatValue(const ObjectGuid& target_guid) const;

  [[nodiscard]] bool IsHighestThreat(const ObjectGuid& unit_guid,
                                     const ObjectGuid& target_guid) const;

  [[nodiscard]] std::vector<SimThreatEntry> GetThreatList(
      const ObjectGuid& target_guid) const;

  void ClearThreatTable(const ObjectGuid& target_guid);

  void ClearAllThreatTables();

  std::uint32_t AddThreatModifier(ThreatModifier modifier);

  void RemoveThreatModifier(std::uint32_t modifier_id);

  void RemoveThreatModifiersBySpell(std::uint32_t spell_id);

  void ClearThreatModifiers();

  void SetStance(const ObjectGuid& unit_guid, float stance_multiplier);

  void ApplyTalentBonus(const ObjectGuid& unit_guid, float bonus_multiplier);

  [[nodiscard]] float GetEffectiveThreatMultiplier(
      const ObjectGuid& unit_guid, std::uint32_t school_mask = 0) const;

  void UpdateThreatDecay(float delta_time, float game_time);

  void SetInMeleeRange(const ObjectGuid& unit_guid, const ObjectGuid& target_guid,
                       bool in_melee);

  void SetDecayRate(float rate) { decay_rate_ = rate; }

  void ApplyTaunt(const ObjectGuid& unit_guid, const ObjectGuid& target_guid,
                  float duration_seconds, float bonus = 0.0f);

  void RemoveTaunt(const ObjectGuid& unit_guid, const ObjectGuid& target_guid);

  [[nodiscard]] bool IsTaunting(const ObjectGuid& unit_guid,
                                const ObjectGuid& target_guid) const;

  [[nodiscard]] CombatLogEntry BuildSpellDamageEntry(
      const RawDamageEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildSpellMissEntry(
      const RawMissEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildSpellHealEntry(
      const RawHealEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildSpellEnergizeEntry(
      const RawEnergizeEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildSwingDamageEntry(
      const RawDamageEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildSwingMissEntry(
      const RawMissEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildRangeDamageEntry(
      const RawDamageEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildRangeMissEntry(
      const RawMissEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildPeriodicDamageEntry(
      const RawDamageEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildPeriodicHealEntry(
      const RawHealEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildPeriodicEnergizeEntry(
      const RawEnergizeEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildPeriodicMissEntry(
      const RawMissEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildDamageShieldEntry(
      const RawDamageShieldEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildDamageSplitEntry(
      const RawDamageSplitEvent& event) const;

  [[nodiscard]] CombatLogEntry BuildEnvironmentalDamageEntry(
      const RawEnvironmentalEvent& event) const;

  void AppendSpellDamage(CombatLog& log, const RawDamageEvent& event);
  void AppendSpellMiss(CombatLog& log, const RawMissEvent& event);
  void AppendSpellHeal(CombatLog& log, const RawHealEvent& event);
  void AppendSpellEnergize(CombatLog& log, const RawEnergizeEvent& event);
  void AppendSwingDamage(CombatLog& log, const RawDamageEvent& event);
  void AppendSwingMiss(CombatLog& log, const RawMissEvent& event);
  void AppendRangeDamage(CombatLog& log, const RawDamageEvent& event);
  void AppendRangeMiss(CombatLog& log, const RawMissEvent& event);
  void AppendPeriodicDamage(CombatLog& log, const RawDamageEvent& event);
  void AppendPeriodicHeal(CombatLog& log, const RawHealEvent& event);
  void AppendPeriodicEnergize(CombatLog& log, const RawEnergizeEvent& event);
  void AppendPeriodicMiss(CombatLog& log, const RawMissEvent& event);
  void AppendDamageShield(CombatLog& log, const RawDamageShieldEvent& event);
  void AppendDamageSplit(CombatLog& log, const RawDamageSplitEvent& event);
  void AppendEnvironmentalDamage(CombatLog& log,
                                 const RawEnvironmentalEvent& event);

  void EnterCombat(const ObjectGuid& unit_guid);

  void LeaveCombat(const ObjectGuid& unit_guid);

  [[nodiscard]] bool IsInCombat(const ObjectGuid& unit_guid) const;

  void Update(float delta_time, float game_time);

  void Reset();

  [[nodiscard]] std::size_t GetEntryCount() const;

 private:
  UnitCombatLog() = default;

  SimThreatEntry* FindEntry(const ObjectGuid& unit_guid,
                            const ObjectGuid& target_guid);

  const SimThreatEntry* FindEntry(const ObjectGuid& unit_guid,
                                  const ObjectGuid& target_guid) const;

  SimThreatEntry& FindOrCreateEntry(const ObjectGuid& unit_guid,
                                    const ObjectGuid& target_guid);

  float ApplyModifiers(float base_threat, std::uint32_t school_mask) const;

  [[nodiscard]] float GetTime() const { return game_time_; }

  void PopulateSourceDest(CombatLogEntry& entry,
                          const ObjectGuid& source,
                          const ObjectGuid& dest) const;

  std::unordered_map<std::uint64_t, std::vector<SimThreatEntry>> threat_tables_;
  mutable std::mutex mutex_;

  std::unordered_map<std::uint32_t, ThreatModifier> threat_modifiers_;
  std::uint32_t next_modifier_id_{1};
  float game_time_{0.0f};
  float decay_rate_{1.0f};

  std::unordered_map<std::uint64_t, float> stances_;

  std::unordered_map<std::uint64_t, float> taunt_expirations_;
};

}

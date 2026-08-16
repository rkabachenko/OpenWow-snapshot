#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
struct SpellEntry;
struct SpellItemEnchantmentEntry;
}

namespace openwow::game {

enum class ProcTriggerFlag : std::uint32_t {
  kNone                        = 0x00000000,
  kOnKillTargetOrAssist        = 0x00000001,
  kOnSuccessfulMeleeAttack     = 0x00000002,
  kOnTakeDamageFromMeleeAttack = 0x00000004,
  kOnSuccessfulRangedAttack    = 0x00000008,
  kOnSpellHitHarmful           = 0x00000010,
  kOnSpellHitHelpful           = 0x00000020,
  kOnMeleeAttackCrit           = 0x00000040,
  kOnRangedAttackCrit          = 0x00000080,
  kOnSpellCritHarmful          = 0x00000100,
  kOnSpellCritHelpful          = 0x00000200,
  kOnDodge                     = 0x00000400,
  kOnParry                     = 0x00000800,
  kOnBlock                     = 0x00001000,
  kOnTakeDamageFromAny         = 0x00002000,
  kOnCastSpell                 = 0x00004000,
  kOnAutoShot                  = 0x00008000,
  kOnBlockVictim               = 0x00010000,
  kOnDamageDone                = 0x00020000,
  kOnHealDone                  = 0x00040000,
  kOnTrapTriggered             = 0x00080000,
  kOnMeleeHit                  = 0x00100000,
  kOnRangedHit                 = 0x00200000,
  kOnSpellMiss                 = 0x00400000,
  kOnSpellResist               = 0x00800000,
};

inline ProcTriggerFlag operator|(ProcTriggerFlag a, ProcTriggerFlag b) {
  return static_cast<ProcTriggerFlag>(
      static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline bool HasProcFlag(ProcTriggerFlag field, ProcTriggerFlag flag) {
  return (static_cast<std::uint32_t>(field) &
          static_cast<std::uint32_t>(flag)) != 0;
}

inline constexpr std::uint32_t kProcFlagHitMask =
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnSuccessfulMeleeAttack) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnSuccessfulRangedAttack) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellHitHarmful) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellHitHelpful) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnMeleeHit) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnRangedHit);

inline constexpr std::uint32_t kProcFlagCritMask =
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnMeleeAttackCrit) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnRangedAttackCrit) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellCritHarmful) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellCritHelpful);

inline constexpr std::uint32_t kProcFlagDefenseMask =
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnDodge) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnParry) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnBlock) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnBlockVictim) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnTakeDamageFromAny) |
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnTakeDamageFromMeleeAttack);

inline constexpr std::uint32_t kProcFlagCastMask =
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnCastSpell);

inline constexpr std::uint32_t kProcFlagHealMask =
    static_cast<std::uint32_t>(ProcTriggerFlag::kOnHealDone);

enum class ProcTriggerType : std::uint8_t {
  kNone = 0,
  kOnHit,
  kOnCrit,
  kOnDodge,
  kOnParry,
  kOnBlock,
  kOnCast,
  kOnHeal,
  kOnDamageTaken,
  kOnSpellMiss,
  kOnSpellResist,
  kOnKill,
};

enum class ProcSource : std::uint8_t {
  kSpell        = 0,
  kEnchantment  = 1,
  kItemUse      = 2,
  kItemEquip    = 3,
  kSetBonus     = 4,
  kTalent       = 5,
};

struct ProcCooldownEntry {
  std::uint32_t spell_id{0};
  std::uint32_t cooldown_ms{0};
  std::uint64_t ready_time_ms{0};
  ProcSource source{ProcSource::kSpell};
};

struct ProcTriggerDescriptor {
  std::uint32_t source_spell_id{0};
  ProcTriggerFlag trigger_flags{ProcTriggerFlag::kNone};
  std::uint32_t proc_chance{0};
  std::uint32_t proc_charges{0};
  std::uint32_t triggered_spell_id{0};
  ProcSource source{ProcSource::kSpell};
  ObjectGuid item_guid;

  std::uint32_t icd_duration_ms{0};
  std::uint64_t icd_ready_time_ms{0};

  std::uint32_t spell_family_name{0};
  std::array<std::uint32_t, 3> spell_family_flags{};
};

struct SetBonusState {
  std::uint32_t set_id{0};
  std::uint32_t items_equipped{0};
  std::uint32_t required_pieces{0};
  std::uint32_t spell_id{0};
  bool active{false};
};

struct TrinketEffectState {
  ObjectGuid item_guid;
  std::uint32_t spell_id{0};
  std::uint32_t cooldown_ms{0};
  std::uint64_t cooldown_ready_ms{0};
  bool on_use{true};
  bool active{false};
};

struct EnchantmentProcState {
  std::uint32_t enchantment_id{0};
  std::uint32_t trigger_spell_id{0};
  std::uint32_t proc_chance{0};
  ProcTriggerFlag trigger_flags{ProcTriggerFlag::kNone};
  std::uint32_t icd_duration_ms{0};
  std::uint64_t icd_ready_ms{0};
  ObjectGuid item_guid;
};

class ProcManager {
 public:
  ProcManager() = default;
  ~ProcManager() = default;

  static ProcManager& Get();

  void RegisterSpellProc(std::uint64_t unit_guid,
                         const ProcTriggerDescriptor& descriptor);

  void RegisterEnchantmentProc(std::uint64_t unit_guid,
                               const EnchantmentProcState& proc);

  void RegisterSetBonus(std::uint64_t unit_guid,
                        const SetBonusState& bonus);

  void RegisterTrinketEffect(std::uint64_t unit_guid,
                             const TrinketEffectState& trinket);

  void UnregisterSpellProcs(std::uint64_t unit_guid,
                            std::uint32_t spell_id);

  void UnregisterEnchantmentProcs(std::uint64_t unit_guid,
                                  std::uint32_t enchantment_id);

  void ClearUnitProcs(std::uint64_t unit_guid);

  bool FireProcs(std::uint64_t unit_guid,
                 ProcTriggerType trigger,
                 std::uint32_t spell_id,
                 std::uint32_t school_mask,
                 std::uint64_t target_guid);

  bool OnHit(std::uint64_t unit_guid,
             std::uint32_t spell_id,
             std::uint32_t school_mask,
             std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnHit,
                     spell_id, school_mask, target_guid);
  }

  bool OnCrit(std::uint64_t unit_guid,
              std::uint32_t spell_id,
              std::uint32_t school_mask,
              std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnCrit,
                     spell_id, school_mask, target_guid);
  }

  bool OnDodge(std::uint64_t unit_guid,
               std::uint32_t spell_id,
               std::uint32_t school_mask,
               std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnDodge,
                     spell_id, school_mask, target_guid);
  }

  bool OnParry(std::uint64_t unit_guid,
               std::uint32_t spell_id,
               std::uint32_t school_mask,
               std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnParry,
                     spell_id, school_mask, target_guid);
  }

  bool OnBlock(std::uint64_t unit_guid,
               std::uint32_t spell_id,
               std::uint32_t school_mask,
               std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnBlock,
                     spell_id, school_mask, target_guid);
  }

  bool OnCast(std::uint64_t unit_guid,
              std::uint32_t spell_id,
              std::uint32_t school_mask,
              std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnCast,
                     spell_id, school_mask, target_guid);
  }

  bool OnHeal(std::uint64_t unit_guid,
              std::uint32_t spell_id,
              std::uint32_t school_mask,
              std::uint64_t target_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnHeal,
                     spell_id, school_mask, target_guid);
  }

  bool OnDamageTaken(std::uint64_t unit_guid,
                     std::uint32_t spell_id,
                     std::uint32_t school_mask,
                     std::uint64_t attacker_guid) {
    return FireProcs(unit_guid, ProcTriggerType::kOnDamageTaken,
                     spell_id, school_mask, attacker_guid);
  }

  [[nodiscard]] bool IsInternalCooldownReady(
      std::uint64_t unit_guid,
      std::uint32_t spell_id) const;

  void StartInternalCooldown(std::uint64_t unit_guid,
                              std::uint32_t spell_id,
                              std::uint32_t duration_ms);

  [[nodiscard]] std::uint32_t GetInternalCooldownRemaining(
      std::uint64_t unit_guid,
      std::uint32_t spell_id) const;

  void UpdateSetBonusStates(std::uint64_t unit_guid,
                             std::uint32_t set_id,
                             std::uint32_t items_equipped);

  [[nodiscard]] bool IsSetBonusActive(std::uint64_t unit_guid,
                                       std::uint32_t spell_id) const;

  bool ActivateTrinketOnUse(std::uint64_t unit_guid,
                            const ObjectGuid& item_guid);

  [[nodiscard]] bool IsTrinketEquipEffectActive(
      std::uint64_t unit_guid,
      const ObjectGuid& item_guid) const;

  void SetEnchantmentProc(std::uint64_t unit_guid,
                          std::uint32_t enchantment_id,
                          std::uint32_t trigger_spell_id,
                          std::uint32_t proc_chance,
                          ProcTriggerFlag flags,
                          std::uint32_t icd_ms,
                          const ObjectGuid& item_guid);

  void RemoveItemProcs(std::uint64_t unit_guid,
                       const ObjectGuid& item_guid);

  [[nodiscard]] static ProcTriggerFlag TypeToFlag(ProcTriggerType type);

  [[nodiscard]] static std::uint32_t TypeToFlagMask(ProcTriggerType type);

  void Clear();

 private:

  std::unordered_map<std::uint64_t,
                     std::vector<ProcTriggerDescriptor>> spell_procs_;

  std::unordered_map<std::uint64_t,
                     std::vector<EnchantmentProcState>> enchantment_procs_;

  std::unordered_map<std::uint64_t,
                     std::vector<SetBonusState>> set_bonuses_;

  std::unordered_map<std::uint64_t,
                     std::vector<TrinketEffectState>> trinket_effects_;

  std::unordered_map<std::uint64_t,
                     std::vector<ProcCooldownEntry>> internal_cooldowns_;

  [[nodiscard]] static bool RollProcChance(std::uint32_t chance);

  [[nodiscard]] static bool MatchesTriggerFlags(
      ProcTriggerFlag trigger_flags,
      ProcTriggerType type,
      bool is_crit);
};

}

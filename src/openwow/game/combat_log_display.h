#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class CombatLogDisplayEventType : uint8_t {
  SpellDamage         = 0,
  SpellHeal           = 1,
  MeleeDamage         = 2,
  SpellMiss           = 3,
  SpellAuraApplied    = 4,
  SpellAuraRemoved    = 5,
  SpellCast           = 6,
  UnitDied            = 7,
  SpellEnergize       = 8,
  SpellDrain          = 9,
  EnvironmentalDamage = 10,
  SpellInterrupt      = 11,
  SpellDispel         = 12,
};

namespace CombatLogFilterFlag {
  inline constexpr uint32_t ShowIncoming    = 1u << 0;
  inline constexpr uint32_t ShowOutgoing    = 1u << 1;
  inline constexpr uint32_t ShowPet         = 1u << 2;
  inline constexpr uint32_t ShowFriendly    = 1u << 3;
  inline constexpr uint32_t ShowHostile     = 1u << 4;
  inline constexpr uint32_t ShowSpellDamage = 1u << 5;
  inline constexpr uint32_t ShowMelee       = 1u << 6;
  inline constexpr uint32_t ShowHealing     = 1u << 7;
  inline constexpr uint32_t ShowAuras       = 1u << 8;
  inline constexpr uint32_t ShowCast        = 1u << 9;
  inline constexpr uint32_t ShowMisc        = 1u << 10;
  inline constexpr uint32_t All             = 0x7FFu;
}

struct CombatLogDisplayEntry {
  CombatLogDisplayEventType type       = CombatLogDisplayEventType::SpellDamage;
  double                    timestamp  = 0.0;
  std::string               sourceName;
  ObjectGuid                sourceGuid;
  std::string               destName;
  ObjectGuid                destGuid;
  uint32_t                  spellId    = 0;
  std::string               spellName;
  int32_t                   amount     = 0;
  int32_t                   overkill   = 0;
  std::string               school;
  bool                      isCritical = false;
};

class CombatLogDisplay {
 public:
  void AddEntry(CombatLogDisplayEntry entry);

  [[nodiscard]] const std::deque<CombatLogDisplayEntry>& GetEntries() const;
  [[nodiscard]] std::vector<const CombatLogDisplayEntry*> GetFilteredEntries(
      uint32_t filterFlags) const;

  void SetMaxEntries(size_t max);
  [[nodiscard]] size_t GetMaxEntries() const;
  [[nodiscard]] size_t GetEntryCount() const;

  [[nodiscard]] static std::string FormatEntry(
      const CombatLogDisplayEntry& entry);

  void SetActiveFilters(uint32_t flags);
  [[nodiscard]] uint32_t GetActiveFilters() const;

  [[nodiscard]] int64_t GetDamageDealt() const;
  [[nodiscard]] int64_t GetHealingDone() const;

  [[nodiscard]] float GetDPS(double windowSeconds) const;
  [[nodiscard]] float GetHPS(double windowSeconds) const;

  void Clear();

 private:
  [[nodiscard]] bool PassesFilter(const CombatLogDisplayEntry& entry,
                                  uint32_t flags) const;

  std::deque<CombatLogDisplayEntry> entries_;
  size_t                            maxEntries_    = 1000;
  uint32_t                          activeFilters_ = CombatLogFilterFlag::All;
};

namespace HitFlags {
inline constexpr int kAbsorb   = 0x00020;
inline constexpr int kResist   = 0x00080;
inline constexpr int kCritical = 0x00200;
inline constexpr int kBlock    = 0x02000;
inline constexpr int kGlancing = 0x10000;
inline constexpr int kCrushing = 0x20000;
}

enum class IndexedUnitCombatSchool : std::uint32_t {
  kMiss      = 0,
  kWound     = 1,
  kDodge     = 2,
  kParry     = 3,
  kInterrupt = 4,
  kBlock     = 5,
  kEvade     = 6,
  kImmune    = 7,
  kDeflect   = 8,
};

[[nodiscard]] int BuildUnitCombatHitFlags(bool is_critical,
                                          bool has_absorb,
                                          bool has_resist,
                                          bool has_block);

void DispatchHitIndicator(const ObjectGuid& unit_guid,
                          const char* damage_school,
                          int amount, int extra_amount, int hit_flags);

void DispatchIndexedUnitCombatEvent(const ObjectGuid& unit_guid,
                                    IndexedUnitCombatSchool school,
                                    int amount,
                                    int extra_amount,
                                    int hit_flags);

void DispatchSpellMissUnitCombatEvent(const ObjectGuid& unit_guid,
                                      std::uint8_t miss_info,
                                      std::uint32_t armor_resistance_mask);

void DispatchWoundEvent(const ObjectGuid& unit_guid,
                        int amount,
                        int extra_amount,
                        int hit_flags);

void DispatchHealEvent(const ObjectGuid& unit_guid,
                       int heal_amount,
                       int absorb_amount,
                       bool is_crit);

}

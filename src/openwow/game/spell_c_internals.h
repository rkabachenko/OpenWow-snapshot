#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
}

namespace openwow::game {

class CGPlayer_C;
class CGUnit_C;
class ObjectManager;
class SpellTargeting;
class WorldSession;

class SpellSchoolMask {
 public:
  [[nodiscard]] static bool IsSchoolAllowed(std::uint8_t school);

  static void SetSchoolLockout(bool active, std::uint32_t allowed_mask = 0);

  static void ClearCastPermitIfGcdTriggered(
      const data::dbc::SpellEntry& spell);

  [[nodiscard]] static bool IsLockoutActive();
  [[nodiscard]] static std::uint32_t GetAllowedMask();

 private:
  static bool s_lockout_active;

  static std::uint32_t s_allowed_mask;

};

class SpellTargetingGlobalState {
 public:
  static void SetTargetingSpellId(std::uint32_t spell_id);

  static void SetTargetingGuid(std::uint64_t guid);

  [[nodiscard]] static std::uint32_t GetTargetingSpellId();
  [[nodiscard]] static std::uint64_t GetTargetingGuid();

  static void Clear();

 private:
  static std::uint32_t s_targeting_spell_id;

  static std::uint64_t s_targeting_guid;

};

class SpellCastGlobalState {
 public:
  static void SetGlobalCastState(std::int32_t value);

  [[nodiscard]] static std::int32_t GetGlobalCastState();

 private:
  static std::int32_t s_global_cast_state;

};

struct SpellEffectTable {
  std::int32_t padding[2];
  std::int32_t count;
  std::int32_t padding2[4];
  std::uint8_t* data;

  static constexpr std::size_t kRecordSize = 76;

  [[nodiscard]] const std::uint8_t* GetEffectByIndex(std::int32_t index) const {
    if (index < 0 || index >= count) return nullptr;
    return data + kRecordSize * index;
  }
};

[[nodiscard]] bool ValidateSpellMovementData(const ObjectManager& objects,
                                              std::uint64_t guid1,
                                              std::uint64_t guid2);

[[nodiscard]] std::uint32_t GetUnitSpellCastTimeDivided(
    const ObjectManager& objects, std::uint32_t spell_id, bool use_pet,
    bool use_target);

[[nodiscard]] std::int32_t GetMinimumSpellPowerBonusForSchoolMask(
    const CGPlayer_C& player,
    std::uint32_t school_mask);

[[nodiscard]] float GetMinimumSpellPowerMultiplierForSchoolMask(
    const CGPlayer_C& player,
    std::uint32_t school_mask);

[[nodiscard]] std::int32_t GetMinimumPowerCostModifierForSchoolMask(
    const CGUnit_C& unit,
    std::uint32_t school_mask);

[[nodiscard]] float GetMinimumPowerCostMultiplierForSchoolMask(
    const CGUnit_C& unit,
    std::uint32_t school_mask);

struct SpellNode {
  ObjectGuid guid{};
  std::uint8_t type{0};
  std::uint32_t flags{0};
  float position[3]{};
  SpellNode* next{nullptr};

  void SetRedirectTarget(const float pos[3]);
};

class SpellNodeList {
 public:
  static void IterateAndUpdate(std::uint8_t type_filter,
                                std::uint32_t update_param);

  static void SetGlobalListHead(std::uintptr_t head);
  [[nodiscard]] static std::uintptr_t GetGlobalListHead();

 private:
  static std::uintptr_t s_global_list_head;

};

[[nodiscard]] bool IsCasterPlayerControlledUnit(
    const ObjectManager& objects, const ObjectGuid& caster_guid);

void CombatText_FireSpellCast(std::uint64_t caster_guid,
                               std::uint32_t spell_attributes_ex6_byte,
                               const char* spell_name);

[[nodiscard]] std::int32_t ComputeCastDuration(
    const WorldSession& session,
    std::uint32_t spell_id,
    bool use_pet,
    bool use_target,
    bool allow_negative);

[[nodiscard]] std::int32_t ComputeSpellDuration(
    const WorldSession& session,
    std::uint32_t spell_id,
    bool use_pet,
    bool use_target,
    bool skip_modifier,
    bool apply_haste);

[[nodiscard]] bool ShouldTargetCorpseForCaster(
    const SpellTargeting& targeting,
    const CGUnit_C* caster,
    const class CGCorpse_C* target_corpse);
[[nodiscard]] bool ShouldTargetCorpse(
    const SpellTargeting& targeting,
    const CGUnit_C& caster,
    const class CGCorpse_C* target_corpse);

[[nodiscard]] float GetPreviewFacingRadians(SpellTargeting& targeting,
                                             const CGPlayer_C& player);

[[nodiscard]] bool ValidateItemLevel(
    std::uint32_t item_effective_level,
    std::uint32_t item_effective_max_level,
    std::uint32_t spell_base_level,
    std::uint32_t spell_max_level,
    bool is_ranged_spell,
    bool is_caster_owner,
    bool bypass_level_check,
    std::uint32_t* out_error);

[[nodiscard]] bool ValidateItemSpellEffects(
    const WorldSession& session, std::uint32_t spell_id,
    std::uint64_t item_guid,
    std::uint32_t* out_error);

[[nodiscard]] bool SpellBookFrame_IsUnitMatchingTarget(
    const ObjectManager& objects, std::uint64_t unit_guid,
    std::uint64_t target_guid);

[[nodiscard]] bool SpellEffect_IsSpellbookDirectCastEffect(
    std::uint32_t effect_id);

[[nodiscard]] bool Spell_HasSpellbookDirectCastEffect(
    const std::uint32_t effect_ids[3]);

[[nodiscard]] bool SpellEffect_IsPetUseSpellActionEffect(
    std::uint32_t effect_id);

[[nodiscard]] bool Spell_HasPetUseSpellActionEffect(
    const std::uint32_t effect_ids[3]);

[[nodiscard]] bool SpellRec_IsNextSwingOrInitiatesCombat(
    const openwow::data::dbc::SpellEntry* spell);

[[nodiscard]] std::span<const std::uint32_t> Spell_GetEffectSpellClassMask(
    const openwow::data::dbc::SpellEntry* spell, std::uint32_t effect_index);

[[nodiscard]] bool Spell_MatchesEffectSpellClassMask(
    const openwow::data::dbc::SpellEntry* spell,
    const openwow::data::dbc::SpellEntry* mask_source_spell,
    std::uint32_t effect_index);

[[nodiscard]] bool Spell_HasAuraType(
    const std::uint32_t effect_apply_aura[3], std::uint32_t aura_type);

[[nodiscard]] bool Spell_HasTrackingAuraType(
    const std::uint32_t effect_apply_aura[3]);

[[nodiscard]] bool Spell_MatchesAuraBypass(
    const openwow::data::dbc::SpellEntry* spell,
    const openwow::data::dbc::SpellEntry& aura_spell,
    std::uint32_t aura_effect_index);

[[nodiscard]] bool CGUnit_C__HasCompatibleAuraType(
    const CGUnit_C& unit,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SpellEntry* spell,
    std::uint32_t aura_type,
    std::uint32_t* blocking_arg_out);

[[nodiscard]] std::int32_t ComputeSpellPower(
    const openwow::data::dbc::SpellEntry& spell,
    const CGUnit_C* unit,
    const WorldSession& session);

[[nodiscard]] bool HasEnoughSpellPower(
    const openwow::data::dbc::SpellEntry& spell,
    const CGUnit_C& unit,
    const WorldSession& session);

[[nodiscard]] std::int32_t ComputeSpellPower(
    const WorldSession& session, std::uint32_t spell_id,
    bool is_pet);

struct SpellMissileVisualIds {
  std::uint32_t missile_model_id{0};
  std::uint32_t missile_cast_id{0};
};

void RemoveSpellFromPlayerList(std::uint32_t spell_id);

void IterateSpellListAndRedirect(std::uintptr_t list_head,
                                  const std::uint8_t* search_data);

class SpellCastTargetPointArray {
 public:
  void Resize(std::uint32_t new_count);

  void SetData(std::uint32_t count, const void* source);

  [[nodiscard]] std::uint32_t GetCapacity() const { return capacity_; }
  [[nodiscard]] std::uint32_t GetUsedCount() const { return used_count_; }
  [[nodiscard]] const std::uint8_t* GetData() const { return data_.data(); }
  [[nodiscard]] std::uint8_t* GetData() { return data_.data(); }

 private:
  std::uint32_t capacity_{0};
  std::uint32_t used_count_{0};
  std::vector<std::uint8_t> data_;
  static constexpr std::size_t kEntrySize = 40;
};

void FinalizeSpellEffectsOnUnit(const WorldSession& session, CGUnit_C& unit,
                                std::uint32_t spell_id,
                                const data::dbc::SpellEntry* spell_rec);

class PendingDynObjVisualList {
 public:
  struct Entry {
    std::uint32_t caster_guid_low{0};
    std::uint32_t caster_guid_high{0};
    std::uint32_t spell_id{0};
    std::uint32_t cast_time{0};
    std::uint32_t expire_tick{0};
  };

  [[nodiscard]] bool HasMatchingEntry(std::uint32_t caster_guid_low,
                                      std::uint32_t caster_guid_high,
                                      std::uint32_t spell_id,
                                      std::uint32_t cast_time) const;

  void Insert(std::uint32_t caster_guid_low,
              std::uint32_t caster_guid_high,
              std::uint32_t spell_id,
              std::uint32_t cast_time,
              std::uint32_t expire_tick);

  void ExpireEntries(std::uint32_t current_tick);

  void Clear();

  [[nodiscard]] std::size_t Size() const { return entries_.size(); }

  static PendingDynObjVisualList& Get();

 private:
  std::vector<Entry> entries_;
};

}

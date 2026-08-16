
#include "openwow/game/spell_c_internals.h"

#include "openwow/game/action_validation_utils.h"

#include "openwow/core/display_settings.h"

#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/aura_manager.h"
#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_targeting.h"
#include "openwow/game/world_session.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <string>

namespace openwow::game {

std::uint32_t ResolveSpellModifierFamily(const WorldSession& session);

namespace {

constexpr std::array<std::uint32_t, 8> kSpellbookDirectCastEffectIds = {
    6u, 27u, 35u, 65u, 119u, 128u, 129u, 143u,
};

constexpr std::array<std::uint32_t, 7> kPetUseSpellActionEffectIds = {
    6u, 35u, 65u, 119u, 128u, 129u, 143u,
};

constexpr std::size_t kSpellEffectCount = 3;
constexpr std::size_t kSpellClassMaskWordCount = 3;

template <std::size_t N>
bool SpellEffectIdMatchesAny(const std::uint32_t effect_id,
                             const std::array<std::uint32_t, N>& allowed_ids) {
  return std::find(allowed_ids.begin(), allowed_ids.end(), effect_id) !=
         allowed_ids.end();
}

template <std::size_t N>
bool SpellHasAnyMatchingEffect(const std::uint32_t effect_ids[3],
                               const std::array<std::uint32_t, N>& allowed_ids) {
  for (int index = 0; index < 3; ++index) {
    if (SpellEffectIdMatchesAny(effect_ids[index], allowed_ids)) {
      return true;
    }
  }

  return false;
}

template <typename T, typename Selector>
T SelectMinimumMaskedSchoolValue(std::uint32_t school_mask,
                                 T default_value,
                                 Selector selector) {
  bool found = false;
  T result = default_value;

  for (std::uint8_t school = 0; school < 7; ++school) {
    if ((school_mask & (1u << school)) == 0) {
      continue;
    }

    const T value = selector(school);
    if (!found || value < result) {
      result = value;
      found = true;
    }
  }

  return found ? result : default_value;
}

}

static bool HasCasterAuraEffectType(std::uint32_t aura_type,
                                     bool use_pet,
                                     bool use_target,
                                     const WorldSession& session) {
  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) return false;

  const auto& mgr = session.objects();
  const CGUnit_C* caster = nullptr;

  if (use_target) {
    const auto target_guid = mgr.GetTargetGuid();
    if (!target_guid.IsEmpty()) {
      const auto* const obj = mgr.GetObjectByGUID(target_guid);
      if (obj != nullptr && obj->IsUnit()) {
        caster = static_cast<const CGUnit_C*>(obj);
      }
    }
  } else if (use_pet) {
    const auto* const player = mgr.GetLocalPlayerTyped();
    if (player != nullptr) {
      const auto pet_guid = player->State().GetPetGUID();
      if (!pet_guid.IsEmpty()) {
        const auto* const obj = mgr.GetObjectByGUID(pet_guid);
        if (obj != nullptr && obj->IsUnit()) {
          caster = static_cast<const CGUnit_C*>(obj);
        }
      }
    }
  } else {
    caster = mgr.GetLocalPlayerTyped();
  }

  if (caster == nullptr) return false;

  for (const auto& aura : caster->Auras().All()) {
    if (aura.spell_id == 0) continue;

    const auto* const aura_spell = dbc->spell().LookupEntry(aura.spell_id);
    if (aura_spell == nullptr) continue;

    for (std::size_t i = 0; i < kSpellEffectCount; ++i) {
      if (aura_spell->effect_apply_aura[i] == aura_type) {
        return true;
      }
    }
  }

  return false;
}

bool SpellSchoolMask::s_lockout_active = false;
std::uint32_t SpellSchoolMask::s_allowed_mask = 0;

bool SpellSchoolMask::IsSchoolAllowed(std::uint8_t school) {
  return !s_lockout_active || ((1u << school) & s_allowed_mask) != 0;
}

void SpellSchoolMask::SetSchoolLockout(bool active, std::uint32_t allowed_mask) {
  s_lockout_active = active;
  s_allowed_mask = allowed_mask;
}

bool SpellSchoolMask::IsLockoutActive() { return s_lockout_active; }
std::uint32_t SpellSchoolMask::GetAllowedMask() { return s_allowed_mask; }

void SpellSchoolMask::ClearCastPermitIfGcdTriggered(
    const data::dbc::SpellEntry& spell) {

  if (spell.start_recovery_category != 0 && spell.start_recovery_time != 0) {
    s_allowed_mask &= ~4u;
  }
}

std::uint32_t SpellTargetingGlobalState::s_targeting_spell_id = 0;
std::uint64_t SpellTargetingGlobalState::s_targeting_guid = 0;

void SpellTargetingGlobalState::SetTargetingSpellId(std::uint32_t spell_id) {
  s_targeting_spell_id = spell_id;
}

void SpellTargetingGlobalState::SetTargetingGuid(std::uint64_t guid) {
  s_targeting_guid = guid;
}

std::uint32_t SpellTargetingGlobalState::GetTargetingSpellId() {
  return s_targeting_spell_id;
}

std::uint64_t SpellTargetingGlobalState::GetTargetingGuid() {
  return s_targeting_guid;
}

void SpellTargetingGlobalState::Clear() {
  s_targeting_spell_id = 0;
  s_targeting_guid = 0;
}

std::int32_t SpellCastGlobalState::s_global_cast_state = 0;

void SpellCastGlobalState::SetGlobalCastState(std::int32_t value) {
  s_global_cast_state = value;
}

std::int32_t SpellCastGlobalState::GetGlobalCastState() {
  return s_global_cast_state;
}

bool ValidateSpellMovementData(const ObjectManager& objects,
                               std::uint64_t guid1,
                               std::uint64_t guid2) {

  if (guid2 != 0) {
    const auto* obj = objects.GetObjectByGUID(ObjectGuid(guid2));
    if (!obj) return false;

  }

  if (guid1 != 0) {
    const auto* obj = objects.GetObjectByGUID(ObjectGuid(guid1));
    if (!obj) return false;
  }

  return true;
}

std::uint32_t GetUnitSpellCastTimeDivided(const ObjectManager& objects,
                                           [[maybe_unused]] std::uint32_t spell_id,
                                           bool use_pet,
                                           bool use_target) {

  const CGObject_C* obj = nullptr;

  if (use_target) {
    const auto target_guid = objects.GetTargetGuid();
    if (!target_guid.IsEmpty()) {
      obj = objects.GetObjectByGUID(target_guid);
    }
  } else if (use_pet) {
    const auto* player = objects.GetLocalPlayerTyped();
    if (player) {
      const auto pet_guid = player->State().GetPetGUID();
      if (!pet_guid.IsEmpty()) {
        obj = objects.GetObjectByGUID(pet_guid);
      }
    }
  } else {
    obj = objects.GetLocalPlayer();
  }

  if (!obj || !obj->IsUnit()) return 0;

  const auto* const unit = static_cast<const CGUnit_C*>(obj);
  return unit->State().GetLevel();
}

std::int32_t GetMinimumSpellPowerBonusForSchoolMask(
    const CGPlayer_C& player,
    const std::uint32_t school_mask) {
  return SelectMinimumMaskedSchoolValue<std::int32_t>(
      school_mask,
      0,
      [&](const std::uint8_t school) {
        return player.GetSpellBonusDamage(school);
      });
}

float GetMinimumSpellPowerMultiplierForSchoolMask(
    const CGPlayer_C& player,
    const std::uint32_t school_mask) {
  return SelectMinimumMaskedSchoolValue<float>(
      school_mask,
      1.0f,
      [&](const std::uint8_t school) {
        return player.GetModDamageDonePercent(school);
      });
}

std::int32_t GetMinimumPowerCostModifierForSchoolMask(
    const CGUnit_C& unit,
    const std::uint32_t school_mask) {
  return SelectMinimumMaskedSchoolValue<std::int32_t>(
      school_mask,
      0,
      [&](const std::uint8_t school) {
        return unit.State().GetPowerCostModifier(school);
      });
}

float GetMinimumPowerCostMultiplierForSchoolMask(
    const CGUnit_C& unit,
    const std::uint32_t school_mask) {
  return SelectMinimumMaskedSchoolValue<float>(
      school_mask,
      1.0f,
      [&](const std::uint8_t school) {
        return unit.State().GetPowerCostMultiplier(school);
      });
}

void SpellNode::SetRedirectTarget(const float pos[3]) {

  flags |= 0x400000u;
  position[0] = pos[0];
  position[1] = pos[1];
  position[2] = pos[2];
}

std::uintptr_t SpellNodeList::s_global_list_head = 0;

void SpellNodeList::IterateAndUpdate(std::uint8_t type_filter,
                                      std::uint32_t update_param) {

  (void)type_filter;
  (void)update_param;
}

void SpellNodeList::SetGlobalListHead(std::uintptr_t head) {
  s_global_list_head = head;
}

std::uintptr_t SpellNodeList::GetGlobalListHead() {
  return s_global_list_head;
}

bool IsCasterPlayerControlledUnit(const ObjectManager& objects,
                                  const ObjectGuid& caster_guid) {
  if (caster_guid.IsEmpty()) {
    return false;
  }

  const auto* player = objects.GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  return player->State().GetPrimaryControlledUnitGUID() == caster_guid;
}

void CombatText_FireSpellCast(std::uint64_t caster_guid,
                               std::uint32_t spell_attributes_ex6_byte,
                               const char* spell_name) {
  if (!CombatLog_IsActivePlayerTarget(caster_guid)) return;

  if ((spell_attributes_ex6_byte & 0x40) == 0) return;

  CombatLog_FireCombatTextSS(CombatTextMsgIdx::kSpellCast, spell_name);
}

std::int32_t ComputeCastDuration(const WorldSession& session,
                                 std::uint32_t spell_id,
                                  bool use_pet,
                                  bool use_target,
                                  bool allow_negative) {

  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) return 0;
  const auto* const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) return 0;
  const auto* const cast_entry =
      dbc->spell_cast_times().LookupEntry(spell->casting_time_index);
  if (cast_entry == nullptr) return 0;

  const std::uint32_t caster_level =
      GetUnitSpellCastTimeDivided(session.objects(), spell_id, use_pet,
                                  use_target);
  std::int32_t result = cast_entry->base_cast_time +
      cast_entry->per_level *
          (static_cast<std::int32_t>(caster_level) -
           static_cast<std::int32_t>(spell->base_level));

  if (result < cast_entry->minimum) {
    result = cast_entry->minimum;
  }

  {
    const auto spell_family = ResolveSpellModifierFamily(session);
    if (spell_family != 0) {
      (void)session.aura().ApplySpellModifierDeltas(
          spell_family, *spell, SpellModOp::kCastingTime, &result);
    }
  }

  const bool is_channeled = (spell->attributes & 0x08) != 0;
  const bool has_no_haste_flag = (spell->attributes_ex3 & 0x20000000) != 0;
  if (!is_channeled && !has_no_haste_flag) {
    const auto& mgr = session.objects();
    const CGUnit_C* caster = nullptr;
    if (use_target) {
      const auto target_guid = mgr.GetTargetGuid();
      if (!target_guid.IsEmpty()) {
        const auto* const obj = mgr.GetObjectByGUID(target_guid);
        if (obj != nullptr && obj->IsUnit()) {
          caster = static_cast<const CGUnit_C*>(obj);
        }
      }
    } else if (use_pet) {
      const auto* const player = mgr.GetLocalPlayerTyped();
      if (player != nullptr) {
        const auto pet_guid = player->State().GetPetGUID();
        if (!pet_guid.IsEmpty()) {
          const auto* const obj = mgr.GetObjectByGUID(pet_guid);
          if (obj != nullptr && obj->IsUnit()) {
            caster = static_cast<const CGUnit_C*>(obj);
          }
        }
      }
    } else {
      caster = mgr.GetLocalPlayerTyped();
    }

    if (caster != nullptr) {
      const float haste = caster->State().GetSpellHaste();

      if (haste > 0.0f && std::abs(haste - 1.0f) > 0.001f) {
        result = static_cast<std::int32_t>(
            static_cast<float>(result) * haste);
      }
    }
  }

  if ((spell->attributes_ex & 0x02) != 0 && !use_pet) {
    const auto& mgr = session.objects();
    const auto* const player = mgr.GetLocalPlayerTyped();
    if (player != nullptr) {
      const std::uint32_t ranged_speed = player->State().GetRangedAttackTime();
      if (ranged_speed > 0 &&
          static_cast<std::int32_t>(ranged_speed) < result) {
        result = static_cast<std::int32_t>(ranged_speed);
      }
    }
  }

  if (!allow_negative && result < 0) {
    result = 0;
  }

  return result;
}

std::int32_t ComputeSpellDuration(const WorldSession& session,
                                   std::uint32_t spell_id,
                                   bool use_pet,
                                   bool use_target,
                                   bool skip_modifier,
                                   bool apply_haste) {

  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) return 0;
  const auto* const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) return 0;
  const auto* const dur_entry =
      dbc->spell_duration().LookupEntry(spell->duration_index);
  if (dur_entry == nullptr) return 0;

  if (dur_entry->duration < 0) {
    return -1;
  }

  const std::uint32_t caster_level =
      GetUnitSpellCastTimeDivided(session.objects(), spell_id, use_pet,
                                  use_target);
  std::int32_t result = dur_entry->duration +
      dur_entry->duration_per_level *
          (static_cast<std::int32_t>(caster_level) -
           static_cast<std::int32_t>(spell->base_level));

  if (dur_entry->max_duration > 0 && result > dur_entry->max_duration) {
    result = dur_entry->max_duration;
  }

  if (!skip_modifier) {
    const auto spell_family = ResolveSpellModifierFamily(session);
    if (spell_family != 0) {
      (void)session.aura().ApplySpellModifierDeltas(
          spell_family, *spell, SpellModOp::kDuration, &result);
    }
  }

  if (apply_haste) {

    bool has_aura_effect = false;
    for (std::size_t i = 0; i < kSpellEffectCount; ++i) {
      if (spell->effect_apply_aura[i] != 0) {
        has_aura_effect = true;
        break;
      }
    }

    if (has_aura_effect) {

      if ((spell->attributes_ex2 & 0x2000) != 0 ||
          HasCasterAuraEffectType(316, use_pet, use_target, session)) {

        if ((spell->attributes_ex3 & 0x20000000) == 0) {
          const auto& mgr = session.objects();
          const CGUnit_C* caster = nullptr;
          if (use_target) {
            const auto target_guid = mgr.GetTargetGuid();
            if (!target_guid.IsEmpty()) {
              const auto* const obj = mgr.GetObjectByGUID(target_guid);
              if (obj != nullptr && obj->IsUnit()) {
                caster = static_cast<const CGUnit_C*>(obj);
              }
            }
          } else if (use_pet) {
            const auto* const player = mgr.GetLocalPlayerTyped();
            if (player != nullptr) {
              const auto pet_guid = player->State().GetPetGUID();
              if (!pet_guid.IsEmpty()) {
                const auto* const obj = mgr.GetObjectByGUID(pet_guid);
                if (obj != nullptr && obj->IsUnit()) {
                  caster = static_cast<const CGUnit_C*>(obj);
                }
              }
            }
          } else {
            caster = mgr.GetLocalPlayerTyped();
          }

          if (caster != nullptr) {
            const float haste = caster->State().GetSpellHaste();

            if (haste > 0.0f && std::abs(haste - 1.0f) > 0.001f) {
              result = static_cast<std::int32_t>(
                  static_cast<float>(result) * haste);
            }
          }
        }
      }
    }
  }

  if (result < 0) {
    result = 0;
  }

  return result;
}

bool ShouldTargetCorpseForCaster(const SpellTargeting& targeting,
                                 const CGUnit_C* caster,
                                 const CGCorpse_C* target_corpse) {
  if (caster == nullptr || target_corpse == nullptr ||
      targeting.GetSpellId() == 0) {
    return false;
  }

  if ((target_corpse->GetCorpseFlags() & kCorpseFlagBones) != 0) {
    return false;
  }

  const auto target_mask =
      static_cast<SpellTargetFlag>(targeting.GetTargetMask());
  const bool is_friendly_corpse =
      caster->Interaction().IsFriendlyCorpseTarget(*target_corpse);
  if (HasFlag(target_mask, SpellTargetFlag::kCorpseAlly)) {
    return is_friendly_corpse;
  }

  if (HasFlag(target_mask, SpellTargetFlag::kCorpseEnemy)) {
    return !is_friendly_corpse;
  }

  return false;
}

bool ShouldTargetCorpse(const SpellTargeting& targeting,
                         const CGUnit_C& caster,
                         const CGCorpse_C* target_corpse) {
  return ShouldTargetCorpseForCaster(targeting, &caster, target_corpse);
}

float GetPreviewFacingRadians(SpellTargeting& targeting,
                              const CGPlayer_C& player) {

  if (targeting.UsesManualPreviewFacing()) {
    return targeting.GetPreviewFacingRadians();
  }

  const float facing = player.GetWorldFacing();
  targeting.SetPreviewFacingRadians(facing);
  return facing;
}

bool ValidateItemLevel(std::uint32_t item_effective_level,
                        std::uint32_t item_effective_max_level,
                        std::uint32_t spell_base_level,
                        std::uint32_t spell_max_level,
                        bool is_ranged_spell,
                        bool is_caster_owner,
                        bool bypass_level_check,
                        std::uint32_t* out_error) {
  if (!out_error) return true;

  std::uint32_t max_level_check = 0;
  if (!is_caster_owner && is_ranged_spell) {
    max_level_check = spell_max_level;
  }

  const std::uint32_t effective = item_effective_level > 0
                                      ? item_effective_level
                                      : item_effective_max_level;

  if (effective >= spell_base_level) {
    if (max_level_check > 0 && effective > max_level_check) {
      *out_error = static_cast<std::uint32_t>(SpellCastResult::kHighLevel);
      return false;
    }
    return true;
  }

  if (bypass_level_check) {
    return true;
  }
  *out_error = static_cast<std::uint32_t>(SpellCastResult::kLowLevel);
  return false;
}

bool ValidateItemSpellEffects(const WorldSession& session,
                               std::uint32_t spell_id,
                               std::uint64_t item_guid,
                               std::uint32_t* out_error) {
  if (item_guid == 0) return true;

  const auto* item = session.objects().GetItem(ObjectGuid(item_guid));
  if (!item) {
    if (out_error) {
      *out_error =
          static_cast<std::uint32_t>(SpellCastResult::kItemNotFound);
    }
    return false;
  }

  const auto* item_template =
      session.item_definitions().GetItem(item->GetEntry());
  if (!item_template) return true;

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return true;
  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) return true;

  constexpr std::uint32_t kEffectEnchantItem = 53;
  constexpr std::uint32_t kEffectEnchantItemTemporary = 54;
  constexpr std::uint32_t kEffectEnchantItemPrismatic = 156;

  const bool bypass_level_check = (spell->attributes_ex2 & 0x08) != 0;

  for (std::size_t i = 0; i < kSpellEffectCount; ++i) {
    const std::uint32_t effect = spell->effect[i];
    if (effect != kEffectEnchantItem &&
        effect != kEffectEnchantItemTemporary &&
        effect != kEffectEnchantItemPrismatic) {
      continue;
    }

    if (!ValidateItemLevel(
            item_template->required_level,
            item_template->item_level,
            spell->base_level,
            spell->max_level,
            true,
            true,
            bypass_level_check,
            out_error)) {
      return false;
    }
  }

  return true;
}

bool SpellBookFrame_IsUnitMatchingTarget(const ObjectManager& objects,
                                           std::uint64_t unit_guid,
                                           std::uint64_t target_guid) {
  if (unit_guid == 0 || target_guid == 0) return false;
  if (unit_guid == target_guid) return true;

  const auto* target_obj = objects.GetObjectByGUID(ObjectGuid(target_guid));
  if (!target_obj || !target_obj->IsUnit()) return false;

  const auto* target_unit = static_cast<const CGUnit_C*>(target_obj);

  auto charm_guid = target_unit->State().GetCharmedUnitGUID();
  if (!charm_guid.IsEmpty() && charm_guid.GetRawValue() == unit_guid) {
    return true;
  }

  auto pet_guid = target_unit->State().GetPetGUID();
  if (!pet_guid.IsEmpty() && pet_guid.GetRawValue() == unit_guid) {
    return true;
  }

  return false;
}

bool SpellEffect_IsSpellbookDirectCastEffect(const std::uint32_t effect_id) {
  return SpellEffectIdMatchesAny(effect_id, kSpellbookDirectCastEffectIds);
}

bool Spell_HasSpellbookDirectCastEffect(const std::uint32_t effect_ids[3]) {
  return SpellHasAnyMatchingEffect(effect_ids, kSpellbookDirectCastEffectIds);
}

bool SpellEffect_IsPetUseSpellActionEffect(const std::uint32_t effect_id) {
  return SpellEffectIdMatchesAny(effect_id, kPetUseSpellActionEffectIds);
}

bool Spell_HasPetUseSpellActionEffect(const std::uint32_t effect_ids[3]) {
  return SpellHasAnyMatchingEffect(effect_ids, kPetUseSpellActionEffectIds);
}

bool SpellRec_IsNextSwingOrInitiatesCombat(
    const openwow::data::dbc::SpellEntry* spell) {
  if (spell == nullptr) {
    return false;
  }

  constexpr std::uint32_t kAttr0NextSwingMask = 0x404u;
  constexpr std::uint32_t kAttr1InitiateCombat = 0x200u;
  constexpr std::uint32_t kAttr2InitiateCombatPostCast = 0x100000u;

  return (spell->attributes & kAttr0NextSwingMask) != 0u
      || (spell->attributes_ex & kAttr1InitiateCombat) != 0u
      || (spell->attributes_ex2 & kAttr2InitiateCombatPostCast) != 0u;
}

std::span<const std::uint32_t> Spell_GetEffectSpellClassMask(
    const openwow::data::dbc::SpellEntry* spell,
    const std::uint32_t effect_index) {
  if (spell == nullptr || effect_index >= kSpellEffectCount) {
    return {};
  }

  const auto start = static_cast<std::size_t>(effect_index) *
                     kSpellClassMaskWordCount;
  return std::span<const std::uint32_t>(
      spell->effect_spell_class_mask.data() + start, kSpellClassMaskWordCount);
}

bool Spell_MatchesEffectSpellClassMask(
    const openwow::data::dbc::SpellEntry* spell,
    const openwow::data::dbc::SpellEntry* mask_source_spell,
    const std::uint32_t effect_index) {
  if (spell == nullptr || mask_source_spell == nullptr ||
      spell->spell_family_name != mask_source_spell->spell_family_name) {
    return false;
  }

  const auto effect_mask =
      Spell_GetEffectSpellClassMask(mask_source_spell, effect_index);
  if (effect_mask.size() != kSpellClassMaskWordCount) {
    return false;
  }

  for (std::size_t word_index = 0; word_index < kSpellClassMaskWordCount;
       ++word_index) {
    if ((spell->spell_family_flags[word_index] & effect_mask[word_index]) !=
        0u) {
      return true;
    }
  }

  return false;
}

bool Spell_HasAuraType(const std::uint32_t effect_apply_aura[3],
                       const std::uint32_t aura_type) {
  for (int index = 0; index < 3; ++index) {
    if (effect_apply_aura[index] == aura_type) {
      return true;
    }
  }

  return false;
}

bool Spell_HasTrackingAuraType(const std::uint32_t effect_apply_aura[3]) {
  for (int i = 0; i < 3; ++i) {
    if (effect_apply_aura[i] == 44 ||
        effect_apply_aura[i] == 45 ||
        effect_apply_aura[i] == 151) {
      return true;
    }
  }
  return false;
}

bool Spell_MatchesAuraBypass(const openwow::data::dbc::SpellEntry* spell,
                             const openwow::data::dbc::SpellEntry& aura_spell,
                             const std::uint32_t aura_effect_index) {
  if (spell == nullptr ||
      aura_effect_index >= kSpellEffectCount ||
      (spell->caster_aura_state & 0x8000u) == 0u ||
      (aura_spell.attributes & 0x20000000u) != 0u) {
    return false;
  }

  for (std::size_t effect_index = 0; effect_index < kSpellEffectCount; ++effect_index) {
    if (spell->effect[effect_index] != 6u) {
      continue;
    }

    const auto effect_misc_value =
        static_cast<std::uint32_t>(spell->effect_misc_value[effect_index]);
    switch (spell->effect_apply_aura[effect_index]) {
      case 38u:
        if (effect_misc_value == aura_spell.effect_apply_aura[aura_effect_index]) {
          return true;
        }
        break;
      case 39u:
      case 267u:
        if ((aura_spell.attributes_ex2 & 0x04000000u) == 0u &&
            (aura_spell.school_mask & effect_misc_value) != 0u) {
          return true;
        }
        break;
      case 41u:
        if (effect_misc_value == aura_spell.dispel) {
          return true;
        }
        break;
      case 77u:
        if (effect_misc_value == aura_spell.mechanic ||
            effect_misc_value == aura_spell.effect_mechanic[aura_effect_index]) {
          return true;
        }
        break;
      default:
        break;
    }
  }

  return false;
}

bool CGUnit_C__HasCompatibleAuraType(const CGUnit_C& unit,
                                     const openwow::data::dbc::DbcLoader& dbc,
                                     const openwow::data::dbc::SpellEntry* spell,
                                     const std::uint32_t aura_type,
                                     std::uint32_t* blocking_arg_out) {
  bool found_matching_aura_type = false;

  for (const auto& aura : unit.Auras().All()) {
    if (aura.spell_id == 0u) {
      continue;
    }

    const auto* const aura_spell = dbc.spell().LookupEntry(aura.spell_id);
    if (aura_spell == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < kSpellEffectCount; ++effect_index) {
      if (aura_spell->effect_apply_aura[effect_index] != aura_type) {
        continue;
      }

      found_matching_aura_type = true;
      if (Spell_MatchesAuraBypass(spell, *aura_spell,
                                  static_cast<std::uint32_t>(effect_index))) {
        continue;
      }

      if (blocking_arg_out != nullptr) {
        const auto effect_mechanic = aura_spell->effect_mechanic[effect_index];
        *blocking_arg_out =
            effect_mechanic != 0u ? effect_mechanic : aura_spell->mechanic;
      }
      return false;
    }
  }

  return found_matching_aura_type;
}

bool SpellHasEffectType(const data::dbc::SpellEntry& spell, std::uint32_t effect_type) {
  for (std::size_t i = 0; i < 3; ++i) {
    if (spell.effect[i] == effect_type) {
      return true;
    }
  }
  return false;
}

std::uint32_t ResolveSpellModifierFamily(const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }
  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return 0;
  }
  const auto* chr_class = dbc->chr_classes().LookupEntry(player->State().GetClass());
  return chr_class != nullptr ? chr_class->spell_family : 0;
}

namespace {

constexpr std::uint32_t kDefaultWeaponSpeedMs = 2000u;

constexpr std::uint32_t kAttrEx4WeaponSpeedCostScaling = 0x400u;

constexpr std::uint32_t kAttr0LevelDamageCalc = 0x80000u;

constexpr std::uint32_t kAttrEx3UseOffHand = 0x1000000u;

constexpr std::uint32_t kEffectSummonAllTotems = 97u;

constexpr std::uint32_t kAttrEx7SubSpellPowerFlag = 0x20u;

std::int32_t AddWrappedI32(const std::int32_t lhs, const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      std::bit_cast<std::uint32_t>(lhs) + std::bit_cast<std::uint32_t>(rhs));
}

std::int32_t MultiplyWrappedI32(const std::int32_t lhs,
                                const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      std::bit_cast<std::uint32_t>(lhs) * std::bit_cast<std::uint32_t>(rhs));
}

}

std::int32_t ComputeSpellPower(
    const data::dbc::SpellEntry& spell,
    const CGUnit_C* unit,
    const WorldSession& session) {
  if (unit == nullptr) {
    return -1;
  }

  const std::uint32_t caster_level = unit->State().GetLevel();
  const auto level_delta = std::bit_cast<std::int32_t>(
      caster_level / 5u - spell.base_level);
  auto cost = AddWrappedI32(
      std::bit_cast<std::int32_t>(spell.mana_cost),
      MultiplyWrappedI32(
          std::bit_cast<std::int32_t>(spell.mana_cost_per_level),
          level_delta));

  if (spell.mana_cost_percentage != 0) {
    const auto percentage_cost = MultiplyWrappedI32(
        std::bit_cast<std::int32_t>(spell.mana_cost_percentage),
        std::bit_cast<std::int32_t>(unit->GetBasePowerByType(
            static_cast<std::int32_t>(spell.power_type)))) / 100;
    cost = AddWrappedI32(cost, percentage_cost);
  }

  if ((spell.attributes_ex4 & kAttrEx4WeaponSpeedCostScaling) != 0
      && unit->IsActivePlayer()) {
    std::uint32_t weapon_speed = kDefaultWeaponSpeedMs;

    const std::uint8_t form_id =
        unit->State().SuppressesCurrentFormSpellQueries()
            ? 0u
            : unit->Animation().GetShapeshiftForm();
    if (form_id != 0) {
      const auto* const dbc = session.GetDbcLoader();
      if (dbc != nullptr) {
        const auto* const form_entry =
            dbc->spell_shapeshift_form().LookupEntry(form_id);
        if (form_entry != nullptr && form_entry->combat_round_time > 0) {
          weapon_speed = form_entry->combat_round_time;
        }
      }
    }

    if (weapon_speed == kDefaultWeaponSpeedMs) {
      const auto& player = static_cast<const CGPlayer_C&>(*unit);
      const AttackSlot slot = (spell.attributes_ex3 & kAttrEx3UseOffHand) != 0
                                  ? kAttackSlotOffHand
                                  : kAttackSlotMainHand;
      const std::uint32_t equip_speed = player.State().GetAttackSpeed(slot);
      if (equip_speed > 0) {
        weapon_speed = equip_speed;
      }
    }

    cost = AddWrappedI32(
        cost, static_cast<std::int32_t>(weapon_speed / 100u));
  }

  const std::int32_t flat_bonus =
      GetMinimumPowerCostModifierForSchoolMask(*unit, spell.school_mask);
  const float multiplier =
      GetMinimumPowerCostMultiplierForSchoolMask(*unit, spell.school_mask);

  float cost_f = static_cast<float>(AddWrappedI32(cost, flat_bonus));
  cost_f *= multiplier;

  const auto total_before_modifiers = static_cast<std::int32_t>(
      std::lrintf(cost_f));
  cost = total_before_modifiers;

  const auto* const dbc = session.GetDbcLoader();
  const auto* const objects = unit->object_manager();
  const auto* player = objects != nullptr ? objects->GetLocalPlayerTyped()
                                           : nullptr;
  const auto* chr_class = dbc != nullptr && player != nullptr
      ? dbc->chr_classes().LookupEntry(player->State().GetClass())
      : nullptr;
  const auto spell_family = chr_class != nullptr ? chr_class->spell_family : 0u;
  if (spell_family != 0) {
    std::int32_t flat_delta = 0;
    std::int32_t pct_total = 100;
    if (session.aura().AccumulateSpellModifierDeltas(
            spell_family, spell, SpellModOp::kCost,
            &flat_delta, &pct_total)) {
      cost = MultiplyWrappedI32(
          pct_total, AddWrappedI32(flat_delta, total_before_modifiers)) / 100;
    }
  }

  if (!unit->IsPlayer() &&
      (spell.attributes & kAttr0LevelDamageCalc) != 0) {
    if (dbc != nullptr) {
      const std::uint32_t unit_level = unit->State().GetLevel();

      const std::uint32_t spell_lvl = spell.spell_level;
      if (spell_lvl > 0 && unit_level > 0) {
        const auto* const spell_scaler =
            dbc->gt_npc_mana_cost_scaler().LookupEntryByRowIndex(spell_lvl - 1);
        const auto* const unit_scaler =
            dbc->gt_npc_mana_cost_scaler().LookupEntryByRowIndex(unit_level - 1);
        if (spell_scaler != nullptr && unit_scaler != nullptr &&
            spell_scaler->value > 0.0f) {
          const float ratio = unit_scaler->value / spell_scaler->value;
          cost = static_cast<std::int32_t>(
              std::lrintf(ratio * static_cast<float>(cost)));
        }
      }
    }
  }

  if (unit->IsPlayer() && dbc != nullptr &&
      SpellHasEffectType(spell, kEffectSummonAllTotems)) {
    for (std::size_t effect_index = 0;
         effect_index < spell.effect.size(); ++effect_index) {
      if (spell.effect[effect_index] != kEffectSummonAllTotems) {
        continue;
      }

      const auto slot_start = static_cast<std::uint32_t>(
          spell.effect_misc_value[effect_index]);
      const auto slot_count = spell.effect_misc_value_b[effect_index];
      if (slot_start >= ActionAssignmentRuntime::kSlotsPerBar ||
          slot_count <= 0 ||
          static_cast<std::uint32_t>(slot_count) >
              ActionAssignmentRuntime::kSlotsPerBar - slot_start) {
        continue;
      }

      for (std::uint32_t slot_offset = 0;
           slot_offset < static_cast<std::uint32_t>(slot_count);
           ++slot_offset) {
        const auto sub_spell_id =
            session.action_assignments().ResolveSpellLikeActionId(
                slot_start + slot_offset);
        if (sub_spell_id == 0) {
          continue;
        }
        const auto* const sub_spell = dbc->spell().LookupEntry(sub_spell_id);
        if (sub_spell == nullptr ||
            (sub_spell->attributes_ex7 & kAttrEx7SubSpellPowerFlag) == 0) {
          continue;
        }

        cost = AddWrappedI32(cost, ComputeSpellPower(*sub_spell, unit, session));
      }
    }
  }

  return cost <= 0 ? 0 : cost;
}

bool HasEnoughSpellPower(const data::dbc::SpellEntry& spell,
                         const CGUnit_C& unit,
                         const WorldSession& session) {
  const auto cost = ComputeSpellPower(spell, &unit, session);
  if (cost < 0) {
    return false;
  }

  const auto power_type = static_cast<std::int32_t>(spell.power_type);
  if (power_type == -2) {
    return static_cast<std::uint32_t>(cost) < unit.State().GetHealth();
  }

  if (power_type == static_cast<std::int32_t>(PowerType::kRunes)) {
    const auto* const objects = unit.object_manager();
    const auto* const active_player =
        objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
    if (active_player == nullptr || active_player != &unit) {
      return false;
    }

    const auto* dbc = session.GetDbcLoader();
    if (spell.rune_cost_id == 0) {
      return true;
    }
    if (dbc == nullptr) {
      return false;
    }

    const auto* rune_cost =
        dbc->spell_rune_cost().LookupEntry(spell.rune_cost_id);
    if (rune_cost == nullptr) {
      return true;
    }

    SpellUsabilityInfo rune_cost_info;
    rune_cost_info.spell_family_name = spell.spell_family_name;
    rune_cost_info.spell_family_flags = spell.spell_family_flags;
    rune_cost_info.attributes_ex3 = spell.attributes_ex3;
    rune_cost_info.school_mask = spell.school_mask;
    const auto rune_cost_pct = detail::ComputeEffectiveRuneCostPct(
        rune_cost_info, *active_player, &session.aura(), dbc);

    return session.runes().CanAfford(RuneCost{
        .blood = static_cast<std::uint8_t>(rune_cost->blood),
        .unholy = static_cast<std::uint8_t>(rune_cost->unholy),
        .frost = static_cast<std::uint8_t>(rune_cost->frost),
    }, rune_cost_pct);
  }

  return static_cast<std::uint32_t>(cost) <=
         unit.GetPowerOrHealth(power_type);
}

std::int32_t ComputeSpellPower(const WorldSession& session,
                               std::uint32_t spell_id,
                               bool is_pet) {

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }
  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  const auto& mgr = session.objects();
  const CGUnit_C* unit = nullptr;
  if (is_pet) {
    const auto* player = mgr.GetLocalPlayerTyped();
    if (player != nullptr) {
      auto pet_guid = player->State().GetPetGUID();
      if (!pet_guid.IsEmpty()) {
        const auto* obj = mgr.GetObjectByGUID(pet_guid);
        if (obj != nullptr && obj->IsUnit()) {
          unit = static_cast<const CGUnit_C*>(obj);
        }
      }
    }
  } else {
    const auto* player = mgr.GetLocalPlayerTyped();
    unit = player;
  }

  return ComputeSpellPower(*spell, unit, session);
}

void RemoveSpellFromPlayerList(std::uint32_t spell_id) {

  (void)spell_id;
}

void IterateSpellListAndRedirect(std::uintptr_t list_head,
                                  const std::uint8_t* search_data) {

  (void)list_head;
  (void)search_data;
}

void SpellCastTargetPointArray::Resize(std::uint32_t new_count) {

  data_.resize(static_cast<std::size_t>(new_count) * kEntrySize, 0);
  capacity_ = new_count;
  if (used_count_ > capacity_) {
    used_count_ = capacity_;
  }
}

void SpellCastTargetPointArray::SetData(std::uint32_t count,
                                        const void* source) {

  if (count != used_count_) {
    data_.resize(static_cast<std::size_t>(count) * kEntrySize);
    capacity_ = count;
  }
  if (count > 0 && source) {
    std::memcpy(data_.data(), source,
                static_cast<std::size_t>(count) * kEntrySize);
  }
  used_count_ = count;
}

void FinalizeSpellEffectsOnUnit(const WorldSession& session, CGUnit_C& unit,
                                const std::uint32_t spell_id,
                                const data::dbc::SpellEntry* spell_rec) {

  const bool resume_attack = (unit.Casts().GetChannelSpellId(unit) == 0u);
  const std::uint32_t channel_cast_id = unit.Casts().GetChannelCast().cast_id;
  unit.Casts().OnChannelingComplete(
      unit, session, channel_cast_id, resume_attack,
      true);

  unit.SpellVisuals().DestroyByKitType(
      session, spell_id, 4u, 0u,
      false, false);

  if (spell_rec != nullptr) {
    const auto* const dbc = unit.dbc_loader();
    if (dbc != nullptr) {
      auto visual_id = spell_rec->spell_visual[0];
      const auto quality_level = static_cast<std::int32_t>(
          openwow::core::DisplaySettingsController::Instance()
              .GetQualityLevel());
      if (quality_level < 2 && spell_rec->spell_visual[1] != 0u) {
        visual_id = spell_rec->spell_visual[1];
      }

      const auto* visual = visual_id != 0u
                               ? dbc->spell_visual().LookupEntry(visual_id)
                               : nullptr;
      if (visual != nullptr && visual->precast_kit != 0u) {

        const auto* kit =
            dbc->spell_visual_kit().LookupEntry(visual->precast_kit);
        if (kit != nullptr &&
            (kit->start_anim_id != 0 || kit->anim_id != 0)) {
          unit.Animation().RefreshSelectedStandAnimation(
              session, 0u, static_cast<std::uint32_t>(-1));
        }
      }
    }
  }

  if (unit.Mount().PendingTransitionSpellId() == spell_id) {
    const std::uint32_t current_mount = unit.Mount().DisplayId(unit);
    if (unit.Mount().CachedDisplayForSpell() != current_mount) {

      unit.Presentation().RefreshActiveDisplayRuntimeState();
    }
    unit.Mount().ClearPendingTransitionSpellId();
  }

  unit.Casts().CancelDelayedMissileTrajectory(spell_id);
}

PendingDynObjVisualList& PendingDynObjVisualList::Get() {
  static PendingDynObjVisualList instance;
  return instance;
}

bool PendingDynObjVisualList::HasMatchingEntry(
    std::uint32_t caster_guid_low,
    std::uint32_t caster_guid_high,
    std::uint32_t spell_id,
    std::uint32_t cast_time) const {
  for (const auto& e : entries_) {
    if (e.caster_guid_low == caster_guid_low &&
        e.caster_guid_high == caster_guid_high &&
        e.spell_id == spell_id &&
        e.cast_time == cast_time) {
      return true;
    }
  }
  return false;
}

void PendingDynObjVisualList::Insert(
    std::uint32_t caster_guid_low,
    std::uint32_t caster_guid_high,
    std::uint32_t spell_id,
    std::uint32_t cast_time,
    std::uint32_t expire_tick) {
  Entry entry{caster_guid_low, caster_guid_high, spell_id, cast_time,
              expire_tick};

  auto it = std::lower_bound(
      entries_.begin(), entries_.end(), expire_tick,
      [](const Entry& e, std::uint32_t tick) {
        return e.expire_tick < tick;
      });
  entries_.insert(it, entry);
}

void PendingDynObjVisualList::ExpireEntries(std::uint32_t current_tick) {

  auto it = entries_.begin();
  while (it != entries_.end()) {

    if (static_cast<std::int32_t>(current_tick - it->expire_tick) >= 0) {
      it = entries_.erase(it);
    } else {
      break;
    }
  }
}

void PendingDynObjVisualList::Clear() {
  entries_.clear();
}

}

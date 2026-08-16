#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/unit/unit_cast_runtime.h"

#include "openwow/core/display_settings.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/proc_manager.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/spell_query_bridge.h"

#include <algorithm>
#include <array>
#include <optional>

namespace openwow::game {

namespace {

constexpr std::uint32_t kTrackedTradeSkillSpellFlag = 0x20u;
constexpr std::uint32_t kOffhandWeaponOverrideEffectId = 40u;
constexpr std::uint32_t kRideVehicleSkillLineId = 778u;
constexpr std::uint32_t kHeldItemPetTargetEffectId = 101u;
constexpr std::uint32_t kTrackedItemRequirementEffectId = 155u;
constexpr std::uint32_t kProficiencyEffectId = 60u;
constexpr std::uint32_t kHiddenSkillLineFlag = 0x80000000u;
constexpr std::uint32_t kSpellAttr1TrackedTarget = 0x4u;

struct HandleSpellCastTrackingInfo {
  std::uint32_t tooltip_flags = 0;
  std::array<std::uint32_t, 3> effect_ids{};
  std::uint32_t skill_line_id = 0;
  std::uint32_t proficiency_id = 0;
};

[[nodiscard]] bool ContainsEffectId(const std::array<std::uint32_t, 3> &effect_ids,
                                    const std::uint32_t effect_id) {
  return std::find(effect_ids.begin(), effect_ids.end(), effect_id) != effect_ids.end();
}

[[nodiscard]] std::optional<std::uint32_t>
ResolveHandleSpellCastSkillLineId(const CGUnit_C &unit, const data::dbc::DbcLoader *dbc_loader,
                                  const std::uint32_t spell_id) {
  if (dbc_loader == nullptr || spell_id == 0u) {
    return std::nullopt;
  }

  const auto race = unit.State().GetRace();
  const auto player_class = unit.State().GetClass();
  if (race == 0u || player_class == 0u) {
    return std::nullopt;
  }

  std::optional<SkillRaceClassIdentity> active_identity;
  const auto* const objects = unit.object_manager();
  if (objects != nullptr) {
    const auto* const active_player = objects->GetActivePlayer();
    if (active_player != nullptr) {
      active_identity = SkillRaceClassIdentity{active_player->State().GetRace(),
                                               active_player->State().GetClass()};
    }
  }

  const auto *ability = FindSkillLineAbilityForRaceClassSpell(
      dbc_loader->skill_line_ability().entries(), dbc_loader->skill_race_class_info().entries(),
      race, player_class, spell_id, active_identity);
  if (ability == nullptr) {
    return std::nullopt;
  }

  const auto *skill_info = FindSkillRaceClassInfoBySkillId(
      dbc_loader->skill_race_class_info().entries(), race, player_class, ability->skill_id);
  if (skill_info == nullptr || (skill_info->flags & kHiddenSkillLineFlag) != 0u) {
    return std::nullopt;
  }

  return ability->skill_id;
}

[[nodiscard]] std::optional<std::uint32_t>
ResolveHandleSpellCastProficiencyId(const data::dbc::DbcLoader *dbc_loader,
                                    const std::uint32_t spell_id) {
  if (dbc_loader == nullptr || spell_id == 0u) {
    return std::nullopt;
  }

  const auto *spell_entry = dbc_loader->spell().LookupEntry(spell_id);
  if (spell_entry == nullptr) {
    return std::nullopt;
  }

  for (std::size_t effect_index = 0; effect_index < spell_entry->effect.size(); ++effect_index) {
    if (spell_entry->effect[effect_index] != kProficiencyEffectId) {
      continue;
    }

    const auto item_class = spell_entry->effect_misc_value[effect_index];
    const auto subclass_mask = spell_entry->effect_item_type[effect_index];
    if (item_class < 0 || subclass_mask == 0u || (subclass_mask & (subclass_mask - 1u)) != 0u) {
      continue;
    }

    std::uint32_t subclass_id = 0u;
    std::uint32_t mask = subclass_mask;
    while ((mask & 1u) == 0u) {
      ++subclass_id;
      mask >>= 1u;
    }

    for (const auto &item_subclass : dbc_loader->item_sub_class().entries()) {
      if (item_subclass.class_id != static_cast<std::uint32_t>(item_class) ||
          item_subclass.subclass_id != subclass_id) {
        continue;
      }

      if (item_subclass.postreq_proficiency != 0u) {
        return item_subclass.postreq_proficiency;
      }
      return std::nullopt;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<HandleSpellCastTrackingInfo>
ResolveHandleSpellCastTrackingInfo(const CGUnit_C &unit, const std::uint32_t spell_id) {
  HandleSpellCastTrackingInfo tracking{};
  bool have_data = false;

  if (const auto query = SpellQueryBridge::Get().Query(spell_id); query.has_value()) {

    tracking.tooltip_flags = query->attributesEx4;
    tracking.effect_ids = query->effectIds;
    tracking.skill_line_id = query->skillLineId;
    tracking.proficiency_id = query->proficiencyId;
    have_data = true;
  }

  const auto *dbc_loader = unit.dbc_loader();
  if (dbc_loader != nullptr) {
    if (const auto *spell_entry = dbc_loader->spell().LookupEntry(spell_id);
        spell_entry != nullptr) {
      tracking.effect_ids = spell_entry->effect;
      have_data = true;
    }

    if (tracking.skill_line_id == 0u) {
      if (const auto skill_line_id = ResolveHandleSpellCastSkillLineId(unit, dbc_loader, spell_id);
          skill_line_id.has_value()) {
        tracking.skill_line_id = *skill_line_id;
      }
    }

    if (tracking.proficiency_id == 0u &&
        ContainsEffectId(tracking.effect_ids, kProficiencyEffectId)) {
      if (const auto proficiency_id = ResolveHandleSpellCastProficiencyId(dbc_loader, spell_id);
          proficiency_id.has_value()) {
        tracking.proficiency_id = *proficiency_id;
      }
    }
  }

  if (!have_data) {
    return std::nullopt;
  }

  return tracking;
}

}

UnitCastRuntime &CGUnit_C::Casts() noexcept { return casts_; }

const UnitCastRuntime &CGUnit_C::Casts() const noexcept { return casts_; }

void UnitCastRuntime::OnPlayerSpellCompleted(CGUnit_C &owner) {
  auto *const objects = owner.object_manager();
  auto *player = objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
  if (!player)
    return;

  if (owner.GetGuid() == player->GetGuid()) {
  }
}

bool UnitCastRuntime::HasTrackedItemRequirementSpell(
    const std::uint32_t spell_id) const {
  for (const auto& id : tracked_item_requirement_spell_ids_) {
    if (id == spell_id)
      return true;
  }
  return false;
}

void UnitCastRuntime::ForgetSpellCastTracking(CGUnit_C &owner,
                                               const std::uint32_t spell_id) {
  if (spell_id == 0u) {
    return;
  }

  const auto tracking = ResolveHandleSpellCastTrackingInfo(owner, spell_id);
  if (!tracking.has_value()) {
    return;
  }

  if ((tracking->tooltip_flags & kTrackedTradeSkillSpellFlag) != 0u &&
      tracking->skill_line_id != 0u) {
    const auto it = tracked_trade_skill_spells_.find(tracking->skill_line_id);
    if (it != tracked_trade_skill_spells_.end() && it->second == spell_id) {
      tracked_trade_skill_spells_.erase(it);
    }
  }

  if (tracking->effect_ids[0] == kTrackedItemRequirementEffectId) {
    ForgetTrackedItemRequirementSpell(spell_id);
  }

  if (tracking->proficiency_id != 0u &&
      ContainsEffectId(tracking->effect_ids, kProficiencyEffectId)) {
    const auto it = tracked_proficiency_spells_.find(tracking->proficiency_id);
    if (it != tracked_proficiency_spells_.end() && it->second == spell_id) {
      tracked_proficiency_spells_.erase(it);
    }
  }

  if (tracking->effect_ids[0] == kOffhandWeaponOverrideEffectId &&
      offhand_weapon_override_spell_id_ == spell_id) {
    ClearOffhandWeaponOverrideSpell();
  }
}

void UnitCastRuntime::ForgetTrackedItemRequirementSpell(
    const std::uint32_t spell_id) {
  if (spell_id == 0u) {
    return;
  }

  tracked_item_requirement_spell_ids_.erase(
      std::remove(tracked_item_requirement_spell_ids_.begin(),
                  tracked_item_requirement_spell_ids_.end(), spell_id),
      tracked_item_requirement_spell_ids_.end());
}

std::optional<std::uint32_t>
UnitCastRuntime::GetTrackedTradeSkillSpell(
    const std::uint32_t skill_line_id) const {
  const auto it = tracked_trade_skill_spells_.find(skill_line_id);
  if (it == tracked_trade_skill_spells_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::uint32_t>
UnitCastRuntime::GetTrackedProficiencySpell(
    const std::uint32_t proficiency_id) const {
  const auto it = tracked_proficiency_spells_.find(proficiency_id);
  if (it == tracked_proficiency_spells_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool UnitCastRuntime::CanEquipWeaponInOffHand() const noexcept {
  return offhand_weapon_override_spell_id_ != 0u;
}

void UnitCastRuntime::ClearOffhandWeaponOverrideSpell() noexcept {
  offhand_weapon_override_spell_id_ = 0u;
}

const std::vector<std::uint32_t> &
UnitCastRuntime::GetTrackedItemRequirementSpellIds() const noexcept {
  return tracked_item_requirement_spell_ids_;
}

void UnitCastRuntime::ClearSpellTracking() {
  pending_spell_casts_.clear();
  tracked_item_requirement_spell_ids_.clear();
  tracked_trade_skill_spells_.clear();
  tracked_proficiency_spells_.clear();
}

void UnitCastRuntime::OnSpellHitProc(const CGUnit_C &owner,
                                     const std::uint32_t spell_id,
                                     const std::uint32_t school_mask,
                                     const std::uint64_t target_guid,
                                     const bool is_crit) {
  auto& procs = ProcManager::Get();

  if (is_crit) {
    procs.OnCrit(owner.GetGuid().GetRawValue(), spell_id, school_mask, target_guid);
  } else {
    procs.OnHit(owner.GetGuid().GetRawValue(), spell_id, school_mask, target_guid);
  }
}

void UnitCastRuntime::OnSpellHealProc(const CGUnit_C &owner,
                                      const std::uint32_t spell_id,
                                      const std::uint32_t school_mask,
                                      const std::uint64_t target_guid) {
  ProcManager::Get().OnHeal(
      owner.GetGuid().GetRawValue(), spell_id, school_mask, target_guid);
}

void UnitCastRuntime::OnDamageTakenProc(const CGUnit_C &owner,
                                        const std::uint32_t spell_id,
                                        const std::uint32_t school_mask,
                                        const std::uint64_t attacker_guid) {
  ProcManager::Get().OnDamageTaken(
      owner.GetGuid().GetRawValue(), spell_id, school_mask, attacker_guid);
}

void UnitCastRuntime::OnDefenseProc(const CGUnit_C &owner,
                                    const std::uint32_t spell_id,
                                    const ProcTriggerType defense_type,
                                    const std::uint64_t attacker_guid) {
  auto& procs = ProcManager::Get();
  switch (defense_type) {
    case ProcTriggerType::kOnDodge:
      procs.OnDodge(owner.GetGuid().GetRawValue(), spell_id, 0, attacker_guid);
      break;
    case ProcTriggerType::kOnParry:
      procs.OnParry(owner.GetGuid().GetRawValue(), spell_id, 0, attacker_guid);
      break;
    case ProcTriggerType::kOnBlock:
      procs.OnBlock(owner.GetGuid().GetRawValue(), spell_id, 0, attacker_guid);
      break;
    default:
      break;
  }
}

void UnitCastRuntime::OnCastProc(const CGUnit_C &owner,
                                 const std::uint32_t spell_id,
                                 const std::uint32_t school_mask,
                                 const std::uint64_t target_guid) {
  ProcManager::Get().OnCast(
      owner.GetGuid().GetRawValue(), spell_id, school_mask, target_guid);
}

void UnitCastRuntime::ProcessMissileHitTargets(
    CGUnit_C &owner, const WorldSession &session,
    std::span<const ObjectGuid> targets, const std::uint32_t spell_id) {
  if (!targets.empty()) {
    const ObjectGuid first_target = targets[0];
    if (!first_target.IsEmpty()) {
      if (const auto spell = SpellQueryBridge::Get().Query(spell_id);
          spell.has_value() &&
          (spell->attributesEx & kSpellAttr1TrackedTarget) != 0u) {
        if (first_target != tracked_spell_target_) {
          tracked_spell_target_ = first_target;
        }
        tracked_spell_target_spell_id_ = spell_id;
      }
    }
  }

  BuildMissileHitTargetList(owner, targets);

  FinalizeTrackedSpell(owner, session, spell_id);
}

void UnitCastRuntime::BuildMissileHitTargetList(
    const CGUnit_C &owner, std::span<const ObjectGuid> targets) {
  missile_hit_other_targets_.clear();

  const ObjectGuid own_guid = owner.GetGuid();
  for (auto i = static_cast<std::ptrdiff_t>(targets.size()) - 1; i >= 0; --i) {
    if (targets[static_cast<std::size_t>(i)] != own_guid) {
      missile_hit_other_targets_.push_back(targets[static_cast<std::size_t>(i)]);
    }
  }
}

ObjectGuid UnitCastRuntime::GetTrackedSpellTarget() const noexcept {
  return tracked_spell_target_;
}

std::uint32_t UnitCastRuntime::GetTrackedSpellTargetSpellId() const noexcept {
  return tracked_spell_target_spell_id_;
}

const std::vector<ObjectGuid> &
UnitCastRuntime::GetMissileHitOtherTargets() const noexcept {
  return missile_hit_other_targets_;
}

void UnitCastRuntime::ClearMissileHitOtherTargets() {
  missile_hit_other_targets_.clear();
}

void UnitCastRuntime::HandleSpellCast(CGUnit_C &owner,
                                      SpellCastRuntime &spell_cast_runtime,
                                      std::uint32_t spell_id,
                                      bool show_learn_visual,
                                      bool play_animation) {
  (void)show_learn_visual;
  (void)play_animation;

  if (spell_id == 0u)
    return;

  const auto tracking = ResolveHandleSpellCastTrackingInfo(owner, spell_id);
  if (!tracking.has_value()) {
    return;
  }

  if (tracking->skill_line_id == kRideVehicleSkillLineId) {
    pending_spell_casts_.push_back(spell_id);
  }

  if ((tracking->tooltip_flags & kTrackedTradeSkillSpellFlag) != 0u &&
      tracking->skill_line_id != 0u) {
    tracked_trade_skill_spells_[tracking->skill_line_id] = spell_id;
  }

  if (tracking->effect_ids[0] == kHeldItemPetTargetEffectId) {
    spell_cast_runtime.SetRememberedHeldItemPetTargetSpellId(spell_id);
  } else if (tracking->effect_ids[0] == kTrackedItemRequirementEffectId) {
    if (!HasTrackedItemRequirementSpell(spell_id)) {
      tracked_item_requirement_spell_ids_.push_back(spell_id);
    }
  } else if (tracking->proficiency_id != 0u &&
             ContainsEffectId(tracking->effect_ids, kProficiencyEffectId)) {
    tracked_proficiency_spells_[tracking->proficiency_id] = spell_id;
  }

  if (tracking->effect_ids[0] == kOffhandWeaponOverrideEffectId) {
    offhand_weapon_override_spell_id_ = spell_id;
  }
}

bool UnitCastRuntime::CheckCasterRequirements(
    const CGUnit_C & , std::int32_t ,
    const std::uint8_t *spell_data, std::int32_t ) const {
  if (!spell_data)
    return false;

  return true;
}

const DelayedMissileTrajectoryState &
UnitCastRuntime::GetDelayedMissileTrajectoryState() const noexcept {
  return delayed_missile_trajectory_;
}

std::uint32_t UnitCastRuntime::GetComboSpellId() const noexcept {
  return combo_spell_id_;
}

void UnitCastRuntime::SetComboSpellId(const std::uint32_t spell_id) {
  combo_spell_id_ = spell_id;
}

void UnitCastRuntime::SetComboPointTarget(const std::uint32_t target_id) {
  combo_point_target_ = target_id;
}

void UnitCastRuntime::SetSpellCooldownExpiry(const std::uint32_t current_time) {
  spell_cooldown_expiry_ = current_time + 5000u;
}

std::uint32_t UnitCastRuntime::GetComboPointTarget() const noexcept {
  return combo_point_target_;
}

std::uint32_t UnitCastRuntime::GetSpellCooldownExpiry() const noexcept {
  return spell_cooldown_expiry_;
}

std::uint32_t UnitCastRuntime::GetChannelSpellId(const CGUnit_C &owner) const {
  return owner.GetUInt32(UNIT_CHANNEL_SPELL);
}

ObjectGuid UnitCastRuntime::GetChannelObject(const CGUnit_C &owner) const {
  return owner.GetGuidField(UNIT_FIELD_CHANNEL_OBJECT);
}

std::uint32_t UnitCastRuntime::GetCreatedBySpell(const CGUnit_C &owner) const {
  return owner.GetUInt32(UNIT_CREATED_BY_SPELL);
}

void UnitCastRuntime::SetCurrentCast(const CastInfo &cast) { current_cast_ = cast; }

void UnitCastRuntime::ClearCurrentCast() { current_cast_ = CastInfo{}; }

void UnitCastRuntime::SetChannelCast(const CastInfo &cast) { channel_cast_ = cast; }

void UnitCastRuntime::ClearChannelCast() { channel_cast_ = CastInfo{}; }

const CastInfo &UnitCastRuntime::GetCurrentCast() const { return current_cast_; }
const CastInfo &UnitCastRuntime::GetChannelCast() const { return channel_cast_; }
bool UnitCastRuntime::IsCasting() const { return current_cast_.spell_id != 0u; }
bool UnitCastRuntime::IsChanneling() const { return channel_cast_.spell_id != 0u; }

bool UnitCastRuntime::IsCurrentCastUninterruptible() const {
  if (IsCasting()) {
    return current_cast_.not_interruptible;
  }
  if (IsChanneling()) {
    return channel_cast_.not_interruptible;
  }
  return false;
}

std::int32_t UnitCastRuntime::GetCurrentCastPrecastStartAnimId(
    const CGUnit_C &owner) const {
  const auto *const dbc = owner.dbc_loader();
  if (dbc == nullptr || current_cast_.spell_id == 0u) {
    return -1;
  }
  const auto *const spell = dbc->spell().LookupEntry(current_cast_.spell_id);
  if (spell == nullptr) {
    return -1;
  }
  auto visual_id = spell->spell_visual[0];
  if (static_cast<std::int32_t>(
          core::DisplaySettingsController::Instance().GetQualityLevel()) < 2 &&
      spell->spell_visual[1] != 0u) {
    visual_id = spell->spell_visual[1];
  }
  if (visual_id == 0u) {
    return -1;
  }
  const auto *const visual = dbc->spell_visual().LookupEntry(visual_id);
  if (visual == nullptr || visual->precast_kit == 0u) {
    return -1;
  }
  const auto *const kit =
      dbc->spell_visual_kit().LookupEntry(visual->precast_kit);
  return kit != nullptr ? static_cast<std::int32_t>(kit->start_anim_id) : -1;
}

void UnitCastRuntime::BeginDelayedMissileTrajectory(
    const std::uint32_t spell_id,
    const std::uint32_t cast_start_tick_ms) {
  delayed_missile_trajectory_.Begin(spell_id, cast_start_tick_ms);
}

void UnitCastRuntime::CompleteDelayedMissileTrajectory(
    const std::uint32_t spell_id, const std::uint32_t spell_go_tick_ms,
    const std::uint32_t server_delay_ms) {
  delayed_missile_trajectory_.Complete(spell_id, spell_go_tick_ms,
                                       server_delay_ms);
}

void UnitCastRuntime::CancelDelayedMissileTrajectory(
    const std::uint32_t spell_id) {
  delayed_missile_trajectory_.Cancel(spell_id);
}

void UnitCastRuntime::SetDelayedMissileTrajectoryActiveCastCount(
    const std::uint8_t cast_count) {
  game::SetDelayedMissileTrajectoryActiveCastCount(delayed_missile_trajectory_,
                                                    cast_count);
}

bool UnitCastRuntime::HasPendingMissileTrajectory() const {
  return delayed_missile_trajectory_.HasPayload();
}

bool UnitCastRuntime::HasActiveMissileTrajectory() const {
  return delayed_missile_trajectory_.HasActiveMissile();
}

void UnitCastRuntime::ResetChannelTiming(std::uint32_t) {
  channel_delay_count_ = 0;
  channel_delay_start_ = 0;
  channel_delay_end_ = 0;
}

void UnitCastRuntime::ApplyChannelPushback(const std::int32_t delay_ms,
                                           std::uint32_t) {
  channel_start_time_ += delay_ms;
  channel_end_time_ += delay_ms;
  channel_delay_count_ = 0;
  channel_delay_start_ = 0;
  channel_delay_end_ = 0;
}

void UnitCastRuntime::ResetChannelBase(const std::int32_t base_time,
                                       const std::uint32_t current_time) {
  const auto remaining = channel_delay_end_ - channel_delay_start_;
  const auto new_end = base_time + static_cast<std::int32_t>(current_time);
  channel_delay_end_ = new_end;
  channel_delay_start_ = new_end - remaining;
  channel_spell_amount_ = 0;
  channel_start_time_ = 0;
  channel_end_time_ = 0;
}

void UnitCastRuntime::FinalizeTrackedSpell(CGUnit_C &owner,
                                           const WorldSession &session,
                                           const std::uint32_t spell_id) {
  if (spell_id != 0u) {

    owner.SpellVisuals().EndSpellVisualProcState(spell_id);
    pending_spell_casts_.erase(
        std::remove(pending_spell_casts_.begin(), pending_spell_casts_.end(),
                    spell_id),
        pending_spell_casts_.end());
  }
  if (IsCasting() || IsChanneling()) {
    return;
  }
  if (owner.Interaction().SpellResumesAutoAttackOnCompletion(spell_id) &&
      owner.Interaction().MatchesActiveMoverInteractionMask(2u)) {
    owner.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  owner.SpellVisuals().RemoveEffectsBySpellId(session, spell_id, true);
  owner.Animation().ResetAuraAnimationVisualState(session);
}

void UnitCastRuntime::OnChannelingComplete(
    CGUnit_C &owner, const WorldSession &session, const std::uint32_t cast_id,
    const bool resume_attack, const bool trigger_completion) {
  if (cast_id != channel_cast_.cast_id) {
    return;
  }
  const auto completed_spell_id = channel_cast_.spell_id;

  owner.Animation().SetChannelingActionLock(false);
  ClearChannelCast();
  if (!trigger_completion) {
    return;
  }
  if (resume_attack &&
      owner.Interaction().SpellResumesAutoAttackOnCompletion(completed_spell_id) &&
      owner.Interaction().MatchesActiveMoverInteractionMask(2u)) {
    owner.Interaction().CompleteAutoAttackInteraction(false, true);
  }
  owner.Animation().ResetAuraAnimationVisualState(session);
}

}

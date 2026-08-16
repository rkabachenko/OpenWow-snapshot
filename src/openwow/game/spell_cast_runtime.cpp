#include "openwow/game/spell_cast_runtime.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/core/client_init.h"
#include "openwow/game/generated_action_bar.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/game/spell_aura_candidate_validation.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spell_validation.h"
#include "openwow/game/world_session.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace openwow::game {

namespace {

std::size_t SlotIndex(const SpellSlotType slot) {
  return static_cast<std::size_t>(slot);
}

bool IsActive(const ActiveSpellInfo& spell) {
  return spell.state == SpellClientState::kActive;
}

void FireCurrentSpellCastChanged() {
  auto& events = openwow::ui::game::ScriptEventDispatch::Get();
  if (events.IsInitialized()) {
    events.FireEvent(openwow::ui::game::events::CURRENT_SPELL_CAST_CHANGED);
  }
}

constexpr std::uint32_t kSpellAttrCastableWhileSitting = 0x08000000u;
constexpr std::uint32_t kSpellAttrExChanneledMask = 0x00000044u;
constexpr std::uint32_t kSpellAttrEx2MovementBypass = 0x00000020u;
constexpr std::uint32_t kSpellInterruptFlagMovement = 0x00000001u;
constexpr std::uint32_t kSpellAuraInterruptMovementMask = 0x00000018u;

constexpr std::uint32_t kSpellMissileFlagRequiresTrajectory = 0x00000001u;

constexpr std::uint32_t kImplicitTargetCaster = 1u;

enum class CastTrajectoryOutcome : std::uint8_t {

  kNotRequired,

  kSolved,
  kCasterIsNotActiveMover,
  kSolverFailed,
};

struct CastTrajectoryResolution {
  CastTrajectoryOutcome outcome{CastTrajectoryOutcome::kNotRequired};
  SpellMissileCastTrajectoryResult trajectory{};

  [[nodiscard]] SpellCastResult ToCastResult() const {
    switch (outcome) {
      case CastTrajectoryOutcome::kCasterIsNotActiveMover:
        return SpellCastResult::kDontReport;
      case CastTrajectoryOutcome::kSolverFailed:
        return SpellCastResult::kError;
      case CastTrajectoryOutcome::kNotRequired:
      case CastTrajectoryOutcome::kSolved:
        break;
    }
    return SpellCastResult::kSuccess;
  }
};

[[nodiscard]] SpellCastResult ValidateCastAuraCandidates(
    const WorldSession& session, const data::dbc::SpellEntry& spell,
    const CGUnit_C& caster, const SpellCastTargets& targets) {
  std::array<SpellTargetCandidateUnit, 2> candidates{};
  std::size_t count = 0;

  const auto& implicit_targets = spell.effect_implicit_target_a;
  if (std::ranges::find(implicit_targets, kImplicitTargetCaster) !=
      std::ranges::end(implicit_targets)) {
    candidates[count++] = {.unit = &caster, .is_implicit_caster = true};
  }
  if (targets.unit_target) {
    if (const auto* const target = session.objects().GetUnit(targets.unit_target);
        target != nullptr) {
      candidates[count++] = {.unit = target, .is_implicit_caster = false};
    }
  }

  return ValidateSpellTargetCandidates(
      session, spell, caster,
      std::span<const SpellTargetCandidateUnit>(candidates.data(), count));
}

[[nodiscard]] CastTrajectoryResolution ResolveCastTrajectory(
    const WorldSession& session, const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell, const CGUnit_C& caster,
    const ObjectGuid caster_guid) {
  const auto* const missile =
      dbc.spell_missile().LookupEntry(spell.spell_missile_id);
  if (missile == nullptr ||
      (missile->flags & kSpellMissileFlagRequiresTrajectory) == 0u) {
    return {};
  }
  if (session.player_control_runtime().ActiveMoverGuid() != caster_guid) {
    return {.outcome = CastTrajectoryOutcome::kCasterIsNotActiveMover};
  }

  const auto trajectory =
      session.missile_trajectory().PrepareSpellCastTrajectory(
          caster, spell.id, session.objects(),
          core::GetClientStartupAdlerSeedState());
  switch (trajectory.status) {
    case SpellMissileCastTrajectoryStatus::kReady:
      return {.outcome = CastTrajectoryOutcome::kSolved,
              .trajectory = trajectory};
    case SpellMissileCastTrajectoryStatus::kFailed:
      return {.outcome = CastTrajectoryOutcome::kSolverFailed};
    case SpellMissileCastTrajectoryStatus::kNotRequired:
      break;
  }
  return {};
}

void ApplySolvedTrajectory(SpellCastTargets& targets,
                           const SpellMissileCastTrajectoryResult& trajectory) {
  targets.target_mask |= net::wotlk::kTargetFlagSourceLocation |
                         net::wotlk::kTargetFlagDestLocation;
  targets.source_transport = {};
  targets.source_x = trajectory.source.x;
  targets.source_y = trajectory.source.y;
  targets.source_z = trajectory.source.z;
  targets.destination_transport = {};
  targets.destination_x = trajectory.destination.x;
  targets.destination_y = trajectory.destination.y;
  targets.destination_z = trajectory.destination.z;
  targets.trajectory_pitch = trajectory.pitch_radians;
  targets.trajectory_speed = trajectory.speed;
}

SpellCastResult ValidatePetCastSendState(
    const WorldSession& session, const CGUnit_C& pet,
    const data::dbc::SpellEntry& spell) {

  if ((spell.attributes & kSpellAttrCastableWhileSitting) == 0u &&
      pet.Animation().GetStandState() != 0u) {
    return SpellCastResult::kNotStanding;
  }

  if (!pet.Interaction().IsPlayerControlled() || !pet.Movement().IsMoving()) {
    return SpellCastResult::kSuccess;
  }

  const bool moving_interrupts_channel =
      (spell.attributes_ex & kSpellAttrExChanneledMask) != 0u &&
      (spell.channel_interrupt_flags & kSpellAuraInterruptMovementMask) != 0u;
  const bool moving_interrupts_cast =
      (spell.interrupt_flags & kSpellInterruptFlagMovement) != 0u &&
      (spell.attributes_ex2 & kSpellAttrEx2MovementBypass) == 0u &&
      (ComputeCastDuration(session, spell.id, true, false, false) > 0 ||
       (spell.aura_interrupt_flags & kSpellAuraInterruptMovementMask) != 0u);
  return moving_interrupts_channel || moving_interrupts_cast
             ? SpellCastResult::kMoving
             : SpellCastResult::kSuccess;
}

const data::dbc::OverrideSpellDataEntry* LookupActiveOverrideSpellDataEntry(
    const CGPlayer_C& player, const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }
  const auto override_spell_data_id = player.GetOverrideSpellDataId();
  return override_spell_data_id != 0
             ? dbc->override_spell_data().LookupEntry(override_spell_data_id)
             : nullptr;
}

void FireSpellcastSentEvent(const WorldSession& session,
                            const std::uint32_t spell_id,
                            const ObjectGuid target_guid) {
  const auto* player = session.objects().GetLocalPlayerTyped();
  const auto* dbc = session.GetDbcLoader();
  const auto* spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (player == nullptr || spell == nullptr) {
    return;
  }

  std::string target_name;
  if (const auto* target = session.objects().Get(target_guid); target != nullptr) {
    target_name = target->GetName();
  }
  openwow::ui::game::ScriptEventDispatch::Get().FirePerUnitEventWithArgs(
      openwow::ui::game::events::UNIT_SPELLCAST_SENT,
      player->GetGuid().GetRawValue(),
      {std::string(spell->spell_name), std::string(spell->rank),
       std::move(target_name)});
}

void ClearActivePlayerAutoRepeatState(WorldSession& session) {
  if (auto* player = session.objects().GetActivePlayer(); player != nullptr) {
    player->Animation().SetAutoRepeatActive(false);
    player->Animation().ResetAuraAnimationVisualState(session);
  }
}

constexpr std::uint32_t kSpellInterruptFlagMovementStart = 0x00000001u;
constexpr std::uint32_t kChannelInterruptFlagMovementStart = 0x00000008u;

const data::dbc::SpellEntry* LookupSpellEntryForMovementInterrupts(
    const WorldSession& session, const std::uint32_t spell_id) {
  const auto* dbc = session.GetDbcLoader();
  return spell_id != 0 && dbc != nullptr ? dbc->spell().LookupEntry(spell_id)
                                         : nullptr;
}

void CancelSpellSlotOnMovementStart(SpellCastRuntime& spells,
                                    WorldSession& session,
                                    const SpellSlotType slot,
                                    const std::uint32_t spell_id) {
  const auto* spell = LookupSpellEntryForMovementInterrupts(session, spell_id);
  if (spell == nullptr ||
      (spell->interrupt_flags & kSpellInterruptFlagMovementStart) == 0) {
    return;
  }
  spells.CancelSpell(session, slot);
}

}

const char* SpellCastResultToString(const SpellCastResult result) {
  return SpellFailedReasonToString(static_cast<std::uint8_t>(result));
}

SpellCastRuntime::SpellCastRuntime() : targeting_(this) {}

void SpellCastRuntime::SetSendCastSpell(PacketSendFunc send) {
  send_cast_spell_ = std::move(send);
}

SpellCastTargets SpellCastRuntime::BuildTargets(
    const WorldSession& session, const std::uint64_t target_guid) const {
  SpellCastTargets targets;
  if (target_guid == 0) {
    return targets;
  }

  const ObjectGuid guid(target_guid);
  const auto* target = session.objects().GetObjectByGUID(guid);
  if (target != nullptr ? target->IsGameObject() : guid.IsGameObject()) {
    targets.target_mask = net::wotlk::kTargetFlagGameObject;
    targets.object_target = guid;
  } else if (target != nullptr ? target->IsItem() : guid.IsItem()) {
    targets.target_mask = net::wotlk::kTargetFlagItem;
    targets.item_target = guid;
  } else if (target != nullptr ? target->IsCorpse() : guid.IsCorpse()) {
    targets.target_mask = net::wotlk::kTargetFlagPvpCorpse;
    targets.unit_target = guid;
  } else {
    targets.target_mask = net::wotlk::kTargetFlagUnit;
    targets.unit_target = guid;
  }
  return targets;
}

SpellCastResult SpellCastRuntime::CastSpell(const WorldSession& session,
                                             const std::uint32_t spell_id,
                                             const std::uint64_t target_guid,
                                             const std::uint8_t cast_flags) {
  const auto validation = ValidatePlayerCastRequest(session, spell_id);
  if (validation != SpellCastResult::kSuccess) {
    return validation;
  }
  const auto* player = session.objects().GetLocalPlayerTyped();
  return ExecuteCast(session, SpellCastOrigin::kPlayer,
                     player != nullptr ? player->GetGuid() : ObjectGuid{},
                     spell_id, target_guid, cast_flags);
}

SpellCastResult SpellCastRuntime::CastTriggeredSpell(
    const WorldSession& session, const std::uint32_t spell_id,
    const std::uint64_t target_guid, const std::uint8_t cast_flags) {
  const auto* player = session.objects().GetLocalPlayerTyped();
  return ExecuteCast(session, SpellCastOrigin::kPlayer,
                     player != nullptr ? player->GetGuid() : ObjectGuid{},
                     spell_id, target_guid, cast_flags);
}

SpellCastResult SpellCastRuntime::CastPetSpell(
    const WorldSession& session, const ObjectGuid pet_guid,
    const std::uint32_t spell_id, const std::uint64_t target_guid,
    std::uint32_t* const blocking_mechanic_out) {
  if (blocking_mechanic_out != nullptr) {
    *blocking_mechanic_out = 0;
  }
  if (!pet_guid || spell_id == 0 ||
      !session.pet().HasActionBarSpellId(spell_id)) {
    return SpellCastResult::kNotKnown;
  }
  const auto* pet = session.objects().GetUnit(pet_guid);
  if (pet == nullptr || pet->State().GetHealth() == 0) {
    return SpellCastResult::kCasterDead;
  }

  if (const auto cooldown =
          ResolvePetBarSpellCooldown(session.pet().pet_bar(), spell_id,
                                     session.GetDbcLoader());
      cooldown.has_value() &&
      (cooldown->enabled == 0.0 ||
       RemainingCooldownMilliseconds(*cooldown) > 0)) {
    return SpellCastResult::kNotReady;
  }

  const auto* const dbc = session.GetDbcLoader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    return SpellCastResult::kNotKnown;
  }
  const auto* const active_player = session.objects().GetActivePlayer();
  const auto control_state = ValidateSpellCasterControlState(
      *pet, *dbc, *spell,
      active_player != nullptr ? active_player->GetGuid() : ObjectGuid{},
      blocking_mechanic_out);
  if (control_state != SpellCastResult::kSuccess) {
    return control_state;
  }
  if (const auto send_state = ValidatePetCastSendState(session, *pet, *spell);
      send_state != SpellCastResult::kSuccess) {
    return send_state;
  }

  return ExecuteCast(session, SpellCastOrigin::kPet, pet_guid, spell_id,
                     target_guid, 0);
}

SpellCastResult SpellCastRuntime::ValidatePlayerCastRequest(
    const WorldSession& session, const std::uint32_t spell_id) const {
  const auto& spellbook = session.spell_book();
  if (!spellbook.spells().empty() && !spellbook.HasSpell(spell_id)) {
    return SpellCastResult::kNotKnown;
  }

  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player != nullptr && player->State().GetHealth() == 0) {
    return SpellCastResult::kCasterDead;
  }
  if (player != nullptr &&
      !OverrideSpellDataAllowsSpellLikeAction(
          LookupActiveOverrideSpellDataEntry(*player, session), spell_id)) {
    return SpellCastResult::kCantDoThatRightNow;
  }

  if (const auto cooldown = ResolveSpellbookCooldown(spellbook, spell_id);
      cooldown.has_value() &&
      (cooldown->enabled == 0.0 || RemainingCooldownMilliseconds(*cooldown) > 0)) {
    return SpellCastResult::kNotReady;
  }
  return SpellCastResult::kSuccess;
}

SpellCastResult SpellCastRuntime::ExecuteCast(
    const WorldSession& session, const SpellCastOrigin origin,
    const ObjectGuid caster_guid, const std::uint32_t spell_id,
    const std::uint64_t target_guid, const std::uint8_t) {

  const bool has_prepared_cast =
      prepared_cast_.spell_id == spell_id &&
      prepared_cast_.origin == origin &&
      (!prepared_cast_.caster_guid ||
       prepared_cast_.caster_guid == caster_guid);
  SpellCastTargets targets =
      has_prepared_cast ? prepared_cast_.targets
                        : BuildTargets(session, target_guid);

  std::uint8_t effective_cast_flags = 0u;
  const auto* const caster = session.objects().GetUnit(caster_guid);
  const auto* const dbc = session.GetDbcLoader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    return SpellCastResult::kNotKnown;
  }
  if (caster != nullptr) {
    if (const auto aura_validation =
            ValidateCastAuraCandidates(session, *spell, *caster, targets);
        aura_validation != SpellCastResult::kSuccess) {
      return aura_validation;
    }

    const auto trajectory =
        ResolveCastTrajectory(session, *dbc, *spell, *caster, caster_guid);
    if (const auto abort = trajectory.ToCastResult();
        abort != SpellCastResult::kSuccess) {
      return abort;
    }
    if (trajectory.outcome == CastTrajectoryOutcome::kSolved) {

      effective_cast_flags |= net::wotlk::kClientSpellCastFlagHasTrajectory;
      ApplySolvedTrajectory(targets, trajectory.trajectory);
    }
  }
  const SpellCastCommand command{
      .origin = origin,
      .caster_guid = caster_guid,
      .spell_id = spell_id,
      .cast_count = next_cast_count_,
      .cast_flags = effective_cast_flags,
      .targets = targets,
  };
  if (!send_cast_spell_ || !send_cast_spell_(command)) {
    return SpellCastResult::kError;
  }
  ++next_cast_count_;
  if (next_cast_count_ == 0) {
    next_cast_count_ = 1;
  }
  if (has_prepared_cast) {
    prepared_cast_ = {};
  }

  ObjectGuid event_target = command.targets.unit_target;
  if (!event_target) {
    event_target = command.targets.object_target;
  }
  if (!event_target) {
    event_target = command.targets.item_target;
  }
  if (origin == SpellCastOrigin::kPlayer) {
    FireSpellcastSentEvent(session, spell_id, event_target);
  }
  return SpellCastResult::kSuccess;
}

void SpellCastRuntime::OnSpellStart(const std::uint32_t spell_id,
                                    const std::uint8_t cast_count,
                                    const ObjectGuid& caster,
                                    const ObjectGuid& target) {
  slots_[SlotIndex(SpellSlotType::kCurrent)] = {
      spell_id, cast_count, caster, target, SpellClientState::kActive};

  FireCurrentSpellCastChanged();
}

void SpellCastRuntime::OnSpellGo(const std::uint32_t spell_id,
                                 const std::uint8_t cast_count) {
  auto& spell = slots_[SlotIndex(SpellSlotType::kCurrent)];
  if (spell.spell_id == spell_id && spell.cast_count == cast_count) {
    spell = {};

    FireCurrentSpellCastChanged();
  }
}

void SpellCastRuntime::OnSpellFailed(const std::uint32_t spell_id,
                                     const std::uint8_t cast_count) {
  auto& spell = slots_[SlotIndex(SpellSlotType::kCurrent)];
  if (spell.spell_id == spell_id && spell.cast_count == cast_count) {
    spell = {};

    FireCurrentSpellCastChanged();
  }
}

void SpellCastRuntime::OnChannelStart(const ObjectGuid& caster_guid,
                                      const std::uint32_t spell_id) {
  if (spell_id != 0) {
    slots_[SlotIndex(SpellSlotType::kChannel)] = {
        spell_id, 0, caster_guid, {}, SpellClientState::kActive};

    FireCurrentSpellCastChanged();
  }
}

void SpellCastRuntime::OnChannelStop() {
  slots_[SlotIndex(SpellSlotType::kChannel)] = {};
  FireCurrentSpellCastChanged();
}

void SpellCastRuntime::FireAutoRepeatStateChange(const bool was_active) const {
  const bool is_active = auto_attack_spell_id_ != 0 || IsAutoRepeating();
  if (was_active == is_active) {
    return;
  }
  auto& events = openwow::ui::game::ScriptEventDispatch::Get();
  if (events.IsInitialized()) {
    events.FireEvent(is_active
                         ? openwow::ui::game::events::START_AUTOREPEAT_SPELL
                         : openwow::ui::game::events::STOP_AUTOREPEAT_SPELL);
  }
}

void SpellCastRuntime::StartAutoRepeat(const std::uint32_t spell_id) {
  const bool was_active = auto_attack_spell_id_ != 0 || IsAutoRepeating();
  slots_[SlotIndex(SpellSlotType::kAutoRepeat)] = {
      spell_id, 0, {}, {}, SpellClientState::kActive};
  FireAutoRepeatStateChange(was_active);
}

void SpellCastRuntime::StopAutoRepeat(WorldSession& session) {
  StopAutoRepeat(session, false);
}

void SpellCastRuntime::StopAutoRepeat(WorldSession& session,
                                      const bool send_cancel_packet) {
  auto* const player = session.objects().GetActivePlayer();
  if (!IsAutoRepeating() &&
      (player == nullptr || !player->Animation().IsAutoRepeatActive())) {
    return;
  }
  const bool was_active = auto_attack_spell_id_ != 0 || IsAutoRepeating() ||
                          (player != nullptr &&
                           player->Animation().IsAutoRepeatActive());
  if (send_cancel_packet) {
    (void)session.Send(net::wotlk::PacketSender::BuildCancelAutoRepeat());
  }
  slots_[SlotIndex(SpellSlotType::kAutoRepeat)] = {};
  ClearActivePlayerAutoRepeatState(session);
  FireAutoRepeatStateChange(was_active);
}

void SpellCastRuntime::SetAutoAttackSpellId(const std::uint32_t spell_id) {
  const bool was_active = auto_attack_spell_id_ != 0 || IsAutoRepeating();
  auto_attack_spell_id_ = spell_id;
  FireAutoRepeatStateChange(was_active);
}

void SpellCastRuntime::SetUnitTarget(const SpellSlotType slot,
                                     const ObjectGuid& unit) {
  if (slot == SpellSlotType::kCurrent && prepared_cast_.spell_id != 0) {
    prepared_cast_.targets.unit_target = unit;
    if (unit) {
      prepared_cast_.targets.target_mask |= net::wotlk::kTargetFlagUnit;
    } else {
      prepared_cast_.targets.target_mask &= ~net::wotlk::kTargetFlagUnit;
    }
  }
  if (IsSlotActive(slot)) {
    slots_[SlotIndex(slot)].target_guid = unit;
  }
}

void SpellCastRuntime::SetObjectTarget(const SpellSlotType slot,
                                       const ObjectGuid& object,
                                       const std::uint32_t target_mask) {
  if (slot == SpellSlotType::kCurrent && prepared_cast_.spell_id != 0) {
    auto& targets = prepared_cast_.targets;
    constexpr std::uint32_t kUnitTargetMask =
        net::wotlk::kTargetFlagUnit | net::wotlk::kTargetFlagUnitRaid |
        net::wotlk::kTargetFlagPvpCorpse |
        net::wotlk::kTargetFlagUnitCorpse |
        net::wotlk::kTargetFlagCorpseAlly |
        net::wotlk::kTargetFlagUnitMinipet |
        net::wotlk::kTargetFlagUnitPassenger;
    constexpr std::uint32_t kObjectTargetMask =
        net::wotlk::kTargetFlagGameObject |
        net::wotlk::kTargetFlagObjectUnk;
    if ((target_mask & kUnitTargetMask) != 0u) {
      targets.unit_target = object;
    }
    if ((target_mask & kObjectTargetMask) != 0u) {
      targets.object_target = object;
    }
    if (object) {
      targets.target_mask |= target_mask;
    } else {
      targets.target_mask &= ~target_mask;
    }
  }
  if (IsSlotActive(slot)) {
    slots_[SlotIndex(slot)].target_guid = object;
  }
}

void SpellCastRuntime::SetSourcePosition(const SpellSlotType slot,
                                         const ObjectGuid& transport,
                                         const float x, const float y,
                                         const float z) {
  if (slot != SpellSlotType::kCurrent || prepared_cast_.spell_id == 0) {
    return;
  }
  auto& targets = prepared_cast_.targets;
  targets.target_mask |= net::wotlk::kTargetFlagSourceLocation;
  targets.source_transport = transport;
  targets.source_x = x;
  targets.source_y = y;
  targets.source_z = z;
}

void SpellCastRuntime::SetDestinationPosition(const SpellSlotType slot,
                                              const ObjectGuid& transport,
                                              const float x, const float y,
                                              const float z) {
  if (slot != SpellSlotType::kCurrent || prepared_cast_.spell_id == 0) {
    return;
  }
  auto& targets = prepared_cast_.targets;
  targets.target_mask |= net::wotlk::kTargetFlagDestLocation;
  targets.destination_transport = transport;
  targets.destination_x = x;
  targets.destination_y = y;
  targets.destination_z = z;
}

void SpellCastRuntime::SetItemTarget(const SpellSlotType slot,
                                     const ObjectGuid& item) {
  if (slot == SpellSlotType::kCurrent && prepared_cast_.spell_id != 0) {
    prepared_cast_.targets.item_target = item;
    if (item) {
      prepared_cast_.targets.target_mask |= net::wotlk::kTargetFlagItem;
    } else {
      prepared_cast_.targets.target_mask &= ~net::wotlk::kTargetFlagItem;
    }
  }
  if (IsSlotActive(slot)) {
    slots_[SlotIndex(slot)].target_guid = item;
  }
}

void SpellCastRuntime::SetTradeItemTarget(const SpellSlotType slot,
                                          const ObjectGuid& item) {
  if (slot == SpellSlotType::kCurrent && prepared_cast_.spell_id != 0) {
    prepared_cast_.targets.item_target = item;
    if (item) {
      prepared_cast_.targets.target_mask |= net::wotlk::kTargetFlagTradeItem;
    } else {
      prepared_cast_.targets.target_mask &= ~net::wotlk::kTargetFlagTradeItem;
    }
  }
  if (IsSlotActive(slot)) {
    slots_[SlotIndex(slot)].target_guid = item;
  }
}

bool SpellCastRuntime::PrepareTargetedCast(
    const std::uint32_t spell_id, const SpellCastOrigin origin,
    const ObjectGuid caster_guid) {

  if (spell_id == 0) {
    return false;
  }
  if (prepared_cast_.spell_id != spell_id ||
      prepared_cast_.origin != origin ||
      prepared_cast_.caster_guid != caster_guid) {
    prepared_cast_ = {};
    prepared_cast_.spell_id = spell_id;
  }
  prepared_cast_.origin = origin;
  prepared_cast_.caster_guid = caster_guid;
  return true;
}

SpellCastResult SpellCastRuntime::CastPreparedSpell(
    const WorldSession& session, const std::uint8_t cast_flags) {
  if (prepared_cast_.spell_id == 0) {
    return SpellCastResult::kBadTargets;
  }
  const PreparedCast prepared = prepared_cast_;
  return prepared.origin == SpellCastOrigin::kPet
             ? CastPetSpell(session, prepared.caster_guid,
                            prepared.spell_id, 0)
             : CastSpell(session, prepared.spell_id, 0, cast_flags);
}

ObjectGuid SpellCastRuntime::GetPreparedTarget(
    const std::uint32_t spell_id) const {
  if (prepared_cast_.spell_id != spell_id) {
    return {};
  }
  if (prepared_cast_.targets.unit_target) {
    return prepared_cast_.targets.unit_target;
  }
  if (prepared_cast_.targets.object_target) {
    return prepared_cast_.targets.object_target;
  }
  return prepared_cast_.targets.item_target;
}

SpellCastOrigin SpellCastRuntime::GetPreparedOrigin(
    const std::uint32_t spell_id) const {
  return prepared_cast_.spell_id == spell_id
             ? prepared_cast_.origin
             : SpellCastOrigin::kPlayer;
}

ObjectGuid SpellCastRuntime::GetPreparedCaster(
    const std::uint32_t spell_id) const {
  return prepared_cast_.spell_id == spell_id
             ? prepared_cast_.caster_guid
             : ObjectGuid{};
}

void SpellCastRuntime::DiscardPreparedTargetedCast(
    const std::uint32_t spell_id) {
  if (prepared_cast_.spell_id == spell_id) {
    prepared_cast_ = {};
  }
}

ActiveSpellInfo SpellCastRuntime::GetSlot(const SpellSlotType slot) const {
  return slots_[SlotIndex(slot)];
}

bool SpellCastRuntime::IsSlotActive(const SpellSlotType slot) const {
  return IsActive(slots_[SlotIndex(slot)]);
}

bool SpellCastRuntime::IsCasting() const {
  return IsSlotActive(SpellSlotType::kCurrent);
}

bool SpellCastRuntime::IsChanneling() const {
  return IsSlotActive(SpellSlotType::kChannel);
}

bool SpellCastRuntime::IsAutoRepeating() const {
  return IsSlotActive(SpellSlotType::kAutoRepeat);
}

std::uint32_t SpellCastRuntime::GetCurrentSpellId() const {
  if (IsCasting()) {
    return slots_[SlotIndex(SpellSlotType::kCurrent)].spell_id;
  }
  return IsChanneling() ? GetChanneledSpellId() : 0;
}

std::uint32_t SpellCastRuntime::GetAutoRepeatSpellId() const {
  return IsAutoRepeating()
             ? slots_[SlotIndex(SpellSlotType::kAutoRepeat)].spell_id
             : 0;
}

std::uint32_t SpellCastRuntime::GetAutoAttackSpellId() const {
  return auto_attack_spell_id_;
}

std::uint32_t SpellCastRuntime::GetChanneledSpellId() const {
  return IsChanneling() ? slots_[SlotIndex(SpellSlotType::kChannel)].spell_id
                        : 0;
}

void SpellCastRuntime::SetRememberedHeldItemPetTargetSpellId(
    const std::uint32_t spell_id) {
  remembered_held_item_pet_target_spell_id_ = spell_id;
}

std::uint32_t SpellCastRuntime::GetRememberedHeldItemPetTargetSpellId() const {
  return remembered_held_item_pet_target_spell_id_;
}

void SpellCastRuntime::CancelSpell(WorldSession& session,
                                   const SpellSlotType slot) {
  if (slot == SpellSlotType::kAutoRepeat) {
    StopAutoRepeat(session, true);
    return;
  }

  const auto spell = slots_[SlotIndex(slot)];
  if (!IsActive(spell) || spell.spell_id == 0) {
    return;
  }
  if (slot == SpellSlotType::kChannel) {
    (void)session.Send(
        net::wotlk::PacketSender::BuildCancelChannelling(spell.spell_id));
  } else {
    (void)session.Send(net::wotlk::PacketSender::BuildCancelCast(
        spell.cast_count, spell.spell_id));
  }
  if (slots_[SlotIndex(slot)].spell_id == spell.spell_id &&
      slots_[SlotIndex(slot)].cast_count == spell.cast_count) {
    slots_[SlotIndex(slot)] = {};
  }
}

void SpellCastRuntime::CancelSpellCastsTargeting(WorldSession& session,
                                                 const ObjectGuid& target) {
  if (target.IsEmpty()) {
    return;
  }
  for (std::size_t index = 0;
       index < static_cast<std::size_t>(SpellSlotType::kCount); ++index) {
    const auto slot = static_cast<SpellSlotType>(index);
    const auto& spell = slots_[SlotIndex(slot)];
    if (IsActive(spell) && spell.target_guid == target) {
      CancelSpell(session, slot);
    }
  }
}

void SpellCastRuntime::CancelAllLocalPlayerCasts(WorldSession& session) {
  for (std::size_t index = 0;
       index < static_cast<std::size_t>(SpellSlotType::kCount); ++index) {
    const auto slot = static_cast<SpellSlotType>(index);
    if (IsActive(slots_[SlotIndex(slot)])) {
      CancelSpell(session, slot);
    }
  }
}

void SpellCastRuntime::SendCancelChannelling(WorldSession& session,
                                             const std::uint32_t spell_id) {
  if (spell_id != 0) {
    (void)session.Send(
        net::wotlk::PacketSender::BuildCancelChannelling(spell_id));
  }
}

void SpellCastRuntime::Reset() {
  slots_ = {};
  prepared_cast_ = {};
  next_cast_count_ = 1;
  auto_attack_spell_id_ = 0;
  remembered_held_item_pet_target_spell_id_ = 0;
  targeting_.Reset();
  send_cast_spell_ = nullptr;
}

SpellTargeting& SpellCastRuntime::GetTargeting() { return targeting_; }

const SpellTargeting& SpellCastRuntime::GetTargeting() const {
  return targeting_;
}

void HandleMovementActivatedSpellInterrupts(WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr ||
      player->GetGuid() != session.player_control_runtime().ActiveMoverGuid()) {
    return;
  }

  auto& spells = session.spells();
  CancelSpellSlotOnMovementStart(spells, session, SpellSlotType::kCurrent,
                                 spells.GetCurrentSpellId());
  CancelSpellSlotOnMovementStart(spells, session, SpellSlotType::kAutoRepeat,
                                 spells.GetAutoRepeatSpellId());
  CancelSpellSlotOnMovementStart(spells, session, SpellSlotType::kChannel,
                                 spells.GetChanneledSpellId());

  const auto channel_spell_id = player->Casts().GetChannelSpellId(*player);
  const auto* channel_spell =
      LookupSpellEntryForMovementInterrupts(session, channel_spell_id);
  if (channel_spell != nullptr &&
      (channel_spell->channel_interrupt_flags &
       kChannelInterruptFlagMovementStart) != 0) {
    spells.SendCancelChannelling(session, channel_spell_id);
  }
}

}

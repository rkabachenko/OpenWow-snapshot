
#include "openwow/game/world_session.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/aura_tracker.h"
#include "openwow/game/actions/adapters/protocol/wotlk_action_packets.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/combat_manager.h"
#include "openwow/game/combat_log_display.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/combat/death/adapters/ui/area_spirit_healer_controller.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/creature_sound.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/object_types.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/passenger_movement.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_channel_runtime.h"
#include "openwow/game/spell_packet_visual_dispatch.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/targeting.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/combat_packets.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/net/wotlk/spell_packets.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/effects/impact_sounds.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kManaRegenInterruptDurationMs = 5000u;
constexpr std::uint32_t kLocalRegenTickIntervalMs = 100u;
constexpr std::uint32_t kUnitSpellcastStartEvent = 0x144u;
constexpr std::uint32_t kUnitSpellcastStopEvent = 0x145u;
constexpr std::uint32_t kUnitSpellcastFailedEvent = 0x146u;
constexpr std::uint32_t kUnitSpellcastFailedQuietEvent = 0x147u;
constexpr std::uint32_t kUnitSpellcastInterruptedEvent = 0x148u;
constexpr std::uint32_t kUnitSpellcastDelayedEvent = 0x149u;
constexpr std::uint32_t kUnitSpellcastSucceededEvent = 0x14au;
constexpr std::uint32_t kUnitSpellcastChannelStartEvent = 0x14bu;
constexpr std::uint32_t kUnitSpellcastChannelUpdateEvent = 0x14cu;
constexpr std::uint32_t kUnitSpellcastChannelStopEvent = 0x14du;

constexpr int kTalentWipeInvalidTrainerSystemMessage = 0x1c5;

constexpr float kTalentWipeInteractionPadding = 4.0f;

[[nodiscard]] bool IsDuelOpponentOf(const WorldSession& session,
                                    const CGUnit_C& unit,
                                    const CGUnit_C& viewer) {
  if (!unit.Interaction().IsPlayerControlled() ||
      !viewer.Interaction().IsPlayerControlled()) {
    return false;
  }

  const CGUnit_C* const unit_player =
      unit.IsPlayer() ? &unit : unit.Interaction().ResolveControllingPlayer();
  const CGUnit_C* const viewer_player =
      viewer.IsPlayer() ? &viewer
                        : viewer.Interaction().ResolveControllingPlayer();

  if (unit_player != nullptr && viewer_player != nullptr) {
    const auto unit_team = unit_player->GetUInt32(PLAYER_DUEL_TEAM);
    const auto viewer_team = viewer_player->GetUInt32(PLAYER_DUEL_TEAM);
    return unit_team != 0u && viewer_team != 0u && unit_team != viewer_team &&
           unit_player->GetUInt64(PLAYER_DUEL_ARBITER) ==
               viewer_player->GetUInt64(PLAYER_DUEL_ARBITER);
  }

  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  const CGUnit_C* uncontrolled = nullptr;
  if (unit.GetGuid() == active_player_guid && viewer_player == nullptr) {
    uncontrolled = &viewer;
  } else if (viewer.GetGuid() == active_player_guid &&
             unit_player == nullptr) {
    uncontrolled = &unit;
  }
  if (uncontrolled == nullptr) {
    return false;
  }

  const ObjectGuid owner = uncontrolled->State().GetCharmedByOrCreatedByGUID();
  return !owner.IsEmpty() && owner == session.duel().GetOpponent();
}

bool IsLocalPlayerSpellEvent(const WorldSession& session,
                             const ObjectGuid& caster_guid);

void FireUnitSpellcastPacketEvent(WorldSession& session,
                                  const ObjectGuid caster,
                                  const std::uint32_t event_id,
                                  const std::uint32_t spell_id,
                                  const std::uint8_t cast_id) {
  const auto* const dbc = session.GetDbcLoader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    ScriptEvents_FireUnitSpellcastEvent(
        caster.GetRawValue(), event_id, nullptr);
    return;
  }

  const UnitSpellcastScriptEventPayload payload{
      .spell_name = spell->spell_name,
      .spell_rank = spell->rank,
      .cast_id = cast_id,
  };
  ScriptEvents_FireUnitSpellcastEvent(
      caster.GetRawValue(), event_id, &payload);
}

std::uint64_t FrameScriptClockMilliseconds() {
  return core::GameClock::GetTickCount32();
}

CastInfo BuildUnitCastInfo(const std::uint32_t spell_id,
                           const std::uint32_t cast_id,
                           const std::int32_t duration_ms,
                           const bool is_channel) {
  CastInfo cast;
  cast.spell_id = spell_id;
  cast.start_time = FrameScriptClockMilliseconds();
  cast.end_time = cast.start_time +
                  static_cast<std::uint64_t>(std::max(0, duration_ms));
  cast.is_channel = is_channel;
  cast.cast_id = cast_id;
  return cast;
}

CastInfo BuildResumedUnitCastInfo(const std::uint32_t spell_id,
                                  const std::uint32_t total_duration_ms,
                                  const std::uint32_t remaining_ms,
                                  const bool is_channel) {
  auto cast = BuildUnitCastInfo(
      spell_id, 0, static_cast<std::int32_t>(remaining_ms), is_channel);
  cast.start_time = cast.end_time > total_duration_ms
                        ? cast.end_time - total_duration_ms
                        : 0;
  return cast;
}

void BeginUnitCast(WorldSession& session, const ObjectGuid caster,
                   const std::uint32_t spell_id,
                   const std::uint32_t cast_id,
                   const std::int32_t duration_ms,
                   const bool is_channel) {
  auto* const unit = session.objects().GetMutableUnit(caster);
  if (unit == nullptr) {
    return;
  }

  const auto cast = BuildUnitCastInfo(
      spell_id, cast_id, duration_ms, is_channel);
  if (is_channel) {
    unit->Casts().SetChannelCast(cast);
  } else {
    unit->Casts().SetCurrentCast(cast);
  }

  if (is_channel) {
    return;
  }

  const auto* const dbc = session.GetDbcLoader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    return;
  }

  constexpr std::int32_t kSheatheStateRangedDrawn = 2;
  if ((spell->attributes & kSpellAttrRequiresRangedWeapon) != 0u) {
    unit->Animation().ChangeSheatheStateAndNotifyServer(
        kSheatheStateRangedDrawn, true, false);
  }
  if ((spell->attributes_ex2 & kSpellAttrEx2AutoRepeat) != 0u &&
      !unit->Animation().IsAutoRepeatActive()) {
    unit->Animation().SetAutoRepeatActive(true);
  }
}

void ClearUnitCast(WorldSession& session, const ObjectGuid caster,
                   const bool is_channel) {
  auto* const unit = session.objects().GetMutableUnit(caster);
  if (unit == nullptr) {
    return;
  }

  if (is_channel) {
    unit->Casts().ClearChannelCast();
  } else {
    unit->Casts().ClearCurrentCast();
  }
}

void UpdateUnitChannelEndTime(WorldSession& session,
                              const ObjectGuid caster,
                              const std::int32_t remaining_ms) {
  auto* const unit = session.objects().GetMutableUnit(caster);
  if (unit == nullptr || !unit->Casts().IsChanneling()) {
    return;
  }

  auto cast = unit->Casts().GetChannelCast();
  cast.end_time = FrameScriptClockMilliseconds() +
                  static_cast<std::uint64_t>(std::max(0, remaining_ms));
  unit->Casts().SetChannelCast(cast);
}

bool ApplyUnitSpellFailure(WorldSession& session,
                            const net::wotlk::SpellFailureData& failure) {
  if (auto* const unit = session.objects().GetMutableUnit(failure.caster_guid);
      unit != nullptr && unit->Casts().IsCasting()) {
    const auto& cast = unit->Casts().GetCurrentCast();
    if (cast.spell_id == failure.spell_id &&
        cast.cast_id == failure.cast_count) {
      unit->Casts().ClearCurrentCast();
    }
  }

  QueueSpellStopVisual(
      session, failure.caster_guid, failure.spell_id);

  std::uint32_t event_id = kUnitSpellcastFailedEvent;
  const auto result = static_cast<SpellCastResult>(failure.result);
  if (result == SpellCastResult::kDontReport ||
      result == SpellCastResult::kSpellInProgress ||
      result == SpellCastResult::kCharmed) {
    event_id = kUnitSpellcastFailedQuietEvent;
  } else if (result == SpellCastResult::kInterrupted ||
             result == SpellCastResult::kInterruptedCombat) {
    event_id = kUnitSpellcastInterruptedEvent;
  }
  FireUnitSpellcastPacketEvent(
      session, failure.caster_guid, event_id, failure.spell_id,
      failure.cast_count);

  const bool is_local =
      IsLocalPlayerSpellEvent(session, failure.caster_guid);
  if (is_local) {
    session.spells().OnSpellFailed(failure.spell_id, failure.cast_count);
  }
  return is_local;
}

void FireTradeSkillUpdates(const std::uint32_t count = 1) {
  for (std::uint32_t index = 0; index < count; ++index) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::TRADE_SKILL_UPDATE);
  }
}

void ClearFailedTradeSkillSpell(const std::uint32_t spell_id) {
  FireTradeSkillUpdates(
      ProfessionSystem::Get().ClearTradeSkillSpell(spell_id));
}

AuraData SlotInfoToAuraData(const AuraSlotInfo& info,
                            const ObjectGuid& target_guid,
                            const ObjectGuid& local_player_guid) {
  AuraData data;
  data.spell_id = info.spell_id;

  data.caster_guid = info.caster_guid.IsEmpty() ? target_guid : info.caster_guid;
  data.duration = info.max_duration;
  data.stacks = info.stack_or_charges;
  data.charges = static_cast<std::int32_t>(info.stack_or_charges);
  data.slot = info.slot;
  data.raw_flags = static_cast<std::uint8_t>(info.flags);
  data.has_raw_flags = true;
  data.is_mine = (data.caster_guid == local_player_guid);
  data.is_cancellable = HasFlag(info.flags, AuraFlag::kPositive);
  data.effect_mask =
      (HasFlag(info.flags, AuraFlag::kEffIndex0) ? 1u : 0u) |
      (HasFlag(info.flags, AuraFlag::kEffIndex1) ? 2u : 0u) |
      (HasFlag(info.flags, AuraFlag::kEffIndex2) ? 4u : 0u);

  if (info.remaining_duration > 0) {
    const auto now_ms = core::GameClock::GetTickCount32();
    data.expiration = now_ms + info.remaining_duration;
  }

  return data;
}

AuraInfo SlotInfoToUnitAura(const AuraSlotInfo& info,
                            const ObjectGuid& target_guid) {
  AuraInfo aura;
  aura.spell_id = info.spell_id;
  aura.flags = static_cast<std::uint32_t>(info.flags);
  aura.stack_count = info.stack_or_charges;
  aura.duration = static_cast<std::int32_t>(info.max_duration);
  aura.remaining = static_cast<std::int32_t>(info.remaining_duration);
  aura.caster_guid = info.caster_guid.IsEmpty() ? target_guid : info.caster_guid;
  return aura;
}

std::vector<AuraInfo> BuildUnitAuraSnapshot(
    const ObjectGuid& target_guid,
    const std::vector<AuraSlotInfo>& raw_slots) {
  std::vector<AuraInfo> auras;
  auras.reserve(raw_slots.size());
  for (const auto& slot : raw_slots) {
    auras.push_back(SlotInfoToUnitAura(slot, target_guid));
  }
  return auras;
}

void SyncTrackedUnitAuraState(WorldSession& session,
                              const ObjectGuid& target,
                              const std::vector<AuraSlotInfo>& auras) {
  if (auto* unit = session.objects().GetMutableUnit(target); unit != nullptr) {
    unit->Auras().SetAuras(BuildUnitAuraSnapshot(target, auras));
  }

  auto local_guid = session.objects().GetLocalPlayerGuid();
  auto& tracker = AuraTracker::Get();
  tracker.ClearAuras(target);
  for (const auto& info : auras) {
    if (info.spell_id == 0) {
      continue;
    }

    auto data = SlotInfoToAuraData(info, target, local_guid);
    tracker.SetAura(target, info.slot, data);
  }
}

[[nodiscard]] std::uint32_t ComputeAuraVisionGateMask(
    const data::dbc::SpellEntry& spell) {
  std::uint32_t mask = 0;
  if (spell.required_aura_vision > 0) {
    mask = 1u << ((spell.required_aura_vision - 1u) & 0x1Fu);
  }
  for (std::size_t index = 0; index < spell.effect_apply_aura.size();
       ++index) {
    constexpr std::uint32_t kAuraModStealthDetect = 17u;
    constexpr std::uint32_t kAuraModInvisibilityDetect = 19u;
    constexpr std::int32_t kInvisibilityTypeGeneral = 0;
    constexpr std::int32_t kInvisibilityTypeDrunk = 10;
    if (spell.effect_apply_aura[index] == kAuraModStealthDetect) {
      mask |= 0x20u;
    } else if (spell.effect_apply_aura[index] == kAuraModInvisibilityDetect &&
               (spell.effect_misc_value[index] == kInvisibilityTypeGeneral ||
                spell.effect_misc_value[index] == kInvisibilityTypeDrunk)) {
      mask |= 0x40u;
    }
  }
  return mask;
}

[[nodiscard]] std::uint32_t LocalPlayerAuraVisionMask(WorldSession& session) {
  const auto* const player = session.objects().GetLocalPlayerTyped();
  const auto* const dbc = session.GetDbcLoader();
  if (player == nullptr || dbc == nullptr) {
    return 0;
  }
  std::uint32_t mask = 0;
  constexpr std::uint8_t kStealthVisFlag = 0x02u;
  if ((player->State().GetVisFlags() & kStealthVisFlag) != 0u) {
    mask |= 0x20u;
  }
  constexpr std::uint32_t kAuraModInvisibility = 18u;
  AuraTracker::Get().ForEachAuraAll(
      player->GetGuid(),
      [&mask, dbc](std::uint8_t, const AuraData& aura) {
        if (aura.spell_id == 0u) {
          return;
        }
        const auto* const spell = dbc->spell().LookupEntry(aura.spell_id);
        if (spell == nullptr) {
          return;
        }
        for (std::size_t index = 0;
             index < spell->effect_apply_aura.size(); ++index) {
          if (spell->effect_apply_aura[index] != kAuraModInvisibility) {
            continue;
          }
          const auto type = spell->effect_misc_value[index];
          if (type >= 0) {
            mask |= 1u << (static_cast<std::uint32_t>(type) & 0x1Fu);
            if (type == 0 || type == 10) {
              mask |= 0x40u;
            }
          }
        }
      });
  return mask;
}

void RouteAuraVisualChanges(WorldSession& session,
                            const AuraUpdateDiff& diff) {
  auto* const unit = session.objects().GetMutableUnit(diff.target);
  const auto* const dbc = session.GetDbcLoader();
  if (unit == nullptr || dbc == nullptr) {
    return;
  }

  const auto resolve_visual = [dbc](const std::uint32_t spell_id) {
    const auto* const spell = dbc->spell().LookupEntry(spell_id);
    if (spell == nullptr || spell->spell_visual[0] == 0u) {
      return std::pair<std::uint32_t,
                       const data::dbc::SpellVisualEntry*>{0u, nullptr};
    }
    const auto visual_id = spell->spell_visual[0];
    return std::pair{visual_id,
                     dbc->spell_visual().LookupEntry(visual_id)};
  };

  for (const auto& change : diff.changes) {
    if (change.old_value.spell_id != 0u &&
        change.old_value.spell_id != change.new_value.spell_id) {

      unit->SpellVisuals().EndSpellVisualProcState(change.old_value.spell_id);
      unit->SpellVisuals().RemoveEffectsBySpellId(
          session, change.old_value.spell_id,
          true);
      const auto [visual_id, visual] =
          resolve_visual(change.old_value.spell_id);
      if (visual != nullptr) {
        unit->SpellVisuals().QueueAuraVisualStop(
            change.slot, change.old_value.spell_id, visual_id,
            visual->state_kit);
        if (visual->state_done_kit != 0u) {
          (void)unit->SpellVisuals().CreateFromKit(
              session, visual->state_done_kit, 1u,
              nullptr, false, {}, change.old_value.spell_id, visual_id,
              SpellVisualPresentationPhase::kStateDone,
              SpellVisualLifecycleAction::kTransient, change.slot);
        }
      }
    }

    if (change.new_value.spell_id != 0u &&
        change.new_value.spell_id != change.old_value.spell_id) {
      const auto [visual_id, visual] =
          resolve_visual(change.new_value.spell_id);

      const auto* const gated_spell =
          dbc->spell().LookupEntry(change.new_value.spell_id);
      if (gated_spell != nullptr) {
        const auto gate = ComputeAuraVisionGateMask(*gated_spell);
        if (gate != 0u &&
            (LocalPlayerAuraVisionMask(session) & gate) == 0u) {
          continue;
        }
      }
      if (visual != nullptr && visual->state_kit != 0u) {
        (void)unit->SpellVisuals().CreateFromKit(
            session, visual->state_kit, 0u,
            nullptr,
            HasFlag(change.new_value.flags, AuraFlag::kNegative), {},
            change.new_value.spell_id, visual_id,
            SpellVisualPresentationPhase::kState,
            SpellVisualLifecycleAction::kAuraStart, change.slot);
      }
    }
  }
}

void FireRunePowerUpdate(const int rune_index, const bool is_ready) {
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::RUNE_POWER_UPDATE, {rune_index + 1, is_ready});
}

void RefreshRuneUsability(WorldSession& session);

void ApplyLocalRuneMaskUpdate(WorldSession& session,
                              const net::wotlk::RuneData& rune_data) {
  std::array<std::uint32_t, kClientTrackedRuneSlots> previous_start_ticks{};
  for (int i = 0; i < kClientTrackedRuneSlots; ++i) {
    previous_start_ticks[static_cast<std::size_t>(i)] =
        session.runes().runes()[static_cast<std::size_t>(i)].cooldown_start_ms;
  }

  session.runes().HandleSpellGoRuneData(
      rune_data.mask_before, rune_data.mask_after, rune_data.cooldowns);

  bool usability_changed = false;
  for (int i = 0; i < kClientTrackedRuneSlots; ++i) {
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << i);
    const bool had_rune_before = (rune_data.mask_before & bit) != 0;
    const bool has_rune_after = (rune_data.mask_after & bit) != 0;

    if (had_rune_before && !has_rune_after) {
      if (previous_start_ticks[static_cast<std::size_t>(i)] == 0) {
        FireRunePowerUpdate(i, false);
        usability_changed = true;
      }
      continue;
    }

    if ((!had_rune_before && has_rune_after) ||
        (has_rune_after && previous_start_ticks[static_cast<std::size_t>(i)] != 0)) {
      FireRunePowerUpdate(i, true);
      usability_changed = true;
    }
  }

  if (usability_changed) {
    RefreshRuneUsability(session);
  }
}

void RefreshRuneUsability(WorldSession& session) {
  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireEvent(ui::game::events::SPELL_UPDATE_USABLE);
  if (::openwow::ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
  dispatch.FirePetBarUpdateUsable();
}

void RefreshPowerDrivenUsability(WorldSession& session, const ObjectGuid& unit_guid) {
  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }

  const auto player_guid = player->GetGuid();
  const auto pet_guid = player->State().GetPetGUID();
  if (unit_guid != player_guid && (pet_guid.IsEmpty() || unit_guid != pet_guid)) {
    return;
  }

  session.spellbook_private_usability().RefreshPower(session);
}

bool FinalizePredictedPowerMutation(WorldSession& session,
                                    const ObjectGuid& unit_guid,
                                    const std::uint8_t power_type,
                                    const PredictedPowerMutationResult& result) {
  if (!result.changed) {
    return false;
  }

  if (result.reached_max) {
    ui::game::ScriptEventDispatch::Get().FireUnitPowerSpecific(unit_guid.GetRawValue(), power_type);
  }
  RefreshPowerDrivenUsability(session, unit_guid);
  return true;
}

bool IsManaPowerSpell(const WorldSession& session, const std::uint32_t spell_id,
                      const bool ) {
  if (spell_id == 0u) {
    return false;
  }

  if (const auto query = SpellQueryBridge::Get().Query(spell_id); query.has_value()) {
    if (query->powerType == PowerType::kMana && query->manaCost > 0u) {
      return true;
    }
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr || spell->power_type != static_cast<std::uint32_t>(PowerType::kMana)) {
    return false;
  }

  return spell->mana_cost != 0u || spell->mana_cost_percentage != 0u ||
         spell->mana_per_second != 0u || spell->mana_per_second_per_level != 0u;
}

bool UnitHasManaRegenInterruptingChannel(const WorldSession& session, const CGUnit_C& unit) {
  return IsManaPowerSpell(session, unit.Casts().GetChannelSpellId(unit), true);
}

float ResolvePredictedRegenRate(const WorldSession& session, const CGUnit_C& unit,
                                const std::int32_t power_type,
                                const std::uint32_t current_time_ms) {
  if (power_type < 0 || power_type > 6) {
    return 0.0f;
  }

  const auto power_type_u8 = static_cast<std::uint8_t>(power_type);
  if (power_type == 0) {
    if (unit.Vitals().HasManaRegenInterrupt(current_time_ms) ||
        UnitHasManaRegenInterruptingChannel(session, unit)) {
      return unit.GetPowerRegenRateInterrupted(power_type_u8);
    }
    return unit.GetPowerRegenRate(power_type_u8);
  }

  if (unit.State().IsInCombat()) {
    return unit.GetPowerRegenRateInterrupted(power_type_u8);
  }
  return unit.GetPowerRegenRate(power_type_u8);
}

void UpdatePredictedRegenForPower(WorldSession& session, CGUnit_C& unit,
                                  const std::int32_t power_type,
                                  const std::uint32_t elapsed_ms,
                                  const std::uint32_t current_time_ms) {
  if (power_type < 0 || power_type > 6) {
    return;
  }

  const auto result = unit.Vitals().AdvanceRegen(
      unit, power_type,
      ResolvePredictedRegenRate(session, unit, power_type, current_time_ms),
      elapsed_ms);
  (void)FinalizePredictedPowerMutation(session, unit.GetGuid(),
                                       static_cast<std::uint8_t>(power_type), result);
}

void UpdatePredictedRegenForUnit(WorldSession& session, CGUnit_C& unit,
                                 const std::uint32_t elapsed_ms,
                                 const std::uint32_t current_time_ms) {
  const std::int32_t display_power_type = unit.State().GetPowerType();
  UpdatePredictedRegenForPower(session, unit, display_power_type, elapsed_ms, current_time_ms);

  if (unit.Animation().GetShapeshiftForm() != 11u) {
    return;
  }

  if (display_power_type != 0) {
    UpdatePredictedRegenForPower(session, unit, 0, elapsed_ms, current_time_ms);
  }
  if (display_power_type != 3) {
    UpdatePredictedRegenForPower(session, unit, 3, elapsed_ms, current_time_ms);
  }
}

bool ApplyCombatLogPredictedPowerDelta(WorldSession& session,
                                       const ObjectGuid& unit_guid,
                                       const std::uint8_t power_type,
                                       const std::int32_t delta) {
  auto* unit = session.objects().GetMutableUnit(unit_guid);
  if (unit == nullptr) {
    return false;
  }

  return FinalizePredictedPowerMutation(
      session, unit_guid, power_type,
      unit->Vitals().ModifyDisplayedPower(*unit, power_type, delta));
}

bool ApplyAbsolutePowerPrediction(WorldSession& session,
                                  const ObjectGuid& unit_guid,
                                  const std::uint8_t power_type,
                                  const std::uint32_t value) {
  auto* unit = session.objects().GetMutableUnit(unit_guid);
  if (unit == nullptr) {
    return false;
  }

  return FinalizePredictedPowerMutation(
      session, unit_guid, power_type,
      unit->Vitals().Set(*unit, power_type, value));
}

void ApplySpellEnergizePrediction(WorldSession& session, PacketReader reader) {
  ObjectGuid target;
  ObjectGuid source;
  std::uint32_t spell_id = 0;
  std::uint32_t power_type = 0;
  std::uint32_t amount = 0;
  if (!reader.ReadPackedGuid(target) || !reader.ReadPackedGuid(source) ||
      !reader.ReadU32(spell_id) || !reader.ReadU32(power_type) || !reader.ReadU32(amount)) {
    return;
  }

  (void)source;
  (void)spell_id;
  if (power_type > 6) {
    return;
  }

  ApplyCombatLogPredictedPowerDelta(
      session, target, static_cast<std::uint8_t>(power_type), static_cast<std::int32_t>(amount));
}

void ApplySpellEnergizePrediction(WorldSession& session, const net::wotlk::WorldPacket& pkt) {
  ApplySpellEnergizePrediction(
      session, PacketReader(pkt.payload.data(), pkt.payload.size()));
}

void ApplyPeriodicAuraPowerPrediction(WorldSession& session, PacketReader reader) {
  ObjectGuid target;
  ObjectGuid caster;
  std::uint32_t spell_id = 0;
  std::uint32_t effect_count = 0;
  if (!reader.ReadPackedGuid(target) || !reader.ReadPackedGuid(caster) ||
      !reader.ReadU32(spell_id) || !reader.ReadU32(effect_count)) {
    return;
  }

  (void)caster;
  (void)spell_id;
  for (std::uint32_t record_index = 0; record_index < effect_count;
       ++record_index) {
    std::uint32_t aura_type = 0;
    if (!reader.ReadU32(aura_type)) return;

    std::uint32_t power_type = 0;
    std::uint32_t amount = 0;
    switch (aura_type) {
      case 21u:
      case 24u:
        if (!reader.ReadU32(power_type) || !reader.ReadU32(amount)) {
          return;
        }
        if (power_type <= 6u) {
          ApplyCombatLogPredictedPowerDelta(
              session, target, static_cast<std::uint8_t>(power_type),
              static_cast<std::int32_t>(amount));
        }
        break;

      case 64u: {
        float multiplier = 0.0f;
        if (!reader.ReadU32(power_type) || !reader.ReadU32(amount) ||
            !reader.ReadFloat(multiplier)) {
          return;
        }
        (void)multiplier;
        if (power_type <= 6u) {
          ApplyCombatLogPredictedPowerDelta(
              session, target, static_cast<std::uint8_t>(power_type),
              -static_cast<std::int32_t>(amount));
        }
        break;
      }

      case 8u:
      case 20u: {
        std::uint32_t ignored = 0;
        std::uint8_t critical = 0;
        if (!reader.ReadU32(ignored) || !reader.ReadU32(ignored) ||
            !reader.ReadU32(ignored) || !reader.ReadU8(critical)) {
          return;
        }
        break;
      }

      case 3u:
      case 89u: {
        std::uint32_t ignored = 0;
        std::uint8_t critical = 0;
        if (!reader.ReadU32(ignored) || !reader.ReadU32(ignored) ||
            !reader.ReadU32(ignored) || !reader.ReadU32(ignored) ||
            !reader.ReadU32(ignored) || !reader.ReadU8(critical)) {
          return;
        }
        break;
      }

      default:
        return;
    }
  }
}

void ApplyPeriodicAuraPowerPrediction(WorldSession& session,
                                      const net::wotlk::WorldPacket& pkt) {
  ApplyPeriodicAuraPowerPrediction(
      session, PacketReader(pkt.payload.data(), pkt.payload.size()));
}

void ApplyAttackerStateRagePrediction(WorldSession& session, const net::wotlk::WorldPacket& pkt) {
  const auto parsed =
      net::wotlk::ParseAttackerStateUpdate(pkt.payload.data(), pkt.payload.size());
  if (!parsed.has_value() ||
      (parsed->hit_info & net::wotlk::CombatHitInfo::kRageGain) == 0u ||
      parsed->rage_gain == 0u) {
    return;
  }

  ApplyCombatLogPredictedPowerDelta(
      session, parsed->attacker, 1u, static_cast<std::int32_t>(parsed->rage_gain));
}

bool IsLocalPlayerSpellEvent(const WorldSession& session,
                             const ObjectGuid& caster_guid) {
  const ObjectGuid local_player_guid = session.objects().GetLocalPlayerGuid();
  return !local_player_guid.IsEmpty() && caster_guid == local_player_guid;
}

bool ReadLearnedSpellPacket(const net::wotlk::WorldPacket& pkt,
                            std::uint32_t& spell_id,
                            std::uint16_t& learn_flags) {
  if (pkt.payload.size() < sizeof(spell_id) + sizeof(learn_flags)) {
    return false;
  }

  std::memcpy(&spell_id, pkt.payload.data(), sizeof(spell_id));
  std::memcpy(&learn_flags, pkt.payload.data() + sizeof(spell_id),
              sizeof(learn_flags));
  return true;
}

bool ReadSupercededSpellIds(const net::wotlk::WorldPacket& pkt,
                            std::uint32_t& old_spell_id,
                            std::uint32_t& new_spell_id) {
  if (pkt.payload.size() < sizeof(old_spell_id) + sizeof(new_spell_id)) {
    return false;
  }

  std::memcpy(&old_spell_id, pkt.payload.data(), sizeof(old_spell_id));
  std::memcpy(&new_spell_id, pkt.payload.data() + sizeof(old_spell_id),
              sizeof(new_spell_id));
  return true;
}

void NotifyChangedActionSlots(WorldSession& session,
                              const std::vector<std::size_t>& changed_slots) {
  if (changed_slots.empty()) {
    return;
  }

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  for (const auto slot_index : changed_slots) {
    dispatch.FireActionbarSlotChanged(
        static_cast<std::uint8_t>(slot_index + 1));
  }

  if (::openwow::ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
}

std::optional<std::array<float, 3>> ProjectSpellPosition(
    const ObjectManager& objects, const net::wotlk::SpellPosition& position) {
  const std::array<float, 3> local{position.x, position.y, position.z};
  if (position.transport_guid.IsEmpty()) {
    return local;
  }

  float parent_transform[16];
  if (Movement_GetObjectTransform(objects,
                                  position.transport_guid.GetRawValue(),
                                  parent_transform) == 0) {
    return std::nullopt;
  }

  std::array<float, 3> world{};
  Passenger_TransformLocalPointToWorld(
      world.data(), local.data(), parent_transform);
  return world;
}

}

void WorldSession::DispatchPendingTriggerCinematicIfReady() {
  if (pending_trigger_cinematic_sequence_id_ == 0 ||
      !openwow::net::ClientServices::Instance().IsWorldSessionReady() ||
      !cinematic_callbacks_.on_trigger_cinematic) {
    return;
  }

  const auto cinematic_sequence_id = pending_trigger_cinematic_sequence_id_;
  pending_trigger_cinematic_sequence_id_ = 0;
  spell_visual_.BeginCinematic();
  cinematic_callbacks_.on_trigger_cinematic(cinematic_sequence_id);
}

void WorldSession::HandleInitialSpells(const net::wotlk::WorldPacket& pkt) {

  (void)spell_book_.HandleInitialSpells(pkt.payload.data(),
                                        pkt.payload.size());
  spellbook_private_usability_.Reset();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Received " + std::to_string(spell_book_.spell_count()) + " initial spells");
}

void WorldSession::HandleLearnedSpell(const net::wotlk::WorldPacket& pkt) {
  if (!spell_book_.HandleLearnedSpell(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  std::uint32_t learned_spell_id = 0;
  std::uint16_t learn_flags = 0;
  if (!ReadLearnedSpellPacket(pkt, learned_spell_id, learn_flags)) {
    return;
  }

  if (learn_flags != 0) {
    NotifyChangedActionSlots(
        *this, action_assignments_.ReplaceSpellActionReferences(0, learned_spell_id));
  }
}

void WorldSession::HandleRemovedSpell(const net::wotlk::WorldPacket& pkt) {
  spell_book_.HandleRemovedSpell(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSupercededSpell(const net::wotlk::WorldPacket& pkt) {
  if (!spell_book_.HandleSupercededSpell(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  std::uint32_t old_spell_id = 0;
  std::uint32_t new_spell_id = 0;
  if (!ReadSupercededSpellIds(pkt, old_spell_id, new_spell_id)) {
    return;
  }

  NotifyChangedActionSlots(
      *this,
      action_assignments_.ReplaceSpellActionReferences(old_spell_id, new_spell_id));
}

void WorldSession::HandleSpellCooldown(const net::wotlk::WorldPacket& pkt) {
  spell_book_.HandleSpellCooldown(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleCastFailed(const net::wotlk::WorldPacket& pkt) {
  const auto failure =
      net::wotlk::ParseCastFailed(pkt.payload.data(), pkt.payload.size());
  if (!failure.has_value()) {
    return;
  }

  const auto* const player = objects().GetLocalPlayerTyped();
  if (player != nullptr) {
    const ObjectGuid player_guid = player->GetGuid();
    if (auto* const unit = objects().GetMutableUnit(player_guid);
        unit != nullptr && unit->Casts().IsCasting()) {
      const auto& cast = unit->Casts().GetCurrentCast();
      if (cast.spell_id == failure->spell_id &&
          cast.cast_id == failure->cast_count) {
        unit->Casts().ClearCurrentCast();
      }
    }

    QueueSpellStopVisual(*this, player_guid, failure->spell_id);
    std::uint32_t event_id = kUnitSpellcastFailedEvent;
    const auto result = static_cast<SpellCastResult>(failure->result);
    if (result == SpellCastResult::kDontReport ||
        result == SpellCastResult::kSpellInProgress ||
        result == SpellCastResult::kCharmed) {
      event_id = kUnitSpellcastFailedQuietEvent;
    } else if (result == SpellCastResult::kInterrupted ||
               result == SpellCastResult::kInterruptedCombat) {
      event_id = kUnitSpellcastInterruptedEvent;
    }
    FireUnitSpellcastPacketEvent(
        *this, player_guid, event_id, failure->spell_id, failure->cast_count);

    if (event_id != kUnitSpellcastFailedQuietEvent) {
      SpellAction_DisplaySpellFailure(*this, failure->spell_id, player_guid,
                                      failure->result);
    }

    spell_cast_runtime_.OnSpellFailed(
        failure->spell_id, failure->cast_count);

    spell_book_.CancelGlobalCooldown(failure->spell_id);
  }
  if (cast_bar_callbacks_.on_cast_interrupt) {
    cast_bar_callbacks_.on_cast_interrupt(failure->spell_id);
  }
  ClearFailedTradeSkillSpell(failure->spell_id);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Cast failed: spell=" +
                         std::to_string(failure->spell_id) +
                         " result=" + std::to_string(failure->result) +
                         " (" + SpellCastResultToString(
                             static_cast<SpellCastResult>(failure->result)) + ")");
}

void WorldSession::HandleSpellStart(const net::wotlk::WorldPacket& pkt) {
  if (auto info = net::wotlk::ParseSpellStart(pkt.payload.data(), pkt.payload.size())) {
    combat_log_.HandleSpellCastStart(
        objects(),
        info->caster_unit_guid.GetRawValue(),
        info->spell_id,
        info->cast_count,
        info->cast_time);

    const bool is_local_player_cast =
        IsLocalPlayerSpellEvent(*this, info->caster_unit_guid);

    if (info->cast_time > 0) {
      BeginUnitCast(*this, info->caster_unit_guid, info->spell_id,
                    info->cast_count,
                    static_cast<std::int32_t>(info->cast_time), false);
      FireUnitSpellcastPacketEvent(
          *this, info->caster_unit_guid, kUnitSpellcastStartEvent,
          info->spell_id, info->cast_count);
    }

    QueueSpellStartVisual(*this, info->caster_unit_guid, info->spell_id);

    if (is_local_player_cast) {

      ProfessionSystem::Get().TransferPlayerCraftToNpc(info->spell_id);

      auto& spell_client = spell_cast_runtime_;
      spell_client.OnSpellStart(
          info->spell_id, info->cast_count,
          info->caster_guid,
          info->targets.object_target_guid.IsEmpty()
              ? info->targets.item_target_guid
              : info->targets.object_target_guid);

      if (info->rune_data.has_value()) {
        ApplyLocalRuneMaskUpdate(*this, *info->rune_data);
      }
    } else if (targeting_system_ != nullptr) {

      targeting_system_->HandleServerSpellStart(
          info->caster_unit_guid.GetRawValue(),
          net::wotlk::HasFlag(info->targets.target_mask,
                              net::wotlk::SpellCastTargetFlags::kUnit),
          info->targets.object_target_guid.GetRawValue());
    }

    if (spell_visual_callbacks_.on_spell_start) {
      spell_visual_callbacks_.on_spell_start(
          info->caster_unit_guid.GetRawValue(), info->spell_id);
    }

    if (cast_bar_callbacks_.on_cast_start &&
        is_local_player_cast &&
        info->cast_time > 0) {
      cast_bar_callbacks_.on_cast_start(info->spell_id, info->cast_time);
    }

  } else {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Rejected malformed SMSG_SPELL_START payload (" +
            std::to_string(pkt.payload.size()) + " bytes)");
  }
}

void WorldSession::HandleSpellGo(const net::wotlk::WorldPacket& pkt) {
  if (auto info = net::wotlk::ParseSpellGo(pkt.payload.data(), pkt.payload.size())) {
    SpellGoVisualData visual_data;
    visual_data.missile_caster_guid = info->caster_guid.GetRawValue();
    visual_data.cast_count = info->cast_count;
    visual_data.explicit_target_guid =
        info->targets.object_target_guid.GetRawValue();
    visual_data.hit_targets.reserve(info->hit_targets.size());
    for (const auto& target : info->hit_targets) {
      visual_data.hit_targets.push_back(target.GetRawValue());
    }
    visual_data.miss_targets.reserve(info->miss_targets.size());
    for (const auto& target : info->miss_targets) {
      visual_data.miss_targets.push_back(SpellVisualMissTargetContext{
          .guid = target.target.GetRawValue(),
          .reason = static_cast<std::uint8_t>(target.reason),
          .reflect_result = static_cast<std::uint8_t>(target.reflect),
      });
    }
    std::optional<std::array<float, 3>> visual_destination;
    if (info->targets.dest_location.has_value()) {
      visual_destination =
          ProjectSpellPosition(objects(), *info->targets.dest_location);
    }
    if (visual_destination.has_value()) {
      visual_data.has_destination = true;
      visual_data.destination_x = (*visual_destination)[0];
      visual_data.destination_y = (*visual_destination)[1];
      visual_data.destination_z = (*visual_destination)[2];
    }

    visual_data.extra_targets.reserve(info->extra_targets.size());
    for (const auto& target : info->extra_targets) {
      const auto world_position = ProjectSpellPosition(
          objects(), net::wotlk::SpellPosition{
                         .transport_guid = target.transport_guid,
                         .x = target.x,
                         .y = target.y,
                         .z = target.z,
                     });

      if (!world_position.has_value()) continue;
      visual_data.extra_targets.push_back(SpellVisualExtraTargetContext{
          .world_x = (*world_position)[0],
          .world_y = (*world_position)[1],
          .world_z = (*world_position)[2],
      });
    }

    if (info->ammo.has_value()) {
      visual_data.missile_item = SpellVisualItemModelContext{
          .display_id = info->ammo->display_id,
          .inventory_type = info->ammo->inventory_type,
      };
    } else {
      const ObjectGuid item_guid =
          !info->targets.item_target_guid.IsEmpty()
              ? info->targets.item_target_guid
              : info->caster_guid;
      if (const auto* item = objects().GetItem(item_guid); item != nullptr) {
        if (const auto* item_template = item->GetItemTemplate();
            item_template != nullptr) {
          visual_data.missile_item = SpellVisualItemModelContext{
              .display_id = item_template->display_id,
              .inventory_type =
                  static_cast<std::uint32_t>(item_template->inventory_type),
          };
        }
      }
    }

    if (const auto* caster = objects().GetPlayer(info->caster_unit_guid);
        caster != nullptr) {
      const std::array<std::optional<SpellVisualItemModelContext>*, 3>
          weapon_contexts{
              &visual_data.main_hand_weapon,
              &visual_data.off_hand_weapon,
              &visual_data.ranged_weapon,
          };
      for (std::uint8_t weapon_slot = 0;
           weapon_slot < weapon_contexts.size(); ++weapon_slot) {
        const auto metadata =
            caster->GetVisibleWeaponSlotMetadata(weapon_slot, true);
        if (metadata.has_value()) {
          *weapon_contexts[weapon_slot] = SpellVisualItemModelContext{
              .display_id = metadata->display_id,
              .inventory_type = metadata->inventory_type,
          };
        }
      }
    }

    ObjectGuid visual_caster = info->caster_unit_guid;
    if (objects().GetMutableUnit(visual_caster) == nullptr &&
        objects().GetMutableUnit(info->caster_guid) != nullptr) {
      visual_caster = info->caster_guid;
    }
    QueueSpellGoVisual(*this, visual_caster, info->spell_id,
                       info->hit_targets, visual_destination, visual_data);

    combat_log_.HandleSpellCastSuccess(
        objects(),
        info->caster_unit_guid.GetRawValue(),
        (info->targets.object_target_guid.IsEmpty()
             ? info->targets.item_target_guid
             : info->targets.object_target_guid)
            .GetRawValue(),
        info->spell_id,
        info->cast_count);

    const bool is_local_player_cast =
        IsLocalPlayerSpellEvent(*this, info->caster_unit_guid);

    if (is_local_player_cast) {
      spell_cast_runtime_.OnSpellGo(
          info->spell_id, info->cast_count);
      const auto* const spell = dbc_ != nullptr
                                    ? dbc_->spell().LookupEntry(info->spell_id)
                                    : nullptr;
      const bool cooldown_starts_on_event =
          spell != nullptr &&
          CheckCooldownStartsOnEvent(*this, *spell, info->caster_guid);

      if (!cooldown_starts_on_event) {
        spell_book_.RecordSuccessfulCastRecovery(info->spell_id);
      }
    }

    if (!is_local_player_cast) {
      if (const auto* const caster = objects().GetUnit(info->caster_unit_guid);
          caster != nullptr) {
        const auto owner_guid = caster->State().GetCharmedBy().IsEmpty()
                                    ? caster->State().GetSummonedBy()
                                    : caster->State().GetCharmedBy();
        if (!owner_guid.IsEmpty() &&
            owner_guid == objects().GetLocalPlayerGuid()) {
          spell_book_.RecordPetSpellGoCooldown(
              info->spell_id, info->caster_unit_guid,
              static_cast<std::uint32_t>(info->cast_flags));
        }
      }
    }

    if (info->rune_data.has_value() && is_local_player_cast) {
      ApplyLocalRuneMaskUpdate(*this, *info->rune_data);
    }

    if (is_local_player_cast && info->missile.has_value() &&
        info->missile->delay_time != 0u) {
      const auto* spell = dbc_ != nullptr
                              ? dbc_->spell().LookupEntry(info->spell_id)
                              : nullptr;
      const auto* missile = spell != nullptr
                                ? dbc_->spell_missile().LookupEntry(
                                      spell->spell_missile_id)
                                : nullptr;
      if (missile != nullptr && (missile->flags & 1u) != 0u) {
        if (auto* caster = objects().GetMutableUnit(info->caster_unit_guid);
            caster != nullptr) {
          const std::uint32_t now =
              CurrentClientTimeMs() != 0u
                  ? CurrentClientTimeMs()
                  : openwow::core::GameClock::GetTickCount32();
          caster->Casts().CompleteDelayedMissileTrajectory(
              info->spell_id, now, info->missile->delay_time);
        }
      }
    }

    bool completed_current_cast = false;
    if (auto* const unit = objects().GetMutableUnit(info->caster_unit_guid);
        unit != nullptr && unit->Casts().IsCasting()) {
      const auto& cast = unit->Casts().GetCurrentCast();
      completed_current_cast = cast.spell_id == info->spell_id &&
                               cast.cast_id == info->cast_count;
      if (completed_current_cast) {
        unit->Casts().ClearCurrentCast();
      }
    }
    FireUnitSpellcastPacketEvent(
        *this, info->caster_unit_guid, kUnitSpellcastSucceededEvent,
        info->spell_id,
        info->cast_count);
    if (completed_current_cast) {
      FireUnitSpellcastPacketEvent(
          *this, info->caster_unit_guid, kUnitSpellcastStopEvent,
          info->spell_id, info->cast_count);
    }

    NotifyLocalUnitManaSpellcast(info->caster_unit_guid, info->spell_id);

    if (spell_visual_callbacks_.on_spell_go) {
      spell_visual_callbacks_.on_spell_go(
          visual_caster.GetRawValue(), info->spell_id, visual_data);
    }
    if (cast_bar_callbacks_.on_cast_complete &&
        is_local_player_cast) {
      cast_bar_callbacks_.on_cast_complete(info->spell_id);
    }

    const auto raw_cast_flags =
        static_cast<std::uint32_t>(info->cast_flags);
    if (is_local_player_cast && (raw_cast_flags & 0x41u) == 0u) {

      ProfessionSystem::Get().TransferPlayerCraftToNpc(info->spell_id);
      switch (ProfessionSystem::Get().OnTradeSkillSpellComplete(
          info->spell_id)) {
        case ProfessionSystem::TradeSkillCompletionAction::kCleared:
          FireTradeSkillUpdates();
          break;
        case ProfessionSystem::TradeSkillCompletionAction::kRepeat:
          interaction().SendCastSpell(info->spell_id, 0, 0);
          FireTradeSkillUpdates();
          break;
        case ProfessionSystem::TradeSkillCompletionAction::kNone:
        case ProfessionSystem::TradeSkillCompletionAction::kCompleted:
          break;
      }
    }

  } else {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Rejected malformed SMSG_SPELL_GO payload (" +
            std::to_string(pkt.payload.size()) + " bytes)");
  }
}

std::uint32_t WorldSession::UpdateLocalUnitRegenAndRunes(const std::uint32_t current_time_ms) {
  if (current_time_ms == 0u) {
    return 0u;
  }

  if (local_unit_regen_last_tick_ms_ == 0u) {
    local_unit_regen_last_tick_ms_ = current_time_ms;
    return 0u;
  }

  const std::uint32_t elapsed_ms = current_time_ms - local_unit_regen_last_tick_ms_;
  if (elapsed_ms < kLocalRegenTickIntervalMs) {
    return 0u;
  }

  local_unit_regen_last_tick_ms_ = current_time_ms;

  auto* player = objects().GetMutablePlayer(objects().GetLocalPlayerGuid());
  if (player == nullptr) {
    return 0u;
  }

  UpdatePredictedRegenForUnit(*this, *player, elapsed_ms, current_time_ms);

  const ObjectGuid pet_guid = player->State().GetPetGUID();
  if (!pet_guid.IsEmpty()) {
    if (auto* pet = objects().GetMutableUnit(pet_guid); pet != nullptr) {
      UpdatePredictedRegenForUnit(*this, *pet, elapsed_ms, current_time_ms);
    }
  }

  return runes_.Update(static_cast<float>(elapsed_ms) * 0.001f);
}

void WorldSession::NotifyLocalUnitManaSpellcast(const ObjectGuid& caster_guid,
                                                const std::uint32_t spell_id) {
  if (!IsManaPowerSpell(*this, spell_id, false)) {
    return;
  }

  const std::uint32_t current_time_ms =
      CurrentClientTimeMs() != 0u ? CurrentClientTimeMs() : openwow::core::GameClock::GetTickCount32();

  const ObjectGuid player_guid = objects().GetLocalPlayerGuid();
  if (player_guid.IsEmpty()) {
    return;
  }

  if (caster_guid == player_guid) {
    if (auto* player = objects().GetMutablePlayer(player_guid); player != nullptr) {
      player->Vitals().NotifyManaRegenInterrupted(
          current_time_ms, kManaRegenInterruptDurationMs);
    }
    return;
  }

  const auto* player = objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }

  const ObjectGuid pet_guid = player->State().GetPetGUID();
  if (!pet_guid.IsEmpty() && caster_guid == pet_guid) {
    if (auto* pet = objects().GetMutableUnit(pet_guid); pet != nullptr) {
      pet->Vitals().NotifyManaRegenInterrupted(
          current_time_ms, kManaRegenInterruptDurationMs);
    }
  }
}

void WorldSession::HandleActionButtons(const net::wotlk::WorldPacket& pkt) {

  if (objects().GetLocalPlayerTyped() == nullptr) {
    action_assignments_.SetSlotValidator({});
  } else if (!action_assignments_.IsServerSyncPending()) {
    action_assignments_.SetSlotValidator(
        [this](const ActionPresentationEntry& btn,
               std::uint32_t ) -> bool {
          if (btn.type == ActionPresentationKind::kSpell && btn.action != 0) {
            return SpellbookSystem::Get().HasSpell(btn.action);
          }
          if (btn.type == ActionPresentationKind::kEquipmentSet) {
            return equipment_.find(btn.action) != nullptr;
          }
          return true;
        });
  }

  const auto decoded = actions::adapters::protocol::DecodeActionAssignments(
      pkt, &action_assignments_.assignments().values());
  if (!decoded) {
    return;
  }
  if (decoded->values) {
    action_assignments_.ApplyServerSnapshot(decoded->state, *decoded->values);
  } else {
    action_assignments_.BeginServerSync();
  }

  if (ui::game::detail::RefreshAllActionSlotValidation(*this)) {
    ui::game::ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }

  if (action_assignments_.last_state() == ActionBarState::kUpdate) {
    ui::game::ScriptEventDispatch::Get().FireActionbarSlotChanged(0);
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Received action buttons (state=" +
                         std::to_string(static_cast<int>(action_assignments_.last_state())) + ")");
}

void WorldSession::HandleTalentsInfo(const net::wotlk::WorldPacket& pkt) {
  if (pkt.payload.empty()) {
    return;
  }

  auto& store = TalentInfoStore::Get();
  const bool is_pet_update = pkt.payload[0] != 0;
  if (is_pet_update) {
    if (!store.ProcessPetServerData(pkt.payload.data() + 1,
                                    pkt.payload.size() - 1)) {
      return;
    }
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PET_TALENT_UPDATE);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "Received pet talent info");
    return;
  }

  const uint32_t previous_active_group = store.GetActiveGroupIndex();
  const uint32_t previous_group_count = store.GetNumGroups();

  if (!store.ProcessServerData(pkt.payload.data() + 1,
                               pkt.payload.size() - 1)) {
    return;
  }
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PLAYER_TALENT_UPDATE);

  const uint32_t new_active_group = store.GetActiveGroupIndex();
  const uint32_t new_group_count = store.GetNumGroups();
  if (previous_active_group != new_active_group) {
    const std::uint32_t new_group_arg = (new_group_count == 0) ? 0u : (new_active_group + 1u);
    const std::uint32_t old_group_arg =
        (previous_group_count == 0) ? 0u : (previous_active_group + 1u);
    openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
        openwow::ui::game::events::ACTIVE_TALENT_GROUP_CHANGED,
        {static_cast<int>(new_group_arg), static_cast<int>(old_group_arg)});
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Received talent info: " +
                         std::to_string(store.GetUnspentPointsForGroup(
                             store.GetActiveGroupIndex(), false)) +
                         " free points, " +
                         std::to_string(store.GetNumGroups()) + " specs");
}

void WorldSession::HandleAttackStart(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandleAttackStart(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (const auto& parsed = combat_.attack_start(); parsed.has_value()) {
    if (auto* const attacker = objects().GetMutableUnit(parsed->attacker);
        attacker != nullptr) {

      attacker->Interaction().SetCachedUpdateTarget(parsed->victim);
    }
    if (auto_attack_combat_event_callback_) {
      auto_attack_combat_event_callback_(
          AutoAttackCombatEvent::AttackStart,
          parsed->attacker.GetRawValue(),
          parsed->victim.GetRawValue());
    }
  }
  combat_log_.HandleAttackStart(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleAttackStop(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandleAttackStop(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (const auto& parsed = combat_.attack_stop(); parsed.has_value()) {
    if (auto* const attacker = objects().GetMutableUnit(parsed->attacker);
        attacker != nullptr) {

      attacker->Interaction().SetCachedUpdateTarget(ObjectGuid{});
    }
    if (auto_attack_combat_event_callback_) {
      auto_attack_combat_event_callback_(
          AutoAttackCombatEvent::AttackStop,
          parsed->attacker.GetRawValue(),
          parsed->victim.GetRawValue());
    }
  }
  combat_log_.HandleAttackStop(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSpellNonMeleeDamageLog(const net::wotlk::WorldPacket& pkt) {
  combat_log_.HandleSpellNonMeleeDamageLog(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSpellHealLog(const net::wotlk::WorldPacket& pkt) {
  combat_log_.HandleSpellHealLog(objects(), pkt.payload.data(),
                                 pkt.payload.size());
}

void WorldSession::HandleSpellEnergizeLog(const net::wotlk::WorldPacket& pkt) {
  ApplySpellEnergizePrediction(*this, pkt);
  combat_log_.HandleSpellEnergizeLog(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSendAllCombatLog(const net::wotlk::WorldPacket& pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint32_t record_count = 0;
  std::uint32_t batch_time_ms = 0;
  if (!reader.ReadU32(record_count) || !reader.ReadU32(batch_time_ms)) {
    return;
  }

  for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
    std::uint32_t record_time_ms = 0;
    std::uint32_t opcode_value = 0;
    if (!reader.ReadU32(record_time_ms) || !reader.ReadU32(opcode_value)) {
      return;
    }

    const std::uint32_t timestamp_offset_ms = batch_time_ms - record_time_ms;
    switch (static_cast<net::wotlk::Opcode>(opcode_value)) {
      case net::wotlk::Opcode::SMSG_SPELLBREAKLOG:
        if (!combat_handler_.HandleSpellBreakLog(reader)) {
          return;
        }
        if (combat_handler_.last_spell_break_log().has_value()) {
          combat_log_.HandleSpellAuraBroken(
              *combat_handler_.last_spell_break_log(), timestamp_offset_ms);
        }
        break;

      case net::wotlk::Opcode::SMSG_PARTYKILLLOG:
        if (!CombatLog_HandlePartyKillOpcode(combat_log_, reader,
                                             timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_ENVIRONMENTAL_DAMAGE_LOG:
        if (!combat_log_.HandleEnvironmentalDamageLog(
                reader, timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_DESTRUCTIBLE_BUILDING_DAMAGE:
        if (!CombatLog_HandleHealOpcode(objects(), combat_log_, reader,
                                        timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_SPELLHEALLOG:
        if (!combat_log_.HandleSpellHealLog(objects(), reader,
                                            timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_SPELLENERGIZELOG:
        ApplySpellEnergizePrediction(*this, reader);
        if (!combat_log_.HandleSpellEnergizeLog(reader, timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_SPELLLOGMISS:
        if (!spell_log_.HandleSpellLogMiss(reader)) {
          return;
        }
        (void)combat_log_.HandleSpellLogMiss(
            spell_log_.last_log_miss(), timestamp_offset_ms);
        break;

      case net::wotlk::Opcode::SMSG_SPELLDAMAGESHIELD:
        if (!spell_log_.HandleSpellDamageShield(reader)) {
          return;
        }
        (void)combat_log_.HandleSpellDamageShield(
            spell_log_.last_damage_shield(), timestamp_offset_ms);
        break;

      case net::wotlk::Opcode::SMSG_SPELLINSTAKILLLOG:
        if (!spell_log_.HandleSpellInstaKillLog(reader)) {
          return;
        }
        (void)combat_log_.HandleSpellInstaKill(
            spell_log_.last_instakill(), timestamp_offset_ms);
        break;

      case net::wotlk::Opcode::SMSG_SPELLORDAMAGE_IMMUNE:
        if (!spell_log_.HandleSpellOrDamageImmune(reader)) {
          return;
        }
        (void)combat_log_.HandleSpellOrDamageImmune(
            spell_log_.last_immune(), timestamp_offset_ms);
        break;

      case net::wotlk::Opcode::SMSG_DISPEL_FAILED:
        if (!spell_log_.HandleDispelFailed(reader)) {
          return;
        }
        (void)combat_log_.HandleDispelFailed(
            spell_log_.last_dispel_failed(), timestamp_offset_ms);
        break;

      case net::wotlk::Opcode::SMSG_SPELLLOGEXECUTE:
        if (!spell_log_.HandleSpellLogExecute(*this, reader)) {
          return;
        }
        for (const SpellLogExecuteDrain& drain :
             spell_log_.last_execute_drains()) {
          combat_log_.HandleSpellPowerDrain(
              objects(), drain.caster_guid, drain.target_guid, drain.spell_id,
              drain.power_type, drain.drain_amount, drain.leech_coefficient,
              drain.is_periodic, timestamp_offset_ms);
        }
        for (const SpellLogExecuteExtraAttacks& extra_attacks :
             spell_log_.last_execute_extra_attacks()) {
          combat_log_.HandleSpellLogExecuteExtraAttacks(
              extra_attacks, timestamp_offset_ms);
        }
        for (const SpellLogExecuteInterrupt& interrupt :
             spell_log_.last_execute_interrupts()) {
          combat_log_.HandleSpellLogExecuteInterrupt(
              interrupt, timestamp_offset_ms);
        }
        for (const SpellLogExecuteSummon& summon :
             spell_log_.last_execute_summons()) {
          combat_log_.HandleSpellLogExecuteSummon(
              summon, timestamp_offset_ms);
        }
        for (const SpellLogExecuteResurrect& resurrect :
             spell_log_.last_execute_resurrects()) {
          combat_log_.HandleSpellLogExecuteResurrect(
              resurrect, timestamp_offset_ms);
        }
        for (const SpellLogExecuteDurabilityDamage& dd :
             spell_log_.last_execute_durability_damages()) {
          combat_log_.HandleSpellLogExecuteDurabilityDamage(
              dd, timestamp_offset_ms);
        }
        for (const SpellLogExecuteDurabilityDamageAll& dda :
             spell_log_.last_execute_durability_damage_alls()) {
          combat_log_.HandleSpellLogExecuteDurabilityDamageAll(
              dda, timestamp_offset_ms);
        }
        break;

      case net::wotlk::Opcode::SMSG_PERIODICAURALOG:
        ApplyPeriodicAuraPowerPrediction(*this, reader);
        if (!combat_log_.HandlePeriodicAuraLog(objects(), reader,
                                               timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_SPELLNONMELEEDAMAGELOG:
        if (!combat_log_.HandleSpellNonMeleeDamageLog(reader, timestamp_offset_ms)) {
          return;
        }
        break;

      case net::wotlk::Opcode::SMSG_ATTACKERSTATEUPDATE:
        if (!combat_.HandleAttackerStateUpdate(reader) ||
            !combat_.last_state_update().has_value()) {
          return;
        }
        combat_log_.HandleAttackerStateUpdate(
            *combat_.last_state_update(), timestamp_offset_ms);
        break;

      case net::wotlk::Opcode::SMSG_SPELLDISPELLOG:
        if (!spell_log_.HandleSpellDispelLog(reader)) {
          return;
        }
        if (!spell_log_.dispel_logs().empty()) {
          (void)combat_log_.HandleSpellDispelOrSteal(
              spell_log_.dispel_logs().back(), timestamp_offset_ms);
        }
        break;

      case net::wotlk::Opcode::SMSG_SPELLSTEALLOG:
        if (!spell_log_.HandleSpellStealLog(reader)) {
          return;
        }
        if (!spell_log_.dispel_logs().empty()) {
          (void)combat_log_.HandleSpellDispelOrSteal(
              spell_log_.dispel_logs().back(), timestamp_offset_ms);
        }
        break;

      case net::wotlk::Opcode::SMSG_PROCRESIST:
        if (!combat_handler_.HandleProcResist(reader)) {
          return;
        }
        if (combat_handler_.last_proc_resist().has_value()) {
          (void)combat_log_.HandleProcResist(
              *combat_handler_.last_proc_resist(), timestamp_offset_ms);
        }
        break;

      case net::wotlk::Opcode::SMSG_ENCHANTMENTLOG:
        if (!CombatLog_HandleEnchantOpcode(
                combat_log_, item_definitions_, reader, timestamp_offset_ms)) {
          return;
        }
        break;

      default:
        break;
    }
  }
}

void WorldSession::HandleLogXpGain(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleLogXpGain(pkt.payload.data(), pkt.payload.size());
  combat_log_.HandleLogXpGain(pkt.payload.data(), pkt.payload.size());

  HandleXPGainPacket(*this, pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleAttackerStateUpdate(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandleAttackerStateUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto& parsed_asu = combat_.last_state_update();
  if (!parsed_asu.has_value()) {
    return;
  }
  const auto& asu = *parsed_asu;
  if (auto_attack_combat_event_callback_) {
    auto_attack_combat_event_callback_(
        AutoAttackCombatEvent::AttackerStateUpdate,
        asu.attacker.GetRawValue(), asu.victim.GetRawValue());
  }
  ApplyAttackerStateRagePrediction(*this, pkt);
  combat_log_.HandleAttackerStateUpdate(asu);

  if (auto* const attacker = objects().GetMutableUnit(asu.attacker);
      attacker != nullptr) {
    if (attacker->Animation().GetCachedSheatheState() != 1) {
      attacker->Animation().ChangeSheatheStateAndNotifyServer(1, true, false);
    }
    attacker->Animation().QueueCombatAudioResult(
        asu.victim.GetRawValue(), asu.hit_info, asu.total_damage,
        asu.overkill, static_cast<std::uint8_t>(asu.victim_state));
    attacker->Animation().PlayAttackAnimation(asu.hit_info, asu.melee_spell_id);
  } else if (auto* const victim = objects().GetMutableUnit(asu.victim);
             victim != nullptr) {
    victim->Animation().ApplyAttackerStateRecordToVictim(*this, asu.hit_info);
  }
}

void WorldSession::HandlePeriodicAuraLog(const net::wotlk::WorldPacket& pkt) {
  ApplyPeriodicAuraPowerPrediction(*this, pkt);
  combat_log_.HandlePeriodicAuraLog(objects(), pkt.payload.data(),
                                    pkt.payload.size());
}

void WorldSession::HandleAuraUpdate(const net::wotlk::WorldPacket& pkt) {
  const auto parsed =
      net::wotlk::ParseAuraUpdate(pkt.payload.data(), pkt.payload.size());
  if (!parsed || parsed->target.IsEmpty()) {
    aura_.HandleAuraUpdate(pkt.payload.data(), pkt.payload.size());
    return;
  }

  const ObjectGuid target = parsed->target;

  const auto& previous_auras = aura_.GetAuras(target.GetRawValue());
  std::vector<std::pair<std::uint8_t, std::optional<AuraSlotInfo>>> previous_slots;
  previous_slots.reserve(parsed->slots.size());
  for (const auto& parsed_slot : parsed->slots) {
    std::optional<AuraSlotInfo> previous;
    if (parsed_slot.slot < previous_auras.size()) {
      previous = previous_auras[parsed_slot.slot];
    }
    previous_slots.emplace_back(parsed_slot.slot, previous);
  }

  if (!aura_.HandleAuraUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& auras = aura_.GetAuras(target.GetRawValue());
  const auto active_aura_count = static_cast<std::size_t>(std::count_if(
      auras.begin(), auras.end(),
      [](const AuraSlotInfo& aura) { return aura.spell_id != 0u; }));
  for (const auto& [slot_index, previous] : previous_slots) {
    const AuraSlotInfo* current_slot =
        (slot_index < auras.size()) ? &auras[slot_index] : nullptr;
    combat_log_.HandleAuraStateTransition(
        target.GetRawValue(), previous.has_value() ? &*previous : nullptr,
        current_slot, active_aura_count);
  }

  SyncTrackedUnitAuraState(*this, target, auras);
  RouteAuraVisualChanges(*this, aura_.last_update_diff());

  auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitAura(target.GetRawValue());
}

void WorldSession::HandleAuraUpdateAll(const net::wotlk::WorldPacket& pkt) {
  const auto parsed =
      net::wotlk::ParseAuraUpdateAll(pkt.payload.data(), pkt.payload.size());
  if (!parsed || parsed->target.IsEmpty()) {
    aura_.HandleAuraUpdateAll(pkt.payload.data(), pkt.payload.size());
    return;
  }

  const ObjectGuid target = parsed->target;
  if (!aura_.HandleAuraUpdateAll(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& auras = aura_.GetAuras(target.GetRawValue());
  const auto active_aura_count = static_cast<std::size_t>(std::count_if(
      auras.begin(), auras.end(),
      [](const AuraSlotInfo& aura) { return aura.spell_id != 0u; }));
  for (const auto& change : aura_.last_update_diff().changes) {
    combat_log_.HandleAuraStateTransition(
        target.GetRawValue(), &change.old_value, &change.new_value,
        active_aura_count);
  }
  SyncTrackedUnitAuraState(*this, target, auras);
  RouteAuraVisualChanges(*this, aura_.last_update_diff());

  auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitAura(target.GetRawValue());
}

void WorldSession::HandleSetFlatSpellModifier(const net::wotlk::WorldPacket& pkt) {
  aura_.HandleSetFlatSpellModifier(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSetPctSpellModifier(const net::wotlk::WorldPacket& pkt) {
  aura_.HandleSetPctSpellModifier(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleCooldownEvent(const net::wotlk::WorldPacket& pkt) {
  if (!aura_.HandleCooldownEvent(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto& event = aura_.cooldown_event();
  if (event.has_value() &&
      event->guid == objects().GetLocalPlayerGuid()) {
    spell_book_.RecordSuccessfulCastRecovery(event->spell_id);
  }
}

void WorldSession::HandleClearCooldown(const net::wotlk::WorldPacket& pkt) {
  if (!aura_.HandleClearCooldown(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  spell_book_.ClearCooldown(aura_.clear_cooldown_spell());
  CooldownTracker::Get().ClearSpellCooldown(
      aura_.clear_cooldown_spell());
}

void WorldSession::HandleSendUnlearnSpells(const net::wotlk::WorldPacket& pkt) {
  trainer_unlearn_spell_cache_.HandleSendUnlearnSpells(
      pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSpellFailure(const net::wotlk::WorldPacket& pkt) {
  const auto failure =
      net::wotlk::ParseSpellFailure(pkt.payload.data(), pkt.payload.size());
  if (!failure.has_value()) {
    return;
  }

  if (ApplyUnitSpellFailure(*this, *failure)) {
    ClearFailedTradeSkillSpell(failure->spell_id);
    if (cast_bar_callbacks_.on_cast_interrupt) {
      cast_bar_callbacks_.on_cast_interrupt(failure->spell_id);
    }
  }
}

void WorldSession::HandleSpellFailedOther(const net::wotlk::WorldPacket& pkt) {
  const auto failure =
      net::wotlk::ParseSpellFailure(pkt.payload.data(), pkt.payload.size());
  if (!failure.has_value()) {
    return;
  }

  if (ApplyUnitSpellFailure(*this, *failure)) {
    ClearFailedTradeSkillSpell(failure->spell_id);
    if (cast_bar_callbacks_.on_cast_interrupt) {
      cast_bar_callbacks_.on_cast_interrupt(failure->spell_id);
    }
  }
}

void WorldSession::HandleSpellDelayed(const net::wotlk::WorldPacket& pkt) {
  const auto delayed =
      net::wotlk::ParseSpellDelayed(pkt.payload.data(), pkt.payload.size());
  if (!delayed.has_value()) {
    return;
  }

  auto* const unit = objects().GetMutableUnit(delayed->caster_guid);
  if (unit == nullptr || !unit->Casts().IsCasting()) {
    return;
  }

  auto cast = unit->Casts().GetCurrentCast();
  cast.end_time += delayed->delay_time;
  unit->Casts().SetCurrentCast(cast);
  FireUnitSpellcastPacketEvent(
      *this, delayed->caster_guid, kUnitSpellcastDelayedEvent, cast.spell_id,
      static_cast<std::uint8_t>(cast.cast_id));
}

void WorldSession::HandleChannelStart(const net::wotlk::WorldPacket& pkt) {
  const auto channel =
      net::wotlk::ParseChannelStart(pkt.payload.data(), pkt.payload.size());
  if (!channel.has_value()) {
    return;
  }

  if (channel->duration == 0) {
    return;
  }
  auto* const unit = objects().GetMutableUnit(channel->caster_guid);
  const auto* const dbc = GetDbcLoader();
  if (unit == nullptr || dbc == nullptr ||
      dbc->spell().LookupEntry(channel->spell_id) == nullptr) {
    return;
  }
  const auto cast = BuildUnitCastInfo(
      channel->spell_id, 0, static_cast<std::int32_t>(channel->duration), true);
  unit->Casts().SetChannelCast(cast);
  QueueChannelStartVisual(*this, channel->caster_guid, channel->spell_id);
  FireUnitSpellcastPacketEvent(
      *this, channel->caster_guid, kUnitSpellcastChannelStartEvent,
      channel->spell_id, 0);

  if (IsLocalPlayerSpellEvent(*this, channel->caster_guid)) {
    spell_cast_runtime_.OnChannelStart(
        channel->caster_guid, channel->spell_id);
    if (cast_bar_callbacks_.on_channel_start) {
      cast_bar_callbacks_.on_channel_start(
          channel->spell_id, static_cast<std::int32_t>(channel->duration));
    }
  }
}

void WorldSession::HandleChannelUpdate(const net::wotlk::WorldPacket& pkt) {
  const auto channel =
      net::wotlk::ParseChannelUpdate(pkt.payload.data(), pkt.payload.size());
  if (!channel.has_value()) {
    return;
  }

  auto* const unit = objects().GetMutableUnit(channel->caster_guid);
  const auto* const dbc = GetDbcLoader();
  if (unit == nullptr || dbc == nullptr) {
    return;
  }

  const std::uint32_t spell_id = unit->Casts().GetChannelCast().spell_id;
  const auto* const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return;
  }

  const auto transition = PrepareChannelUpdate(
      *unit, *spell, channel->caster_guid.GetRawValue(),
      static_cast<std::int32_t>(channel->remaining),
      core::GameClock::GetTickCount32());
  if (transition == ChannelUpdateTransition::kIgnored) {
    return;
  }

  const bool stopped = transition == ChannelUpdateTransition::kStopped;
  if (stopped) {
    QueueChannelStopVisual(*this, channel->caster_guid, spell_id);
    ClearUnitCast(*this, channel->caster_guid, true);
    unit->Animation().SetChannelingActionLock(false);
    unit->Animation().EndSpellVisualStandAnimation(*this);
    FireUnitSpellcastPacketEvent(
        *this, channel->caster_guid, kUnitSpellcastChannelStopEvent, spell_id,
        0);
  } else {
    UpdateUnitChannelEndTime(
        *this, channel->caster_guid,
        static_cast<std::int32_t>(channel->remaining));
    FireUnitSpellcastPacketEvent(
        *this, channel->caster_guid, kUnitSpellcastChannelUpdateEvent,
        spell_id, 0);
  }
  CompleteChannelUpdate(*this, *unit, spell_id, transition);

  if (IsLocalPlayerSpellEvent(*this, channel->caster_guid)) {
    if (stopped) {
      spell_cast_runtime_.OnChannelStop();
    }
    if (cast_bar_callbacks_.on_channel_update) {
      cast_bar_callbacks_.on_channel_update(
          static_cast<std::int32_t>(channel->remaining));
    }
  }
}

void WorldSession::HandleCancelCombat(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleCancelCombat(pkt.payload.data(), pkt.payload.size());

  if (cancel_combat_callback_) {
    cancel_combat_callback_();
  }
}

void WorldSession::HandleAiReaction(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleAiReaction(pkt.payload.data(), pkt.payload.size());

  const auto& ai = combat_.ai_reaction();
  if (!ai.has_value()) return;

  auto* unit = objects().GetMutableUnit(ai->unit);
  if (!unit) return;

  switch (static_cast<AiReactionType>(ai->reaction)) {
    case AiReactionType::kAlert:

      unit->Sound().PlayCreatureSound(
          *unit, static_cast<std::uint32_t>(CreatureSoundType::Alert), false);
      break;
    case AiReactionType::kHostile:
      (void)unit->Sound().PlayPrioritySound(*unit, 0u);
      break;
    default:
      break;
  }
}

void WorldSession::HandlePartyKillLog(const net::wotlk::WorldPacket& pkt) {
  (void)CombatLog_HandlePartyKillOpcode(
      combat_log_, pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleAttackSwingError(const net::wotlk::WorldPacket& ,
                                          AttackSwingError error) {

  combat_.SetSwingError(error, CurrentClientTimeMs());

  if (error == AttackSwingError::kDeadTarget ||
      error == AttackSwingError::kCantAttack) {
    if (auto* const targeting = targeting_system(); targeting != nullptr) {
      targeting->StopAttack(true);
    }
  }
}

void WorldSession::HandleHealthUpdate(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandleHealthUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& health_update = combat_.last_health_update();
  if (!health_update.has_value()) {
    return;
  }

  auto* unit = objects().GetMutableUnit(health_update->guid);
  if (unit != nullptr) {
    (void)unit->Vitals().Set(*unit, -2, health_update->health);
  }
}

void WorldSession::HandlePowerUpdate(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandlePowerUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& power_update = combat_.last_power_update();
  if (!power_update.has_value()) {
    return;
  }

  ApplyAbsolutePowerPrediction(
      *this, power_update->guid, static_cast<std::uint8_t>(power_update->power_type),
      power_update->value);
}

void WorldSession::HandleThreatUpdate(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleThreatUpdate(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleHighestThreatUpdate(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleHighestThreatUpdate(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleThreatRemove(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleThreatRemove(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleThreatClear(const net::wotlk::WorldPacket& pkt) {
  combat_.HandleThreatClear(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleLevelUpInfo(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandleLevelUpInfo(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& level_up = combat_.level_up();
  if (!level_up.has_value() || objects().GetActivePlayer() == nullptr) {
    return;
  }

  const auto& info = *level_up;
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::PLAYER_LEVEL_UP,
      {
          static_cast<int>(info.level),
          static_cast<int>(info.health_delta),
          static_cast<int>(info.mana_delta),
          info.level > 9u ? 1 : 0,
          static_cast<int>(info.stat_delta[0]),
          static_cast<int>(info.stat_delta[1]),
          static_cast<int>(info.stat_delta[2]),
          static_cast<int>(info.stat_delta[3]),
          static_cast<int>(info.stat_delta[4]),
      });
}

void WorldSession::HandleEnvironmentalDamageLog(const net::wotlk::WorldPacket& pkt) {
  if (!combat_.HandleEnvironmentalDamageLog(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (!combat_log_.HandleEnvironmentalDamageLog(
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& environmental_damage = combat_.env_damage();
  if (!environmental_damage.has_value()) {
    return;
  }
  if (environmental_damage->damage == 0 && environmental_damage->absorbed == 0 &&
      environmental_damage->resisted == 0) {
    return;
  }
  if (objects().GetActivePlayerGuid().GetRawValue() !=
      environmental_damage->guid.GetRawValue()) {
    return;
  }

  const std::uint32_t school_mask =
      CombatLog_GetEnvironmentalDamageSchoolMask(environmental_damage->type);
  if (school_mask == 0) {
    return;
  }

  int hit_flags = 0;
  if (environmental_damage->absorbed != 0) {
    hit_flags |= HitFlags::kAbsorb;
  }
  if (environmental_damage->resisted != 0) {
    hit_flags |= HitFlags::kResist;
  }

  DispatchWoundEvent(environmental_damage->guid,
                     static_cast<int>(environmental_damage->damage),
                     static_cast<int>(school_mask),
                     hit_flags);
}

void WorldSession::HandleConvertRune(const net::wotlk::WorldPacket& pkt) {
  if (!runes_.HandleConvertRune(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const std::uint8_t rune_index = pkt.payload.empty() ? 0u : pkt.payload[0];
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::RUNE_TYPE_UPDATE,
      {static_cast<int>(rune_index) + 1});
  RefreshRuneUsability(*this);
}

void WorldSession::HandleResyncRunes(const net::wotlk::WorldPacket& pkt) {
  if (!runes_.HandleResyncRunes(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  for (std::uint32_t i = 0; i < runes_.active_rune_count(); ++i) {
    FireRunePowerUpdate(static_cast<int>(i), false);
  }
}

void WorldSession::HandleAddRunePower(const net::wotlk::WorldPacket& pkt) {
  std::uint32_t mask = 0;
  if (pkt.payload.size() >= sizeof(mask)) {
    std::memcpy(&mask, pkt.payload.data(), sizeof(mask));
  }

  const std::uint32_t previous_ready_mask = runes_.ready_mask();
  if (!runes_.HandleAddRunePower(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  for (std::uint32_t i = 0; i < runes_.active_rune_count(); ++i) {
    const std::uint32_t bit = 1u << i;
    if ((mask & bit) != 0 && (previous_ready_mask & bit) == 0) {
      FireRunePowerUpdate(static_cast<int>(i), false);
    }
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::ACTIONBAR_UPDATE_COOLDOWN);
}

void WorldSession::HandlePlaySpellVisual(const net::wotlk::WorldPacket& pkt) {
  if (!spell_visual_.HandlePlaySpellVisual(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& ev = spell_visual_.visual_events().back();

  const bool created = HandleSpellVisualTrigger(
      *this, ev.target_guid, ev.spell_visual_kit_id);

  if (created && spell_visual_callbacks_.on_play_visual) {
    spell_visual_callbacks_.on_play_visual(ev.target_guid, ev.spell_visual_kit_id);
  }
}

void WorldSession::HandlePlaySpellImpact(const net::wotlk::WorldPacket& pkt) {
  if (!spell_visual_.HandlePlaySpellImpact(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& ev = spell_visual_.visual_events().back();

  const bool created = HandleSpellVisualTriggerWithTarget(
      *this, ev.target_guid, ev.spell_visual_kit_id);

  if (created && spell_visual_callbacks_.on_play_impact) {
    spell_visual_callbacks_.on_play_impact(ev.target_guid, ev.spell_visual_kit_id);
  }
}

void WorldSession::HandleTriggerCinematic(const net::wotlk::WorldPacket& pkt) {
  if (!spell_visual_.HandleTriggerCinematic(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto cinematic_sequence_id = spell_visual_.last_cinematic().cinematic_sequence_id;
  if (!openwow::net::ClientServices::Instance().IsWorldSessionReady()) {
    pending_trigger_cinematic_sequence_id_ = cinematic_sequence_id;
    return;
  }

  if (cinematic_callbacks_.on_trigger_cinematic) {
    spell_visual_.BeginCinematic();
    cinematic_callbacks_.on_trigger_cinematic(cinematic_sequence_id);
  }
}

void WorldSession::HandleTriggerMovie(const net::wotlk::WorldPacket& pkt) {
  if (!spell_visual_.HandleTriggerMovie(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (movie_callbacks_.on_trigger_movie) {
    movie_callbacks_.on_trigger_movie(spell_visual_.last_movie().movie_id);
  }
}

void WorldSession::HandleSpellDispelLog(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellDispelLog(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (!spell_log_.dispel_logs().empty()) {
    combat_log_.HandleSpellDispelOrSteal(spell_log_.dispel_logs().back());
  }
}

void WorldSession::HandleSpellStealLog(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellStealLog(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (!spell_log_.dispel_logs().empty()) {
    combat_log_.HandleSpellDispelOrSteal(spell_log_.dispel_logs().back());
  }
}

void WorldSession::HandleSpellDamageShield(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellDamageShield(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  combat_log_.HandleSpellDamageShield(spell_log_.last_damage_shield());
}

void WorldSession::HandleSpellLogMiss(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellLogMiss(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  combat_log_.HandleSpellLogMiss(spell_log_.last_log_miss());
}

void WorldSession::HandleSpellInstaKillLog(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellInstaKillLog(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  combat_log_.HandleSpellInstaKill(spell_log_.last_instakill());
}

void WorldSession::HandleSpellOrDamageImmune(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellOrDamageImmune(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  combat_log_.HandleSpellOrDamageImmune(spell_log_.last_immune());
}

void WorldSession::HandleDispelFailed(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleDispelFailed(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  combat_log_.HandleDispelFailed(spell_log_.last_dispel_failed());
}

void WorldSession::HandleModifyCooldown(const net::wotlk::WorldPacket& pkt) {
  spell_log_.HandleModifyCooldown(pkt.payload.data(), pkt.payload.size());

  const auto& mc = spell_log_.last_modify_cooldown();
  if (mc.spell_id != 0) {
    CooldownTracker::Get().AdjustSpellCooldown(mc.spell_id,
                                                mc.cooldown_delta_ms);
    spell_book_.ModifyCooldown(mc.spell_id, mc.cooldown_delta_ms);
  }
}

void WorldSession::HandleSpellLogExecute(const net::wotlk::WorldPacket& pkt) {
  if (!spell_log_.HandleSpellLogExecute(*this, pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  for (const SpellLogExecuteDrain& drain : spell_log_.last_execute_drains()) {
    combat_log_.HandleSpellPowerDrain(
        objects(), drain.caster_guid, drain.target_guid, drain.spell_id,
        drain.power_type, drain.drain_amount, drain.leech_coefficient,
        drain.is_periodic);
  }
  for (const SpellLogExecuteExtraAttacks& extra_attacks :
       spell_log_.last_execute_extra_attacks()) {
    combat_log_.HandleSpellLogExecuteExtraAttacks(extra_attacks);
  }
  for (const SpellLogExecuteInterrupt& interrupt :
       spell_log_.last_execute_interrupts()) {
    combat_log_.HandleSpellLogExecuteInterrupt(interrupt);
  }
  for (const SpellLogExecuteSummon& summon : spell_log_.last_execute_summons()) {
    combat_log_.HandleSpellLogExecuteSummon(summon);
  }
  for (const SpellLogExecuteResurrect& resurrect :
       spell_log_.last_execute_resurrects()) {
    combat_log_.HandleSpellLogExecuteResurrect(resurrect);
  }
  for (const SpellLogExecuteDurabilityDamage& dd :
       spell_log_.last_execute_durability_damages()) {
    combat_log_.HandleSpellLogExecuteDurabilityDamage(dd);
  }
  for (const SpellLogExecuteDurabilityDamageAll& dda :
       spell_log_.last_execute_durability_damage_alls()) {
    combat_log_.HandleSpellLogExecuteDurabilityDamageAll(dda);
  }
}

void WorldSession::HandleTotemCreated(const net::wotlk::WorldPacket& pkt) {
  spell_book_.HandleTotemCreated(pkt.payload.data(), pkt.payload.size(),
                                 CurrentClientTimeMs());
}

void WorldSession::HandleResumeCastBar(const net::wotlk::WorldPacket& pkt) {
  spell_book_.HandleResumeCastBar(pkt.payload.data(), pkt.payload.size());

  const auto& rcb = spell_book_.last_resume_cast_bar();
  if (!rcb.has_value() || rcb->time_remaining == 0 || rcb->cast_time == 0) {
    return;
  }

  bool is_channel = false;
  if (const auto* dbc = GetDbcLoader(); dbc != nullptr) {
    if (const auto* spell = dbc->spell().LookupEntry(rcb->spell_id);
        spell != nullptr) {
      is_channel = (spell->attributes_ex & 0x4u) != 0u;
    }
  }

  if (auto* const caster = objects().GetMutableUnit(rcb->caster);
      caster != nullptr) {
    const auto cast = BuildResumedUnitCastInfo(
        rcb->spell_id, rcb->cast_time, rcb->time_remaining, is_channel);
    if (is_channel) {
      caster->Casts().SetChannelCast(cast);
    } else {
      caster->Casts().SetCurrentCast(cast);
    }
  }

  if (is_channel) {
    FireUnitSpellcastPacketEvent(
        *this, rcb->caster, kUnitSpellcastChannelStartEvent, rcb->spell_id, 0);
  } else {
    FireUnitSpellcastPacketEvent(
        *this, rcb->caster, kUnitSpellcastStartEvent, rcb->spell_id, 0);
  }

  if (is_channel) {
    if (IsLocalPlayerSpellEvent(*this, rcb->caster)) {
      spell_cast_runtime_.OnChannelStart(
          rcb->caster, rcb->spell_id);
    }
    if (cast_bar_callbacks_.on_channel_start) {
      cast_bar_callbacks_.on_channel_start(
          rcb->spell_id, static_cast<std::int32_t>(rcb->time_remaining));
    }
  } else if (cast_bar_callbacks_.on_cast_start) {
    cast_bar_callbacks_.on_cast_start(
        rcb->spell_id, static_cast<std::int32_t>(rcb->time_remaining));
  }
}

void WorldSession::HandleTalentWipeConfirm(const net::wotlk::WorldPacket& pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint64_t trainer_guid = 0;
  std::uint32_t cost = 0;
  if (!reader.ReadU64(trainer_guid) || !reader.ReadU32(cost)) {
    return;
  }

  if (trainer_guid == 0) {
    ui::game::DisplaySystemMessage(kTalentWipeInvalidTrainerSystemMessage);
    return;
  }

  const auto* const player = objects().GetLocalPlayerTyped();
  const auto* const trainer = objects().GetUnit(ObjectGuid(trainer_guid));
  if (player == nullptr || trainer == nullptr) {
    return;
  }

  const float dx = trainer->GetX() - player->GetX();
  const float dy = trainer->GetY() - player->GetY();
  const float dz = trainer->GetZ() - player->GetZ();
  const float distance_squared = dx * dx + dy * dy + dz * dz;
  const float interaction_distance =
      trainer->State().GetBoundingRadius() + kTalentWipeInteractionPadding;
  if (distance_squared > interaction_distance * interaction_distance) {
    return;
  }

  if (!spell_book_.HandleTalentWipeConfirm(
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ui::game::ScriptEventDispatch::Get().FireConfirmTalentWipe(cost);
}

void WorldSession::HandleSummonCancel(const net::wotlk::WorldPacket& pkt) {
  (void)pkt;
  spell_book_.HandleSummonCancel();
  ui::game::ScriptEventDispatch::Get().FireCancelSummon();
}

void WorldSession::HandleSpellUpdateChainTargets(const net::wotlk::WorldPacket& pkt) {
  if (!spell_book_.HandleSpellUpdateChainTargets(pkt.payload.data(), pkt.payload.size()))
    return;

  const auto& chain = spell_book_.last_chain_targets();
  if (!chain.has_value())
    return;

  const ObjectGuid caster_guid(chain->caster_guid);
  CGUnit_C* caster = objects().GetMutableUnit(caster_guid);
  if (!caster)
    return;

  std::vector<ObjectGuid> target_guids;
  target_guids.reserve(chain->targets.size());
  for (const auto raw : chain->targets)
    target_guids.emplace_back(raw);

  caster->Casts().ProcessMissileHitTargets(
      *caster, *this, target_guids, chain->spell_id);
}

void WorldSession::HandleNotifyDestLocSpellCast(const net::wotlk::WorldPacket& pkt) {
  std::optional<DestLocSpellCast> dispatched_record;
  if (!spell_book_.HandleNotifyDestLocSpellCast(
          pkt.payload.data(),
          pkt.payload.size(),
          CurrentClientTimeMs(),
          &dispatched_record)) {
    return;
  }

  if (dispatched_record.has_value()) {
    QueueDestLocSpellCastVisual(*this, *dispatched_record);
  }
}

void WorldSession::HandlePetLearnedSpell(const net::wotlk::WorldPacket& pkt) {
  spell_book_.HandlePetLearnedSpell(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandlePetUnlearnedSpell(const net::wotlk::WorldPacket& pkt) {
  spell_book_.HandlePetUnlearnedSpell(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleBreakTarget(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleBreakTarget(pkt.payload.data(),
                                         pkt.payload.size())) {
    return;
  }

  auto* const targeting = targeting_system();
  if (targeting == nullptr) {
    return;
  }

  const ObjectGuid guid(combat_handler_.last_break_target_guid());
  if (GroupSystem::Get().IsActivePlayerPartyOrRaidUnitGuid(
          objects(), guid.GetRawValue())) {
    const auto* const unit = objects().GetUnit(guid);
    if (unit == nullptr) {
      return;
    }

    const auto* const viewer = objects().GetActivePlayer();
    if (viewer == nullptr || !IsDuelOpponentOf(*this, *unit, *viewer)) {
      return;
    }
  }

  targeting->InvalidateTrackedGuidReferences(guid.GetRawValue());
}

void WorldSession::HandleClearTarget(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleClearTarget(pkt.payload.data(),
                                         pkt.payload.size())) {
    return;
  }

  if (auto* const targeting = targeting_system(); targeting != nullptr) {
    targeting->ClearTarget(combat_handler_.last_clear_target_guid(),
                           true);
  }
}

void WorldSession::HandleForceDisplayUpdate(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleForceDisplayUpdate(pkt.payload.data(),
                                                 pkt.payload.size())) {
    return;
  }
  const ObjectGuid guid(combat_handler_.last_force_display_update_guid());
  if (auto* unit = objects().GetMutableUnit(guid); unit != nullptr) {
    unit->Presentation().RefreshActiveDisplayRuntimeState();
  }
}

void WorldSession::HandleResurrectFailed(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleResurrectFailed(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto* player = objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  auto& events = ui::game::ScriptEventDispatch::Get();
  if (player->State().GetHealth() > 0u) {
    events.FirePlayerAlive();
    return;
  }

  events.FirePlayerDead();
  ui::game::SetNpcInteractionTarget({});
  CloseActiveLootWindow(
      *this,
      CloseLootWindowOptions{
          .send_release = true,
          .skip_item_check = true,
          .show_interrupted = false,
          .clear_dead_target = true,
      });
}

void WorldSession::HandleSpiritHealerConfirm(const net::wotlk::WorldPacket& pkt) {
  ObjectGuid healer;
  if (!combat_handler_.ParseSpiritHealerConfirm(
          pkt.payload.data(), pkt.payload.size(), healer)) {
    return;
  }

  const auto xp_loss =
      combat::death::ui::HandleSpiritHealerConfirm(*this, healer);
  if (xp_loss.has_value() && death_callbacks_.on_spirit_healer_confirm) {
    death_callbacks_.on_spirit_healer_confirm(*xp_loss);
  }
}

void WorldSession::HandleAreaSpiritHealerTime(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleAreaSpiritHealerTime(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& info = combat_handler_.last_area_spirit_healer_time();
  if (!info.has_value()) {
    return;
  }

  combat::death::ui::SetAreaSpiritHealerCountdown(
      *this, info->healer, info->time_left);
}

void WorldSession::HandleDestructibleBuildingDamage(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleDestructibleBuildingDamage(
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto& building = combat_handler_.last_building_damage();
  if (building.has_value()) {
    CombatLog_HandleHealOpcode(
        objects(), combat_log_, building->target_guid, building->caster_guid,
        building->owner_guid, static_cast<std::int32_t>(building->damage),
        building->spell_id);
  }
}

void WorldSession::HandleCombatEventFailed(const net::wotlk::WorldPacket& pkt) {
  combat_handler_.HandleCombatEventFailed();
  HandleAttackStop(pkt);
}

void WorldSession::HandleProcResist(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleProcResist(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (combat_handler_.last_proc_resist().has_value()) {
    combat_log_.HandleProcResist(*combat_handler_.last_proc_resist());
  }
}

void WorldSession::HandleSpellBreakLog(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleSpellBreakLog(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (combat_handler_.last_spell_break_log().has_value()) {
    combat_log_.HandleSpellAuraBroken(*combat_handler_.last_spell_break_log());
  }
}

void WorldSession::HandleAuraCastLog(const net::wotlk::WorldPacket& pkt) {
  combat_handler_.HandleAuraCastLog(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleResetRangedCombatTimer(const net::wotlk::WorldPacket& pkt) {
  combat_handler_.HandleResetRangedCombatTimer(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleSetProjectilePosition(const net::wotlk::WorldPacket& pkt) {
  if (!combat_handler_.HandleSetProjectilePosition(
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto& correction = combat_handler_.last_projectile_position();
  if (!correction.has_value()) return;
  QueueSpellMissileCorrection(SpellMissilePositionCorrection{
      .caster_guid = correction->caster,
      .cast_count = correction->cast_id,
      .world_position = {correction->x, correction->y, correction->z},
  });
}

}

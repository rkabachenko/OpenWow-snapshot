
#include "openwow/game/spell_action.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/localized_format.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/combat_log.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstdio>
#include <list>
#include <optional>
#include <unordered_map>

namespace openwow::game {

namespace {

struct PeriodicClientSpellTimerEntry {
  ObjectGuid target_guid;
  std::uint32_t owner_spell_id = 0;
  std::uint32_t trigger_spell_id = 0;
  std::uint32_t next_fire_tick = 0;
  std::uint32_t interval_ms = 0;
};

std::unordered_map<int, PendingItemSpellHistoryEntry> &GetPendingItemSpellHistoryStorage() {
  static std::unordered_map<int, PendingItemSpellHistoryEntry> entries;
  return entries;
}

std::list<PeriodicClientSpellTimerEntry> &GetPeriodicClientSpellTimers() {
  static std::list<PeriodicClientSpellTimerEntry> timers;
  return timers;
}

PeriodicClientSpellHooks &GetPeriodicClientSpellHooks() {
  static PeriodicClientSpellHooks hooks;
  return hooks;
}

std::uint32_t DefaultPeriodicClientSpellTickCount() {
  return openwow::core::GameClock::GetTickCount32();
}

bool DefaultPeriodicClientSpellTargetResolver(const ObjectManager& objects,
                                              const ObjectGuid target_guid) {
  return objects.GetUnit(target_guid) != nullptr;
}

bool DefaultPeriodicClientSpellExecutor(const WorldSession& session,
                                        const std::uint32_t trigger_spell_id,
                                        const ObjectGuid target_guid) {
  return session.spells().CastTriggeredSpell(session, trigger_spell_id,
                                             target_guid.GetRawValue(), 0) ==
         SpellCastResult::kSuccess;
}

std::uint32_t GetPeriodicClientSpellTickCount() {
  const auto provider = GetPeriodicClientSpellHooks().tick_count;
  return provider != nullptr ? provider() : DefaultPeriodicClientSpellTickCount();
}

bool ResolvePeriodicClientSpellTarget(const ObjectManager& objects,
                                      const ObjectGuid target_guid) {
  const auto resolver = GetPeriodicClientSpellHooks().target_resolver;
  return resolver != nullptr ? resolver(objects, target_guid)
                             : DefaultPeriodicClientSpellTargetResolver(objects, target_guid);
}

bool ExecutePeriodicClientSpell(const WorldSession& session,
                                const std::uint32_t trigger_spell_id,
                                const ObjectGuid target_guid) {
  const auto executor = GetPeriodicClientSpellHooks().executor;
  return executor != nullptr ? executor(session, trigger_spell_id, target_guid)
                             : DefaultPeriodicClientSpellExecutor(session, trigger_spell_id,
                                                                  target_guid);
}

bool HasTimerElapsed(const std::uint32_t now_tick, const std::uint32_t deadline_tick) {
  return static_cast<std::int32_t>(now_tick - deadline_tick) >= 0;
}

const auto kGlyphSlotTargetMask = SpellTargetFlag::kGlyphSlot;
constexpr std::uint32_t kSpellEffectApplyGlyph = 74u;
constexpr std::uint32_t kSpellFailedInvalidGlyph = 175u;
constexpr std::uint32_t kSpellFailedUniqueGlyph = 176u;
constexpr std::uint32_t kSpellFailedGlyphSocketLocked = 177u;

void DisplayLocalizedSpellFailure(
    const WorldSession& session, const std::uint32_t spell_id,
    const std::uint32_t error_code, const ObjectGuid& caster_guid,
    const std::string& substitution = {}) {
  SpellAction_DisplaySpellFailure(session, spell_id, caster_guid, error_code,
                                  substitution);

  if (caster_guid) {
    ui::game::ScriptEventDispatch::Get().FireUnitSpellcastFailed(
        caster_guid.GetRawValue());
  }
}

const auto kUnitCursorTargetMask =
    SpellTargetFlag::kUnit | SpellTargetFlag::kUnitRaid |
    SpellTargetFlag::kUnitParty | SpellTargetFlag::kUnitEnemy |
    SpellTargetFlag::kUnitAlly | SpellTargetFlag::kCorpseEnemy |
    SpellTargetFlag::kUnitDead | SpellTargetFlag::kCorpseAlly |
    SpellTargetFlag::kUnitMinipet;
const auto kFriendlyCursorTargetMask =
    SpellTargetFlag::kUnitRaid | SpellTargetFlag::kUnitParty |
    SpellTargetFlag::kUnitAlly | SpellTargetFlag::kCorpseAlly;
const auto kHostileCursorTargetMask =
    SpellTargetFlag::kUnitEnemy | SpellTargetFlag::kCorpseEnemy;
const auto kItemCursorTargetMask =
    SpellTargetFlag::kItem | SpellTargetFlag::kGoItem;
const auto kGameObjectCursorTargetMask =
    SpellTargetFlag::kGameobject | SpellTargetFlag::kGoItem;
const auto kCorpseCursorTargetMask =
    SpellTargetFlag::kCorpseEnemy | SpellTargetFlag::kCorpseAlly;
const auto kPositionCursorTargetMask =
    SpellTargetFlag::kSourceLocation | SpellTargetFlag::kDestLocation;
const auto kExplicitCursorTargetMask =
    kUnitCursorTargetMask | kItemCursorTargetMask |
    kGameObjectCursorTargetMask | kGlyphSlotTargetMask;
constexpr std::uint32_t kSpellAttrExSelfCast = 0x00000020u;
constexpr std::uint32_t kSpellAttrEx5RedirectThroughTarget = 0x00000800u;

const auto kRedirectFriendlySelectorMask =
    SpellTargetFlag::kUnitRaid | SpellTargetFlag::kUnitParty |
    SpellTargetFlag::kUnitAlly;

enum class SpellActionTargetState : std::uint8_t {
  kInvalid,
  kReady,
  kAwaitingUnitOrObject,
  kAwaitingPosition,
};

struct SpellActionTargetResolution {
  SpellActionTargetState state = SpellActionTargetState::kInvalid;
  ObjectGuid target;
  std::uint32_t cursor_mask = 0;
  std::uint32_t packet_target_mask = 0;
  SpellCastResult failure = SpellCastResult::kBadTargets;
};

bool SpellTargetsFriendly(const std::uint32_t mask) {
  const auto flags = static_cast<SpellTargetFlag>(mask);
  return HasFlag(flags, kFriendlyCursorTargetMask) &&
         !HasFlag(flags, kHostileCursorTargetMask);
}

SpellUnitTargetResolution ResolveActionUnitTarget(
    const WorldSession& session,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell,
    const CGUnit_C& caster,
    const CGPlayer_C& active_player,
    const CGUnit_C* const selected_target,
    const std::uint32_t target_mask,
    const bool allow_self_fallback) {
  const CGUnit_C* target = selected_target;
  SpellUnitTargetResolution selector_resolution;

  if (target != nullptr) {
    selector_resolution = SpellTargetValidator::ResolveUnitTarget(
        session, dbc, spell.id, caster, *target, target_mask, false);
  }

  if (!selector_resolution.IsValid() && allow_self_fallback &&
      (target == nullptr || target->GetGuid() != active_player.GetGuid())) {
    target = &active_player;
    selector_resolution = SpellTargetValidator::ResolveUnitTarget(
        session, dbc, spell.id, caster, active_player, target_mask, false);
  }
  if (!selector_resolution.IsValid() && allow_self_fallback &&
      caster.GetGuid() != active_player.GetGuid() &&
      (target == nullptr || target->GetGuid() != caster.GetGuid())) {
    target = &caster;
    selector_resolution = SpellTargetValidator::ResolveUnitTarget(
        session, dbc, spell.id, caster, caster, target_mask, false);
  }

  if (!selector_resolution.IsValid() || target == nullptr) {
    return selector_resolution;
  }

  return SpellTargetValidator::ResolveUnitTarget(
      session, dbc, spell.id, caster, *target, target_mask, true);
}

SpellActionTargetResolution UnresolvedSpellActionTarget(
    const WorldSession& session,
    const data::dbc::SpellEntry& spell,
    SpellActionTargetResolution resolution) {
  const auto cursor_flags =
      static_cast<SpellTargetFlag>(resolution.cursor_mask);
  const bool redirect_accepts_friendly =
      (spell.attributes_ex5 & kSpellAttrEx5RedirectThroughTarget) != 0 &&
      HasFlag(cursor_flags, kRedirectFriendlySelectorMask);

  if (HasFlag(cursor_flags, SpellTargetFlag::kUnitEnemy) &&
      !redirect_accepts_friendly) {
    resolution.state = SpellActionTargetState::kInvalid;
    resolution.failure = session.objects().GetTargetGuid()
                             ? SpellCastResult::kBadTargets
                             : SpellCastResult::kBadImplicitTargets;
    return resolution;
  }

  resolution.state = SpellActionTargetState::kAwaitingUnitOrObject;
  resolution.failure = SpellCastResult::kSuccess;
  return resolution;
}

SpellActionTargetResolution ResolveSpellActionTarget(
    const WorldSession& session,
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellEntry& spell,
    const CGUnit_C& caster,
    const CGPlayer_C& active_player,
    const ObjectGuid explicit_target) {
  SpellActionTargetResolution resolution;
  resolution.cursor_mask = SpellTargetValidator::BuildTargetMask(spell, &dbc);

  auto cursor_flags = static_cast<SpellTargetFlag>(resolution.cursor_mask);
  const bool requires_unit = HasFlag(cursor_flags, kUnitCursorTargetMask);
  const bool requires_item = HasFlag(cursor_flags, kItemCursorTargetMask);
  const bool requires_game_object =
      HasFlag(cursor_flags, kGameObjectCursorTargetMask);
  const bool requires_glyph = HasFlag(cursor_flags, kGlyphSlotTargetMask);
  const bool requires_explicit =
      HasFlag(cursor_flags, kExplicitCursorTargetMask);
  const bool self_only =
      (spell.attributes_ex & kSpellAttrExSelfCast) != 0;
  const bool allow_self_fallback =
      requires_unit &&
      (self_only ||
       ui::game::CVarSystem::Instance().GetCVarBool("autoSelfCast"));

  ObjectGuid target = requires_explicit ? explicit_target : ObjectGuid{};
  if (!target && requires_explicit) {
    target = session.objects().GetTargetGuid();
  }
  if (!target && allow_self_fallback) {
    target = active_player.GetGuid();
  }

  if (target) {
    const auto* const object = session.objects().GetObjectByGUID(target);
    if (object == nullptr || object->IsUnit()) {
      if (!requires_unit) {
        return UnresolvedSpellActionTarget(session, spell, resolution);
      }
      const auto* const selected_unit = object != nullptr
          ? &static_cast<const CGUnit_C&>(*object)
          : nullptr;
      const auto target_result = ResolveActionUnitTarget(
          session, dbc, spell, caster, active_player, selected_unit,
          resolution.cursor_mask, allow_self_fallback);
      if (!target_result.IsValid()) {

        if (target_result.result != SpellTargetResult::kInvalidTarget) {
          resolution.failure =
              SpellTargetResultToCastResult(target_result.result);
          return resolution;
        }
        return UnresolvedSpellActionTarget(session, spell, resolution);
      }
      target = target_result.resolved_target;
      resolution.packet_target_mask = target_result.packet_target_mask;
      resolution.cursor_mask &= ~target_result.consumed_target_mask;
    } else if (object->IsItem()) {
      if (!requires_item) {
        return UnresolvedSpellActionTarget(session, spell, resolution);
      }
      resolution.packet_target_mask =
          static_cast<std::uint32_t>(SpellTargetFlag::kItem);
      resolution.cursor_mask &=
          ~static_cast<std::uint32_t>(kItemCursorTargetMask);
    } else if (object->IsGameObject()) {
      if (!requires_game_object) {
        return UnresolvedSpellActionTarget(session, spell, resolution);
      }
      resolution.packet_target_mask =
          static_cast<std::uint32_t>(SpellTargetFlag::kGameobject);
      resolution.cursor_mask &=
          ~static_cast<std::uint32_t>(kGameObjectCursorTargetMask);
    } else if (object->IsCorpse()) {
      if (!HasFlag(cursor_flags, kCorpseCursorTargetMask)) {
        return UnresolvedSpellActionTarget(session, spell, resolution);
      }
      const bool friendly = caster.Interaction().IsFriendlyCorpseTarget(
          static_cast<const CGCorpse_C&>(*object));
      if (friendly && HasFlag(cursor_flags, SpellTargetFlag::kCorpseAlly)) {
        resolution.packet_target_mask =
            static_cast<std::uint32_t>(SpellTargetFlag::kCorpseAlly);
      } else if (!friendly &&
                 HasFlag(cursor_flags, SpellTargetFlag::kCorpseEnemy)) {
        resolution.packet_target_mask =
            static_cast<std::uint32_t>(SpellTargetFlag::kCorpseEnemy);
      } else {
        return UnresolvedSpellActionTarget(session, spell, resolution);
      }
      resolution.cursor_mask &=
          ~static_cast<std::uint32_t>(kCorpseCursorTargetMask);
    } else {
      return UnresolvedSpellActionTarget(session, spell, resolution);
    }
    resolution.target = target;
  } else if (requires_unit || requires_item || requires_game_object ||
             requires_glyph) {
    return UnresolvedSpellActionTarget(session, spell, resolution);
  }

  cursor_flags = static_cast<SpellTargetFlag>(resolution.cursor_mask);
  resolution.state = HasFlag(cursor_flags, kPositionCursorTargetMask)
      ? SpellActionTargetState::kAwaitingPosition
      : SpellActionTargetState::kReady;
  resolution.failure = SpellCastResult::kSuccess;
  return resolution;
}

bool BeginSpellActionTargeting(
    const data::dbc::SpellEntry& spell,
    const data::dbc::DbcLoader& dbc,
    const CGUnit_C& caster,
    const SpellActionTargetResolution& resolution,
    SpellCastRuntime& spell_client,
    const SpellCastOrigin origin = SpellCastOrigin::kPlayer) {
  if (!spell_client.PrepareTargetedCast(spell.id, origin,
                                        caster.GetGuid())) {
    return false;
  }
  if (HasFlag(static_cast<SpellTargetFlag>(resolution.packet_target_mask),
              SpellTargetFlag::kItem)) {
    spell_client.SetItemTarget(SpellSlotType::kCurrent, resolution.target);
  } else if (resolution.target) {
    spell_client.SetObjectTarget(SpellSlotType::kCurrent, resolution.target,
                                 resolution.packet_target_mask);
  }

  const auto mode = resolution.state == SpellActionTargetState::kAwaitingPosition
      ? SpellTargetingMode::GroundTarget
      : SpellTargetingMode::Unit;
  auto& targeting = spell_client.GetTargeting();
  targeting.StartTargeting(spell.id, mode, 0.0f, 0.0f,
                           resolution.cursor_mask);

  const auto* const range_entry = spell.range_index != 0
      ? dbc.spell_range().LookupEntry(spell.range_index)
      : nullptr;
  const auto range = SpellTargetValidator::GetUntargetedRangeWindow(
      spell, range_entry, caster,
      SpellTargetsFriendly(resolution.cursor_mask));
  targeting.SetMinRange(range.min_range);
  targeting.SetMaxRange(range.max_range);
  return true;
}

void DisplaySpellActionFailure(const WorldSession& session,
                               const std::uint32_t spell_id,
                               const SpellCastResult failure,
                               const ObjectGuid caster_guid) {

  if (failure == SpellCastResult::kDontReport) {
    if (caster_guid) {
      ui::game::ScriptEventDispatch::Get().FireUnitSpellcastFailedQuiet(
          caster_guid.GetRawValue());
    }
    return;
  }
  DisplayLocalizedSpellFailure(session, spell_id,
                               static_cast<std::uint8_t>(failure), caster_guid);
}

void DisplaySpellActionFailure(
    const WorldSession& session,
    const std::uint32_t spell_id,
    const SpellRequirementValidation& validation,
    const ObjectGuid caster_guid) {
  if (validation.result == SpellCastResult::kDontReport) {
    DisplaySpellActionFailure(session, spell_id, validation.result,
                              caster_guid);
    return;
  }

  std::string substitution;
  if (validation.extra1 >= 0) {
    if (validation.result == SpellCastResult::kPreventedByMechanic) {
      const auto* const dbc = session.GetDbcLoader();
      const auto* const mechanic =
          dbc != nullptr
              ? dbc->spell_mechanic().LookupEntry(
                    static_cast<std::uint32_t>(validation.extra1))
              : nullptr;
      if (mechanic != nullptr) {
        substitution.assign(mechanic->name);
      }
    } else if (validation.result == SpellCastResult::kReagents ||
               validation.result == SpellCastResult::kTotems) {
      const auto item_name = session.item_definitions().GetItemNameSnapshot(
          static_cast<std::uint32_t>(validation.extra1));
      if (item_name.has_value()) {
        substitution = *item_name;
      }
    }
  }

  DisplayLocalizedSpellFailure(session, spell_id,
                               static_cast<std::uint8_t>(validation.result),
                               caster_guid, substitution);
}

}

void SpellAction_DisplaySpellFailure(const WorldSession& session,
                                     const std::uint32_t spell_id,
                                     const ObjectGuid caster_guid,
                                     const std::uint32_t error_code,
                                     const std::string& substitution) {

  std::string localized_format;
  std::string effective_substitution = substitution;
  const auto result = static_cast<SpellCastResult>(error_code);

  if (result == SpellCastResult::kAlreadyAtFullHealth) {
    localized_format = Localization::Get().GetString(
        "ERR_SPELL_FAILED_ALREADY_AT_FULL_HEALTH", {});
  } else if (result == SpellCastResult::kAlreadyAtFullMana) {
    localized_format = Localization::Get().GetString(
        "ERR_SPELL_FAILED_ALREADY_AT_FULL_MANA", {});
  } else if (result == SpellCastResult::kAlreadyAtFullPower ||
             result == SpellCastResult::kNoPower) {
    localized_format = Localization::Get().GetString(
        result == SpellCastResult::kAlreadyAtFullPower
            ? "ERR_SPELL_FAILED_ALREADY_AT_FULL_POWER_S"
            : "ERR_OUT_OF_POWER_DISPLAY",
        {});

    const auto* const dbc = session.GetDbcLoader();
    const auto* const spell_entry =
        dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
    if (spell_entry != nullptr) {
      const char* const power_token =
          PowerTypeToString(spell_entry->power_type);
      effective_substitution =
          Localization::Get().GetString(power_token, power_token);
    }
  } else {
    const char* const failure_key = SpellFailedReasonToString(error_code);

    localized_format = Localization::Get().GetString(failure_key, {});
  }

  if (localized_format.empty()) {
    return;
  }

  std::string message = localized_format;
  if (!effective_substitution.empty()) {
    char formatted[1024]{};
    core::FormatLocalized(formatted, sizeof(formatted),
                          localized_format.c_str(),
                          effective_substitution.c_str());
    message = formatted;
  }
  if (message.empty()) {
    return;
  }

  ui::UIErrorManager::Get().AddErrorMessage(message);
  ui::game::ScriptEventDispatch::Get().FireUiErrorMessage(message);

  auto& combat_log = const_cast<CombatLog&>(session.combat_log());
  (void)combat_log.HandleSpellCastFailed(caster_guid.GetRawValue(), spell_id,
                                         message);
}

bool ConfirmSpellGroundTarget(const WorldSession& session,
                              const SpellGroundClickData& click) {
  auto& spell_client = session.spells();
  auto& targeting = spell_client.GetTargeting();
  const auto targeting_state = targeting.GetState();
  if (!targeting_state.isActive || targeting_state.spellId == 0) {
    return false;
  }
  const auto spell_id = targeting_state.spellId;

  const auto target_mask = targeting.GetTargetMask();
  const auto target_flags = static_cast<SpellTargetFlag>(target_mask);
  const auto position_flags =
      SpellTargetFlag::kSourceLocation | SpellTargetFlag::kDestLocation;
  if (!HasFlag(target_flags, position_flags)) {

    return false;
  }

  const SpellCastOrigin cast_origin =
      spell_client.GetPreparedOrigin(spell_id);
  const ObjectGuid caster_guid =
      spell_client.GetPreparedCaster(spell_id);
  if (!spell_client.PrepareTargetedCast(spell_id, cast_origin,
                                        caster_guid)) {
    return false;
  }

  if (HasFlag(target_flags, SpellTargetFlag::kSourceLocation)) {
    spell_client.SetSourcePosition(SpellSlotType::kCurrent, click.object_guid,
                                   click.x, click.y, click.z);

    targeting.SetTargetMask(
        target_mask &
        ~static_cast<std::uint32_t>(SpellTargetFlag::kSourceLocation));
  }
  else if (HasFlag(target_flags, SpellTargetFlag::kDestLocation)) {
    spell_client.SetDestinationPosition(SpellSlotType::kCurrent,
                                        click.object_guid, click.x, click.y,
                                        click.z);

    targeting.SetTargetMask(
        target_mask &
        ~static_cast<std::uint32_t>(SpellTargetFlag::kDestLocation));
  }

  if (targeting.GetTargetMask() == 0) {

    (void)spell_client.CastPreparedSpell(session);
    spell_client.DiscardPreparedTargetedCast(spell_id);
    targeting.CancelTargeting();
  }

  return true;
}

bool SpellAction_PlaceGlyphInSocket(WorldSession& session, int socket_index) {
  auto *const active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  if (socket_index < 0 || socket_index >= 6) {
    return false;
  }

  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  auto &targeting = session.spells().GetTargeting();
  const auto targeting_state = targeting.GetState();
  if (!targeting_state.isActive || targeting_state.spellId == 0) {
    return false;
  }
  const auto active_spell_id = targeting_state.spellId;

  const auto *const spell_entry = dbc->spell().LookupEntry(active_spell_id);
  if (spell_entry == nullptr) {
    return false;
  }

  if ((active_player->GetGlyphsEnabled() & (1u << socket_index)) == 0u) {
    DisplayLocalizedSpellFailure(session, active_spell_id,
                                 kSpellFailedGlyphSocketLocked,
                                 active_player->GetGuid());
    return false;
  }

  const auto glyph_slot_id = active_player->GetGlyphSlot(static_cast<std::uint8_t>(socket_index));
  const auto *const glyph_slot_entry = dbc->glyph_slot().LookupEntry(glyph_slot_id);

  std::uint32_t glyph_property_id = 0;
  for (std::size_t effect_index = 0; effect_index < spell_entry->effect.size(); ++effect_index) {
    if (spell_entry->effect[effect_index] != kSpellEffectApplyGlyph) {
      continue;
    }

    const auto candidate_property_id =
        static_cast<std::uint32_t>(spell_entry->effect_misc_value[effect_index]);
    const auto *const candidate_property_entry =
        dbc->glyph_properties().LookupEntry(candidate_property_id);
    if (candidate_property_entry == nullptr || glyph_slot_entry == nullptr) {
      continue;
    }

    if (candidate_property_entry->type != glyph_slot_entry->type) {
      DisplayLocalizedSpellFailure(session, active_spell_id,
                                   kSpellFailedInvalidGlyph,
                                   active_player->GetGuid());
      return false;
    }

    glyph_property_id = candidate_property_id;
    break;
  }

  if (glyph_property_id == 0u) {
    DisplayLocalizedSpellFailure(session, active_spell_id,
                                 kSpellFailedInvalidGlyph,
                                 active_player->GetGuid());
    return false;
  }

  for (std::uint8_t slot_index = 0; slot_index < 6; ++slot_index) {
    if (active_player->GetGlyph(slot_index) == glyph_property_id) {
      DisplayLocalizedSpellFailure(session, active_spell_id,
                                   kSpellFailedUniqueGlyph,
                                   active_player->GetGuid());
      return false;
    }
  }

  if (!HasFlag(static_cast<SpellTargetFlag>(targeting.GetTargetMask()),
               kGlyphSlotTargetMask)) {
    return false;
  }

  const auto source_item_guid = targeting.GetPendingSourceItem();
  if (!source_item_guid) {
    return false;
  }

  if (!session.interaction().SendUseItemByGuidToGlyphSlot(
          source_item_guid.GetRawValue(), static_cast<std::uint32_t>(socket_index))) {
    return false;
  }

  targeting.CancelTargeting();
  return true;
}

void SpellAction_CastTalentGroupSpell(const WorldSession& session,
                                      std::uint32_t spell_id, int a4, int a5,
                                      int a6) {
  (void)a4;
  (void)a5;
  (void)a6;

  auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  auto &sqb = SpellQueryBridge::Get();
  if (!sqb.HasSpellData(spell_id)) {
    return;
  }

  if (!SpellbookSystem::Get().HasSpell(spell_id)) {
    return;
  }

  const auto* const dbc = session.GetDbcLoader();
  if (SpellHasAttackActionEffect(spell_id, dbc)) {

    session.spells().CastSpell(session, spell_id);
  } else if (SpellHasRangedAttackActionFlags(spell_id, dbc)) {
    auto& casts = session.spells();
    if (casts.CastSpell(session, spell_id) == SpellCastResult::kSuccess) {
      casts.StartAutoRepeat(spell_id);
    }
  } else {

    session.spells().CastSpell(session, spell_id);
  }
}

bool SpellAction_TryAssignTargetByGuid(WorldSession& session,
                                       std::uint64_t guid) {
  if (guid == 0) {
    return false;
  }

  const auto* const dbc = session.GetDbcLoader();
  const auto targeting_state = session.spells().GetTargeting().GetState();
  const auto spell_id = targeting_state.isActive ? targeting_state.spellId : 0u;
  const auto* const spell = dbc != nullptr && spell_id != 0
      ? dbc->spell().LookupEntry(spell_id)
      : nullptr;
  if (spell == nullptr) {
    return false;
  }

  const ObjectGuid target(guid);
  return TryAssignTarget(session, reinterpret_cast<std::uintptr_t>(&target),
                         reinterpret_cast<std::uintptr_t>(spell));
}

namespace {

HeldItemPetSpellResolver g_held_item_pet_spell_resolver = nullptr;

bool DefaultHeldItemPetSpellResolver(const WorldSession& session,
                                     const std::uint32_t spell_id) {

  auto &sqb = SpellQueryBridge::Get();
  if (!sqb.HasSpellData(spell_id)) {
    return false;
  }
  session.spells().CastSpell(session, spell_id);
  return true;
}

}

void SpellAction_SetHeldItemPetSpellResolver(const HeldItemPetSpellResolver resolver) {
  g_held_item_pet_spell_resolver = resolver;
}

void SpellAction_ResetHeldItemPetSpellResolver() {
  g_held_item_pet_spell_resolver = nullptr;
}

bool SpellAction_TryCastHeldItemOnPet(const WorldSession& session,
                                      const CGUnit_C* item_obj,
                                      const CGUnit_C* target_unit) {
  if (item_obj == nullptr) {
    return false;
  }
  if (target_unit == nullptr) {
    return false;
  }

  if (target_unit->State().GetPetNumber() == 0) {
    return false;
  }

  const auto spell_id =
      session.spells().GetRememberedHeldItemPetTargetSpellId();
  if (spell_id == 0) {
    return false;
  }

  auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  if (target_unit->State().GetCreatedBy() != active_player->GetGuid()) {
    return false;
  }

  const auto resolver = g_held_item_pet_spell_resolver
                            ? g_held_item_pet_spell_resolver
                            : DefaultHeldItemPetSpellResolver;
  if (!resolver(session, spell_id)) {
    return false;
  }

  session.spells().GetTargeting().ConfirmTarget(target_unit->GetGuid());
  return true;
}

void SpellAction_SchedulePeriodicClientSpell(const ObjectGuid target_guid,
                                             const std::uint32_t owner_spell_id,
                                             const std::uint32_t trigger_spell_id,
                                             const std::uint32_t interval_ms) {
  auto &timers = GetPeriodicClientSpellTimers();
  PeriodicClientSpellTimerEntry timer;
  timer.target_guid = target_guid;
  timer.owner_spell_id = owner_spell_id;
  timer.trigger_spell_id = trigger_spell_id;
  timer.next_fire_tick = GetPeriodicClientSpellTickCount() + interval_ms;
  timer.interval_ms = interval_ms;
  timers.push_front(timer);
}

void SpellAction_CancelPeriodicClientSpell(const ObjectGuid target_guid,
                                           const std::uint32_t owner_spell_id_filter) {
  auto &timers = GetPeriodicClientSpellTimers();
  for (auto it = timers.begin(); it != timers.end();) {
    const bool target_matches = it->target_guid == target_guid;
    const bool owner_matches =
        owner_spell_id_filter == 0 || it->owner_spell_id == owner_spell_id_filter;
    if (target_matches && owner_matches) {
      it = timers.erase(it);
      continue;
    }
    ++it;
  }
}

void SpellAction_SetPeriodicClientSpellHooks(const PeriodicClientSpellHooks hooks) {
  GetPeriodicClientSpellHooks() = hooks;
}

void SpellAction_ResetPeriodicClientSpellHooks() {
  GetPeriodicClientSpellHooks() = {};
}

void SpellAction_ResetPeriodicClientSpellTimers() {
  GetPeriodicClientSpellTimers().clear();
}

std::size_t SpellAction_GetPeriodicClientSpellTimerCount() {
  return GetPeriodicClientSpellTimers().size();
}

int SpellAction_ProcessPeriodicClientSpellTimers(const WorldSession& session) {
  for (auto &timer : GetPeriodicClientSpellTimers()) {
    const std::uint32_t now_tick = GetPeriodicClientSpellTickCount();
    if (!HasTimerElapsed(now_tick, timer.next_fire_tick)) {
      continue;
    }

    if (ResolvePeriodicClientSpellTarget(session.objects(), timer.target_guid)) {
      (void)ExecutePeriodicClientSpell(session, timer.trigger_spell_id, timer.target_guid);
    }

    timer.next_fire_tick = GetPeriodicClientSpellTickCount() + timer.interval_ms;
  }

  return 1;
}

PendingItemSpellHistoryEntry *
SpellAction_FindOrCreatePendingItemSpellHistory(const int item_entry_id, const int spell_id,
                                                const int auxiliary_value, const int cast_flag) {
  auto &pending_entries = GetPendingItemSpellHistoryStorage();
  auto [it, inserted] = pending_entries.try_emplace(item_entry_id, PendingItemSpellHistoryEntry{});
  PendingItemSpellHistoryEntry &entry = it->second;

  if (inserted) {
    entry.item_entry_id = item_entry_id;
  }

  entry.spell_id = spell_id;
  entry.auxiliary_value = auxiliary_value;
  entry.cast_flag = static_cast<std::uint8_t>(cast_flag & 0xFF);
  return &entry;
}

void SpellAction_Update(const WorldSession& session, float dt) {
  (void)dt;
  (void)SpellAction_ProcessPeriodicClientSpellTimers(session);
}

bool SpellAction_ResolveTargeting(const WorldSession& session,
                                  std::uint32_t spell_id,
                                  std::uint64_t explicit_target_guid,
                                  int cast_flags) {
  (void)cast_flags;

  const auto* const dbc = session.GetDbcLoader();
  const auto* const player = session.objects().GetActivePlayer();
  if (dbc == nullptr || player == nullptr) {
    return false;
  }
  const auto* const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto preflight = session.spells().ValidatePlayerCastRequest(session,
                                                                    spell_id);
  if (preflight != SpellCastResult::kSuccess) {
    DisplaySpellActionFailure(session, spell_id, preflight,
                              player->GetGuid());
    return false;
  }

  const auto resolution = ResolveSpellActionTarget(
      session, *dbc, *spell, *player, *player,
      ObjectGuid(explicit_target_guid));
  if (resolution.state == SpellActionTargetState::kInvalid) {
    return false;
  }
  if (resolution.state == SpellActionTargetState::kReady) {
    return true;
  }

  return BeginSpellActionTargeting(
      *spell, *dbc, *player, resolution, session.spells());
}

bool SpellAction_ValidateAndInitiateCast(const WorldSession& session,
                                         std::uint32_t spell_id,
                                         std::uint64_t target_guid,
                                         int item_slot, int cast_flags) {
  (void)item_slot;

  const auto* const dbc = session.GetDbcLoader();
  auto* const player = session.objects().GetActivePlayer();
  if (dbc == nullptr || player == nullptr) {
    return false;
  }
  const auto* const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto preflight = session.spells().ValidatePlayerCastRequest(session,
                                                                    spell_id);
  if (preflight != SpellCastResult::kSuccess) {
    DisplaySpellActionFailure(session, spell_id, preflight,
                              player->GetGuid());
    return false;
  }

  const auto resolution = ResolveSpellActionTarget(
      session, *dbc, *spell, *player, *player, ObjectGuid(target_guid));
  if (resolution.state == SpellActionTargetState::kInvalid) {
    DisplaySpellActionFailure(session, spell_id, resolution.failure,
                              player->GetGuid());
    return false;
  }

  const std::uint64_t resolved_target = resolution.target.GetRawValue();
  const auto requirements = ValidateSpellRequirementsDetailed(
      session, reinterpret_cast<std::uintptr_t>(player),
      reinterpret_cast<std::uintptr_t>(spell),
      reinterpret_cast<std::uintptr_t>(&resolved_target), 0, false);
  if (!requirements.IsSuccess()) {
    DisplaySpellActionFailure(session, spell_id, requirements,
                              player->GetGuid());
    return false;
  }
  if (!HasEnoughSpellPower(*spell, *player, session)) {
    DisplaySpellActionFailure(session, spell_id, SpellCastResult::kNoPower,
                              player->GetGuid());
    return false;
  }

  auto& spell_client = session.spells();
  if (resolution.state != SpellActionTargetState::kReady) {
    return BeginSpellActionTargeting(
        *spell, *dbc, *player, resolution, spell_client);
  }

  const bool has_prepared_object_target =
      resolution.target && resolution.packet_target_mask != 0u;
  if (has_prepared_object_target) {
    if (!spell_client.PrepareTargetedCast(spell_id)) {
      return false;
    }
    if (HasFlag(static_cast<SpellTargetFlag>(resolution.packet_target_mask),
                SpellTargetFlag::kItem)) {
      spell_client.SetItemTarget(SpellSlotType::kCurrent, resolution.target);
    } else {
      spell_client.SetObjectTarget(SpellSlotType::kCurrent, resolution.target,
                                   resolution.packet_target_mask);
    }
  }

  const auto result = spell_client.CastSpell(
      session, spell_id, resolved_target,
      static_cast<std::uint8_t>(static_cast<std::uint32_t>(cast_flags)));
  if (has_prepared_object_target) {
    spell_client.DiscardPreparedTargetedCast(spell_id);
  }
  if (result != SpellCastResult::kSuccess) {
    DisplaySpellActionFailure(session, spell_id, result,
                              player->GetGuid());
    return false;
  }
  return true;
}

bool SpellAction_ValidateAndInitiatePetCast(
    const WorldSession& session, const ObjectGuid pet_guid,
    const std::uint32_t spell_id, const ObjectGuid target_guid) {
  const auto* const dbc = session.GetDbcLoader();
  const auto* const active_player = session.objects().GetActivePlayer();
  const auto* const pet = session.objects().GetUnit(pet_guid);
  if (dbc == nullptr || active_player == nullptr || pet == nullptr ||
      pet->State().IsDead()) {
    return false;
  }
  const auto* const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto resolution = ResolveSpellActionTarget(
      session, *dbc, *spell, *pet, *active_player, target_guid);
  if (resolution.state == SpellActionTargetState::kInvalid) {
    DisplaySpellActionFailure(session, spell_id, resolution.failure,
                              pet->GetGuid());
    return false;
  }

  const std::uint64_t resolved_target = resolution.target.GetRawValue();
  const auto requirements = ValidateSpellRequirementsDetailed(
      session, reinterpret_cast<std::uintptr_t>(pet),
      reinterpret_cast<std::uintptr_t>(spell),
      reinterpret_cast<std::uintptr_t>(&resolved_target), 0, false);
  if (!requirements.IsSuccess()) {
    DisplaySpellActionFailure(session, spell_id, requirements,
                              pet->GetGuid());
    return false;
  }
  if (!HasEnoughSpellPower(*spell, *pet, session)) {
    DisplaySpellActionFailure(session, spell_id, SpellCastResult::kNoPower,
                              pet->GetGuid());
    return false;
  }

  auto& spell_client = session.spells();
  if (resolution.state != SpellActionTargetState::kReady) {
    return BeginSpellActionTargeting(
        *spell, *dbc, *pet, resolution, spell_client,
        SpellCastOrigin::kPet);
  }

  const bool has_prepared_object_target =
      resolution.target && resolution.packet_target_mask != 0u;
  if (!spell_client.PrepareTargetedCast(
          spell_id, SpellCastOrigin::kPet, pet_guid)) {
    return false;
  }
  if (has_prepared_object_target) {
    if (HasFlag(static_cast<SpellTargetFlag>(resolution.packet_target_mask),
                SpellTargetFlag::kItem)) {
      spell_client.SetItemTarget(SpellSlotType::kCurrent,
                                 resolution.target);
    } else {
      spell_client.SetObjectTarget(SpellSlotType::kCurrent,
                                   resolution.target,
                                   resolution.packet_target_mask);
    }
  }

  std::uint32_t blocking_mechanic = 0;
  const auto result = spell_client.CastPetSpell(
      session, pet_guid, spell_id, resolved_target, &blocking_mechanic);
  if (result != SpellCastResult::kSuccess) {
    spell_client.DiscardPreparedTargetedCast(spell_id);
    DisplaySpellActionFailure(
        session, spell_id,
        SpellRequirementValidation{
            result,
            blocking_mechanic != 0
                ? static_cast<std::int32_t>(blocking_mechanic)
                : -1,
            -1},
        pet->GetGuid());
    return false;
  }
  return true;
}

namespace detail {

void ClearPendingItemSpellHistory() {
  GetPendingItemSpellHistoryStorage().clear();
}

std::size_t GetPendingItemSpellHistoryCount() {
  return GetPendingItemSpellHistoryStorage().size();
}

std::optional<PendingItemSpellHistoryEntry>
GetPendingItemSpellHistoryEntry(const int item_entry_id) {
  const auto &pending_entries = GetPendingItemSpellHistoryStorage();
  const auto it = pending_entries.find(item_entry_id);
  if (it == pending_entries.end()) {
    return std::nullopt;
  }

  return it->second;
}

}

}

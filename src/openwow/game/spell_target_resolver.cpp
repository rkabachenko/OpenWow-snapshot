#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_cast_diagnostics.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/active_player_environment.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/group_system.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/proc_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/aura_application.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/recent_cast_tracker.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_usability.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

namespace openwow::game {

namespace {

bool HasActiveCursorSpell(const SpellCastRuntime& spells) {
  const auto state = spells.GetTargeting().GetState();
  return (state.isActive && state.spellId != 0) ||
         spells.GetCurrentSpellId() != 0;
}

std::optional<SpellQueryResult> QueryActiveCursorSpell(
    const SpellCastRuntime& spells) {
  const auto spell_id = spells.GetCurrentSpellId();
  if (spell_id == 0) {
    return std::nullopt;
  }
  return SpellQueryBridge::Get().Query(spell_id);
}

std::uint32_t ResolveActiveTargetingSpellId(const SpellCastRuntime& spells) {
  const auto spell_id = spells.GetCurrentSpellId();
  if (spell_id != 0) {
    return spell_id;
  }

  const auto state = spells.GetTargeting().GetState();
  return state.isActive ? state.spellId : 0u;
}

}

std::optional<SpellTargetRangeWindow> QueryActiveGroundClickRangeWindow(
    const WorldSession& session, const SpellCastRuntime& spells) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return std::nullopt;
  }

  const auto* caster = session.objects().GetActivePlayer();
  if (caster == nullptr) {
    return std::nullopt;
  }

  const auto spell_id = ResolveActiveTargetingSpellId(spells);
  if (spell_id == 0) {
    return std::nullopt;
  }

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              "NOSPELLIDFOUND|" + std::to_string(static_cast<int>(spell_id)));
    return std::nullopt;
  }

  const auto* range_entry = dbc->spell_range().LookupEntry(spell->range_index);
  return SpellTargetValidator::GetUntargetedRangeWindow(
      *spell, range_entry, *caster, false, &session);
}

Position ResolveGroundClickWorldPosition(const WorldSession& session,
                                         const SpellGroundClickData& click) {
  Position world_position{click.x, click.y, click.z, 0.0f};
  if (click.object_guid.IsEmpty()) {
    return world_position;
  }

  const auto* object = session.objects().Get(click.object_guid);
  if (object == nullptr) {
    return world_position;
  }

  const float orientation = object->GetOrientation();
  const float cos_orientation = std::cos(orientation);
  const float sin_orientation = std::sin(orientation);
  world_position.x = object->GetX() + click.x * cos_orientation - click.y * sin_orientation;
  world_position.y = object->GetY() + click.x * sin_orientation + click.y * cos_orientation;
  world_position.z = object->GetZ() + click.z;
  return world_position;
}

namespace {

bool IsHarmfulImplicitTarget(const std::uint32_t target) {
  switch (target) {
    case 2:
    case 6:
    case 15:
    case 16:
    case 24:
    case 28:
    case 53:
    case 54:
    case 93:
      return true;
    default:
      return false;
  }
}

bool IsHelpfulImplicitTarget(const std::uint32_t target,
                             const std::uint32_t aura_type) {
  switch (target) {
    case 1:
      return aura_type != 4;
    case 3:
    case 4:
    case 5:
    case 20:
    case 21:
    case 27:
    case 29:
    case 30:
    case 31:
    case 33:
    case 34:
    case 35:
    case 45:
    case 56:
    case 57:
    case 58:
    case 59:
    case 61:
    case 62:
      return true;
    default:
      return false;
  }
}

}

SpellHelpfulHarmfulDisposition GetHelpfulHarmfulDisposition(
    const data::dbc::SpellEntry& spell) {
  if ((spell.targets & 0x100u) != 0u) {
    return SpellHelpfulHarmfulDisposition::kHelpful;
  }
  if ((spell.targets & 0x80u) != 0u) {
    return SpellHelpfulHarmfulDisposition::kHarmful;
  }

  for (std::size_t effect_index = 0;
       effect_index < spell.effect_implicit_target_a.size();
       ++effect_index) {
    if (IsHarmfulImplicitTarget(spell.effect_implicit_target_a[effect_index]) ||
        IsHarmfulImplicitTarget(spell.effect_implicit_target_b[effect_index])) {
      return SpellHelpfulHarmfulDisposition::kHarmful;
    }
  }

  for (std::size_t effect_index = 0;
       effect_index < spell.effect_implicit_target_a.size();
       ++effect_index) {
    if (IsHelpfulImplicitTarget(spell.effect_implicit_target_a[effect_index],
                                spell.effect_apply_aura[effect_index]) ||
        IsHelpfulImplicitTarget(spell.effect_implicit_target_b[effect_index],
                                spell.effect_apply_aura[effect_index])) {
      return SpellHelpfulHarmfulDisposition::kHelpful;
    }
  }

  return SpellHelpfulHarmfulDisposition::kNeutral;
}

bool WriteTargetRangeWindow(const WorldSession& session,
                            const std::uint32_t spell_id,
                            const CGUnit_C& caster,
                            const CGObject_C* pending_target,
                            float* out_min,
                            float* out_max) {
  *out_min = 0.0f;
  *out_max = 0.0f;

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto* range_entry =
      spell->range_index != 0 ? dbc->spell_range().LookupEntry(spell->range_index)
                              : nullptr;

  bool use_friendly_range = GetHelpfulHarmfulDisposition(*spell) ==
                            SpellHelpfulHarmfulDisposition::kHelpful;
  if (pending_target != nullptr && pending_target->IsUnit()) {
    use_friendly_range = caster.Interaction().CanAssistSpellTarget(
        static_cast<const CGUnit_C&>(*pending_target), false);
  }

  const auto target_window =
      pending_target != nullptr && pending_target->IsUnit()
          ? SpellTargetValidator::GetTargetRangeWindow(
                *spell, range_entry, caster,
                static_cast<const CGUnit_C&>(*pending_target),
                use_friendly_range, &session)
          : SpellTargetValidator::GetUntargetedRangeWindow(
                *spell, range_entry, caster, use_friendly_range, &session);

  *out_min = target_window.min_range;
  *out_max = target_window.max_range;
  return true;
}

constexpr std::uint32_t kSpellEffectSummon = 28;
constexpr std::uint32_t kSpellAuraMounted = 78;
constexpr std::uint32_t kSpellAuraModShapeshift = 36;

constexpr std::uint32_t kSummonPropertiesCompanionSlot = 5;
constexpr std::uint32_t kAuraFlagActive = 0x10;

bool IsCompanionSpellRecord(const openwow::data::dbc::SpellEntry& spell,
                            const openwow::data::dbc::DbcLoader& dbc_loader) {
  if (spell.effect_apply_aura[0] == kSpellAuraMounted) {
    return true;
  }

  if (spell.effect[0] != kSpellEffectSummon) {
    return false;
  }

  const auto summon_properties_id =
      static_cast<std::uint32_t>(spell.effect_misc_value_b[0]);
  if (summon_properties_id == 0) {
    return false;
  }

  const auto* summon_properties =
      dbc_loader.summon_properties().LookupEntry(summon_properties_id);
  return summon_properties != nullptr &&
         summon_properties->slot == kSummonPropertiesCompanionSlot;
}

bool HasMatchingVehicleOverrideState(const openwow::data::dbc::SpellEntry& spell,
                                     const CGUnit_C& unit) {
  const auto* vehicle = unit.Vehicle().GetVehicleUnit();
  if (vehicle == nullptr) {
    return false;
  }

  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  if (active_player_guid.IsEmpty() ||
      vehicle->Interaction().GetControllingPlayerGuid() != active_player_guid) {
    return false;
  }

  for (std::size_t effect_index = 0; effect_index < spell.effect.size();
       ++effect_index) {
    if (spell.effect[effect_index] != kSpellEffectSummon ||
        spell.effect_misc_value[effect_index] <= 0) {
      continue;
    }

    if (vehicle->GetEntry() ==
        static_cast<std::uint32_t>(spell.effect_misc_value[effect_index])) {
      return true;
    }
  }

  return false;
}

bool HasActiveMatchingAura(const openwow::data::dbc::SpellEntry& spell,
                           const CGUnit_C& unit) {
  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  const bool is_active_player = unit.GetGuid() == active_player_guid;

  for (const auto& aura : unit.Auras().All()) {
    if (aura.spell_id != spell.id) {
      continue;
    }

    if (is_active_player &&
        Spell_HasAuraType(spell.effect_apply_aura.data(),
                          kSpellAuraModShapeshift) &&
        unit.State().SuppressesCurrentFormSpellQueries()) {
      return false;
    }

    if ((aura.flags & kAuraFlagActive) != 0) {
      return true;
    }
  }

  return false;
}

bool HasEquivalentMountedAura(const openwow::data::dbc::SpellEntry& spell,
                              const openwow::data::dbc::DbcLoader& dbc_loader,
                              const CGUnit_C& unit) {
  if (spell.effect_apply_aura[0] != kSpellAuraMounted ||
      spell.effect_misc_value[0] <= 0) {
    return false;
  }

  for (const auto& aura : unit.Auras().All()) {
    if (aura.spell_id == 0) {
      continue;
    }

    const auto* active_spell = dbc_loader.spell().LookupEntry(aura.spell_id);
    if (active_spell == nullptr ||
        active_spell->effect_apply_aura[0] != kSpellAuraMounted) {
      continue;
    }

    if (active_spell->effect_misc_value[0] == spell.effect_misc_value[0]) {
      return true;
    }
  }

  return false;
}

bool HandleSpellVisualTriggerImpl(WorldSession& session,
                                  const std::uint64_t guid_raw,
                                  const std::uint32_t kit_id,
                                  const bool has_target) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto* kit = dbc->spell_visual_kit().LookupEntry(kit_id);
  if (kit == nullptr) {
    return false;
  }

  auto* unit = static_cast<CGUnit_C*>(CGObject_HasFlags(
      session.objects(), guid_raw, kTypeMaskUnit));
  if (unit == nullptr) {
    return false;
  }

  const std::uint32_t dispatch_type = has_target ? 1u : 0u;
  return unit->SpellVisuals().CreateFromKit(
      session, kit->id, dispatch_type, nullptr, false, {}, 0u, 0u,
      SpellVisualPresentationPhase::kEffect,
      SpellVisualLifecycleAction::kTransient, 0u,
      SpellVisualSpellBinding::kUnbound);

}

bool HandleSpellVisualTrigger(WorldSession& session, const std::uint64_t guid,
                              const std::uint32_t kit_id) {
  return HandleSpellVisualTriggerImpl(session, guid, kit_id,
                                      false);
}

bool HandleSpellVisualTriggerWithTarget(WorldSession& session,
                                        const std::uint64_t guid,
                                        const std::uint32_t kit_id) {
  return HandleSpellVisualTriggerImpl(session, guid, kit_id,
                                      true);
}

bool BuildSpellTooltipHasModifier(const WorldSession& session,
                                   std::uintptr_t spell_rec,
                                   std::uintptr_t modifier_type) {
  if (spell_rec == 0) {
    return false;
  }

  const auto& modifiers = session.aura().modifiers();
  for (const auto& mod : modifiers) {
    if (static_cast<std::uintptr_t>(mod.op) == modifier_type &&
        mod.value != 0) {
      return true;
    }
  }
  return false;
}

bool HasNonStanceShapeshiftEffect(const data::dbc::SpellEntry& spell,
                                  const data::dbc::DbcLoader& dbc) {
  for (std::size_t i = 0; i < data::dbc::kMaxSpellEffects; ++i) {
    if (spell.effect_apply_aura[i] != kSpellAuraModShapeshift)
      continue;

    const auto form_id = static_cast<std::uint32_t>(spell.effect_misc_value[i]);
    const auto* form = dbc.spell_shapeshift_form().LookupEntry(form_id);
    if (!form || (form->flags & data::dbc::kShapeshiftFormFlagIsStance) == 0)
      return true;
  }
  return false;
}

bool CanStopChanneling(const WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) return false;

  if (!player->Casts().IsChanneling()) return false;

  return !player->Casts().IsCurrentCastUninterruptible();
}

constexpr std::uint32_t kAttrEx4ChannelMissile = 0x00040000u;

constexpr std::uint32_t kTargetFlagLocationMask = 0x60u;
constexpr std::uint32_t kTargetFlagUnitOrGoMask = 0x802u;

constexpr std::uint8_t kVisualAllowUnitTargetsInAoE = 0x01u;
constexpr std::uint32_t kVisualRequireTargetsForAoE = 0x100u;

constexpr std::size_t kSpellEntryGoInfoOffset = 8u;
constexpr std::size_t kGoInfoTargetFlagsOffset = 32u;

void ProcessSpellMissileEffects(WorldSession& session,
                                 std::uintptr_t caster,
                                 std::uintptr_t spell_entry,
                                 std::uintptr_t spell_rec,
                                 std::uintptr_t visual_rec,
                                 std::uintptr_t visual_kit,
                                 std::uintptr_t target_list,
                                 float travel_time_seconds) {
  auto* const caster_unit = reinterpret_cast<CGUnit_C*>(caster);
  if (caster_unit == nullptr || caster_unit->object_manager() == nullptr) {
    return;
  }
  auto& objects = *caster_unit->object_manager();

  if (visual_kit != 0) {
    const auto* const kit =
        reinterpret_cast<const data::dbc::SpellVisualKitEntry*>(visual_kit);
    if (caster_unit != nullptr && kit != nullptr) {
      (void)caster_unit->SpellVisuals().CreateFromKit(
          session, kit->id, 1u);
    }
  }

  const auto* const tl =
      reinterpret_cast<const SpellMissileTargetList*>(target_list);
  const std::uint32_t target_count = (tl != nullptr) ? tl->count : 0u;
  const ObjectGuid* const target_guids =
      (tl != nullptr) ? tl->guids : nullptr;

  if (visual_rec == 0) {
    return;
  }

  const auto* const spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
  const auto* const visual =
      reinterpret_cast<const data::dbc::SpellVisualEntry*>(visual_rec);

  const auto travel_time_ms =
      static_cast<std::int32_t>(travel_time_seconds * 1000.0f);

  if ((spell->attributes_ex4 & kAttrEx4ChannelMissile) != 0) {
    const std::uintptr_t spell_go_info =
        (spell_entry != 0) ? (spell_entry + kSpellEntryGoInfoOffset) : 0;

    (void)spell_go_info;
    (void)travel_time_ms;
    return;
  }

  const std::uintptr_t spell_go_info =
      (spell_entry != 0) ? (spell_entry + kSpellEntryGoInfoOffset) : 0;

  if (caster == 0 || visual->has_missile == 0) {
    return;
  }

  if (spell_go_info == 0) {
    return;
  }
  const std::uint32_t target_flags =
      *reinterpret_cast<const std::uint32_t*>(
          spell_go_info + kGoInfoTargetFlagsOffset);

  const bool is_aoe = (target_flags & kTargetFlagLocationMask) != 0;

  const bool can_target_units =
      !is_aoe ||
      (static_cast<std::uint8_t>(visual->flags) &
       kVisualAllowUnitTargetsInAoE) != 0;

  if (target_count != 0 &&
      (target_flags & kTargetFlagUnitOrGoMask) != 0 &&
      can_target_units) {
    for (std::uint32_t i = 0; i < target_count; ++i) {
      auto* const target_obj = CGObject_HasFlags(
          objects,
          target_guids[i].GetRawValue(), 1u);
      if (target_obj == nullptr) {
        continue;
      }

      (void)target_obj;
      (void)travel_time_ms;
    }
    return;
  }

  if (is_aoe &&
      (target_count != 0 ||
       (visual->flags & kVisualRequireTargetsForAoE) == 0)) {
    (void)travel_time_ms;
  }
}

void InitTargetPointArray(std::uintptr_t array_ptr,
                           std::uintptr_t data,
                           std::uint32_t count,
                           bool zero_fill) {
  auto* arr = reinterpret_cast<SpellCastTargetPointArray*>(array_ptr);
  if (!arr) return;

  if (!zero_fill) {
    arr->SetData(0, nullptr);
    return;
  }

  arr->SetData(count, reinterpret_cast<const void*>(data));
  auto* entries = arr->GetData();
  for (std::uint32_t i = 0; i < count; ++i) {
    auto* entry = reinterpret_cast<float*>(entries + i * 40);
    entry[0] = 0.0f;
    entry[1] = 0.0f;
    entry[2] = 0.0f;
    entry[6] = 0.0f;
    entry[7] = 0.0f;
    entry[8] = 0.0f;
  }
}

void ResizeTargetPointArray(std::uintptr_t array_ptr,
                             std::uint32_t new_count) {
  auto* arr = reinterpret_cast<SpellCastTargetPointArray*>(array_ptr);
  if (!arr) return;

  const std::uint32_t old_count = arr->GetUsedCount();
  if (new_count == old_count) return;

  arr->Resize(new_count);

  if (new_count > old_count) {
    auto* entries = arr->GetData();
    for (std::uint32_t i = old_count; i < new_count; ++i) {
      auto* entry = reinterpret_cast<float*>(entries + i * 40);
      entry[0] = 0.0f;
      entry[1] = 0.0f;
      entry[2] = 0.0f;
      entry[6] = 0.0f;
      entry[7] = 0.0f;
      entry[8] = 0.0f;
    }
  }
}

bool CheckCooldownStartsOnEvent(const WorldSession& session,
                                 const data::dbc::SpellEntry& spell,
                                 ObjectGuid item_guid) {
  constexpr std::uint32_t kSpellAttr0CooldownOnEvent = 0x02000000;
  constexpr std::uint32_t kSpellCategoryFlagCooldownStartsOnEvent = 0x04;

  const auto attr_fallback = [&]() -> bool {
    return (spell.attributes & kSpellAttr0CooldownOnEvent) != 0;
  };

  const auto* item = session.objects().GetItem(item_guid);
  if (item == nullptr) {
    return attr_fallback();
  }

  const ObjectGuid owner_guid = item->GetOwner();
  const auto* owner_unit = session.objects().GetUnit(owner_guid);

  const auto* tmpl = item->GetItemTemplate();
  if (tmpl == nullptr) {
    return attr_fallback();
  }

  int on_use_idx = -1;
  for (int i = 0; i < 5; ++i) {
    if (tmpl->spells[i].spell_id != 0 && tmpl->spells[i].trigger == 0) {
      on_use_idx = i;
      break;
    }
  }

  if (owner_unit != nullptr && on_use_idx >= 0) {
    const auto category_id = tmpl->spells[on_use_idx].category;
    if (category_id != 0) {
      const auto* dbc = session.GetDbcLoader();
      if (dbc != nullptr) {
        const auto* cat = dbc->spell_category().LookupEntry(category_id);
        if (cat != nullptr &&
            (cat->flags & kSpellCategoryFlagCooldownStartsOnEvent) != 0) {
          return true;
        }
      }
    }
  }

  return attr_fallback();
}

int ComputeReagentCastCount(const PlayerInventoryReplica& inventory,
                            const data::dbc::SpellEntry& spell) {
  int min_casts = std::numeric_limits<int>::max();

  for (std::size_t i = 0; i < data::dbc::kMaxSpellReagents; ++i) {
    const auto reagent_id = spell.reagent[i];
    const auto required   = static_cast<int>(spell.reagent_count[i]);

    if (reagent_id == -2 || reagent_id == 0 || required <= 0) {
      continue;
    }

    const auto carried = static_cast<int>(
        inventory.GetItemCount(
            static_cast<std::uint32_t>(reagent_id)));
    const int casts = carried / required;
    if (casts < min_casts) {
      min_casts = casts;
    }
  }

  return (min_casts == std::numeric_limits<int>::max()) ? 0 : min_casts;
}

int GetReagentCastCount(const WorldSession& session,
                        std::uint32_t spell_id) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  return ComputeReagentCastCount(session.inventory_replica(), *spell);
}

bool CursorRequiresUnitTarget(const SpellCastRuntime& spells) {
  constexpr auto kUnitTargetFlags =
      SpellTargetFlag::kUnit | SpellTargetFlag::kUnitRaid |
      SpellTargetFlag::kUnitParty | SpellTargetFlag::kUnitAlly |
      SpellTargetFlag::kUnitDead | SpellTargetFlag::kCorpseAlly;
  const auto target_mask =
      static_cast<SpellTargetFlag>(spells.GetTargeting().GetTargetMask());
  if (!HasFlag(target_mask, kUnitTargetFlags)) {
    return false;
  }

  const auto spell = QueryActiveCursorSpell(spells);
  if (!spell) {
    return false;
  }

  return (spell->attributesEx4 & 0x80000u) == 0;
}

bool CursorSupportsAutoTarget(const SpellCastRuntime& spells) {
  if (!HasActiveCursorSpell(spells)) {
    return false;
  }

  constexpr auto kAutoTargetFlags = SpellTargetFlag::kCorpseEnemy |
                                    SpellTargetFlag::kUnitDead |
                                    SpellTargetFlag::kCorpseAlly;
  const auto target_mask =
      static_cast<SpellTargetFlag>(spells.GetTargeting().GetTargetMask());
  if (HasFlag(target_mask, kAutoTargetFlags)) {
    return true;
  }

  const auto spell = QueryActiveCursorSpell(spells);
  return spell.has_value() && spell->cursorAutoTarget;
}

bool CursorHasAreaTargetFlag(const SpellCastRuntime& spells) {
  if (!HasActiveCursorSpell(spells)) {
    return false;
  }

  const auto spell = QueryActiveCursorSpell(spells);
  return spell.has_value() && spell->cursorAreaTarget;
}

bool ActiveTargetingSpellAllowsTapped(const WorldSession& session,
                                       const SpellCastRuntime& spells) {
  constexpr std::uint32_t kAttrEx6AllowTapped = 0x01000000u;

  const auto spell_id = ResolveActiveTargetingSpellId(spells);
  if (spell_id == 0u) {
    return false;
  }

  const auto *const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto *const spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  return (spell->attributes_ex6 & kAttrEx6AllowTapped) != 0u;
}

}

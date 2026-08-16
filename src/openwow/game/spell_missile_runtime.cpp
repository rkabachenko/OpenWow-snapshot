#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_cost_and_range.h"
#include "openwow/game/spell_missile_runtime.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_runtime_detail.h"

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

bool ValidateSpellTarget(WorldSession& session,
                          std::uintptr_t caster,
                          std::uintptr_t spell_rec,
                          std::uint64_t target_guid,
                          std::uint8_t* out_result,
                          bool show_error,
                          std::uintptr_t extra) {
  if (out_result) *out_result = 0;
  if (spell_rec == 0) return false;
  if (target_guid == 0) return false;

  const auto* spell =
      reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return false;

  const auto* target_obj = session.objects().GetObjectByGUID(
      ObjectGuid(target_guid));
  if (target_obj == nullptr) return false;

  if (!target_obj->IsUnit()) {
    if (out_result) *out_result = 1;
    return true;
  }

  const auto* target_unit = static_cast<const CGUnit_C*>(target_obj);
  const auto* caster_unit =
      caster != 0 ? reinterpret_cast<const CGUnit_C*>(caster) : nullptr;

  const bool is_harmful = GetHelpfulHarmfulDisposition(*spell) ==
                          SpellHelpfulHarmfulDisposition::kHarmful;
  const bool use_friendly_range = !is_harmful;

  float min_range = 0.0f, max_range = 0.0f;
  const auto* range_entry =
      spell->range_index != 0 ? dbc->spell_range().LookupEntry(spell->range_index)
                               : nullptr;
  const auto range = SpellTargetValidator::GetTargetRangeWindow(
      *spell, range_entry,
      caster_unit != nullptr ? *caster_unit : *target_unit,
      *target_unit, use_friendly_range, &session);
  min_range = range.min_range;
  max_range = range.max_range;

  const auto requirements = SpellTargetValidator::BuildRequirements(
      *spell, use_friendly_range, min_range, max_range);

  UnitTargetInfo info;
  info.guid = target_unit->GetGuid();
  info.is_dead = target_unit->State().IsDead();
  info.is_player = target_unit->IsPlayer();
  info.is_self = caster_unit != nullptr &&
                 caster_unit->GetGuid() == target_unit->GetGuid();
  info.is_in_party =
      GroupSystem::Get().IsPartyUnitGuid(
          session.objects(), target_unit->GetGuid().GetRawValue());
  info.is_in_raid =
      GroupSystem::Get().IsRaidUnitGuid(
          session.objects(), target_unit->GetGuid().GetRawValue());
  info.relation = caster_unit != nullptr
      ? (caster_unit->GetGuid() == target_unit->GetGuid()
           ? UnitRelation::kFriendly
           : caster_unit->Interaction().CanAttackSpellTarget(*target_unit)
               ? UnitRelation::kHostile
                : caster_unit->Interaction().CanAssistSpellTarget(*target_unit, false)
                   ? UnitRelation::kFriendly
                   : UnitRelation::kNeutral)
      : UnitRelation::kNeutral;
  info.distance = caster_unit != nullptr
      ? static_cast<float>(std::sqrt(caster_unit->GetSquaredDistanceToPosition(
            target_unit->GetPosition())))
      : 0.0f;
  info.is_immune = false;
  info.creature_type = SpellTargetValidator::GetSpellTargetCreatureTypeId(
      session, *target_unit);

  const auto result = SpellTargetValidator::Validate(requirements, info);
  if (result != SpellTargetResult::kValid) {
    if (out_result) {
      *out_result = static_cast<std::uint8_t>(result);
    }
    if (show_error) {
      HandleCastFailure(session, extra, spell_rec,
                        static_cast<std::uint32_t>(
                            SpellTargetResultToCastResult(result)),
                        0, 0, false);
    }
    return false;
  }

  return true;
}

void CancelPendingSpellCast(WorldSession& session) {
  const auto active_spell = session.spells().GetTargeting().GetSpellId();
  if (active_spell != 0) {
    CancelOrCompleteCast(session, active_spell, true,
                         true, 27);
  }
}

void StopActiveCast(WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  auto& sc = session.spells();
  const auto& current = sc.GetSlot(SpellSlotType::kCurrent);
  if (current.state == SpellClientState::kIdle || current.spell_id == 0) {
    return;
  }

  CancelOrCompleteCast(session, 1, true,
                       true, 27);
}

int HandleSpellDelayedPacket(WorldSession& session,
                              std::uintptr_t , std::uintptr_t ,
                              std::uintptr_t ,
                              std::uintptr_t ) {
  const auto& delayed = session.aura().spell_delayed();
  if (!delayed.has_value()) return 1;

  auto* player = session.objects().GetActivePlayer();
  if (player != nullptr) {
    player->Animation().SetAutoRepeatActive(false);
  }

  auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitSpellcastDelayed(delayed->caster.GetRawValue());

  return 1;
}

int ComputeMissileTrajectory(std::uintptr_t visual_kit,
                              std::uintptr_t caster,
                              std::uintptr_t spell_entry,
                              std::uintptr_t spell_rec,
                              std::uintptr_t target_list,
                              std::uintptr_t target_count_ptr,
                              float* out_pitch,
                              float* out_speed) {

  if (visual_kit == 0 || caster == 0) {
    return 0;
  }

  auto* caster_unit = reinterpret_cast<CGUnit_C*>(caster);
  if (caster_unit == nullptr || caster_unit->object_manager() == nullptr) {
    return 0;
  }
  const auto* const dbc = caster_unit->dbc_loader();
  if (dbc == nullptr) return 0;
  const auto& objects = *caster_unit->object_manager();
  const auto* kit = reinterpret_cast<const data::dbc::SpellVisualKitEntry*>(visual_kit);

  const auto* kit_record = dbc->spell_visual_kit().LookupEntry(kit->id);
  if (kit_record == nullptr) {
    return 0;
  }

  const auto caster_pos = caster_unit->GetPosition();
  float tx = caster_pos.x, ty = caster_pos.y, tz = caster_pos.z;

  if (target_list != 0 && target_count_ptr != 0) {
    const auto* list =
        reinterpret_cast<const SpellMissileTargetList*>(target_list);
    if (list->count > 0 && list->guids != nullptr) {
      const auto* target_obj = objects.GetObjectByGUID(list->guids[0]);
      if (target_obj != nullptr) {
        const auto target_pos = target_obj->GetPosition();
        tx = target_pos.x;
        ty = target_pos.y;
        tz = target_pos.z;
      }
    }
  }

  const float dx = tx - caster_pos.x;
  const float dy = ty - caster_pos.y;
  const float dz = tz - caster_pos.z;

  const float horiz_dist = std::sqrt(dx * dx + dy * dy);
  if (horiz_dist < 0.01f) {
    if (out_pitch) *out_pitch = 0.0f;
    if (out_speed) *out_speed = 0.0f;
    return 1;
  }

  if (out_pitch) {
    *out_pitch = std::atan2(dz, horiz_dist);
  }

  if (out_speed) {
    const float total_dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    constexpr float kDefaultTravelTime = 1.0f;
    const float travel_time = kDefaultTravelTime;
    *out_speed = total_dist / (travel_time + 0.001f);
  }

  (void)spell_entry;
  (void)spell_rec;

  return 1;
}

std::uintptr_t AllocItemCooldownHashNode(std::uintptr_t callback,
                                          std::uint32_t extra_size,
                                          bool zero_fill) {
  const std::uint32_t total_size = 36 + extra_size;
  void* ptr = std::calloc(1, total_size);
  if (ptr == nullptr) {
    return 0;
  }

  reinterpret_cast<std::uintptr_t*>(ptr)[0] = 0;
  auto* node = reinterpret_cast<std::uintptr_t*>(ptr);
  node[1] = callback;

  (void)zero_fill;

  return reinterpret_cast<std::uintptr_t>(ptr);
}

std::uintptr_t AllocDestLocSpellCastHashNode(std::uintptr_t callback,
                                              std::uint32_t extra_size,
                                              bool zero_fill) {
  const std::uint32_t total_size = 40 + extra_size;
  void* ptr = std::calloc(1, total_size);
  if (ptr == nullptr) {
    return 0;
  }

  auto* node = reinterpret_cast<std::uintptr_t*>(ptr);
  node[1] = callback;

  (void)zero_fill;

  return reinterpret_cast<std::uintptr_t>(ptr);
}

bool FillItemCooldownByEntry(WorldSession& session,
                              std::uint32_t entry_id,
                              std::uint32_t* out_duration,
                              std::uint32_t* out_start_time,
                              bool* out_enabled) {
  if (entry_id == 0) {
    return false;
  }

  const auto* tmpl =
      session.query_cache().GetOrRequestItemTemplate(entry_id);
  if (!tmpl) {
    return false;
  }

  const auto slot_idx = FindFirstOnUseSpellIndex(*tmpl);
  if (slot_idx < 0) {
    return false;
  }

  const auto spell_id =
      tmpl->spells[static_cast<std::size_t>(slot_idx)].spell_id;
  if (spell_id == 0) {
    return false;
  }

  auto& tracker = CooldownTracker::Get();
  const CooldownInfo* cd = tracker.GetSpellCooldown(spell_id);
  if (!cd || cd->duration == 0) {
    cd = tracker.GetItemCooldown(entry_id);
  }
  if (!cd || cd->duration == 0) {
    return false;
  }

  if (out_duration) {
    *out_duration = cd->duration;
  }
  if (out_start_time) {
    *out_start_time = cd->start_time;
  }
  if (out_enabled) {
    *out_enabled = true;
  }
  return true;
}

bool FillItemCooldownByInventoryItem(const CGItem_C& item,
                                      std::uint32_t* out_duration,
                                      std::uint32_t* out_start_time,
                                      bool* out_enabled) {
  const auto spell_id = item.ResolveUseSpellId();
  const auto entry_id = item.GetEntry();

  auto& tracker = CooldownTracker::Get();

  const CooldownInfo* cd = nullptr;
  if (spell_id != 0) {
    cd = tracker.GetSpellCooldown(spell_id);
  }
  if (cd == nullptr || cd->duration == 0) {
    cd = tracker.GetItemCooldown(entry_id);
  }
  if (cd == nullptr || cd->duration == 0) {
    return false;
  }

  if (out_duration) {
    *out_duration = cd->duration;
  }
  if (out_start_time) {
    *out_start_time = cd->start_time;
  }
  if (out_enabled) {
    *out_enabled = true;
  }
  return true;
}

void OnItemTemplateCooldownCacheLoaded(WorldSession& session,
                                        std::uint32_t entry_id,
                                        std::uint32_t ,
                                        std::uint32_t ,
                                        bool success) {
  if (!success) {
    return;
  }

  if (FillItemCooldownByEntry(session, entry_id, nullptr, nullptr, nullptr)) {
    auto& dispatch =
        openwow::ui::game::ScriptEventDispatch::Get();
    dispatch.FireActionbarSpellAndShapeshiftCooldownUpdates(false);
    dispatch.FireSpellUpdateCooldown();
  }
}

void Spell_C_CreateMissileVisuals(WorldSession& session,
                                  const std::uint8_t* event_record) {

  if (event_record == nullptr) return;

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return;

  const std::uint32_t spell_id =
      *reinterpret_cast<const std::uint32_t*>(event_record + 12);
  if (spell_id == 0) return;

  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) return;

  const auto caster_guid_raw =
      *reinterpret_cast<const std::uint64_t*>(event_record);

  const float* target_pos =
      reinterpret_cast<const float*>(event_record + 16);

  auto* caster_obj = CGObject_HasFlags(session.objects(), caster_guid_raw,
                                       kTypeMaskUnit);
  if (caster_obj == nullptr) return;

  if (spell->spell_visual[0] != 0) {
    const auto* visual = dbc->spell_visual().LookupEntry(
        static_cast<std::uint32_t>(spell->spell_visual[0]));
    if (visual != nullptr) {
      SpellMissileTargetList empty_list;
      ProcessSpellMissileEffects(
          session,
          reinterpret_cast<std::uintptr_t>(caster_obj),
          0,
          reinterpret_cast<std::uintptr_t>(spell),
          reinterpret_cast<std::uintptr_t>(visual),
          0,
          reinterpret_cast<std::uintptr_t>(&empty_list),
          0.0f);
    }
  }

  (void)target_pos;
}

}

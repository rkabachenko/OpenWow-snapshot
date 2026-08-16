#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_cost_and_range.h"
#include "openwow/game/spell_runtime_detail.h"
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

void ItemCooldownList_Method(std::uintptr_t ) {
}

void DestLocSpellCastList_Method(std::uintptr_t ) {
}

void SpellC_OnWorldEnter() {
  detail::ClearPendingItemSpellHistory();
  RecentCastTracker::GetPlayerTracker().Clear();
  RecentCastTracker::GetPetTracker().Clear();
}

void SpellC_ResetMeleeAttackCastFailureReason() {
  SpellCastDiagnostics::Get().last_cast_failure_reason =
      kMeleeAttackCastFailureReasonResetSentinel;
}

void SpellC_UpdateActiveCasts(SpellCastRuntime& spells,
                              std::uint32_t current_time) {
  (void)spells;

  CooldownTracker::Get().PruneExpiredCooldowns(current_time);

  PendingDynObjVisualList::Get().ExpireEntries(current_time);
}

void SpellC_CleanupExpiredMacros(std::uint32_t current_time) {
  RecentCastTracker::GetPlayerTracker().ExpireEntries(current_time);
  RecentCastTracker::GetPetTracker().ExpireEntries(current_time);
}

void PendingSpellCast_Cleanup(std::uintptr_t ) {
}

void PeriodicClientTrigger_Cleanup(std::uintptr_t list) {
  using Word = std::uintptr_t;
  auto& root =
      *reinterpret_cast<core::StormIntrusiveListRootWords<Word>*>(list);

  for (void* node = core::GetStormIntrusiveFirstNativeNode(root);
       node != nullptr;
       node = core::GetStormIntrusiveFirstNativeNode(root)) {
    auto* link = core::GetStormIntrusiveNodeLinkWords(root, node);
    core::UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(link)));
    std::free(node);
  }
}

void SpellHistory_FreeOwnedNodes(std::uintptr_t list_root_addr) {
  using Word = std::uintptr_t;
  auto& root =
      *reinterpret_cast<core::StormIntrusiveListRootWords<Word>*>(
          list_root_addr);

  for (void* node = core::GetStormIntrusiveFirstNativeNode(root);
       node != nullptr;
       node = core::GetStormIntrusiveFirstNativeNode(root)) {
    auto* link = core::GetStormIntrusiveNodeLinkWords(root, node);
    core::UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(link)));
    std::free(node);
  }
}

void SpellHistory_CleanupBucket(std::uintptr_t bucket_addr) {
  using Word = std::uintptr_t;

  constexpr std::size_t kRootSize =
      sizeof(core::StormIntrusiveListRootWords<Word>);
#if INTPTR_MAX == INT32_MAX
  static_assert(kRootSize == 12);
#endif

  auto& item_cooldown_root =
      *reinterpret_cast<core::StormIntrusiveListRootWords<Word>*>(bucket_addr);
  auto& dest_loc_cast_root =
      *reinterpret_cast<core::StormIntrusiveListRootWords<Word>*>(
          bucket_addr + kRootSize);

  SpellHistory_FreeOwnedNodes(bucket_addr);
  SpellHistory_FreeOwnedNodes(bucket_addr + kRootSize);

  core::UnlinkAllStormIntrusiveNativeNodes(dest_loc_cast_root);

  auto* dest_root_link =
      core::GetStormIntrusiveRootLinkWords(dest_loc_cast_root);
  if (dest_root_link->previous_link != 0) {
    core::UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(dest_root_link)));
  }

  core::UnlinkAllStormIntrusiveNativeNodes(item_cooldown_root);

  auto* item_root_link =
      core::GetStormIntrusiveRootLinkWords(item_cooldown_root);
  if (item_root_link->previous_link != 0) {
    core::UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(item_root_link)));
  }
}

void SpellHistory_Record(const WorldSession& session,
                          std::uintptr_t ,
                          std::uint32_t spell_id,
                          std::uint32_t cast_id,
                          std::uint64_t caster_guid,
                          std::uint32_t duration,
                          std::uint32_t cooldown,
                          std::uint32_t category_cooldown,
                          bool is_channel,
                          std::uint32_t item_id,
                          std::uint32_t flags) {

  if (spell_id == 0) return;

  auto& tracker = CooldownTracker::Get();

  if (cooldown > 0) {
    const std::uint32_t start_time = core::GameClock::GetTickCount32();
    tracker.SetSpellCooldown(spell_id, cooldown, start_time, 0);
  }

  if (category_cooldown > 0) {
    const std::uint32_t start_time = core::GameClock::GetTickCount32();
    const auto* dbc = session.GetDbcLoader();
    if (dbc != nullptr) {
      const auto* spell = dbc->spell().LookupEntry(spell_id);
      if (spell != nullptr && spell->category > 0) {
        tracker.SetCategoryCooldown(spell->category,
                                     category_cooldown, start_time);
      }
    }
  }

  if (duration > 0 && !is_channel) {
    RecentCastTracker::GetPlayerTracker().RecordCast(
        spell_id, cast_id, core::GameClock::GetTickCount32());
  }

  if (item_id != 0 && cooldown > 0) {
    tracker.SetItemCooldown(item_id, cooldown,
                            core::GameClock::GetTickCount32());
  }

  (void)caster_guid;
  (void)flags;
}

void DisplayCastError(WorldSession& session,
                       std::uintptr_t spell_rec,
                       std::uintptr_t spell_entry,
                       std::uint32_t error_code,
                       std::uint32_t extra1,
                       std::uint32_t extra2) {
  HandleCastFailure(
      session,
      spell_entry,
      spell_rec,
      static_cast<std::uint32_t>(error_code),
      static_cast<std::int32_t>(extra1),
      static_cast<std::int32_t>(extra2),
      false);
}

bool IsCurrentItemEntry(const WorldSession& session,
                         std::uint32_t item_entry_id) {
  if (item_entry_id == 0) {
    return false;
  }

  static constexpr std::array<SpellSlotType, 3> kSlots = {
      SpellSlotType::kCurrent,
      SpellSlotType::kAutoRepeat,
      SpellSlotType::kChannel,
  };

  const auto& spell_client = session.spells();
  const auto& objects = session.objects();
  for (const auto slot_type : kSlots) {
    const auto& slot = spell_client.GetSlot(slot_type);
    if (slot.state == SpellClientState::kIdle || slot.target_guid.IsEmpty()) {
      continue;
    }

    const auto* item = objects.GetItem(slot.target_guid);
    if (item != nullptr && item->GetEntry() == item_entry_id) {
      return true;
    }
  }

  return false;
}

bool IsSpellRecordCurrentForUnit(const openwow::data::dbc::SpellEntry& spell,
                                 const openwow::data::dbc::DbcLoader& dbc_loader,
                                 const CGUnit_C* unit) {
  if (unit == nullptr) {
    return false;
  }

  if (spell.active_icon_id == 0 && !IsCompanionSpellRecord(spell, dbc_loader)) {
    return false;
  }

  if (HasMatchingVehicleOverrideState(spell, *unit)) {
    return true;
  }

  if (HasActiveMatchingAura(spell, *unit)) {
    return true;
  }

  return HasEquivalentMountedAura(spell, dbc_loader, *unit);
}

bool IsCurrentSpellId(const WorldSession& session, std::uint32_t spell_id) {
  if (spell_id == 0) {
    return false;
  }

  const auto* dbc_loader = session.GetDbcLoader();
  if (dbc_loader == nullptr) {
    return false;
  }

  const auto* spell = dbc_loader->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto* player = session.objects().GetActivePlayer();
  if (spell->effect[0] == kSpellEffectAttack) {
    return player != nullptr && !player->State().GetTarget().IsEmpty();
  }

  if (spell->effect[0] == kSpellEffectTradeSkill) {
    if (player == nullptr || ProfessionSystem::Get().IsTradeSkillLinked()) {
      return false;
    }

    const auto resolved_skill_line = ResolveSpellSkillLineId(
        session.objects(), dbc_loader, player->State().GetRace(), player->State().GetClass(),
        spell_id);
    return resolved_skill_line.has_value() &&
           *resolved_skill_line == ProfessionSystem::Get().GetOpenSkillLine();
  }

  return IsSpellRecordCurrentForUnit(*spell, *dbc_loader, player);
}

void CancelOrCompleteCast(WorldSession& session,
                           std::uintptr_t spell_entry,
                           bool update_ui,
                           bool send_cancel,
                           std::uint32_t reason) {
  if (spell_entry == 0) {
    return;
  }

  auto& spell_client = session.spells();
  auto& targeting = spell_client.GetTargeting();
  const auto targeting_state = targeting.GetState();
  const bool is_targeting_spell =
      targeting_state.isActive && spell_entry == targeting_state.spellId;

  static constexpr std::array<SpellSlotType, 3> kSlots = {
      SpellSlotType::kCurrent,
      SpellSlotType::kAutoRepeat,
      SpellSlotType::kChannel,
  };

  std::uint32_t spell_id = 0;
  std::uint64_t caster_guid_raw = 0;
  bool slot_found = false;

  if (!is_targeting_spell) {
    for (const auto slot_type : kSlots) {
      const auto& slot = spell_client.GetSlot(slot_type);
      if (slot.state == SpellClientState::kIdle) {
        continue;
      }

      if (!slot_found) {
        slot_found = true;
        spell_id = slot.spell_id;
        caster_guid_raw = slot.caster_guid.GetRawValue();
      }

      if (send_cancel && slot.spell_id != 0) {
        spell_client.CancelSpell(session, slot_type);
      }
    }
  }

  if (!slot_found && !is_targeting_spell) {
    return;
  }

  if (is_targeting_spell) {
    targeting.CancelTargeting();
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return;
  }

  const data::dbc::SpellEntry* spell = nullptr;
  if (spell_id != 0) {
    spell = dbc->spell().LookupEntry(spell_id);
  }
  if (spell == nullptr && !is_targeting_spell) {
    return;
  }

  auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();

  if (caster_guid_raw != 0) {
    switch (reason) {
      case 187:
        dispatch.FireUnitSpellcastSent(caster_guid_raw);
        break;
      case 27:
      case 105:
      case 24:
        dispatch.FireUnitSpellcastInterrupted(caster_guid_raw);
        break;
      case 40:
      case 41:
        dispatch.FireUnitSpellcastDelayed(caster_guid_raw);
        break;
      default:
        dispatch.FireUnitSpellcastStop(caster_guid_raw);
        break;
    }
  }

  if (update_ui) {
    if (ProfessionSystem::Get().ClearAllTradeSkillSpells()) {
      ui::game::ScriptEventDispatch::Get().FireEvent(
          ui::game::events::TRADE_SKILL_UPDATE);
    }
  }
}

void CancelPendingCastsByGuid(WorldSession& session,
                              const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  auto& spell_client = session.spells();

  static constexpr std::array<SpellSlotType, 3> kSlots = {
      SpellSlotType::kCurrent,
      SpellSlotType::kAutoRepeat,
      SpellSlotType::kChannel,
  };

  for (const auto slot_type : kSlots) {
    const auto& slot = spell_client.GetSlot(slot_type);
    if (slot.state == SpellClientState::kIdle) {
      continue;
    }
    if (slot.caster_guid.GetRawValue() == guid) {
      spell_client.CancelSpell(session, slot_type);
    }
  }
}

void CancelPendingCastsForActivePlayer(WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }
  CancelPendingCastsByGuid(session, player->GetGuid().GetRawValue());
}

int HandleSpellFailurePacket(WorldSession& session,
                              std::uintptr_t , std::uintptr_t ,
                              std::uintptr_t ,
                              std::uintptr_t ) {
  const auto& failure = session.aura().spell_failure();
  if (!failure.has_value()) return 1;

  HandleCastFailure(
      session,
      reinterpret_cast<std::uintptr_t>(nullptr),
      reinterpret_cast<std::uintptr_t>(nullptr),
      static_cast<std::uint32_t>(failure->result),
      0,
      0,
      false);

  return 1;
}

int HandleCooldownEventPacket(std::uintptr_t , std::uintptr_t ,
                               std::uintptr_t ,
                               std::uintptr_t ) {
  return 1;
}

}

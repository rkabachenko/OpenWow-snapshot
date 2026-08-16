#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_channel_runtime.h"
#include "openwow/game/spell_runtime_values.h"
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
namespace {

constexpr std::uint32_t kDelayAttrOnNextSwing = 0x00000002u;
constexpr std::uint32_t kDelayAttrPassiveOrHidden = 0x00000180u;
constexpr std::uint32_t kDelayAttrEx4NoCastLog = 0x00000001u;
constexpr std::uint32_t kDelayAttrEx4AlwaysProcess = 0x08000000u;

}

bool ShouldProcessSpellDelay(const data::dbc::SpellEntry& spell,
                             std::uint64_t caster_guid_raw) {
  if ((spell.attributes_ex4 & kDelayAttrEx4AlwaysProcess) != 0)
    return true;
  if ((spell.attributes & kDelayAttrOnNextSwing) != 0)
    return false;
  if (caster_guid_raw == CGObject_C::GetActivePlayerGuid().GetRawValue())
    return true;
  if ((spell.attributes & kDelayAttrPassiveOrHidden) != 0)
    return false;
  if ((spell.attributes_ex4 & kDelayAttrEx4NoCastLog) != 0)
    return false;
  return true;
}

int HandleSpellDelayPacket(WorldSession& session,
                            std::uintptr_t , std::uintptr_t ,
                            std::uintptr_t ,
                            std::uintptr_t ) {
  const auto& delayed = session.aura().spell_delayed();
  if (!delayed.has_value()) return 1;

  const auto caster_guid_raw = delayed->caster.GetRawValue();
  const auto delay_ms = static_cast<std::int32_t>(delayed->delay_ms);

  auto* obj = CGObject_HasFlags(session.objects(), caster_guid_raw,
                                kTypeMaskUnit);
  if (obj == nullptr) return 1;
  auto* unit = static_cast<CGUnit_C*>(obj);

  const auto channel_spell_id = unit->Casts().GetChannelSpellId(*unit);
  if (channel_spell_id == 0) return 1;

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) return 1;

  const auto* spell = dbc->spell().LookupEntry(channel_spell_id);
  if (spell == nullptr) return 1;

  if (!ShouldProcessSpellDelay(*spell, caster_guid_raw)) return 1;

  unit->Casts().ApplyChannelPushback(delay_ms, 0u);

  auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitSpellcastDelayed(caster_guid_raw);

  return 1;
}

int HandleChannelStartPacket(const WorldSession& session,
                              std::uintptr_t , std::uintptr_t ,
                              std::uintptr_t ,
                              std::uintptr_t ) {
  auto& client = session.spells();
  const auto& channel = client.GetSlot(SpellSlotType::kChannel);
  if (channel.state == SpellClientState::kIdle) {
  }
  (void)session;
  return 1;
}

constexpr std::uint32_t kChannelAttrOnNextSwing          = 0x00000002u;
constexpr std::uint32_t kChannelAttrPassiveOrHidden      = 0x00000180u;
constexpr std::uint32_t kChannelAttrEx3HideChannelBar    = 0x00002000u;
constexpr std::uint32_t kChannelAttrEx4NoCastLog         = 0x00000001u;
constexpr std::uint32_t kChannelAttrEx4AlwaysProcess     = 0x08000000u;

bool ShouldProcessChannelUpdate(const data::dbc::SpellEntry& spell,
                                std::uint64_t caster_guid_raw) {
  if ((spell.attributes_ex4 & kChannelAttrEx4AlwaysProcess) != 0) {
    return true;
  }
  if ((spell.attributes & kChannelAttrOnNextSwing) != 0) {
    return false;
  }
  if (caster_guid_raw == CGObject_C::GetActivePlayerGuid().GetRawValue()) {
    return true;
  }
  if ((spell.attributes & kChannelAttrPassiveOrHidden) != 0) {
    return false;
  }
  if ((spell.attributes_ex3 & kChannelAttrEx3HideChannelBar) != 0) {
    return false;
  }
  if ((spell.attributes_ex4 & kChannelAttrEx4NoCastLog) != 0) {
    return false;
  }
  return true;
}

ChannelUpdateTransition PrepareChannelUpdate(
    CGUnit_C& unit, const data::dbc::SpellEntry& spell,
    const std::uint64_t caster_guid, const std::int32_t time_remaining,
    const std::uint32_t current_time) {
  if (!ShouldProcessChannelUpdate(spell, caster_guid)) {
    return ChannelUpdateTransition::kIgnored;
  }

  unit.Casts().ResetChannelBase(time_remaining, current_time);
  if (time_remaining != 0) {
    return ChannelUpdateTransition::kUpdated;
  }

  unit.Casts().ResetChannelTiming(current_time);
  return ChannelUpdateTransition::kStopped;
}

void CompleteChannelUpdate(WorldSession& session, CGUnit_C& unit,
                           const std::uint32_t spell_id,
                           const ChannelUpdateTransition transition) {
  if (transition == ChannelUpdateTransition::kStopped) {
    unit.SpellVisuals().TeardownChannelEffectsIfAuraMissing(session, spell_id);
  }
}

int HandleChannelUpdatePacket(WorldSession& session,
                               std::uintptr_t , std::uintptr_t ,
                               std::uintptr_t ,
                               std::uintptr_t ) {

  const auto& channel = session.aura().channel_info();
  if (!channel.has_value()) {
    return 1;
  }

  const auto caster_guid_raw = channel->caster.GetRawValue();
  const auto time_remaining = static_cast<std::uint32_t>(channel->time_remaining);

  auto* obj = CGObject_HasFlags(session.objects(), caster_guid_raw,
                                kTypeMaskUnit);
  if (obj == nullptr) {
    return 1;
  }
  auto* unit = static_cast<CGUnit_C*>(obj);

  const auto channel_spell_id = unit->Casts().GetChannelSpellId(*unit);
  if (channel_spell_id == 0) {
    return 1;
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 1;
  }

  const auto* spell = dbc->spell().LookupEntry(channel_spell_id);
  if (spell == nullptr) {
    return 1;
  }

  const auto transition = PrepareChannelUpdate(
      *unit, *spell, caster_guid_raw,
      static_cast<std::int32_t>(time_remaining),
      core::GameClock::GetTickCount32());
  if (transition == ChannelUpdateTransition::kIgnored) {
    return 1;
  }

  auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
  if (transition == ChannelUpdateTransition::kUpdated) {
    dispatch.FireUnitSpellcastChannelUpdate(caster_guid_raw);
  } else {
    dispatch.FireUnitSpellcastChannelStop(caster_guid_raw);
  }
  CompleteChannelUpdate(session, *unit, channel_spell_id, transition);

  return 1;
}

}

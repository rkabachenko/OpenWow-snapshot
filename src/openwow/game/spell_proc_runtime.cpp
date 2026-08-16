#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/spell_proc_runtime.h"
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
#include "openwow/game/spell_cast_runtime.h"
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
void SpellC_OnCastComplete(WorldSession& session,
                            std::uintptr_t spell_entry,
                            std::uintptr_t spell_rec) {

  if (spell_rec == 0 || spell_entry == 0) {
    return;
  }

  const auto* entry = reinterpret_cast<const data::dbc::SpellEntry*>(spell_rec);
  if (entry == nullptr) {
    return;
  }

  auto& obj_mgr = session.objects();
  auto* caster_unit = obj_mgr.GetActivePlayer();
  if (caster_unit != nullptr && entry->proc_flags != 0) {
    const ObjectGuid target = caster_unit->State().GetTarget();
    caster_unit->Casts().OnCastProc(*caster_unit, entry->id,
                                    entry->school_mask, target.GetRawValue());

    if ((entry->targets & 0x80u) != 0) {
      auto* target_unit = obj_mgr.GetMutableUnit(target);
      if (target_unit != nullptr) {
        target_unit->Casts().OnSpellHitProc(
            *target_unit, entry->id, entry->school_mask, target.GetRawValue(),
            false);
      }
    }
  }

  bool has_aura_effect = false;
  for (std::size_t i = 0; i < entry->effect_apply_aura.size(); ++i) {
    if (entry->effect_apply_aura[i] != 0) {
      has_aura_effect = true;
      break;
    }
  }

  if (has_aura_effect && caster_unit != nullptr) {
    const auto* const dbc = caster_unit->dbc_loader();
    if (dbc != nullptr) {
      AuraApplicationRequest request;
      request.spell_id = entry->id;
      request.caster_guid = caster_unit->GetGuid();
      request.duration = AuraApplication::GetSpellDuration(
          *entry, *dbc, caster_unit->State().GetLevel());
      request.remaining = request.duration;

      if (entry->stack_amount > 0) {
        request.stack_count = 1;
      }

      for (std::size_t i = 0; i < entry->effect_base_points.size(); ++i) {
        request.effect_amounts[i] = entry->effect_base_points[i];
      }

      if ((entry->targets & 0x80u) != 0) {
        request.is_harmful = true;
      }

      AuraApplication::Get().ApplyAura(
          session, *caster_unit, *entry, request, *dbc);
    }
  }

  (void)spell_entry;
}

}

#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"

#include "openwow/game/world_session.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/console.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/achievements/adapters/protocol/achievement_protocol.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/calendar/adapters/protocol/calendar_date_fields_packed.h"
#include "openwow/game/calendar/calendar_time.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/combat/application/client_control_transition.h"
#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/activities/dance/adapters/protocol/dance_protocol.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/emote_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/faction_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/inventory/loot/adapters/ui/loot_roll_result_presenter.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/minimap_ping.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/reputation_info.h"

#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/taxi_map_frame.h"
#include "openwow/game/taxi_runtime_slice.h"
#include "openwow/game/taxi_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/game/combat/adapters/ui/combo_point_presentation.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/quest_log_interleaved.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/title_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/core/init_subsystems.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

const data::dbc::FactionEntry* FindFactionEntryById(
    const data::dbc::DbcLoader* dbc, const std::uint32_t faction_id) {
  if (dbc == nullptr) return nullptr;
  for (const auto& faction : dbc->faction().entries()) {
    if (faction.id == faction_id) return &faction;
  }
  return nullptr;
}

}

void PrimeReputationInfo(ReputationInfo& reputation,
                         const data::dbc::DbcLoader* dbc,
                         const ObjectManager& objects) {
  reputation.BindDbc(dbc);
  if (const auto* player = objects.GetActivePlayer(); player != nullptr) {
    reputation.SetPlayerIdentity(player->State().GetRace(),
                                 player->State().GetClass(),
                                 player->State().GetGender());
  } else {
    reputation.ClearPlayerIdentity();
  }
}

void WorldSession::HandleInitializeFactions(const net::wotlk::WorldPacket &pkt) {
  auto &reputation_info = ReputationInfo::Get();
  PrimeReputationInfo(reputation_info, dbc_, map_runtime_.objects());
  if (!reputation_runtime_.Apply(
          ReputationRuntime::Mutation::kInitialize, reputation_info, objects(),
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  SyncFactionsToFactionSystem();
}

void WorldSession::HandleSetFactionStanding(const net::wotlk::WorldPacket &pkt) {
  auto &reputation_info = ReputationInfo::Get();
  PrimeReputationInfo(reputation_info, dbc_, map_runtime_.objects());
  if (!reputation_runtime_.Apply(
          ReputationRuntime::Mutation::kStanding, reputation_info, objects(),
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  SyncFactionsToFactionSystem();
  if (reputation_info.did_last_standing_change_faction_state()) {
    RefreshActivePlayerFactionDependentState();
  }
}

void WorldSession::HandleSetFactionVisible(const net::wotlk::WorldPacket &pkt) {
  auto &reputation_info = ReputationInfo::Get();
  PrimeReputationInfo(reputation_info, dbc_, map_runtime_.objects());
  if (!reputation_runtime_.Apply(
          ReputationRuntime::Mutation::kVisible, reputation_info, objects(),
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  SyncFactionsToFactionSystem();
}

void WorldSession::SyncFactionsToFactionSystem() {
  std::vector<FactionInfo> infos;

  for (std::uint32_t slot = 0; slot < kMaxFactionSlots; ++slot) {
    const auto &raw = reputation_runtime_.factions().GetFaction(slot);

    if (raw.flags == 0 && raw.standing == 0)
      continue;

    const data::dbc::FactionEntry *dbc_entry = nullptr;
    if (dbc_) {
      for (const auto &fac : dbc_->faction().entries()) {
        if (fac.reputation_list_id == static_cast<std::int32_t>(slot)) {
          dbc_entry = &fac;
          break;
        }
      }
    }

    FactionInfo info;
    info.faction_id = dbc_entry ? dbc_entry->id : slot;
    info.name = dbc_entry ? std::string(dbc_entry->name) : "Faction " + std::to_string(slot);
    info.description = dbc_entry ? std::string(dbc_entry->description) : "";
    info.reputation = raw.standing;
    info.flags = raw.flags;

    info.standing = FactionSystem::StandingFromRep(raw.standing);
    info.bar_min = FactionManager::GetBarMin(FactionManager::GetRankFromStanding(raw.standing));
    info.bar_max = FactionManager::GetBarMax(FactionManager::GetRankFromStanding(raw.standing));

    constexpr std::uint8_t kFlagVisible = 0x01;
    constexpr std::uint8_t kFlagAtWar = 0x02;
    constexpr std::uint8_t kFlagForced = 0x10;
    constexpr std::uint8_t kFlagInactive = 0x20;
    info.at_war = (raw.flags & kFlagAtWar) != 0;
    info.can_toggle_at_war = raw.standing >= -3000 && (raw.flags & kFlagForced) == 0;
    info.is_inactive = (raw.flags & kFlagInactive) != 0;
    info.is_watched = (reputation_runtime_.factions().GetWatchedFaction() == slot);
    if (dbc_entry && dbc_entry->parent_faction_id != 0) {
      if (const auto *parent_entry = FindFactionEntryById(dbc_, dbc_entry->parent_faction_id)) {
        info.is_child = parent_entry->parent_faction_id != 0;
      }
    }

    if (raw.flags & kFlagVisible) {
      infos.push_back(std::move(info));
    }
  }

  FactionSystem::Get().SetFactions(infos);
}

void WorldSession::HandleSetFactionAtWar(const net::wotlk::WorldPacket &pkt) {
  auto &reputation_info = ReputationInfo::Get();
  PrimeReputationInfo(reputation_info, dbc_, map_runtime_.objects());
  if (!reputation_runtime_.Apply(
          ReputationRuntime::Mutation::kAtWar, reputation_info, objects(),
          pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  SyncFactionsToFactionSystem();
  RefreshActivePlayerFactionDependentState();
}

}

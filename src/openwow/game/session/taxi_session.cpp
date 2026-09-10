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
#include <bit>
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

constexpr int kNewTaxiPathSystemMessageIndex = 0x101;

std::optional<TaxiSliceState> BuildTaxiMapOpenRuntimeSlice(const data::dbc::DbcLoader &dbc,
                                                           const WorldSession &session) {
  const auto state = BuildTaxiRuntimeSliceState(dbc, session);
  if (state.nodes.empty() || state.texture_path.empty()) {
    return std::nullopt;
  }
  return state;
}

void LogRetailTaxiNodeConsoleDump(const TaxiNodeDisplay& display,
                                  const data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) {
    return;
  }

  for (std::size_t word = 0; word < display.mask.size(); ++word) {
    std::uint32_t bits = display.mask[word];
    while (bits != 0) {
      const auto bit_index = static_cast<std::uint32_t>(std::countr_zero(bits));
      const auto node_id = static_cast<std::uint32_t>(word * 32) + bit_index + 1u;
      if (const auto* node = dbc->taxi_nodes().LookupEntry(node_id);
          node != nullptr) {

        core::ida::ConsoleLog("[%02d]: %.*s", node->id,
                              static_cast<int>(node->name.size()),
                              node->name.data());
      }
      bits &= bits - 1u;
    }
  }
}

}

void WorldSession::HandleShowTaxiNodes(const net::wotlk::WorldPacket &pkt) {

  if (objects().GetLocalPlayerTyped() == nullptr) {
    return;
  }

  auto incoming_taxi = taxi_;
  const auto previous_taxi_guid = taxi_.GetFlightMasterGuid();
  auto &taxi_system = TaxiSystem::Get();
  const bool taxi_map_was_open = taxi_system.IsTaxiMapOpen();

  auto result = incoming_taxi.HandleShowTaxiNodes(pkt.payload.data(), pkt.payload.size());
  switch (result) {
  case TaxiShowResult::kOpenMap: {
    if (taxi_map_was_open && previous_taxi_guid != 0 &&
        previous_taxi_guid == incoming_taxi.GetFlightMasterGuid()) {
      break;
    }

    if (taxi_map_was_open && previous_taxi_guid != 0 &&
        previous_taxi_guid != incoming_taxi.GetFlightMasterGuid()) {
      TaxiMapFrame_Close(*this);
    }

    taxi_ = std::move(incoming_taxi);
    if (const auto *dbc = GetDbcLoader()) {
      if (const auto state = BuildTaxiMapOpenRuntimeSlice(*dbc, *this);
          state.has_value()) {
        auto interaction_guid = taxi_.GetFlightMasterGuid();
        ui::game::SetNpcInteractionTarget(ObjectGuid(interaction_guid));
        taxi_system.OpenTaxiMap(taxi_.GetCurrentNode());
        taxi_system.CacheDisplaySlice(*state);
        openwow::diagnostics::Log(
            openwow::diagnostics::LogLevel::kInfo,
            "interaction publish SMSG_SHOWTAXINODES guid=" +
                std::to_string(interaction_guid) + " currentNode=" +
                std::to_string(taxi_.GetCurrentNode()) + " nodes=" +
                std::to_string(state->nodes.size()) +
                " stage=TAXIMAP_OPENED");
        ui::game::ScriptEventDispatch::Get().FireTaxiMapOpened();
        TutorialSystem::Instance().TriggerTutorial(0x22u);
        break;
      }
    }
    const auto failed_guid = taxi_.GetFlightMasterGuid();
    const auto failed_current_node = taxi_.GetCurrentNode();
    const auto failed_reachable_count = taxi_.GetReachableNodeCount();
    taxi_system.ResetRouteDisplayState();
    taxi_system.CloseTaxiMap();
    taxi_.CloseTaxiMap();
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "taxi map publication failed guid=" +
            std::to_string(failed_guid) +
            " currentNode=" + std::to_string(failed_current_node) +
            " reachable=" + std::to_string(failed_reachable_count) +
            " stage=runtime-slice");
    break;
  }
  case TaxiShowResult::kErrorNoPath:
    ui::game::DisplaySystemMessage(196);

    ui::game::CloseGossipInteraction(*this);

    break;
  case TaxiShowResult::kConsoleDump:
    LogRetailTaxiNodeConsoleDump(incoming_taxi.last_display(), GetDbcLoader());
    break;
  case TaxiShowResult::kParseError:
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "interaction reject malformed SMSG_SHOWTAXINODES bytes=" +
            std::to_string(pkt.payload.size()) + " stage=taxi-snapshot");
    break;
  }
}

void WorldSession::HandleActivateTaxiReply(const net::wotlk::WorldPacket &pkt) {

  if (objects().GetLocalPlayerTyped() == nullptr) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "taxi activation reply ignored opcode=SMSG_ACTIVATETAXIREPLY "
        "stage=taxi-reply reason=missing-player bytes=" +
            std::to_string(pkt.payload.size()));
    return;
  }

  if (!taxi_.HandleActivateTaxiReply(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "interaction reject malformed SMSG_ACTIVATETAXIREPLY bytes=" +
            std::to_string(pkt.payload.size()) + " stage=taxi-reply");
    return;
  }

  const auto reply = static_cast<std::uint32_t>(taxi_.last_reply());
  openwow::diagnostics::Log(
      reply < kTaxiReplySystemMessagesCount
          ? openwow::diagnostics::LogLevel::kInfo
          : openwow::diagnostics::LogLevel::kWarn,
      "taxi activation reply opcode=SMSG_ACTIVATETAXIREPLY stage=taxi-reply guid=" +
          std::to_string(taxi_.GetFlightMasterGuid()) +
          " code=" + std::to_string(reply) +
          " result=" + TaxiHandler::TaxiReplyToString(taxi_.last_reply()));

  if (reply >= kTaxiReplySystemMessagesCount)
    return;

  if (reply == 0) {
    TaxiMapFrame_Close(*this);
  } else {
    ui::game::DisplaySystemMessage(kTaxiReplySystemMessages[reply]);
  }
}

void WorldSession::HandleNewTaxiPath(const net::wotlk::WorldPacket &pkt) {
  taxi_.HandleNewTaxiPath(pkt.payload.data(), pkt.payload.size());

  ui::game::DisplaySystemMessage(kNewTaxiPathSystemMessageIndex);

  ui::game::CloseGossipInteraction(*this);
}

void WorldSession::HandleTaxiNodeStatus(const net::wotlk::WorldPacket &pkt) {
  if (!taxi_.HandleTaxiNodeStatus(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "interaction reject malformed SMSG_TAXINODE_STATUS bytes=" +
            std::to_string(pkt.payload.size()) + " stage=taxi-node-status");
    return;
  }

  const auto &status = taxi_.last_status();
  auto *unit = objects().GetMutableUnit(ObjectGuid(status.npc_guid));
  constexpr std::uint32_t kNpcFlagFlightmaster = 0x00002000;
  if (unit == nullptr || (unit->State().GetNpcFlags() & kNpcFlagFlightmaster) == 0) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Taxi node status stage=resolve source=SMSG_TAXINODE_STATUS guid=" +
            std::to_string(status.npc_guid) +
            " status=" + std::to_string(status.status) +
            " reason=" + (unit == nullptr ? "unit-unavailable" : "flightmaster-role-missing"));
    return;
  }

  unit->SetOverlayModelIndexOverride(status.status == 0 ? kOverlayModelIndexTaxiEnable : 0);
  unit->UpdateOverlayModel();
}

}

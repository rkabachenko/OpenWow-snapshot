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
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_visual_system.h"
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

std::string LookupTransferAbortGlobalString(std::string_view key) {
  if (key.empty()) {
    return {};
  }
  return Localization::Get().GetString(std::string(key));
}
std::string BuildTransferAbortTemplate(const TransferAbortedInfo &aborted,
                                       const data::dbc::DbcLoader *dbc) {
  switch (aborted.reason) {
  case TransferAbortReason::kError:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_ERROR");
  case TransferAbortReason::kMaxPlayers:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_MAX_PLAYERS");
  case TransferAbortReason::kNotFound:
  case TransferAbortReason::kNotFound12:
  case TransferAbortReason::kNotFound13:
  case TransferAbortReason::kNotFound14:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_NOT_FOUND");
  case TransferAbortReason::kTooManyInstances:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_TOO_MANY_INSTANCES");
  case TransferAbortReason::kZoneInCombat:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_ZONE_IN_COMBAT");
  case TransferAbortReason::kInsufExpanLvl:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_INSUF_EXPAN_LVL" +
                                           std::to_string(aborted.arg));
  case TransferAbortReason::kDifficulty: {
    const auto *difficulty_entry =
        data::DBClient_FindMapDifficulty(dbc, aborted.map_id, aborted.arg);
    if (difficulty_entry != nullptr && !difficulty_entry->message.empty()) {
      return LookupTransferAbortGlobalString(difficulty_entry->message);
    }
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_DIFFICULTY" +
                                           std::to_string(aborted.arg + 1));
  }
  case TransferAbortReason::kUniqueMessage:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_UNIQUE_MESSAGE" +
                                           std::to_string(aborted.arg));
  case TransferAbortReason::kTooManyRealmInstances:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_TOO_MANY_REALM_INSTANCES");
  case TransferAbortReason::kNeedGroup:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_NEED_GROUP");
  case TransferAbortReason::kRealmOnly:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_REALM_ONLY");
  case TransferAbortReason::kMapNotAllowed:
    return LookupTransferAbortGlobalString("TRANSFER_ABORT_MAP_NOT_ALLOWED");
  case TransferAbortReason::kNone:
    return {};
  }

  return {};
}

std::optional<std::string> BuildTransferAbortMessage(const TransferAbortedInfo &aborted,
                                                     const data::dbc::MapEntry *map_entry,
                                                     const data::dbc::DbcLoader *dbc) {
  if (map_entry == nullptr || map_entry->name.empty()) {
    return std::nullopt;
  }

  const std::string template_text = BuildTransferAbortTemplate(aborted, dbc);
  if (template_text.empty()) {
    return std::nullopt;
  }

  const std::string formatted =
      Localization::Get().FormatString(template_text, {std::string(map_entry->name)});
  if (formatted.empty()) {
    return std::nullopt;
  }

  return formatted;
}

}

const data::dbc::MapEntry* WorldSession::LookupMapEntry(
    const std::uint32_t map_id) const {
  return dbc_ != nullptr ? dbc_->map().LookupEntry(map_id) : nullptr;
}

void WorldSession::UpdateResetInstanceVisibilityForMapTransition(
    const std::uint32_t previous_map_id, const std::uint32_t new_map_id) {
  if (previous_map_id == new_map_id) {
    return;
  }

  const auto *map_entry = LookupMapEntry(previous_map_id);
  if (map_entry == nullptr || (map_entry->map_type != 1 && map_entry->map_type != 2)) {
    return;
  }

  const auto difficulty = static_cast<std::uint8_t>(GroupSystem::Get().GetDungeonDifficulty());
  if (HasMapDifficultyRaidDuration(previous_map_id, difficulty)) {
    return;
  }

  instance_.SetResetInstanceVisibilityAnchor(previous_map_id,
                                             static_cast<std::uint32_t>(std::time(nullptr)));
}

bool WorldSession::ResolveWorldTransferMap(const std::uint32_t map_id,
                                           std::string *const map_internal_name) const {
  if (const auto *entry = LookupMapEntry(map_id)) {
    if (map_internal_name != nullptr) {
      *map_internal_name = entry->internal_name;
    }
    return true;
  }

  if (map_internal_name != nullptr) {
    map_internal_name->clear();
  }
  return dbc_ == nullptr;
}

void WorldSession::ResetRuntimeForWorldTransfer() {

  SuspendIncomingChatDelivery();

  map_runtime_.objects().Reset();

  SpellVisuals_CleanAll();

  misc_.SetWeatherSoundKitId(kNoWeatherSoundKitId);
  sound_runtime_.SetWeatherSoundKit(kNoWeatherSoundKitId);

  petition_.ClearGuildRegistrarInteraction();
  petition_.ClearPetitionVendorInteraction();
  petition_.ClearTabardVendorInteraction();
  monster_move_.Clear();
  movement_spline_mgr_.Clear();
  movement_ext_.Clear();
  movement_.ResetTransientState();
  ResetMovementCollisionSolver();
  SpellC_OnWorldEnter();
}

void WorldSession::DispatchWorldTransfer(const WorldTransferRequest &request) {
  if (has_current_map_) {
    UpdateResetInstanceVisibilityForMapTransition(current_map_id_, request.map_id);
  }

  float bootstrap_x = request.x;
  float bootstrap_y = request.y;
  float bootstrap_z = request.z;
  float bootstrap_o = request.orientation;
  if (request.is_transport_relative) {
    if (const auto *const local_player = map_runtime_.objects().GetLocalPlayerTyped();
        local_player != nullptr) {
      bootstrap_x = local_player->GetX();
      bootstrap_y = local_player->GetY();
      bootstrap_z = local_player->GetZ();
      bootstrap_o = local_player->GetOrientation();
    }
  }

  ResetRuntimeForWorldTransfer();

  if (map_generation_replacement_callback_) {
    map_generation_replacement_callback_(request.map_id);
  }

  login_verify_.map_id = request.map_id;
  login_verify_.x = bootstrap_x;
  login_verify_.y = bootstrap_y;
  login_verify_.z = bootstrap_z;
  login_verify_.orientation = bootstrap_o;
  current_map_id_ = request.map_id;
  has_current_map_ = true;
  map_runtime_.objects().SetMapId(request.map_id);

  if (enter_world_transition_callback_) {
    enter_world_transition_callback_(request.map_id, bootstrap_x, bootstrap_y, bootstrap_z,
                                     bootstrap_o, request.map_internal_name);
  }

  movement_.ApplyTeleport(bootstrap_x, bootstrap_y, bootstrap_z, bootstrap_o);
  if (world_camera_ != nullptr) {
    world_camera_->SetTarget(bootstrap_x, bootstrap_y, bootstrap_z);

    world_camera_->SetReferenceFacing(bootstrap_o);
    world_camera_->SetPitch(0.0f);
  }

  if (request.send_worldport_ack) {

    world_transition_.ArmWorldportAck();
  }

  state_ = request.state_after_dispatch;
}

bool WorldSession::TrySendPendingWorldportAck() {
  if (!world_transition_.HasPendingWorldportAck()) {
    return false;
  }

  world_transition_.ClearWorldportAck();
  net::wotlk::WorldPacket ack(net::wotlk::Opcode::MSG_MOVE_WORLDPORT_ACK);
  Send(ack);
  return true;
}

void WorldSession::FlushDeferredWorldTransfer() {
  if (!world_transition_.HasStaged()) {
    return;
  }

  DispatchWorldTransfer(*world_transition_.TakeStaged());
}

void WorldSession::HandleTransferPending(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleTransferPending(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "TRANSFER_PENDING: payload too small");
    return;
  }

  openwow::game::CancelPendingCastsForActivePlayer(*this);
  loot_.Clear();
  item_interactions_.reset(item_interactions_.player_generation() + 1);
  if (held_cursor_ != nullptr) {
    held_cursor_->Clear();
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "World transfer pending...");

  if (transfer_pending_callback_) {
    transfer_pending_callback_(session_.transfer_pending());
  }
}

void WorldSession::HandleNewWorld(const net::wotlk::WorldPacket &pkt) {
  world_transition_.CancelStaged();
  if (!session_.HandleNewWorld(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_NEW_WORLD");
    return;
  }

  const auto &nw = session_.new_world();
  if (!nw.fully_consumed) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_NEW_WORLD");
    return;
  }

  std::string map_internal_name;
  if (!ResolveWorldTransferMap(nw.map_id, &map_internal_name)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_NEW_WORLD zoneID");
    return;
  }

  const auto &pending = session_.transfer_pending();
  const bool is_transport_relative =
      pending.has_map_change_details && pending.transport_entry != 0;

  world_transition_.Stage(WorldTransferRequest{
      .map_id = nw.map_id,
      .x = nw.x,
      .y = nw.y,
      .z = nw.z,
      .orientation = nw.orientation,
      .map_internal_name = std::move(map_internal_name),
      .send_worldport_ack = true,
      .state_after_dispatch = WorldState::kTeleporting,
      .is_transport_relative = is_transport_relative,
  });
}

void WorldSession::HandleTransferAborted(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  auto& transfer_aborted = world_transition_.transfer_aborted();
  std::uint8_t reason = 0;
  if (!reader.ReadU32(transfer_aborted.map_id) || !reader.ReadU8(reason)) {
    return;
  }
  transfer_aborted.reason = static_cast<TransferAbortReason>(reason);
  transfer_aborted.arg = 0;
  (void)reader.ReadU8(transfer_aborted.arg);

  {
    const auto *map_entry = LookupMapEntry(transfer_aborted.map_id);
    const auto message = BuildTransferAbortMessage(transfer_aborted, map_entry, dbc_);
    if (message.has_value()) {
      ChatFrame_DisplayMessage(objects(), message->c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr,
                               nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
    }
  }

  world_transition_.ClearWorldportAck();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "World transfer aborted...");
  openwow::core::LoadingScreen_CleanupResources(sound_runtime_);
}

void WorldSession::HandleQueryTimeResponse(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  auto& query_time = world_transition_.query_time();
  std::uint32_t server_time = 0;
  if (!reader.ReadU32(server_time) || !reader.ReadU32(query_time.daily_reset_secs)) {
    return;
  }
  query_time.server_time = server_time;
  const auto local_now = static_cast<std::int64_t>(std::time(nullptr));
  query_time.local_time_offset_secs =
      local_now - static_cast<std::int64_t>(server_time);
  if (local_now == static_cast<std::int64_t>(server_time)) {
    query_time.local_time_offset_secs = 1;
  }
  query_time.local_daily_reset_deadline_secs =
      local_now + static_cast<std::int64_t>(query_time.daily_reset_secs);
  query_time.local_refresh_deadline_secs = std::min(
      query_time.local_daily_reset_deadline_secs, local_now + 3600);
  QuestLog::Get().SignalWatchUpdate();
}

}

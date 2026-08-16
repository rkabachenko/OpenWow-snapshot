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

constexpr int kVoiceChatParentalDisableAllError = 588;
constexpr int kVoiceChatParentalDisableMicError = 589;
constexpr char kEnableVoiceChatCVarName[] = "EnableVoiceChat";
constexpr char kEnableMicrophoneCVarName[] = "EnableMicrophone";

bool TryReadVoiceToggleCVar(const char *name, bool &enabled) {
  auto &cvars = ui::game::CVarSystem::Instance();
  if (!cvars.Exists(name)) {
    return false;
  }

  enabled = cvars.GetCVarBool(name);
  return true;
}

void WriteVoiceToggleCVar(const char *name, const bool enabled) {
  ui::game::CVarSystem::Instance().SetCVar(name, enabled ? "1" : "0", true);
}

bool WorldUiReadyForVoiceChatFeedback() {
  return ui::game::runtime::WorldUiRuntimeContext::FromLua(
             ui::frame_script_events::FrameScript_GetLuaStateTyped()) != nullptr ||
         ui::game::ScriptEventDispatch::Get().IsInitialized();
}

bool ReadBoundedCString(PacketReader &reader, const std::size_t max_length, std::string &out) {
  return reader.ReadCString(out) && out.size() < max_length;
}

std::optional<VoiceChatChannelType>
ResolveVoiceChannelTypeFromSessionCode(const std::uint8_t session_type) {
  switch (session_type) {
  case 0:
    return VoiceChatChannelType::kCustom;
  case 1:
    return VoiceChatChannelType::kBattleground;
  case 2:
    return VoiceChatChannelType::kParty;
  case 3:
    return VoiceChatChannelType::kRaid;
  default:
    return std::nullopt;
  }
}

std::string ResolveVoiceSessionName(const std::uint8_t session_type,
                                    const std::string_view channel_name) {
  switch (session_type) {
  case 1:
    return "Battleground";
  case 2:
    return "Party";
  case 3:
    return "Raid";
  default:
    return std::string(channel_name);
  }
}

std::string ResolveVoiceSpeakerName(WorldSession &session, const std::uint64_t guid) {
  if (const auto *player_name = session.query_cache().GetPlayerName(guid);
      player_name != nullptr && !player_name->name.empty()) {
    if (!player_name->realm_name.empty()) {
      return player_name->name + "-" + player_name->realm_name;
    }
    return player_name->name;
  }

  return session.objects().GetPlayerName(ObjectGuid(guid));
}
void ApplyVoiceChatServerAllowed(audio::SoundRuntime &sound_runtime, const bool allowed) {
  auto &voice_chat = VoiceChat::Get();
  const bool previous_allowed = voice_chat.IsServerAllowed();

  voice_chat.SetServerAllowed(sound_runtime, allowed);
  if (previous_allowed != allowed) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::VOICE_CHAT_ENABLED_UPDATE);
  }
}

}

void WorldSession::DisplayVoiceChatSystemMessage(const int error_index) {
  ui::game::DisplaySystemMessage(error_index);
}

void WorldSession::HandleAvailableVoiceChannel(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  const auto previous_voice_selection = CaptureVoiceDisplaySelectionSnapshot();

  std::uint64_t session_id = 0;
  std::uint8_t session_type = 0;
  std::string channel_name;
  std::uint64_t unused_session_token = 0;
  if (!reader.ReadU64(session_id) || !reader.ReadU8(session_type) ||
      !ReadBoundedCString(reader, 0x80u, channel_name) || !reader.ReadU64(unused_session_token)) {
    return;
  }

  const auto channel_type = ResolveVoiceChannelTypeFromSessionCode(session_type);
  if (!channel_type.has_value()) {
    return;
  }

  const auto session_name = ResolveVoiceSessionName(session_type, channel_name);
  VoiceChat::Get().UpsertChannelSession(session_id, session_name, *channel_type);
  if (*channel_type != VoiceChatChannelType::kCustom) {
    VoiceChat_SyncDisplaySelectionForSessionType(*this, *channel_type, &previous_voice_selection);
  }
  VoiceChat_NotifyDisplayChannelVoiceAvailable(sound_runtime(), session_name,
                                               *channel_type,
                                               &previous_voice_selection);
}

void WorldSession::HandleVoiceParentalControls(const net::wotlk::WorldPacket &pkt) {
  if (pkt.payload.size() < 2) {
    return;
  }

  bool old_voice_enabled = false;
  bool old_microphone_enabled = false;
  if (!TryReadVoiceToggleCVar(kEnableVoiceChatCVarName, old_voice_enabled) ||
      !TryReadVoiceToggleCVar(kEnableMicrophoneCVarName, old_microphone_enabled)) {
    return;
  }

  const bool voice_enabled = pkt.payload[0] != 0;
  const bool microphone_enabled = pkt.payload[1] != 0;
  WriteVoiceToggleCVar(kEnableVoiceChatCVarName, voice_enabled);
  WriteVoiceToggleCVar(kEnableMicrophoneCVarName, microphone_enabled);

  if (!WorldUiReadyForVoiceChatFeedback()) {
    return;
  }

  if (voice_enabled != old_voice_enabled) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::VOICE_CHAT_ENABLED_UPDATE);
  }

  if (!voice_enabled && old_voice_enabled) {
    FormatVoiceChatParentalError(objects(), kVoiceChatParentalDisableAllError);
    return;
  }

  if (!microphone_enabled && old_microphone_enabled) {
    FormatVoiceChatParentalError(objects(), kVoiceChatParentalDisableMicError);
  }
}

void WorldSession::HandleVoiceChatStatus(const net::wotlk::WorldPacket &pkt) {
  if (pkt.payload.empty()) {
    return;
  }

  ApplyVoiceChatServerAllowed(sound_runtime(), pkt.payload[0] != 0);
}

void WorldSession::HandleVoiceSetTalkerMuted(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());

  std::uint64_t raw_guid = 0;
  std::uint8_t packet_value = 0;
  if (!reader.ReadU64(raw_guid) || !reader.ReadU8(packet_value)) {
    return;
  }

  const bool muted = packet_value == 0;
  const ObjectGuid guid(raw_guid);
  VoiceChat_EnqueueIntCommand(static_cast<std::uint32_t>(ComSatCommandType::kRemoteTalkerVol),
                              muted ? 1 : 0, static_cast<std::uint32_t>(raw_guid),
                              static_cast<std::uint32_t>(raw_guid >> 32));
  VoiceChat::Get().MutePlayer(guid, muted);
  RefreshWatchedChannelRosterLocalMuteFlags();
}

void WorldSession::HandleVoiceSessionRosterUpdate(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());

  std::uint64_t session_id = 0;
  std::uint16_t unused_port = 0;
  std::uint8_t session_type = 0;
  std::string channel_name;
  std::uint8_t unused_ip_and_ticket[16]{};
  std::uint32_t unused_ip = 0;
  std::uint16_t unused_voice_port = 0;
  std::uint8_t member_count = 0;
  std::uint64_t first_member_guid = 0;
  std::uint8_t unused_local_status = 0;
  std::uint8_t local_status_flags = 0;
  if (!reader.ReadU64(session_id) || !reader.ReadU16(unused_port) || !reader.ReadU8(session_type) ||
      !ReadBoundedCString(reader, 0x80u, channel_name) ||
      !reader.ReadBytes(unused_ip_and_ticket, sizeof(unused_ip_and_ticket)) ||
      !reader.ReadU32(unused_ip) || !reader.ReadU16(unused_voice_port) ||
      !reader.ReadU8(member_count) || !reader.ReadU64(first_member_guid) ||
      !reader.ReadU8(unused_local_status) || !reader.ReadU8(local_status_flags)) {
    return;
  }

  const auto channel_type = ResolveVoiceChannelTypeFromSessionCode(session_type);
  if (!channel_type.has_value()) {
    return;
  }

  VoiceSessionRosterUpdate update;
  update.session_id = session_id;
  update.session_name = ResolveVoiceSessionName(session_type, channel_name);
  update.channel_type = *channel_type;
  update.member_count = member_count;

  if (member_count > 0) {
    update.members.push_back({ObjectGuid(first_member_guid), local_status_flags});
  }

  for (std::uint8_t index = 1; index < member_count; ++index) {
    std::uint64_t guid = 0;
    std::uint8_t unused_member_status = 0;
    std::uint8_t unused_member_detail = 0;
    std::uint8_t member_status_flags = 0;
    if (!reader.ReadU64(guid) || !reader.ReadU8(unused_member_status) ||
        !reader.ReadU8(unused_member_detail) || !reader.ReadU8(member_status_flags)) {
      return;
    }

    update.members.push_back({ObjectGuid(guid), member_status_flags});
  }

  VoiceChat_AddActiveComSatSession(static_cast<std::uint32_t>(session_id),
                                   static_cast<std::uint32_t>(session_id >> 32));
  const auto result = VoiceChat::Get().ApplySessionRosterUpdate(*this, update);
  for (const auto &removed_member : result.removed_members) {
    if (removed_member.IsEmpty() ||
        !VoiceChat_StopTrackedRemoteSpeaker(removed_member.GetRawValue())) {
      continue;
    }

    const std::string speaker_name = ResolveVoiceSpeakerName(*this, removed_member.GetRawValue());
    ui::game::ScriptEventDispatch::Get().FireVoiceStop(removed_member.GetRawValue(), speaker_name);
  }

  if (update.channel_type == VoiceChatChannelType::kCustom) {
    RefreshSelectedJoinedChannelVoiceRoster(update.session_name);
  }

  if (result.display_slot.has_value()) {
    const auto selected_slot = ChatSystem::Get().GetSelectedDisplayChannelIndex();
    const bool selected_display_updated =
        selected_slot.has_value() &&
        static_cast<std::uint32_t>(*selected_slot) == *result.display_slot;
    DispatchChannelVoiceUpdateForDisplaySlot(*result.display_slot, true, selected_display_updated);
    if (selected_display_updated) {
      ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
          ui::game::events::VOICE_CHANNEL_STATUS_UPDATE,
          {std::to_string(*result.display_slot + 1u), std::to_string(update.member_count)});
    }
  }

  ui::game::ScriptEventDispatch::Get().FireVoiceSessionsUpdate();
}

void WorldSession::HandleVoiceSessionLeave(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());

  std::uint64_t unused_channel_guid = 0;
  std::uint64_t session_id = 0;
  if (!reader.ReadU64(unused_channel_guid) || !reader.ReadU64(session_id)) {
    return;
  }

  VoiceChat_RemoveActiveComSatSession(static_cast<std::uint32_t>(session_id),
                                      static_cast<std::uint32_t>(session_id >> 32));
  const auto result = VoiceChat::Get().RemoveSessionById(*this, session_id);
  for (const auto &removed_member : result.removed_members) {
    if (removed_member.IsEmpty() ||
        !VoiceChat_StopTrackedRemoteSpeaker(removed_member.GetRawValue())) {
      continue;
    }

    const std::string speaker_name = ResolveVoiceSpeakerName(*this, removed_member.GetRawValue());
    ui::game::ScriptEventDispatch::Get().FireVoiceStop(removed_member.GetRawValue(), speaker_name);
  }
}

}

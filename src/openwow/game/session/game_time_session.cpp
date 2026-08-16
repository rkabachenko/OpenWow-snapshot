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
#include "openwow/game/packet_reader.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/title_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/core/init_subsystems.h"
#include <algorithm>
#include <array>
#include <climits>
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

constexpr std::uint32_t PackedGameTimeMinute(const std::uint32_t packed_time) {
  return packed_time & 0x3Fu;
}

constexpr std::uint32_t PackedGameTimeHour(const std::uint32_t packed_time) {
  return (packed_time >> 6) & 0x1Fu;
}

constexpr std::uint32_t kMirrorTimerKnownTypeCount = 3;
constexpr int kUnknownMirrorTimerType =
    static_cast<int>(kMirrorTimerKnownTypeCount);

constexpr std::uint32_t kExhaustionTutorialIndex = 0x1Au;
constexpr std::uint32_t kBreathTutorialIndex = 0x1Cu;

struct RawMirrorTimerPause {
  std::uint32_t type = 0;
  std::uint8_t paused = 0;
};

std::optional<RawMirrorTimerPause> ReadRawMirrorTimerPause(
    const net::wotlk::WorldPacket& packet) {
  PacketReader reader(packet.payload.data(), packet.payload.size());
  RawMirrorTimerPause result;
  if (!reader.ReadU32(result.type) || !reader.ReadU8(result.paused)) {
    return std::nullopt;
  }
  return result;
}

int MirrorTimerNameIndex(const std::uint32_t type) {
  return type < kMirrorTimerKnownTypeCount ? static_cast<int>(type)
                                             : kUnknownMirrorTimerType;
}

std::uint32_t ReadLittleEndianU32OrZero(const std::uint8_t* data,
                                        const std::size_t length) {
  std::uint32_t value = 0;
  if (length < sizeof(value)) {
    return value;
  }

  for (std::size_t byte_index = 0; byte_index < sizeof(value); ++byte_index) {
    value |= static_cast<std::uint32_t>(data[byte_index])
             << (byte_index * CHAR_BIT);
  }
  return value;
}

}

void WorldSession::HandleLoginSetTimeSpeed(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleLoginSetTimeSpeed(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_LOGIN_SETTIMESPEED");
    return;
  }

  RefreshWorldSceneGameTime();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "Gamespeed set to " + std::to_string(session_.game_time().game_speed));
}

void WorldSession::HandleGameTimeSet(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleGameTimeSet(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_GAMETIME_SET");
    return;
  }
  RefreshWorldSceneGameTime();
}

void WorldSession::HandleGameSpeedSet(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleGameSpeedSet(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_GAMESPEED_SET");
  }
}

void WorldSession::HandleStartMirrorTimer(const net::wotlk::WorldPacket &pkt) {
  session_.HandleStartMirrorTimer(pkt.payload.data(), pkt.payload.size());
  const auto &info = session_.mirror_timer_start();
  const int timer_index = static_cast<int>(info.type);
  const std::string timer_name = GetMirrorTimerName(timer_index);
  const std::string label = ResolveMirrorTimerLabel(timer_index, info.spell_id);

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::MIRROR_TIMER_START,
      {timer_name, static_cast<int>(info.value), static_cast<int>(info.max_value),
       static_cast<int>(info.scale), info.paused ? 1 : 0, label});

  if (timer_index == 0) {
    TutorialSystem::Instance().TriggerTutorial(kExhaustionTutorialIndex);
  } else if (timer_index == 1) {
    TutorialSystem::Instance().TriggerTutorial(kBreathTutorialIndex);
  }

  StartMirrorTimer(timer_index, info.value, info.max_value, info.scale, info.paused, info.spell_id);
}

void WorldSession::HandleStopMirrorTimer(const net::wotlk::WorldPacket &pkt) {
  session_.HandleStopMirrorTimer(pkt.payload.data(), pkt.payload.size());
  const int timer_index = static_cast<int>(session_.mirror_timer_stop().type);

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::MIRROR_TIMER_STOP, {std::string(GetMirrorTimerName(timer_index))});
  StopMirrorTimer(timer_index);
}

void WorldSession::HandlePauseMirrorTimer(const net::wotlk::WorldPacket &pkt) {
  const auto raw_info = ReadRawMirrorTimerPause(pkt);
  misc_.HandlePauseMirrorTimer(pkt.payload.data(), pkt.payload.size());
  if (raw_info.has_value()) {
    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::MIRROR_TIMER_PAUSE,
        {std::string(GetMirrorTimerName(MirrorTimerNameIndex(raw_info->type))),
         static_cast<int>(raw_info->paused)});
    return;
  }

  const auto &pause_info = misc_.last_pause_mirror_timer();
  if (!pause_info.has_value()) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::MIRROR_TIMER_PAUSE,
      {std::string(GetMirrorTimerName(static_cast<int>(pause_info->timer_type))),
       pause_info->paused ? 1 : 0});
}

void WorldSession::HandleWorldStateTimerUpdate(const net::wotlk::WorldPacket &pkt) {
  world_states_.SetWorldStateUiServerTime(
      ReadLittleEndianU32OrZero(pkt.payload.data(), pkt.payload.size()));
}

void WorldSession::HandleGameTimeUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleGameTimeUpdate(pkt.payload.data(), pkt.payload.size())) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Bad SMSG_GAMETIME_UPDATE");
    return;
  }
  RefreshWorldSceneGameTime();
}

void WorldSession::RefreshWorldSceneGameTime() {
  const auto packed_time = session_.game_time().packed_time;
  scene_state_.SetGameTime(PackedGameTimeHour(packed_time),
                           PackedGameTimeMinute(packed_time));
}

}

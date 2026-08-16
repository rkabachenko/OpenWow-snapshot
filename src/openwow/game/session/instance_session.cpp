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

std::string GetDungeonDifficultyGlobalStringKey(const DungeonDifficulty difficulty) {
  return "DUNGEON_DIFFICULTY" + std::to_string(static_cast<std::uint32_t>(difficulty) + 1u);
}

std::string GetRaidDifficultyGlobalStringKey(const RaidDifficulty difficulty) {
  return "RAID_DIFFICULTY" + std::to_string(static_cast<std::uint32_t>(difficulty) + 1u);
}

const data::dbc::MapDifficultyEntry *LookupMapDifficultyEntry(const data::dbc::DbcLoader *dbc,
                                                              const std::uint32_t map_id,
                                                              const std::uint8_t difficulty);

std::string GetPlayerDifficultyGlobalStringKey(const std::uint8_t difficulty) {
  return "PLAYER_DIFFICULTY" + std::to_string(static_cast<std::uint32_t>(difficulty) + 1u);
}

void DisplayDungeonDifficultyChangedMessage(const DungeonDifficulty difficulty) {
  const auto difficulty_key = GetDungeonDifficultyGlobalStringKey(difficulty);
  const auto difficulty_name = Localization::Get().GetString(difficulty_key, difficulty_key);
  ui::game::DisplaySystemMessage(503, difficulty_name.c_str());
}

}

void DisplayPlayerDifficultyChangedMessage(const std::uint8_t difficulty) {
  const auto difficulty_key = GetPlayerDifficultyGlobalStringKey(difficulty);
  const auto difficulty_name = Localization::Get().GetString(difficulty_key, difficulty_key);
  ui::game::DisplaySystemMessage(672, difficulty_name.c_str());
}

namespace {

std::int32_t ResolveCurrentMapIdForDifficultyUi(const WorldSession &session) {
  if (session.has_current_map()) {
    return static_cast<std::int32_t>(session.current_map_id());
  }

  const auto world_state_map_id = session.world_states().map_id();
  if (world_state_map_id >= 0) {
    return world_state_map_id;
  }

  return static_cast<std::int32_t>(session.objects().GetMapId());
}

bool CurrentMapAllowsPlayerDifficultyChange(const WorldSession &session) {
  const auto map_id = ResolveCurrentMapIdForDifficultyUi(session);
  if (map_id < 0) {
    return false;
  }

  const auto *map_entry = session.LookupMapEntry(static_cast<std::uint32_t>(map_id));
  return map_entry != nullptr && (map_entry->flags & 0x100u) != 0;
}

std::string ResolveMapDifficultyName(const data::dbc::MapDifficultyEntry *const map_difficulty) {
  if (map_difficulty == nullptr || map_difficulty->difficulty_string.empty()) {
    return {};
  }

  const auto difficulty_key = std::string(map_difficulty->difficulty_string);
  return Localization::Get().GetString(difficulty_key, difficulty_key);
}

std::uint8_t ResolveCurrentInstanceDifficultyIndexForRaidUi(const WorldSession &session) {
  return static_cast<std::uint8_t>(session.instance_difficulty().difficulty_index);
}

void DisplayRaidDifficultyChangedMessages(const WorldSession &session) {
  const auto map_id = ResolveCurrentMapIdForDifficultyUi(session);
  const auto *map_entry =
      map_id >= 0 ? session.LookupMapEntry(static_cast<std::uint32_t>(map_id)) : nullptr;
  const auto &group_system = GroupSystem::Get();
  const auto raid_difficulty = group_system.GetRaidDifficulty();

  if (map_entry != nullptr && (map_entry->flags & 0x100u) != 0) {
    const auto difficulty_key = GetRaidDifficultyGlobalStringKey(raid_difficulty);
    const auto difficulty_name = Localization::Get().GetString(difficulty_key, difficulty_key);
    ui::game::DisplaySystemMessage(671, difficulty_name.c_str());
    DisplayPlayerDifficultyChangedMessage(group_system.GetPlayerDifficultyIndex());
    return;
  }

  if (map_entry != nullptr && map_entry->map_type == 2) {
    if (const auto *map_difficulty =
            LookupMapDifficultyEntry(session.GetDbcLoader(), map_entry->id,
                                     ResolveCurrentInstanceDifficultyIndexForRaidUi(session));
        map_difficulty != nullptr) {
      const auto difficulty_name = ResolveMapDifficultyName(map_difficulty);
      ui::game::DisplaySystemMessage(670, difficulty_name.c_str());
      return;
    }
  }

  const auto difficulty_key = GetRaidDifficultyGlobalStringKey(raid_difficulty);
  const auto difficulty_name = Localization::Get().GetString(difficulty_key, difficulty_key);
  ui::game::DisplaySystemMessage(670, difficulty_name.c_str());
}

void ApplyRaidDifficultyUpdate(WorldSession &session, const DifficultyUpdate &update) {
  auto &group_system = GroupSystem::Get();
  const auto previous_effective = group_system.GetRaidDifficulty();

  if (update.update_default != 0) {
    group_system.SetDefaultRaidDifficulty(static_cast<RaidDifficulty>(update.difficulty));
  }

  if (update.update_group != 0) {
    if (CurrentMapAllowsPlayerDifficultyChange(session)) {
      session.interaction().SendChangePlayerDifficulty(update.difficulty > 1u ? 1u : 0u);
    } else {
      group_system.SetCurrentRaidDifficulty(static_cast<RaidDifficulty>(update.difficulty));
    }
  }

  if (previous_effective != group_system.GetRaidDifficulty()) {
    session.RefreshGameObjectDifficultyVisibility();
    DisplayRaidDifficultyChangedMessages(session);
  }
}

}

void DisplayGroupDifficultyChangedMessagesIfNeeded(const WorldSession &session,
                                                   const DungeonDifficulty previous_dungeon,
                                                   const RaidDifficulty previous_raid) {
  const auto &group_system = GroupSystem::Get();
  const auto current_raid = group_system.GetRaidDifficulty();
  if (previous_raid != current_raid) {
    DisplayRaidDifficultyChangedMessages(session);
  }

  const auto current_dungeon = group_system.GetDungeonDifficulty();
  if (previous_dungeon != current_dungeon) {
    DisplayDungeonDifficultyChangedMessage(current_dungeon);
  }
}

namespace {

void SyncEncounterBossTokens(const InstanceHandler &instance) {
  auto &registry = ui::game::UnitTokenRegistry::Get();
  registry.ClearBossFrames();
  const auto &frames = instance.encounter_unit_frames();
  for (std::size_t index = 0; index < frames.size(); ++index) {
    registry.SetBossFrame(static_cast<std::uint8_t>(index), frames[index].guid.GetRawValue());
  }
}

const data::dbc::MapDifficultyEntry *LookupMapDifficultyEntry(const data::dbc::DbcLoader *dbc,
                                                               const std::uint32_t map_id,
                                                               const std::uint8_t difficulty) {
  return data::DBClient_FindMapDifficulty(dbc, map_id, difficulty);
}

std::string ResolveDungeonNameWithDifficulty(const WorldSession &session,
                                             const std::uint32_t map_id,
                                             const std::uint32_t difficulty) {
  return FormatDungeonNameWithDifficulty(session.GetDbcLoader(), map_id, difficulty);
}

std::string ResolveInstanceResetMapName(const WorldSession &session, const std::uint32_t map_id) {
  if (const auto *map_entry = session.LookupMapEntry(map_id); map_entry != nullptr) {
    return std::string(map_entry->name);
  }

  return std::to_string(map_id);
}

const char *GetInstanceResetFailedStringKey(const std::uint32_t reason) {
  switch (static_cast<InstanceResetFailReason>(reason)) {
  case InstanceResetFailReason::kPlayersInside:
    return "INSTANCE_RESET_FAILED";
  case InstanceResetFailReason::kOfflineMembers:
    return "INSTANCE_RESET_FAILED_OFFLINE";
  case InstanceResetFailReason::kZoning:
    return "INSTANCE_RESET_FAILED_ZONING";
  }

  return nullptr;
}

void DisplayLocalizedSystemMessage(WorldSession &session, const char *key,
                                   const std::vector<std::string> &args) {
  const std::string format = Localization::Get().GetString(key != nullptr ? key : "");
  const std::string message = Localization::Get().FormatString(format, args);
  ChatFrame_DisplayMessage(session.objects(), message.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr,
                           nullptr, 0, 0, 0, 0, 0, nullptr);
}

}

const data::dbc::MapDifficultyEntry *WorldSession::LookupMapDifficultyEntry(
    const std::uint32_t map_id, const std::uint8_t difficulty) const {
  return data::DBClient_FindMapDifficulty(dbc_, map_id, difficulty);
}

bool WorldSession::HasMapDifficultyRaidDuration(const std::uint32_t map_id,
                                                const std::uint8_t difficulty) const {
  if (difficulty > 3) {
    return false;
  }

  const auto *entry = LookupMapDifficultyEntry(map_id, difficulty);
  return entry != nullptr && entry->raid_duration != 0;
}

void WorldSession::HandleInstanceDifficulty(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU32(instance_difficulty_.difficulty_index) ||
      !reader.ReadU32(instance_difficulty_.player_difficulty_index)) {
    return;
  }

  auto &group_system = GroupSystem::Get();
  const auto previous_player_difficulty = group_system.GetPlayerDifficultyIndex();
  group_system.SetPlayerDifficultyIndex(
      static_cast<std::uint8_t>(instance_difficulty_.player_difficulty_index));
  if (previous_player_difficulty != group_system.GetPlayerDifficultyIndex()) {
    RefreshGameObjectDifficultyVisibility();
  }
}

void WorldSession::HandleSetDungeonDifficulty(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleSetDungeonDifficulty(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto &group_system = GroupSystem::Get();
  const auto previous_effective = group_system.GetDungeonDifficulty();
  const auto update = instance_.dungeon_difficulty();
  group_system.ApplyDungeonDifficultyUpdate(static_cast<DungeonDifficulty>(update.difficulty),
                                            update.update_default != 0, update.update_group != 0);
  const auto current_effective = group_system.GetDungeonDifficulty();
  if (previous_effective != current_effective) {
    RefreshGameObjectDifficultyVisibility();
    DisplayDungeonDifficultyChangedMessage(current_effective);
  }
}

void WorldSession::HandleSetRaidDifficulty(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleSetRaidDifficulty(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ApplyRaidDifficultyUpdate(*this, instance_.raid_difficulty());
}

void WorldSession::RequestRaidDifficultyChange(const std::uint32_t difficulty) {
  ApplyRaidDifficultyUpdate(
      *this, DifficultyUpdate{.difficulty = difficulty,
                              .update_default = 1u,
                              .update_group = 1u});
  interaction().SendSetRaidDifficulty(difficulty);
}

void WorldSession::HandleRaidInstanceInfo(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleRaidInstanceInfo(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::UPDATE_INSTANCE_INFO);
}

void WorldSession::HandleInstanceReset(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleInstanceReset(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  DisplayLocalizedSystemMessage(*this, "INSTANCE_RESET_SUCCESS",
                                {ResolveInstanceResetMapName(*this, instance_.last_reset_map())});
}

void WorldSession::HandleInstanceResetFailed(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleInstanceResetFailed(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &reset_failed = instance_.last_reset_failed();
  const char *const key = GetInstanceResetFailedStringKey(reset_failed.reason);
  if (key == nullptr) {
    return;
  }

  DisplayLocalizedSystemMessage(*this, key,
                                {ResolveInstanceResetMapName(*this, reset_failed.map_id)});
}

void WorldSession::HandleEncounterUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleEncounterUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (!instance_.last_encounter_fires_unit_frame_event()) {
    return;
  }

  SyncEncounterBossTokens(instance_);
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::INSTANCE_ENCOUNTER_ENGAGE_UNIT);
}

void WorldSession::HandleRaidGroupOnly(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleRaidGroupOnly(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &boot_warning = instance_.last_instance_boot_warning();
  if (boot_warning.remaining_ms == 0) {
    switch (boot_warning.reason) {
    case 1:
      ui::game::DisplaySystemMessage(423);
      break;
    case 2:
      ui::game::DisplaySystemMessage(424);
      break;
    case 3:
      ui::game::DisplaySystemMessage(425);
      break;
    case 4:
      ui::game::DisplaySystemMessage(426);
      break;
    default:
      break;
    }

    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::INSTANCE_BOOT_STOP);
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::INSTANCE_BOOT_START);
}

void WorldSession::HandleInstanceLockWarning(const net::wotlk::WorldPacket &pkt) {
  instance_.HandleInstanceLockWarning(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleInstanceSaveCreated(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleInstanceSaveCreated(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  FormatInstanceSaveCreated(objects(), instance_.last_instance_save_created());
}

void WorldSession::HandleUpdateLastInstance(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleUpdateLastInstance(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  UpdateResetInstanceVisibilityForMapTransition(instance_.last_instance_map_id(), current_map_id_);
}

void WorldSession::HandleUpdateInstanceOwnership(const net::wotlk::WorldPacket &pkt) {
  instance_.HandleUpdateInstanceOwnership(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleRaidReadyCheckError(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  ui::game::DisplaySystemMessage(501);
}

void WorldSession::HandleRaidInstanceMessage(const net::wotlk::WorldPacket &pkt) {
  if (!instance_.HandleRaidInstanceMessage(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &message = instance_.last_raid_instance_msg();
  if (!message.has_value()) {
    return;
  }

  const std::string dungeon_name =
      ResolveDungeonNameWithDifficulty(*this, message->map_id, message->difficulty);
  switch (message->type) {
  case 1:
    DisplayLocalizedSystemMessage(*this, "RAID_INSTANCE_WARNING_HOURS",
                                  {dungeon_name, std::to_string(message->time_remaining / 3600u)});
    return;
  case 2:
    DisplayLocalizedSystemMessage(*this, "RAID_INSTANCE_WARNING_MIN",
                                  {dungeon_name, std::to_string(message->time_remaining / 60u)});
    return;
  case 3:
    DisplayLocalizedSystemMessage(*this, "RAID_INSTANCE_WARNING_MIN_SOON",
                                  {dungeon_name, std::to_string(message->time_remaining / 60u)});
    return;
  case 4:
    ui::game::ScriptEventDispatch::Get().FireRaidInstanceWelcome(
        dungeon_name, static_cast<int>(message->time_remaining),
        static_cast<int>(message->welcome_flag1 != 0),
        static_cast<int>(message->welcome_flag2 != 0));
    return;
  case 5:
    DisplayLocalizedSystemMessage(*this, "RAID_INSTANCE_EXPIRED", {dungeon_name});
    return;
  default:
    return;
  }
}

void WorldSession::HandleResetFailedNotify(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  FormatResetFailedNotify(objects());
}

void WorldSession::HandleViewPhaseShift(const net::wotlk::WorldPacket &pkt) {
  instance_.HandleViewPhaseShift(pkt.payload.data(), pkt.payload.size());
  world_states_.SetWorldStateUiFilterMask(instance_.last_phase_mask());
}

void WorldSession::RefreshGameObjectDifficultyVisibility() {
  map_runtime_.objects().ForEachObject([this](const ObjectGuid &, CGObject_C &object) {
    if (!object.IsGameObject()) {
      return;
    }

    static_cast<CGGameObject_C &>(object).RefreshDifficultyVisibilityControlState(*this);
  });
}

}

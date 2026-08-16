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

void WorldSession::BindDecomposedPacketHandlers() {
  using Opcode = net::wotlk::Opcode;
  const auto bind = [this](Opcode opcode, const char* owner, auto handler) {
    decomposed_packet_registrations_.push_back(
        packet_dispatcher_.Register(opcode, owner, std::move(handler)));
  };
  bind(Opcode::SMSG_QUEST_QUERY_RESPONSE, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestQueryResponse(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_QUEST_DETAILS, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverQuestDetails(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_OFFER_REWARD, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverOfferReward(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_REQUEST_ITEMS, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverRequestItems(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_STATUS, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverStatus(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_STATUS_MULTIPLE, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverStatusMultiple(pkt); return true; });
  bind(Opcode::SMSG_QUESTUPDATE_COMPLETE, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestUpdateComplete(pkt); return true; });
  bind(Opcode::SMSG_QUESTUPDATE_ADD_KILL, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestUpdateAddKill(pkt); return true; });
  bind(Opcode::SMSG_QUEST_CONFIRM_ACCEPT, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestConfirmAccept(pkt); return true; });
  bind(Opcode::SMSG_QUESTLOG_FULL, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestLogFull(pkt); return true; });
  bind(Opcode::SMSG_LOOT_RESPONSE, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootResponse(pkt); return true; });
  bind(Opcode::SMSG_LOOT_RELEASE_RESPONSE, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootReleaseResponse(pkt); return true; });
  bind(Opcode::SMSG_LOOT_REMOVED, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootRemoved(pkt); return true; });
  bind(Opcode::SMSG_LOOT_MONEY_NOTIFY, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootMoneyNotify(pkt); return true; });
  bind(Opcode::SMSG_ALL_ACHIEVEMENT_DATA, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleAllAchievementData(pkt); return true; });
  bind(Opcode::SMSG_ACHIEVEMENT_EARNED, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleAchievementEarned(pkt); return true; });
  bind(Opcode::SMSG_CRITERIA_UPDATE, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCriteriaUpdate(pkt); return true; });
  bind(Opcode::SMSG_CRITERIA_DELETED, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCriteriaDeleted(pkt); return true; });
  bind(Opcode::SMSG_SERVER_FIRST_ACHIEVEMENT, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleServerFirstAchievement(pkt); return true; });
  bind(Opcode::SMSG_RESPOND_INSPECT_ACHIEVEMENTS, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInspectAchievements(pkt); return true; });
  bind(Opcode::SMSG_PET_SPELLS, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetSpells(pkt); return true; });
  bind(Opcode::SMSG_PET_MODE, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetMode(pkt); return true; });
  bind(Opcode::SMSG_PET_ACTION_FEEDBACK, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetActionFeedback(pkt); return true; });
  bind(Opcode::SMSG_PET_CAST_FAILED, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetCastFailed(pkt); return true; });
  bind(Opcode::SMSG_PET_NAME_QUERY_RESPONSE, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetNameQueryResponse(pkt); return true; });
  bind(Opcode::MSG_LIST_STABLED_PETS, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandleStabledPets(pkt); return true; });
  bind(Opcode::SMSG_PET_GUIDS, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetGuids(pkt); return true; });
  bind(Opcode::SMSG_WEATHER, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleWeather(pkt); return true; });
  bind(Opcode::SMSG_BINDPOINTUPDATE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleBindPointUpdate(pkt); return true; });
  bind(Opcode::SMSG_PLAYERBOUND, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayerBound(pkt); return true; });
  bind(Opcode::SMSG_PLAYED_TIME, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayedTime(pkt); return true; });
  bind(Opcode::SMSG_WHO, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleWho(pkt); return true; });
  bind(Opcode::SMSG_MOTD, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMotd(pkt); return true; });
  bind(Opcode::SMSG_TUTORIAL_FLAGS, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleTutorialFlags(pkt); return true; });
  bind(Opcode::SMSG_DUEL_REQUESTED, "duel_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDuelRequested(pkt); return true; });
  bind(Opcode::SMSG_DUEL_WINNER, "duel_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDuelWinner(pkt); return true; });
  bind(Opcode::SMSG_DUEL_COMPLETE, "duel_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDuelComplete(pkt); return true; });
  bind(Opcode::SMSG_EMOTE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleEmote(pkt); return true; });
  bind(Opcode::SMSG_TEXT_EMOTE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleTextEmote(pkt); return true; });
  bind(Opcode::SMSG_NOTIFICATION, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleNotification(pkt); return true; });
  bind(Opcode::SMSG_EXPLORATION_EXPERIENCE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleExplorationExperience(pkt); return true; });
  bind(Opcode::SMSG_EQUIPMENT_SET_LIST, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleEquipmentSetList(pkt); return true; });
  bind(Opcode::SMSG_DEATH_RELEASE_LOC, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDeathReleaseLoc(pkt); return true; });
  bind(Opcode::SMSG_CORPSE_RECLAIM_DELAY, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCorpseReclaimDelay(pkt); return true; });
  bind(Opcode::SMSG_INITIALIZE_FACTIONS, "reputation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInitializeFactions(pkt); return true; });
  bind(Opcode::SMSG_SET_FACTION_STANDING, "reputation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetFactionStanding(pkt); return true; });
  bind(Opcode::SMSG_SET_FACTION_VISIBLE, "reputation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetFactionVisible(pkt); return true; });
  bind(Opcode::SMSG_LOGIN_SETTIMESPEED, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLoginSetTimeSpeed(pkt); return true; });
  bind(Opcode::SMSG_GAMETIME_SET, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameTimeSet(pkt); return true; });
  bind(Opcode::SMSG_GAMESPEED_SET, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameSpeedSet(pkt); return true; });
  bind(Opcode::SMSG_TRANSFER_PENDING, "world_transition_controller", [this](const net::wotlk::WorldPacket& pkt) { HandleTransferPending(pkt); return true; });
  bind(Opcode::SMSG_NEW_WORLD, "world_transition_controller", [this](const net::wotlk::WorldPacket& pkt) { HandleNewWorld(pkt); return true; });
  bind(Opcode::SMSG_FORCE_MOVE_ROOT, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleForceMoveRoot(pkt); return true; });
  bind(Opcode::SMSG_FORCE_MOVE_UNROOT, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleForceMoveUnroot(pkt); return true; });
  bind(Opcode::SMSG_MOVE_KNOCK_BACK, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMoveKnockBack(pkt); return true; });
  bind(Opcode::SMSG_MOVE_SET_CAN_FLY, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMoveSetCanFly(pkt); return true; });
  bind(Opcode::SMSG_MOVE_UNSET_CAN_FLY, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMoveUnsetCanFly(pkt); return true; });
  bind(Opcode::SMSG_START_MIRROR_TIMER, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleStartMirrorTimer(pkt); return true; });
  bind(Opcode::SMSG_STOP_MIRROR_TIMER, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleStopMirrorTimer(pkt); return true; });
  bind(Opcode::SMSG_SET_PROFICIENCY, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetProficiency(pkt); return true; });
  bind(Opcode::SMSG_STANDSTATE_UPDATE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleStandStateUpdate(pkt); return true; });
  bind(Opcode::SMSG_UPDATE_COMBO_POINTS, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleUpdateComboPoints(pkt); return true; });
  bind(Opcode::SMSG_PLAY_SOUND, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlaySound(pkt); return true; });
  bind(Opcode::SMSG_SHOWTAXINODES, "taxi_session", [this](const net::wotlk::WorldPacket& pkt) { HandleShowTaxiNodes(pkt); return true; });
  bind(Opcode::SMSG_ACTIVATETAXIREPLY, "taxi_session", [this](const net::wotlk::WorldPacket& pkt) { HandleActivateTaxiReply(pkt); return true; });
  bind(Opcode::SMSG_NEW_TAXI_PATH, "taxi_session", [this](const net::wotlk::WorldPacket& pkt) { HandleNewTaxiPath(pkt); return true; });
  bind(Opcode::SMSG_TAXINODE_STATUS, "taxi_session", [this](const net::wotlk::WorldPacket& pkt) { HandleTaxiNodeStatus(pkt); return true; });
  bind(Opcode::SMSG_SET_PHASE_SHIFT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePhaseShift(pkt); return true; });
  bind(Opcode::SMSG_INSTANCE_DIFFICULTY, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInstanceDifficulty(pkt); return true; });
  bind(Opcode::SMSG_TRANSFER_ABORTED, "world_transition_controller", [this](const net::wotlk::WorldPacket& pkt) { HandleTransferAborted(pkt); return true; });
  bind(Opcode::SMSG_QUERY_TIME_RESPONSE, "world_transition_controller", [this](const net::wotlk::WorldPacket& pkt) { HandleQueryTimeResponse(pkt); return true; });
  bind(Opcode::SMSG_GOSSIP_COMPLETE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGossipComplete(pkt); return true; });
  bind(Opcode::SMSG_GOSSIP_POI, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGossipPoi(pkt); return true; });
  bind(Opcode::MSG_CORPSE_QUERY, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCorpseQuery(pkt); return true; });
  bind(Opcode::MSG_RANDOM_ROLL, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleRandomRoll(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_QUEST_COMPLETE, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverQuestComplete(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_QUEST_LIST, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestGiverQuestList(pkt); return true; });
  bind(Opcode::SMSG_RESURRECT_REQUEST, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleResurrectRequest(pkt); return true; });
  bind(Opcode::SMSG_SHOW_BANK, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleShowBank(pkt); return true; });
  bind(Opcode::SMSG_CLIENT_CONTROL_UPDATE, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleClientControlUpdate(pkt); return true; });
  bind(Opcode::SMSG_CANCEL_AUTO_REPEAT, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCancelAutoRepeat(pkt); return true; });
  bind(Opcode::SMSG_DISMOUNT, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDismount(pkt); return true; });
  bind(Opcode::SMSG_REALM_SPLIT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleRealmSplit(pkt); return true; });
  bind(Opcode::MSG_SET_DUNGEON_DIFFICULTY, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetDungeonDifficulty(pkt); return true; });
  bind(Opcode::MSG_SET_RAID_DIFFICULTY, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetRaidDifficulty(pkt); return true; });
  bind(Opcode::SMSG_RAID_INSTANCE_INFO, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleRaidInstanceInfo(pkt); return true; });
  bind(Opcode::SMSG_INSTANCE_RESET, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInstanceReset(pkt); return true; });
  bind(Opcode::SMSG_INSTANCE_RESET_FAILED, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInstanceResetFailed(pkt); return true; });
  bind(Opcode::SMSG_UPDATE_INSTANCE_ENCOUNTER_UNIT, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleEncounterUpdate(pkt); return true; });
  bind(Opcode::SMSG_RAID_GROUP_ONLY, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleRaidGroupOnly(pkt); return true; });
  bind(Opcode::SMSG_INSTANCE_LOCK_WARNING_QUERY, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInstanceLockWarning(pkt); return true; });
  bind(Opcode::SMSG_INSTANCE_SAVE_CREATED, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInstanceSaveCreated(pkt); return true; });
  bind(Opcode::SMSG_UPDATE_LAST_INSTANCE, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleUpdateLastInstance(pkt); return true; });
  bind(Opcode::SMSG_UPDATE_INSTANCE_OWNERSHIP, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleUpdateInstanceOwnership(pkt); return true; });
  bind(Opcode::SMSG_RAID_READY_CHECK_ERROR, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleRaidReadyCheckError(pkt); return true; });
  bind(Opcode::SMSG_INSPECT_TALENT, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInspectTalent(pkt); return true; });
  bind(Opcode::MSG_INSPECT_HONOR_STATS, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInspectHonorStats(pkt); return true; });
  bind(Opcode::SMSG_TITLE_EARNED, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleTitleEarned(pkt); return true; });
  bind(Opcode::SMSG_ENABLE_BARBER_SHOP, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleEnableBarberShop(pkt); return true; });
  bind(Opcode::SMSG_BARBER_SHOP_RESULT, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleBarberShopResult(pkt); return true; });
  bind(Opcode::MSG_MINIMAP_PING, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMinimapPing(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_SEND_CALENDAR, "calendar_snapshot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarSendCalendar(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_SEND_EVENT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarSendEvent(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_SEND_NUM_PENDING, "calendar_snapshot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarSendNumPending(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_COMMAND_RESULT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarCommandResult(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInvite(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInviteAlert(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_STATUS, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventStatus(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_RAID_LOCKOUT_ADDED, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarRaidLockoutAdded(pkt); return true; });
  bind(Opcode::SMSG_PLAYER_VEHICLE_DATA, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayerVehicleData(pkt); return true; });
  bind(Opcode::SMSG_FORCE_SET_VEHICLE_REC_ID, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleForceSetVehicleRecId(pkt); return true; });
  bind(Opcode::SMSG_ON_CANCEL_EXPECTED_RIDE_VEHICLE_AURA, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCancelExpectedRideVehicleAura(pkt); return true; });
  bind(Opcode::SMSG_LOOT_ITEM_NOTIFY, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootItemNotify(pkt); return true; });
  bind(Opcode::SMSG_LOOT_LIST, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootList(pkt); return true; });
  bind(Opcode::SMSG_LOOT_MASTER_LIST, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootMasterList(pkt); return true; });
  bind(Opcode::SMSG_LOOT_SLOT_CHANGED, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootSlotChanged(pkt); return true; });
  bind(Opcode::SMSG_QUEST_POI_QUERY_RESPONSE, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { (void)HandleQuestPoiQueryResponse(pkt); return true; });
  bind(Opcode::MSG_QUEST_PUSH_RESULT, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestPushResult(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_QUEST_FAILED, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestGiverQuestFailed(pkt); return true; });
  bind(Opcode::SMSG_QUESTUPDATE_FAILED, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestUpdateFailed(pkt); return true; });
  bind(Opcode::SMSG_QUESTUPDATE_ADD_PVP_KILL, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestUpdateAddPvpKill(pkt); return true; });
  bind(Opcode::SMSG_LOOT_CLEAR_MONEY, "loot_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLootClearMoney(pkt); return true; });
  bind(Opcode::SMSG_DUEL_COUNTDOWN, "duel_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDuelCountdown(pkt); return true; });
  bind(Opcode::SMSG_DUEL_OUTOFBOUNDS, "duel_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDuelOutOfBounds(pkt); return true; });
  bind(Opcode::SMSG_DUEL_INBOUNDS, "duel_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDuelInBounds(pkt); return true; });
  bind(Opcode::SMSG_DURABILITY_DAMAGE_DEATH, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDurabilityDamageDeath(pkt); return true; });
  bind(Opcode::SMSG_PLAY_MUSIC, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayMusic(pkt); return true; });
  bind(Opcode::SMSG_PLAY_OBJECT_SOUND, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayObjectSound(pkt); return true; });
  bind(Opcode::SMSG_GAMEOBJECT_CUSTOM_ANIM, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameObjectCustomAnim(pkt); return true; });
  bind(Opcode::SMSG_GAMEOBJECT_DESPAWN_ANIM, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameObjectDespawnAnim(pkt); return true; });
  bind(Opcode::SMSG_GAMEOBJECT_RESET_STATE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameObjectResetState(pkt); return true; });
  bind(Opcode::SMSG_GAMEOBJECT_PAGETEXT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameObjectPageText(pkt); return true; });
  bind(Opcode::SMSG_AREA_TRIGGER_MESSAGE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleAreaTriggerMessage(pkt); return true; });
  bind(Opcode::SMSG_ZONE_UNDER_ATTACK, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleZoneUnderAttack(pkt); return true; });
  bind(Opcode::SMSG_FORCED_DEATH_UPDATE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleForcedDeathUpdate(pkt); return true; });
  bind(Opcode::SMSG_PRE_RESURRECT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePreResurrect(pkt); return true; });
  bind(Opcode::SMSG_FEIGN_DEATH_RESISTED, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleFeignDeathResisted(pkt); return true; });
  bind(Opcode::SMSG_CAMERA_SHAKE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCameraShake(pkt); return true; });
  bind(Opcode::SMSG_PET_TAME_FAILURE, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetTameFailure(pkt); return true; });
  bind(Opcode::SMSG_PET_NAME_INVALID, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetNameInvalid(pkt); return true; });
  bind(Opcode::SMSG_PET_BROKEN, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetBroken(pkt); return true; });
  bind(Opcode::SMSG_PET_ACTION_SOUND, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetActionSound(pkt); return true; });
  bind(Opcode::SMSG_PET_DISMISS_SOUND, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetDismissSound(pkt); return true; });
  bind(Opcode::SMSG_PAUSE_MIRROR_TIMER, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePauseMirrorTimer(pkt); return true; });
  bind(Opcode::SMSG_OVERRIDE_LIGHT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleOverrideLight(pkt); return true; });
  bind(Opcode::SMSG_SET_FORCED_REACTIONS, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetForcedReactions(pkt); return true; });
  bind(Opcode::SMSG_MIRRORIMAGE_DATA, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMirrorImageData(pkt); return true; });
  bind(Opcode::SMSG_MOUNTRESULT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMountResult(pkt); return true; });
  bind(Opcode::SMSG_DISMOUNTRESULT, "control_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDismountResult(pkt); return true; });
  bind(Opcode::SMSG_MOUNTSPECIAL_ANIM, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleMountSpecialAnim(pkt); return true; });
  bind(Opcode::SMSG_FISH_ESCAPED, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleFishEscaped(pkt); return true; });
  bind(Opcode::SMSG_FISH_NOT_HOOKED, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleFishNotHooked(pkt); return true; });
  bind(Opcode::SMSG_BINDER_CONFIRM, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleBinderConfirm(pkt); return true; });
  bind(Opcode::SMSG_BINDZONEREPLY, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleBindZoneReply(pkt); return true; });
  bind(Opcode::SMSG_PLAYERBINDERROR, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayerBindError(pkt); return true; });
  bind(Opcode::SMSG_CROSSED_INEBRIATION_THRESHOLD, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCrossedInebriationThreshold(pkt); return true; });
  bind(Opcode::SMSG_SET_FACTION_ATWAR, "reputation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleSetFactionAtWar(pkt); return true; });
  bind(Opcode::SMSG_PLAYER_SKINNED, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayerSkinned(pkt); return true; });
  bind(Opcode::SMSG_TALENTS_INVOLUNTARILY_RESET, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleTalentsInvoluntarilyReset(pkt); return true; });
  bind(Opcode::SMSG_TOGGLE_XP_GAIN, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleToggleXpGain(pkt); return true; });
  bind(Opcode::SMSG_QUEST_FORCE_REMOVE, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { if (!RequireActivePlayerForQuestDispatch()) return true; HandleQuestForceRemove(pkt); return true; });
  bind(Opcode::SMSG_QUESTGIVER_QUEST_INVALID, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestgiverQuestInvalid(pkt); return true; });
  bind(Opcode::SMSG_QUESTUPDATE_ADD_ITEM, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestUpdateAddItem(pkt); return true; });
  bind(Opcode::SMSG_QUESTUPDATE_FAILEDTIMER, "quest_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQuestUpdateFailedTimer(pkt); return true; });
  bind(Opcode::SMSG_QUERY_QUESTS_COMPLETED_RESPONSE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleQueryQuestsCompleted(pkt); return true; });
  bind(Opcode::SMSG_PET_UNLEARN_CONFIRM, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetUnlearnConfirm(pkt); return true; });
  bind(Opcode::SMSG_STABLE_RESULT, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandleStableResult(pkt); return true; });
  bind(Opcode::SMSG_PET_RENAMEABLE, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetRenameable(pkt); return true; });
  bind(Opcode::SMSG_PET_UPDATE_COMBO_POINTS, "pet_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePetUpdateComboPoints(pkt); return true; });
  bind(Opcode::SMSG_RAID_INSTANCE_MESSAGE, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleRaidInstanceMessage(pkt); return true; });
  bind(Opcode::SMSG_RESET_FAILED_NOTIFY, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleResetFailedNotify(pkt); return true; });
  bind(Opcode::MSG_VIEW_PHASE_SHIFT, "instance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleViewPhaseShift(pkt); return true; });
  bind(Opcode::SMSG_GMTICKET_SYSTEMSTATUS, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMTicketSystemStatus(pkt); return true; });
  bind(Opcode::SMSG_GMTICKET_GETTICKET, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMTicketGetTicket(pkt); return true; });
  bind(Opcode::SMSG_GMTICKET_CREATE, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMTicketCreate(pkt); return true; });
  bind(Opcode::SMSG_GM_TICKET_STATUS_UPDATE, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMTicketStatusUpdate(pkt); return true; });
  bind(Opcode::SMSG_GMRESPONSE_RECEIVED, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMResponseReceived(pkt); return true; });
  bind(Opcode::SMSG_GMRESPONSE_STATUS_UPDATE, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMResponseStatusUpdate(pkt); return true; });
  bind(Opcode::SMSG_KICK_REASON, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleKickReason(pkt); return true; });
  bind(Opcode::SMSG_INVALIDATE_PLAYER, "identity_cache_coordinator", [this](const net::wotlk::WorldPacket& pkt) { HandleInvalidatePlayer(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE_REMOVED, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInviteRemoved(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInviteRemovedAlert(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInviteStatusAlert(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventModeratorStatusAlert(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_REMOVED_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventRemovedAlert(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_UPDATED_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventUpdatedAlert(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_CLEAR_PENDING_ACTION, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarClearPendingAction(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_FILTER_GUILD, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarFilterGuild(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_ARENA_TEAM, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarArenaTeam(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_RAID_LOCKOUT_REMOVED, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarRaidLockoutRemoved(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_RAID_LOCKOUT_UPDATED, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarRaidLockoutUpdated(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE_NOTES, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInviteNotes(pkt); return true; });
  bind(Opcode::SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT, "calendar_event_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCalendarEventInviteNotesAlert(pkt); return true; });
  bind(Opcode::SMSG_ACHIEVEMENT_DELETED, "achievement_session", [this](const net::wotlk::WorldPacket& pkt) { HandleAchievementDeleted(pkt); return true; });
  bind(Opcode::SMSG_NPC_WONT_TALK, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleNpcWontTalk(pkt); return true; });
  bind(Opcode::MSG_DELAY_GHOST_TELEPORT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDelayGhostTeleport(pkt); return true; });
  bind(Opcode::SMSG_CLEAR_FAR_SIGHT_IMMEDIATE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleClearFarSightImmediate(pkt); return true; });
  bind(Opcode::SMSG_CORPSE_MAP_POSITION_QUERY_RESPONSE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCorpseMapPositionResponse(pkt); return true; });
  bind(Opcode::SMSG_CORPSE_NOT_IN_INSTANCE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleCorpseNotInInstance(pkt); return true; });
  bind(Opcode::SMSG_GHOSTEE_GONE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGhosteeGone(pkt); return true; });
  bind(Opcode::SMSG_OPEN_CONTAINER, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleOpenContainer(pkt); return true; });
  bind(Opcode::SMSG_PLAY_TIME_WARNING, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayTimeWarning(pkt); return true; });
  bind(Opcode::SMSG_PROPOSE_LEVEL_GRANT, "refer_a_friend_session", [this](const net::wotlk::WorldPacket& pkt) { HandleProposeLevelGrant(pkt); return true; });
  bind(Opcode::SMSG_REFER_A_FRIEND_EXPIRED, "refer_a_friend_session", [this](const net::wotlk::WorldPacket& pkt) { HandleReferAFriendExpired(pkt); return true; });
  bind(Opcode::SMSG_REFER_A_FRIEND_FAILURE, "refer_a_friend_session", [this](const net::wotlk::WorldPacket& pkt) { HandleReferAFriendFailure(pkt); return true; });
  bind(Opcode::SMSG_INVALID_PROMOTION_CODE, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInvalidPromotionCode(pkt); return true; });
  bind(Opcode::SMSG_WORLD_STATE_UI_TIMER_UPDATE, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleWorldStateTimerUpdate(pkt); return true; });
  bind(Opcode::SMSG_CHANGEPLAYER_DIFFICULTY_RESULT, "world_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleChangeDifficultyResult(pkt); return true; });
  bind(Opcode::SMSG_SET_PLAYER_DECLINED_NAMES_RESULT, "character_presentation_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDeclinedNamesResult(pkt); return true; });
  bind(Opcode::SMSG_GAMETIME_UPDATE, "game_time_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGameTimeUpdate(pkt); return true; });
  bind(Opcode::SMSG_GMRESPONSE_CREATE_TICKET, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMResponseCreateTicket(pkt); return true; });
  bind(Opcode::SMSG_GMRESPONSE_DB_ERROR, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMResponseDbError(pkt); return true; });
  bind(Opcode::SMSG_GMTICKET_DELETETICKET, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMTicketDeleteTicket(pkt); return true; });
  bind(Opcode::SMSG_GMTICKET_UPDATETEXT, "gm_ticket_session", [this](const net::wotlk::WorldPacket& pkt) { HandleGMTicketUpdateText(pkt); return true; });
  bind(Opcode::SMSG_AVAILABLE_VOICE_CHANNEL, "voice_session", [this](const net::wotlk::WorldPacket& pkt) { HandleAvailableVoiceChannel(pkt); return true; });
  bind(Opcode::SMSG_VOICE_SET_TALKER_MUTED, "voice_session", [this](const net::wotlk::WorldPacket& pkt) { HandleVoiceSetTalkerMuted(pkt); return true; });
  bind(Opcode::SMSG_VOICE_PARENTAL_CONTROLS, "voice_session", [this](const net::wotlk::WorldPacket& pkt) { HandleVoiceParentalControls(pkt); return true; });
  bind(Opcode::SMSG_VOICE_CHAT_STATUS, "voice_session", [this](const net::wotlk::WorldPacket& pkt) { HandleVoiceChatStatus(pkt); return true; });
  bind(Opcode::SMSG_VOICE_SESSION_ROSTER_UPDATE, "voice_session", [this](const net::wotlk::WorldPacket& pkt) { HandleVoiceSessionRosterUpdate(pkt); return true; });
  bind(Opcode::SMSG_VOICE_SESSION_LEAVE, "voice_session", [this](const net::wotlk::WorldPacket& pkt) { HandleVoiceSessionLeave(pkt); return true; });
  bind(Opcode::SMSG_NOTIFY_DANCE, "dance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDanceManagement(pkt); return true; });
  bind(Opcode::SMSG_DANCE_QUERY_RESPONSE, "dance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleDanceQueryResponse(pkt); return true; });
  bind(Opcode::SMSG_INVALIDATE_DANCE, "dance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleInvalidateDance(pkt); return true; });
  bind(Opcode::SMSG_LEARNED_DANCE_MOVES, "dance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleLearnedDanceMoves(pkt); return true; });
  bind(Opcode::SMSG_PLAY_DANCE, "dance_session", [this](const net::wotlk::WorldPacket& pkt) { HandlePlayDance(pkt); return true; });
  bind(Opcode::SMSG_STOP_DANCE, "dance_session", [this](const net::wotlk::WorldPacket& pkt) { HandleStopDance(pkt); return true; });
}

}

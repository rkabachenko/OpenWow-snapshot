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
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/combat/application/client_control_transition.h"
#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/activities/dance/adapters/protocol/dance_protocol.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/emote_manager.h"
#include "openwow/game/objects/cggameobject.h"
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
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

void RefreshQuestgiverStatusesAfterQuestFailure(WorldSession &session,
                                                const std::uint32_t quest_id) {
  const auto *entry = session.quests().FindQuestLogEntry(quest_id);
  if (entry == nullptr || entry->status == QuestStatus::kComplete ||
      entry->status == QuestStatus::kRewarded ||
      session.objects().GetLocalPlayerTyped() == nullptr) {
    return;
  }

  session.Send(net::wotlk::PacketSender::BuildQuestgiverStatusMultipleQuery());
}
[[nodiscard]] bool ShouldKeepQuestDialogOpenAfterTurnIn(const WorldSession &session,
                                                         const std::uint32_t quest_id) {
  if (quest_id == 0) {
    return false;
  }

  const auto *quest_template = session.quests().GetTemplate(quest_id);
  return quest_template != nullptr && quest_template->next_quest_in_chain != 0;
}

void SyncQuestNpcUnitToken(const WorldSession &session) {
  const auto &interaction = session.quests().quest_frame_interaction_state();
  const auto guid = interaction.secondary_guid.GetRawValue() != 0
                        ? interaction.secondary_guid.GetRawValue()
                        : interaction.interaction_guid.GetRawValue();
  ui::game::UnitTokenRegistry::Get().SetQuestNpc(guid);
}

void LogQuestDialogTransition(const std::string &what,
                              const std::uint32_t quest_id,
                              const std::uint64_t giver_guid) {
  std::ostringstream line;
  line << what << " quest=" << quest_id << " giver=0x" << std::uppercase
       << std::hex << std::setw(16) << std::setfill('0') << giver_guid;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, line.str());
}

void LogMalformedQuestPacket(const std::string_view opcode,
                             const std::size_t payload_size) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      "quest replication reject malformed " + std::string(opcode) +
          " bytes=" + std::to_string(payload_size));
}

void PublishPendingQuestWatchUpdate(WorldSession &session) {
  const auto quest_id = session.quests().TakeQuestWatchUpdateQuestId();
  if (!quest_id.has_value()) {
    return;
  }

  const int visible_index =
      ui::game::detail::FindVisibleQuestIndexById(session, *quest_id);
  ui::game::ScriptEventDispatch::Get().FireQuestWatchUpdate(visible_index);
}

}

QuestDialogCloseState GetActiveQuestDialogCloseState(const QuestManager &quests) {
  const auto &interaction = quests.quest_frame_interaction_state();
  if (interaction.interaction_guid.GetRawValue() == 0 &&
      interaction.secondary_guid.GetRawValue() == 0) {
    return {};
  }

  QuestDialogCloseState state{
      .interaction_guid = interaction.interaction_guid,
      .secondary_guid = interaction.secondary_guid,
      .quest_id = interaction.quest_id,
      .is_open = true,
      .close_on_decline = false,
  };

  if (quests.has_active_reward()) {
    state.close_on_decline = quests.active_reward().close_on_decline;
  } else if (quests.has_active_request()) {
    state.close_on_decline = quests.active_request().close_on_decline;
  } else if (quests.has_active_details()) {
    state.close_on_decline = quests.active_details().activate_accept;
  }
  return state;
}

void CloseQuestDialogLikeIda58CA70(WorldSession &session, const QuestDialogCloseState &dialog,
                                   const bool keep_dialog_open,
                                   const bool notify_server_for_shared_player_dialog) {
  if (!dialog.is_open) {
    return;
  }

  if (notify_server_for_shared_player_dialog && dialog.interaction_guid.GetRawValue() != 0) {
    if (const auto *object = session.objects().GetObjectByGUID(dialog.interaction_guid);
        object != nullptr && object->IsPlayer()) {
      const auto result_guid = dialog.secondary_guid.GetRawValue() != 0
                                   ? dialog.secondary_guid.GetRawValue()
                                   : dialog.interaction_guid.GetRawValue();
      session.interaction().SendQuestPushResult(result_guid, dialog.quest_id, 3);
    }
  }

  if (keep_dialog_open) {
    return;
  }

  session.quests().CloseQuestFrameInteraction();
  SyncQuestNpcUnitToken(session);
  if (session.gossip().gossip_guid().IsEmpty()) {
    ui::game::HandleNpcInteractionLoss(
        session, dialog.interaction_guid,
        ui::game::NpcInteractionClosureCause::UnitUnavailable);
  }

  LogQuestDialogTransition("quest dialog close", dialog.quest_id,
                           dialog.interaction_guid.GetRawValue());

  ui::game::ScriptEventDispatch::Get().FireQuestFinished();
}

namespace {

constexpr int kQuestInvalidLowLevelSystemMessageId = 151;
constexpr int kQuestInvalidMissingItemsSystemMessageId = 152;
constexpr int kQuestInvalidWrongRaceSystemMessageId = 153;
constexpr int kQuestInvalidNotEnoughMoneySystemMessageId = 154;
constexpr int kQuestInvalidExpansionSystemMessageId = 155;
constexpr int kQuestInvalidOnlyOneTimedSystemMessageId = 156;
constexpr int kQuestInvalidNeedPrereqsSystemMessageId = 157;
constexpr int kQuestInvalidAlreadyOnSystemMessageId = 158;
constexpr int kQuestInvalidAlreadyDoneSystemMessageId = 159;
constexpr int kQuestInvalidAlreadyDoneDailySystemMessageId = 160;
constexpr int kQuestInvalidTooManyDailyQuestsSystemMessageId = 558;
constexpr int kQuestInvalidCaisSystemMessageId = 564;
constexpr int kQuestInvalidDailyQuestLimit = 25;
constexpr int kQuestFailedSystemMessageId = 148;
constexpr int kQuestFailedBagFullSystemMessageId = 149;
constexpr int kQuestFailedMaxCountSystemMessageId = 150;
constexpr int kQuestFailedTooManyDailyQuestsSystemMessageId = 558;
constexpr int kQuestFailedDailyQuestLimit = 25;
constexpr int kInventoryFullSystemMessageId = 0;
constexpr int kQuestForceRemoveSystemMessageId = 0x2D1;

void DisplayQuestgiverQuestInvalidMessage(const std::uint32_t reason) {
  switch (reason) {
  case 1:
    ui::game::DisplaySystemMessage(kQuestInvalidLowLevelSystemMessageId);
    return;
  case 6:
    ui::game::DisplaySystemMessage(kQuestInvalidWrongRaceSystemMessageId);
    return;
  case 7:
    ui::game::DisplaySystemMessage(kQuestInvalidAlreadyDoneSystemMessageId);
    return;
  case 12:
    ui::game::DisplaySystemMessage(kQuestInvalidOnlyOneTimedSystemMessageId);
    return;
  case 13:
  case 18:
    ui::game::DisplaySystemMessage(kQuestInvalidAlreadyOnSystemMessageId);
    return;
  case 16:
    ui::game::DisplaySystemMessage(kQuestInvalidExpansionSystemMessageId);
    return;
  case 21:
    ui::game::DisplaySystemMessage(kQuestInvalidMissingItemsSystemMessageId);
    return;
  case 23:
    ui::game::DisplaySystemMessage(kQuestInvalidNotEnoughMoneySystemMessageId);
    return;
  case 26:
    ui::game::DisplaySystemMessage(kQuestInvalidTooManyDailyQuestsSystemMessageId,
                                   kQuestInvalidDailyQuestLimit);
    return;
  case 27:
    ui::game::DisplaySystemMessage(kQuestInvalidCaisSystemMessageId);
    return;
  case 29:
    ui::game::DisplaySystemMessage(kQuestInvalidAlreadyDoneDailySystemMessageId);
    return;
  default:
    ui::game::DisplaySystemMessage(kQuestInvalidNeedPrereqsSystemMessageId);
    return;
  }
}

void DisplayQuestgiverQuestFailedResolvedMessage(WorldSession &session,
                                                 const std::uint32_t quest_id,
                                                 const std::uint32_t reason,
                                                 const QuestTemplate &quest_template) {
  switch (reason) {
  case 4:
  case 16:
  case 50:
    ui::game::DisplaySystemMessage(kQuestFailedBagFullSystemMessageId,
                                   quest_template.title.c_str());
    ui::game::DisplaySystemMessage(kInventoryFullSystemMessageId);
    return;
  case 17:
    ui::game::DisplaySystemMessage(kQuestFailedMaxCountSystemMessageId,
                                   quest_template.title.c_str());
    return;
  case 74:
    ui::game::DisplaySystemMessage(kQuestFailedTooManyDailyQuestsSystemMessageId,
                                   kQuestFailedDailyQuestLimit);
    return;
  default:
    if (!ui::game::detail::IsVisibleQuestFailedById(session, quest_id)) {
      ui::game::DisplaySystemMessage(kQuestFailedSystemMessageId, quest_template.title.c_str());
    }
    return;
  }
}

void DisplayQuestgiverQuestFailedMessage(WorldSession &session, const QuestFailedInfo &failed) {
  const auto owner = session.lifetime_token();
  const auto *quest_template = session.quests().GetOrRequestTemplate(
      failed.quest_id,
      {.dedupe_callbacks = false, .callback = [owner, &session, failed](const bool success) {
         if (owner.expired()) {
           return;
         }
         if (!success) {
           return;
         }

         const auto *resolved_template = session.quests().GetTemplate(failed.quest_id);
         if (resolved_template == nullptr) {
           return;
         }

         DisplayQuestgiverQuestFailedResolvedMessage(session, failed.quest_id, failed.reason,
                                                     *resolved_template);
       }});
  if (quest_template == nullptr) {
    return;
  }

  DisplayQuestgiverQuestFailedResolvedMessage(session, failed.quest_id, failed.reason,
                                              *quest_template);
}
constexpr int kQuestRewardItemSystemMessage = 162;
constexpr int kQuestRewardMultipleItemsSystemMessage = 163;

void LogQuestRewardPresentationFailure(const std::uint32_t quest_id,
                                       const std::uint32_t item_id,
                                       const std::string_view reason) {
  diagnostics::Log(diagnostics::LogLevel::kWarn,
      "Quest reward presentation failed opcode=SMSG_QUESTGIVER_QUEST_COMPLETE quest=" +
          std::to_string(quest_id) + " item=" + std::to_string(item_id) +
          " reason=" + std::string(reason));
}

void DisplayQuestCompletionChatReward(WorldSession &session,
                                      const std::uint32_t quest_id,
                                      const char *format_key, const int amount,
                                      const int chat_type) {
  const auto format = Localization::Get().GetString(format_key, "");
  if (format.empty()) {
    LogQuestRewardPresentationFailure(quest_id, 0,
        "missing localized format " + std::string(format_key));
    return;
  }
  std::array<char, 3000> text{};
  FormatRuntimeStringTemplateInto(text.data(), text.size(), format.c_str(), amount);
  ChatFrame_DisplayMessage(session.objects(), text.data(), chat_type,
      nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

std::string BuildQuestCompletionItemLink(const std::uint32_t item_id,
                                         const ItemTemplate &item_template) {
  if (item_id == 0 || item_template.name.empty()) {
    return {};
  }

  return HyperlinkParser::BuildItemLink(
      item_id, item_template.name,
      static_cast<std::uint32_t>(item_template.quality),
      0, 0, 0, 0, 0, 0, 0, 0);
}

void DisplayQuestCompletionRewardItemMessage(WorldSession &session, const std::uint32_t item_id,
                                             const std::uint32_t item_count,
                                             const bool include_count) {
  if (item_id == 0) {
    return;
  }

  const auto owner = session.lifetime_token();
  const auto *item_template = session.query_cache().GetOrRequestItemTemplate(
      item_id, QueryCache::QueryRequestOptions{
                   .dedupe_callbacks = false,
                   .callback = [owner, &session, item_id, item_count, include_count](const bool success) {
                     if (owner.expired()) {
                       return;
                     }
                     if (!success || session.query_cache().GetItemTemplate(item_id) == nullptr) {
                       LogQuestRewardPresentationFailure(0, item_id,
                           success ? "item cache callback has no record" : "item query failed");
                       return;
                     }
                     DisplayQuestCompletionRewardItemMessage(session, item_id, item_count,
                                                             include_count);
                   }});
  if (item_template == nullptr) {
    return;
  }

  const auto item_link = BuildQuestCompletionItemLink(item_id, *item_template);
  if (item_link.empty()) {
    LogQuestRewardPresentationFailure(0, item_id, "item record has no display name");
    return;
  }

  if (include_count && item_count > 1) {
    ui::game::DisplaySystemMessage(kQuestRewardMultipleItemsSystemMessage, item_count,
                                   item_link.c_str());
    return;
  }

  ui::game::DisplaySystemMessage(kQuestRewardItemSystemMessage, item_link.c_str());
}

void DisplayQuestCompletionRewards(WorldSession &session, const QuestCompleteInfo complete,
                                    const std::uint32_t selected_reward_item_id) {
  const auto quest_id = complete.quest_id;
  const auto owner = session.lifetime_token();
  const auto *quest_template = session.quests().GetOrRequestTemplate(
      quest_id, QuestManager::QueryRequestOptions{
                    .dedupe_callbacks = false,
                    .callback = [owner, &session, complete, selected_reward_item_id](const bool success) {
                      if (owner.expired()) {
                        return;
                      }
                      const auto quest_id = complete.quest_id;
                      if (!success || session.quests().GetTemplate(quest_id) == nullptr) {
                        LogQuestRewardPresentationFailure(quest_id, 0,
                            success ? "quest cache callback has no record" : "quest query failed");
                        return;
                      }
                      DisplayQuestCompletionRewards(session, complete, selected_reward_item_id);
                    }});
  if (quest_template == nullptr) {
    return;
  }

  if (quest_template->title.empty()) {
    LogQuestRewardPresentationFailure(quest_id, 0, "quest record has no title");
  } else {
    ui::game::DisplaySystemMessage(147, quest_template->title.c_str());
  }
  const int sound_result =
      session.sound_runtime().PlaySoundKitByName("igQuestListComplete");
  diagnostics::Log(
      sound_result == 0 || sound_result == 9 || sound_result == 17
          ? diagnostics::LogLevel::kInfo : diagnostics::LogLevel::kWarn,
      "quest feedback stage=completion source=SMSG_QUESTGIVER_QUEST_COMPLETE quest=" +
          std::to_string(quest_id) + " sound=igQuestListComplete result=" +
          std::to_string(sound_result) + " queued=" + std::to_string(sound_result == 0) +
          " allSound=" + std::to_string(ui::game::CVarSystem::Instance().GetCVarBool("Sound_EnableAllSound")) +
          " sfx=" + std::to_string(ui::game::CVarSystem::Instance().GetCVarBool("Sound_EnableSFX")));
  if (complete.xp_reward != 0) {
    ui::game::DisplaySystemMessage(161, static_cast<std::int32_t>(complete.xp_reward));
  }
  if (static_cast<std::int32_t>(complete.money_reward) > 0) {
    const auto money = MoneyDisplay::FormatLocalizedCoinText(complete.money_reward, ", ");
    if (money.empty()) {
      LogQuestRewardPresentationFailure(quest_id, 0, "money format unavailable");
    } else {
      ui::game::DisplaySystemMessage(164, money.c_str());
    }
  }
  if (static_cast<std::int32_t>(complete.honor_reward) > 0) {
    const auto honor = static_cast<int>(complete.honor_reward / 10u);
    DisplayQuestCompletionChatReward(session, quest_id, "COMBATLOG_HONORAWARD",
                                    honor, ChatDisplayType::kCombatHonor);
    CombatLog_FireCombatTextSD(CombatTextMsgIdx::kHonorGained, honor);
  }
  if (static_cast<std::int32_t>(complete.talent_reward) > 0) {
    DisplayQuestCompletionChatReward(session, quest_id, "LEVEL_UP_CHAR_POINTS",
        static_cast<int>(complete.talent_reward), ChatDisplayType::kMoney);
  }
  if (static_cast<std::int32_t>(complete.arena_points) > 0) {
    DisplayQuestCompletionChatReward(session, quest_id, "COMBATLOG_ARENAPOINTSAWARD",
        static_cast<int>(complete.arena_points), ChatDisplayType::Value(ChatMsg::kArenaPoints));
    CombatLog_FireCombatTextSD(CombatTextMsgIdx::kArenaPointsGained,
                             static_cast<int>(complete.arena_points));
  }

  for (const auto &reward_item : quest_template->reward_items) {
    if (reward_item.item_id == 0) {
      continue;
    }

    DisplayQuestCompletionRewardItemMessage(session, reward_item.item_id, reward_item.count, true);
  }

  if (selected_reward_item_id != 0) {
    DisplayQuestCompletionRewardItemMessage(session, selected_reward_item_id, 1, false);
  }
  const auto dialog = GetActiveQuestDialogCloseState(session.quests());
  const auto keep_dialog_open =
      dialog.is_open && ShouldKeepQuestDialogOpenAfterTurnIn(session, quest_id);
  CloseQuestDialogLikeIda58CA70(session, dialog, keep_dialog_open, false);
}

}

void WorldSession::HandleQuestQueryResponse(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUEST_QUERY_RESPONSE", pkt.payload.size());
  }
}

bool WorldSession::HandleQuestGiverQuestDetails(const net::wotlk::WorldPacket &pkt) {

  static_assert(sizeof(QuestDialogEmoteEntry) == 2 * sizeof(std::uint32_t));
  static_assert(offsetof(QuestDialogEmoteEntry, delay) == 0);
  static_assert(offsetof(QuestDialogEmoteEntry, emote_id) == sizeof(std::uint32_t));

  if (!quests_.HandleQuestGiverQuestDetails(pkt.payload.data(),
                                             pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_QUEST_DETAILS", pkt.payload.size());
    return false;
  }

  if (quests_.has_active_details()) {
    const auto &details = quests_.active_details();
    if (auto *unit = map_runtime_.objects().GetMutableUnit(details.npc_guid)) {
      if (!unit->IsPlayer()) {
        unit->Animation().EmoteQueueHandler(
            reinterpret_cast<const std::uint32_t *>(details.emotes.data()),
            static_cast<std::int32_t>(details.emotes.size()));
      }
    }
  }

  std::string transition = "quest dialog details";
  if (quests_.has_active_details()) {
    const auto &details = quests_.active_details();
    std::ostringstream context;
    context << transition << " flags=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(details.quest_flags) << std::dec
            << " activateAccept=" << (details.activate_accept ? 1 : 0)
            << " startCheat=" << details.accept_packet_value;
    transition = context.str();
  }
  LogQuestDialogTransition(
      transition,
      quests_.has_active_details() ? quests_.active_details().quest_id : 0u,
      quests_.has_active_details()
          ? quests_.active_details().npc_guid.GetRawValue()
          : 0ull);

  ui::game::StoreNpcInteractionTarget(
      *this, quests_.quest_frame_interaction_state().interaction_guid);
  SyncQuestNpcUnitToken(*this);
  ui::game::ScriptEventDispatch::Get().FireQuestDetail();
  return true;
}

bool WorldSession::HandleQuestGiverOfferReward(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestGiverOfferReward(pkt.payload.data(),
                                             pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_OFFER_REWARD", pkt.payload.size());
    return false;
  }

  if (quests_.has_active_reward()) {
    const auto &reward = quests_.active_reward();
    if (auto *unit = map_runtime_.objects().GetMutableUnit(reward.npc_guid)) {
      unit->Animation().EmoteQueueHandler(
          reinterpret_cast<const std::uint32_t *>(reward.emotes.data()),
          static_cast<std::int32_t>(reward.emotes.size()));
    }
  }

  LogQuestDialogTransition(
      "quest dialog offer-reward",
      quests_.has_active_reward() ? quests_.active_reward().quest_id : 0u,
      quests_.has_active_reward()
          ? quests_.active_reward().npc_guid.GetRawValue()
          : 0ull);

  ui::game::StoreNpcInteractionTarget(
      *this, quests_.quest_frame_interaction_state().interaction_guid);
  SyncQuestNpcUnitToken(*this);
  ui::game::ScriptEventDispatch::Get().FireQuestComplete();
  return true;
}

bool WorldSession::HandleQuestGiverRequestItems(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestGiverRequestItems(pkt.payload.data(),
                                            pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_REQUEST_ITEMS", pkt.payload.size());
    return false;
  }

  if (quests_.has_active_request()) {
    const auto &request = quests_.active_request();
    if (auto *unit = map_runtime_.objects().GetMutableUnit(request.npc_guid)) {
      unit->Animation().EmoteQueueHandler(
          reinterpret_cast<const std::uint32_t *>(request.emotes.data()),
          static_cast<std::int32_t>(request.emotes.size()));
    }
  }

  LogQuestDialogTransition(
      "quest dialog request-items",
      quests_.has_active_request() ? quests_.active_request().quest_id : 0u,
      quests_.has_active_request()
          ? quests_.active_request().npc_guid.GetRawValue()
          : 0ull);

  ui::game::StoreNpcInteractionTarget(
      *this, quests_.quest_frame_interaction_state().interaction_guid);
  SyncQuestNpcUnitToken(*this);
  ui::game::ScriptEventDispatch::Get().FireQuestProgress();
  return true;
}

namespace {

constexpr std::uint32_t kNpcFlagQuestGiver = 0x00000002u;

[[nodiscard]] bool IsQuestGiverStatusEligible(const WorldObject &obj) {
  if (obj.IsUnit()) {
    return (static_cast<const CGUnit_C &>(obj).State().GetNpcFlags() &
            kNpcFlagQuestGiver) != 0u;
  }
  if (obj.IsGameObject()) {
    return static_cast<const CGGameObject_C &>(obj).IsQuestGiverType();
  }
  return false;
}

void ApplyQuestgiverServerStatus(WorldObject &obj, const OverlayDisplayType status,
                                 const char *source) {
  if (obj.GetOverlayDisplayType() != status &&
      openwow::diagnostics::IsLogEnabled(openwow::diagnostics::LogLevel::kDebug)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kDebug,
        "NPC interaction status stage=server-state source=" + std::string(source) +
            " guid=" + obj.GetGuid().ToString() +
            " old=" + std::to_string(static_cast<std::uint32_t>(obj.GetOverlayDisplayType())) +
            " new=" + std::to_string(static_cast<std::uint32_t>(status)));
  }
  obj.SetQuestGiverIconStatus(status);
}

}

bool WorldSession::HandleQuestGiverStatus(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestGiverStatus(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_STATUS", pkt.payload.size());
    return false;
  }

  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  ObjectGuid guid;
  std::uint8_t ignored_status = 0;
  if (reader.ReadGuid(guid) && reader.ReadU8(ignored_status)) {
    const auto status = quests_.FindQuestGiverStatus(guid);
    if (status.has_value()) {
      if (auto *obj = map_runtime_.objects().GetMutable(guid)) {

        if (IsQuestGiverStatusEligible(*obj)) {
          ApplyQuestgiverServerStatus(*obj, static_cast<OverlayDisplayType>(
              static_cast<std::uint8_t>(*status)), "SMSG_QUESTGIVER_STATUS");
        }
      }
    }
  }
  return true;
}

bool WorldSession::HandleQuestGiverStatusMultiple(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestGiverStatusMultiple(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_STATUS_MULTIPLE", pkt.payload.size());
    return false;
  }

  map_runtime_.objects().EnumVisibleObjectsMutable([this](WorldObject &obj) {
    if (!IsQuestGiverStatusEligible(obj)) {
      return;
    }

    const auto status = quests_.FindQuestGiverStatus(obj.GetGuid());
    ApplyQuestgiverServerStatus(obj,
        status.has_value()
            ? static_cast<OverlayDisplayType>(
                  static_cast<std::uint8_t>(*status))
            : OverlayDisplayType::kNone, "SMSG_QUESTGIVER_STATUS_MULTIPLE");
  });
  return true;
}

bool WorldSession::HandleQuestUpdateComplete(const net::wotlk::WorldPacket &pkt) {
  if (quests_.HandleQuestUpdateComplete(pkt.payload.data(), pkt.payload.size())) {
    PublishPendingQuestWatchUpdate(*this);
    return true;
  }
  LogMalformedQuestPacket("SMSG_QUESTUPDATE_COMPLETE", pkt.payload.size());
  return false;
}

bool WorldSession::HandleQuestUpdateAddKill(const net::wotlk::WorldPacket &pkt) {
  if (quests_.HandleQuestUpdateAddKill(pkt.payload.data(), pkt.payload.size())) {
    PublishPendingQuestWatchUpdate(*this);
    return true;
  }
  LogMalformedQuestPacket("SMSG_QUESTUPDATE_ADD_KILL", pkt.payload.size());
  return false;
}

void WorldSession::HandleQuestConfirmAccept(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestConfirmAccept(pkt.payload.data(), pkt.payload.size()) ||
      !quests_.has_pending_confirm_accept()) {
    LogMalformedQuestPacket("SMSG_QUEST_CONFIRM_ACCEPT", pkt.payload.size());
    return;
  }

  const auto &prompt = quests_.pending_confirm_accept();
  const std::string sharer_name = ResolveImmediateChatParticipantName(prompt.sharer_guid);
  ui::game::ScriptEventDispatch::Get().FireQuestAcceptConfirm(sharer_name, prompt.title);
}

void WorldSession::HandleQuestLogFull(const net::wotlk::WorldPacket & ) {
  quests_.HandleQuestLogFull();
}

bool WorldSession::HandleQuestGiverQuestComplete(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  QuestCompleteInfo complete{};
  if (!reader.ReadU32(complete.quest_id) ||
      !reader.ReadU32(complete.xp_reward) ||
      !reader.ReadU32(complete.money_reward) ||
      !reader.ReadU32(complete.honor_reward) ||
      !reader.ReadU32(complete.talent_reward) ||
      !reader.ReadU32(complete.arena_points) || reader.Remaining() != 0) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_QUEST_COMPLETE", pkt.payload.size());
    return false;
  }
  last_quest_complete_ = complete;
  const auto quest_id = last_quest_complete_.quest_id;
  const auto selected_reward_item_id = quests_.TakePendingRewardSelectionItem(quest_id);
  DisplayQuestCompletionRewards(*this, complete, selected_reward_item_id);
  return true;
}

bool WorldSession::HandleQuestGiverQuestList(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  QuestListInfo quest_list{};
  if (reader.ReadU64(quest_list.npc_guid) &&
      reader.ReadCString(quest_list.greeting) &&
      reader.ReadU32(quest_list.emote_delay) &&
      reader.ReadU32(quest_list.emote_id)) {
    std::uint8_t count = 0;
    if (reader.ReadU8(count)) {
      quest_list.quests.reserve(count);
      bool parsed = true;
      for (std::uint8_t i = 0; i < count; ++i) {
        QuestListEntry entry{};
        std::uint8_t repeatable = 0;
        if (!reader.ReadU32(entry.quest_id) || !reader.ReadU32(entry.quest_icon) ||
            !reader.ReadI32(entry.quest_level) || !reader.ReadU32(entry.quest_flags) ||
            !reader.ReadU8(repeatable) || !reader.ReadCString(entry.title)) {
          parsed = false;
          break;
        }
        entry.is_repeatable = repeatable != 0;
        quest_list.quests.push_back(std::move(entry));
      }
      if (parsed && reader.Remaining() == 0) {
        last_quest_list_ = std::move(quest_list);
        const ObjectGuid giver(last_quest_list_.npc_guid);
        quests_.ShowQuestGreeting(giver, last_quest_list_.greeting);
        ui::game::StoreNpcInteractionTarget(*this, giver);
        SyncQuestNpcUnitToken(*this);
        LogQuestDialogTransition(
            "quest dialog greeting entries=" +
                std::to_string(last_quest_list_.quests.size()),
            0u, last_quest_list_.npc_guid);
        ui::game::ScriptEventDispatch::Get().FireQuestGreeting();
        return true;
      }
    }
  }
  LogMalformedQuestPacket("SMSG_QUESTGIVER_QUEST_LIST", pkt.payload.size());
  return false;
}

bool WorldSession::HandleQuestPoiQueryResponse(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestPoiQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUEST_POI_QUERY_RESPONSE", pkt.payload.size());
    return false;
  }
  auto &poi_data = QuestPOIData::Get();
  std::vector<std::uint32_t> activeQuestIds;
  activeQuestIds.reserve(quests_.quest_log().size());
  for (const auto &entry : quests_.quest_log()) {
    if (entry.quest_id != 0) {
      activeQuestIds.push_back(entry.quest_id);
    }
  }

  for (const auto &quest_data : quests_.last_poi_response()) {
    pending_quest_poi_queries_.erase(quest_data.quest_id);
    std::vector<QuestPOIEntry> entries;
    entries.reserve(quest_data.pois.size());
    for (const auto &poi_entry : quest_data.pois) {
      QuestPOIEntry entry;
      entry.questId = quest_data.quest_id;
      entry.objectiveIndex = poi_entry.objective_index;
      entry.mapId = poi_entry.map_id;
      entry.areaId = poi_entry.area_id;
      entry.floorId = poi_entry.floor_id;
      entry.flags = poi_entry.unk3;
      entry.priority = poi_entry.unk4;
      entry.points.reserve(poi_entry.points.size());
      for (const auto &point : poi_entry.points) {
        entry.points.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
      }
      entries.push_back(std::move(entry));
    }
    poi_data.ReplaceQuestQueryResult(quest_data.quest_id, std::move(entries), activeQuestIds);
  }
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::QUEST_POI_UPDATE);
  return true;
}

void WorldSession::HandleQuestPushResult(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestPushResult(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("MSG_QUEST_PUSH_RESULT", pkt.payload.size());
  }
}

void WorldSession::HandleQuestGiverQuestFailed(const net::wotlk::WorldPacket &pkt) {
  const auto dialog = GetActiveQuestDialogCloseState(quests_);
  if (!quests_.HandleQuestGiverQuestFailed(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_QUEST_FAILED", pkt.payload.size());
    return;
  }
  DisplayQuestgiverQuestFailedMessage(*this, *quests_.last_quest_failed());
  CloseQuestDialogLikeIda58CA70(*this, dialog, false, false);
}

void WorldSession::HandleQuestUpdateFailed(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestUpdateFailed(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTUPDATE_FAILED", pkt.payload.size());
    return;
  }

  RefreshQuestgiverStatusesAfterQuestFailure(*this, quests_.last_update_failed_quest());
}

void WorldSession::HandleQuestUpdateAddPvpKill(const net::wotlk::WorldPacket &pkt) {
  if (quests_.HandleQuestUpdateAddPvpKill(pkt.payload.data(), pkt.payload.size())) {
    const auto &kill = quests_.last_pvp_kill();
    if (kill.has_value() && quests_.GetTemplate(kill->quest_id) == nullptr) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "quest update references unresolved template "
          "opcode=SMSG_QUESTUPDATE_ADD_PVP_KILL quest=" +
              std::to_string(kill->quest_id));
    }
    PublishPendingQuestWatchUpdate(*this);
    return;
  }
  LogMalformedQuestPacket("SMSG_QUESTUPDATE_ADD_PVP_KILL", pkt.payload.size());
}

void WorldSession::HandleQuestForceRemove(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestForceRemove(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUEST_FORCE_REMOVE", pkt.payload.size());
    return;
  }

  const auto *tmpl = quests_.GetTemplate(quests_.force_remove_quest());
  if (tmpl) {
    ui::game::DisplaySystemMessage(kQuestForceRemoveSystemMessageId,
                                   tmpl->title.c_str());
  }
}

void WorldSession::HandleQuestgiverQuestInvalid(const net::wotlk::WorldPacket &pkt) {
  const auto dialog = GetActiveQuestDialogCloseState(quests_);
  if (!quests_.HandleQuestgiverQuestInvalid(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTGIVER_QUEST_INVALID", pkt.payload.size());
    return;
  }
  DisplayQuestgiverQuestInvalidMessage(quests_.quest_invalid_reason());
  CloseQuestDialogLikeIda58CA70(*this, dialog, false, false);
}

void WorldSession::HandleQuestUpdateAddItem(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestUpdateAddItem(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTUPDATE_ADD_ITEM", pkt.payload.size());
  }
}

void WorldSession::HandleQuestUpdateFailedTimer(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQuestUpdateFailedTimer(pkt.payload.data(), pkt.payload.size())) {
    LogMalformedQuestPacket("SMSG_QUESTUPDATE_FAILEDTIMER", pkt.payload.size());
    return;
  }

  RefreshQuestgiverStatusesAfterQuestFailure(*this, quests_.failed_timer_quest());
}

}

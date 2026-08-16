
#include "openwow/game/world_session.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/voice_chat.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/guild_bank_cursor_utils.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/ui/game/world_state_ui_sync.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "openwow/game/group_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/lfg_system.h"
#include "openwow/game/inventory/loot/loot_state.h"

namespace openwow::game {

void DisplayGroupDifficultyChangedMessagesIfNeeded(const WorldSession &session,
                                                   DungeonDifficulty previous_dungeon,
                                                   RaidDifficulty previous_raid);

namespace {

constexpr std::uint32_t kPackedDungeonIdMask = 0x00FFFFFFu;
constexpr std::uint32_t kLfgAcceptSoundKitId = 17318;
constexpr std::uint32_t kLfgFailureSoundKitId = 17341;
constexpr std::uint32_t kAutoCompletePartyFlag = 0x01u;
constexpr std::uint32_t kAutoCompleteGuildFlag = 0x02u;
constexpr std::uint32_t kAutoCompleteFriendFlag = 0x04u;
constexpr std::uint32_t kAutoCompleteOnlineFlag = 0x20u;
constexpr int kIgnoreNotFoundSystemMessageId = 287;
constexpr int kLootMethodSystemMessageBase = 245;
constexpr int kLootThresholdSystemMessageId = 250;
constexpr int kBgPlayerJoinedSystemMessageId = 486;
constexpr int kBgPlayerLeftSystemMessageId = 487;
constexpr int kBattlefieldPortDeniedSystemMessageId = 135;

constexpr int kArenaTeamSlotCount = 3;
constexpr int kArenaRosterUpdateReason = 1;

constexpr int kBattlegroundInfoThrottledSystemMessageId = 0x293;

constexpr int kArenaTeamChangeFailedQueuedSystemMessageId = 0x295;

constexpr std::uint32_t kRemovedFromPvpQueueReasonGrantLevel = 0u;
constexpr std::uint32_t kRemovedFromPvpQueueReasonXpGain = 1u;
constexpr std::uint32_t kRemovedFromPvpQueueReasonFactionChange = 2u;

constexpr int kRemovedFromPvpQueueGrantLevelSystemMessageId = 0x29A;

constexpr int kRemovedFromPvpQueueXpGainSystemMessageId = 0x29B;

constexpr int kRemovedFromPvpQueueFactionChangeSystemMessageId = 0x2A3;

constexpr std::uint8_t kPvpAfkResultNotifySystemEnabled = 5;
constexpr std::uint8_t kPvpAfkResultNotifySystemDisabled = 6;
constexpr const char *kEnablePvpNotifyAfkCVar = "enablePVPNotifyAFK";
constexpr const char *kPvpReportAfkSystemEnabledKey =
    "PVP_REPORT_AFK_SYSTEM_ENABLED";
constexpr const char *kPvpReportAfkSystemDisabledKey =
    "PVP_REPORT_AFK_SYSTEM_DISABLED";
constexpr std::uint8_t kMasterLootMethod = 2;
constexpr const char *kOptOutLootToggleOnKey = "OPT_OUT_LOOT_TOGGLE_ON";
constexpr const char *kOptOutLootToggleOffKey = "OPT_OUT_LOOT_TOGGLE_OFF";
constexpr std::uint32_t kSocialNoSystemMessage = 730u;

void DispatchSocialApiError(const WorldSession &session,
                            const std::uint32_t message_id) {
  const auto *dbc = session.GetDbcLoader();
  const auto *entry = dbc != nullptr
                          ? dbc->startup_strings().LookupEntry(message_id)
                          : nullptr;
  if (entry == nullptr || entry->text.empty()) {
    return;
  }

  const std::string message(entry->text);
  ui::UIErrorManager::Get().AddErrorMessage(message);
  ui::game::ScriptEventDispatch::Get().FireUiErrorMessage(message);
}

void DisplaySystemChatMessage(const ObjectManager& objects, const std::string& message) {
  if (message.empty()) {
    return;
  }
  ChatFrame_DisplayMessage(objects, message.c_str(), ChatDisplayType::kSystem, nullptr, 0,
                           nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

void ResetArenaRosterRequestsAndNotify(WorldSession& session) {
  session.battleground().ResetAllArenaRosterRequests();
  for (int slot = 0; slot < kArenaTeamSlotCount; ++slot) {
    ui::game::ScriptEventDispatch::Get().FireGlobalEventArgs(
        ui::game::events::ARENA_TEAM_ROSTER_UPDATE, {kArenaRosterUpdateReason});
  }
}

void DispatchBattlefieldListUi(WorldSession &session, const BattlefieldListInfo &list) {
  if (list.bg_type_id == 32 && list.battlemaster_guid != 0) {
    auto battlemaster_guid = list.battlemaster_guid;
    ui::game::SetNpcInteractionTarget(ObjectGuid(battlemaster_guid));
    session.battleground().SetBattlefieldListBattlemasterGuid(battlemaster_guid);
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::NPC_PVPQUEUE_ANYWHERE);
    return;
  }

  if (list.from_where != 0) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PVPQUEUE_ANYWHERE_SHOW);
    return;
  }

  ui::game::ShowBattlefieldList(
      session, ObjectGuid(list.battlemaster_guid));
}

bool GuildEventRefreshesRoster(const GuildEventType type) {
  const auto raw_type = static_cast<std::uint8_t>(type);
  return raw_type < static_cast<std::uint8_t>(GuildEventType::kBankBagSlotsChanged) ||
         raw_type > static_cast<std::uint8_t>(GuildEventType::kBankTextChanged);
}

void NotifyGuildRosterServerRefresh(GuildSystem &guild_system) {
  guild_system.ResetGuildRosterRequestCooldown();
  ui::game::ScriptEventDispatch::Get().FireGuildRosterUpdate(1);
}

std::string ResolveLootThresholdLabel(const std::uint8_t loot_threshold) {
  char key[32]{};
  openwow::core::SStrPrintf(key, sizeof(key), "ITEM_QUALITY%d_DESC", loot_threshold);
  return Localization::Get().GetString(key, key);
}

void DisplayLootMethodSystemMessage(const std::uint8_t loot_method) {
  if (loot_method <= 4) {
    ui::game::DisplaySystemMessage(kLootMethodSystemMessageBase + loot_method);
  }
}

bool HasResolvedRaidRosterName(const WorldSession &session, const std::uint64_t guid_value) {
  if (guid_value == 0) {
    return true;
  }

  const auto guid = ObjectGuid(guid_value);

  if (const auto *player = session.objects().GetPlayer(guid);
      player != nullptr && !player->ResolveRetailName(session).empty()) {
    return true;
  }

  if (const auto *name_info = session.query_cache().GetPlayerName(guid_value);
      name_info != nullptr && !name_info->name.empty()) {
    return true;
  }

  if (const auto *name_entry = session.objects().GetNameEntry(guid);
      name_entry != nullptr && !name_entry->name.empty()) {
    return true;
  }

  return false;
}

std::string ResolveObservedGroupMemberName(const WorldSession &session, const ObjectGuid guid,
                                           std::string_view fallback_name) {
  if (!fallback_name.empty()) {
    return std::string(fallback_name);
  }
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *player = session.objects().GetPlayer(guid)) {
    if (auto name = player->ResolveRetailName(session); !name.empty()) {
      return name;
    }
  }

  if (const auto *name_info = session.query_cache().GetPlayerName(guid.GetRawValue());
      name_info != nullptr && !name_info->name.empty()) {
    return name_info->name;
  }

  if (const auto *name_entry = session.objects().GetNameEntry(guid);
      name_entry != nullptr && !name_entry->name.empty()) {
    return name_entry->name;
  }

  return {};
}

std::uint8_t ResolveObservedGroupMemberClassId(const WorldSession &session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return 0;
  }

  if (const auto *object = session.objects().Get(guid)) {
    if (const auto class_id = openwow::ui::game::detail::GetUnitClass(object); class_id != 0) {
      return class_id;
    }
  }

  if (const auto *name_info = session.query_cache().GetPlayerName(guid.GetRawValue());
      name_info != nullptr && name_info->class_id != 0) {
    return name_info->class_id;
  }

  if (const auto *name_entry = session.objects().GetNameEntry(guid);
      name_entry != nullptr && name_entry->class_id != 0) {
    return name_entry->class_id;
  }

  return 0;
}

void DisplayOptOutOfLootStateMessage(const ObjectManager& objects, const bool opt_out) {
  const char *key = opt_out ? kOptOutLootToggleOnKey : kOptOutLootToggleOffKey;
  const std::string message = Localization::Get().GetString(key, key);
  DisplaySystemChatMessage(objects, message);
}

const LfgRoleCheckPlayer *SelectVisibleRoleCheckMember(const LfgRoleCheckUpdate &role_check,
                                                       ObjectGuid active_player_guid) {
  if (role_check.players.empty()) {
    return nullptr;
  }

  if (!active_player_guid.IsEmpty()) {
    for (const auto &player : role_check.players) {
      if (player.guid == active_player_guid.GetRawValue()) {
        return &player;
      }
    }
  }

  return &role_check.players.front();
}

void ApplyPartyMemberPetGuidToGroupSystem(const PartyMemberStats &stats) {
  if ((stats.update_mask & GroupUpdateFlag::kPetGuid) == 0) {
    return;
  }

  GroupSystem::Get().SetMemberPetGuid(stats.guid.GetRawValue(), stats.pet_guid);
}

bool ShouldRefreshLfgStatusAfterGroupList(const bool was_in_group, const bool was_in_raid,
                                          const GroupManager &group) {
  const bool is_in_group = group.IsInGroup();
  return (!was_in_group && is_in_group) || (!was_in_raid && is_in_group && group.IsRaid());
}

void RefreshLfgStateAfterGroupAcquisition(WorldSession &session) {
  session.lfg().ClearServerInfoSnapshots();
  session.interaction().SendLfgGetStatus();

  if (auto *ui = session.world_ui_runtime()) {
    ui->frame_events().dispatcher().FireEventArgs(
        ui::game::events::LFG_UPDATE, {true});
  }
}

void FireLfgLockAndRandomInfoEvents(WorldSession& session) {
  if (auto *ui = session.world_ui_runtime()) {
    ui->frame_events().dispatcher().FireEvent(
        ui::game::events::LFG_LOCK_INFO_RECEIVED);
    ui->frame_events().dispatcher().FireEvent(
        ui::game::events::LFG_UPDATE_RANDOM_INFO);
  }
}

std::string ResolveActivePlayerName(const WorldSession &session) {
  if (const auto *player = session.objects().GetActivePlayer()) {
    auto name = player->GetPlayerName();
    if (!name.empty()) {
      return name;
    }
  }

  const auto guid = session.objects().GetActivePlayerGuid();
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *cached_name = session.query_cache().GetPlayerName(guid.GetRawValue())) {
    return cached_name->name;
  }

  return session.objects().GetPlayerName(guid);
}

bool ProposalPlayerDeclined(const LfgProposalPlayer &player) {
  return player.has_answered && !player.has_accepted;
}

bool ProposalHasCurrentPlayerDecline(const LfgProposal &proposal) {
  return std::any_of(proposal.players.begin(), proposal.players.end(),
                     [](const LfgProposalPlayer &player) {
                       return player.is_current_player && ProposalPlayerDeclined(player);
                     });
}

bool ProposalHasPartyDecline(const LfgProposal &proposal) {
  return std::any_of(proposal.players.begin(), proposal.players.end(),
                     [](const LfgProposalPlayer &player) {
                       return player.same_group && ProposalPlayerDeclined(player);
                     });
}

bool UpdatePlayerMarksProposalSuccess(const LfgUpdateInfo &update) {
  return update.update_type == LfgUpdateType::kAddedToQueue;
}

bool UpdatePartyMarksProposalSuccess(const LfgUpdateInfo &update) {
  return update.update_type == LfgUpdateType::kLeaderUnk1 ||
         update.update_type == LfgUpdateType::kAddedToQueue ||
         update.update_type == LfgUpdateType::kUnknown16;
}

void FireLfgUpdateEvent(WorldSession& session, const bool refresh_requested) {
  if (auto *ui = session.world_ui_runtime()) {
    ui->frame_events().dispatcher().FireEventArgs(
        ui::game::events::LFG_UPDATE, {refresh_requested});
  }
}

void FireLfgProposalResolutionEvent(WorldSession& session, const bool succeeded) {
  if (auto *ui = session.world_ui_runtime()) {
    ui->frame_events().dispatcher().FireEvent(
        succeeded ? ui::game::events::LFG_PROPOSAL_SUCCEEDED
                                     : ui::game::events::LFG_PROPOSAL_FAILED);
  }
}

bool DidServerLfgInfoChange(const std::optional<LfgUpdateInfo> &previous_update,
                            const LfgUpdateInfo &current_update) {
  return !previous_update.value_or(LfgUpdateInfo{}).MatchesServerSnapshot(current_update);
}

std::string ResolvePlayerName(WorldSession &session, const std::uint64_t guid) {
  const auto snapshot = UnitQueryBridge::Get().GetPlayerInfoByGUID(&session, guid);
  if (!snapshot.has_value()) {
    return {};
  }
  return snapshot->name;
}

std::string ResolveAutoCompletePlayerName(WorldSession &session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *cached_name = session.query_cache().GetPlayerName(guid.GetRawValue())) {
    if (cached_name->name.empty()) {
      return {};
    }
    if (cached_name->realm_name.empty()) {
      return cached_name->name;
    }
    return cached_name->name + "-" + cached_name->realm_name;
  }

  return session.objects().GetPlayerName(guid);
}

void QueueAutoCompleteNameQueryIfNeeded(WorldSession &session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return;
  }
  if (session.query_cache().GetPlayerName(guid.GetRawValue()) != nullptr) {
    return;
  }
  if (!session.objects().GetPlayerName(guid).empty()) {
    return;
  }
  (void)session.query_cache().RequestNameQuery(guid.GetRawValue());
}

std::uint32_t ResolveReadyCheckNowTick(const WorldSession &session) {
  const auto client_time_ms = session.CurrentClientTimeMs();
  return client_time_ms != 0 ? client_time_ms : openwow::core::GameClock::GetTickCount32();
}

std::string ResolveReadyCheckPlayerName(WorldSession &session, const std::uint64_t guid) {
  if (guid == 0) {
    return {};
  }

  if (const auto *cached_name = session.query_cache().GetPlayerName(guid)) {
    if (!cached_name->name.empty()) {
      return cached_name->name;
    }
  }

  return session.objects().GetPlayerName(ObjectGuid(guid));
}

void DisplayReadyCheckChatMessage(const ObjectManager& objects, const std::string &message) {
  DisplaySystemChatMessage(objects, message);
}

void DisplayLocalReadyCheckStartedMessage(const ObjectManager& objects) {
  const auto message = Localization::Get().GetString("READY_CHECK_START", "READY_CHECK_START");
  DisplayReadyCheckChatMessage(objects, message);
}

struct ReadyCheckCompletionSummary {
  bool has_pending = false;
  bool all_ready = true;
  std::vector<std::string> pending_names;
};

std::size_t CountRemovedGroupMembers(const std::vector<GroupMember> &previous_members,
                                     const std::vector<GroupMember> &current_members) {
  std::unordered_set<std::uint64_t> current_guids;
  current_guids.reserve(current_members.size());
  for (const auto &member : current_members) {
    current_guids.insert(member.guid.GetRawValue());
  }

  std::size_t removed_member_count = 0;
  for (const auto &member : previous_members) {
    if (!current_guids.contains(member.guid.GetRawValue())) {
      ++removed_member_count;
    }
  }

  return removed_member_count;
}

bool HasOnlineWaitingReadyCheckMembers(const GroupSystem &group_system) {
  std::unordered_map<std::uint64_t, bool> online_state_by_guid;
  online_state_by_guid.reserve(group_system.GetNumGroupMembers());
  for (std::size_t index = 0; index < group_system.GetNumGroupMembers(); ++index) {
    const auto member = group_system.GetMemberSnapshot(index);
    if (!member.has_value() || member->guid == 0) {
      continue;
    }

    online_state_by_guid.emplace(member->guid, member->is_online);
  }

  if (group_system.IsInRaid()) {
    for (std::size_t index = 0; index < group_system.GetNumGroupMembers(); ++index) {
      const auto member = group_system.GetMemberSnapshot(index);
      if (!member.has_value() || member->guid == 0 || !member->is_online) {
        continue;
      }

      if (group_system.GetTrackedReadyCheckStatus(member->guid) == ReadyCheckQueryResult::Waiting) {
        return true;
      }
    }
    return false;
  }

  for (std::uint32_t slot = 0; slot < group_system.GetTrackedPartyMemberCount(); ++slot) {
    const auto guid = group_system.GetTrackedPartyMemberGuid(slot);
    if (guid == 0) {
      continue;
    }

    const auto online_it = online_state_by_guid.find(guid);
    if (online_it == online_state_by_guid.end() || !online_it->second) {
      continue;
    }

    if (group_system.GetTrackedReadyCheckStatus(guid) == ReadyCheckQueryResult::Waiting) {
      return true;
    }
  }

  return false;
}

ReadyCheckCompletionSummary BuildReadyCheckCompletionSummary(WorldSession &session) {
  auto &group_system = GroupSystem::Get();
  ReadyCheckCompletionSummary summary;

  auto append_guid_status = [&](const std::uint64_t guid) {
    if (guid == 0) {
      return;
    }

    switch (group_system.GetTrackedReadyCheckStatus(guid)) {
    case ReadyCheckQueryResult::Ready:
      return;
    case ReadyCheckQueryResult::Waiting: {
      summary.has_pending = true;
      summary.all_ready = false;
      const auto name = ResolveReadyCheckPlayerName(session, guid);
      if (!name.empty()) {
        summary.pending_names.push_back(name);
      }
      return;
    }
    case ReadyCheckQueryResult::NotReady:
    case ReadyCheckQueryResult::None:
      summary.all_ready = false;
      return;
    }
  };

  if (group_system.IsInRaid()) {
    for (std::size_t index = 0; index < group_system.GetNumGroupMembers(); ++index) {
      const auto member = group_system.GetMemberSnapshot(index);
      if (!member.has_value()) {
        continue;
      }
      append_guid_status(member->guid);
    }
    return summary;
  }

  for (std::uint32_t slot = 0; slot < group_system.GetTrackedPartyMemberCount(); ++slot) {
    append_guid_status(group_system.GetTrackedPartyMemberGuid(slot));
  }
  return summary;
}

void DisplayReadyCheckCompletionMessage(const ObjectManager& objects,
                                        const ReadyCheckCompletionSummary &summary) {
  if (!summary.pending_names.empty()) {
    std::string pending_names = summary.pending_names.front();
    for (std::size_t index = 1; index < summary.pending_names.size(); ++index) {
      pending_names.append(", ");
      pending_names.append(summary.pending_names[index]);
    }

    const auto format = Localization::Get().GetString("RAID_MEMBERS_AFK", "RAID_MEMBERS_AFK");
    DisplayReadyCheckChatMessage(objects,
                                 Localization::Get().FormatString(format, {pending_names}));
    return;
  }

  const auto message_key = summary.all_ready ? "READY_CHECK_ALL_READY" : "READY_CHECK_FINISHED";
  DisplayReadyCheckChatMessage(objects,
                               Localization::Get().GetString(message_key, message_key));
}

void TouchRecentPlayerAutoCompleteTarget(WorldSession &session, const ObjectGuid guid,
                                         const std::uint32_t base_flag, const bool online,
                                         std::string_view fallback_name,
                                         const bool update_timestamp) {
  if (guid.IsEmpty()) {
    return;
  }

  std::string display_name = ResolveAutoCompletePlayerName(session, guid);
  if (display_name.empty()) {
    display_name.assign(fallback_name);
  }

  auto &autocomplete = ui::game::AutoComplete::Get();
  autocomplete.TouchRecentPlayerGuid(guid.GetRawValue(),
                                     base_flag | (online ? kAutoCompleteOnlineFlag : 0u),
                                     update_timestamp, display_name);
  if (!online) {
    autocomplete.ClearRecentPlayerGuidContextBits(guid.GetRawValue(), kAutoCompleteOnlineFlag);
  }
  if (display_name.empty()) {
    QueueAutoCompleteNameQueryIfNeeded(session, guid);
  }
}

void RefreshFriendAutoCompleteTargets(WorldSession &session,
                                      const std::vector<const ContactInfo *> &friends) {
  for (const ContactInfo *friend_contact : friends) {
    if (friend_contact == nullptr) {
      continue;
    }
    TouchRecentPlayerAutoCompleteTarget(session, friend_contact->guid, kAutoCompleteFriendFlag,
                                        friend_contact->status != FriendStatus::kOffline, {},
                                        false);
  }
}

void RefreshPartyAutoCompleteTargets(WorldSession &session,
                                     const std::vector<GroupMember> &previous_members,
                                     const GroupManager &current_group) {
  auto &autocomplete = ui::game::AutoComplete::Get();

  for (const auto &previous_member : previous_members) {
    if (previous_member.guid.IsEmpty()) {
      continue;
    }

    const auto still_present =
        std::any_of(current_group.members().begin(), current_group.members().end(),
                    [&](const GroupMember &current_member) {
                      return current_member.guid == previous_member.guid;
                    });
    if (!still_present) {
      autocomplete.ClearRecentPlayerGuidContextBits(
          previous_member.guid.GetRawValue(), kAutoCompletePartyFlag | kAutoCompleteOnlineFlag);
    }
  }

  for (const auto &member : current_group.members()) {
    TouchRecentPlayerAutoCompleteTarget(session, member.guid, kAutoCompletePartyFlag,
                                        (member.online_status & 0x01u) != 0, member.name, false);
  }
}

void RefreshGuildAutoCompleteTargets(WorldSession &session, const GuildRoster &roster) {
  for (const auto &member : roster.members) {
    TouchRecentPlayerAutoCompleteTarget(session, member.guid, kAutoCompleteGuildFlag,
                                        member.status != 0, member.name, false);
  }
}

bool HasRaidMemberGuid(const std::vector<GroupMember> &members, const std::uint64_t guid) {
  return std::any_of(members.begin(), members.end(), [guid](const GroupMember &member) {
    return member.guid.GetRawValue() == guid;
  });
}

void AnnounceRaidRosterChanges(const std::vector<GroupMember> &previous_raid_members,
                               const std::uint8_t previous_group_type,
                               const GroupManager &current_group,
                               const ObjectGuid active_player_guid) {
  if (!current_group.IsRaid()) {
    return;
  }

  if (previous_raid_members.empty()) {
    ui::game::DisplaySystemMessage(437);
  } else {
    for (const auto &member : current_group.members()) {
      const auto guid = member.guid.GetRawValue();
      if (guid == 0 || member.name.empty() || HasRaidMemberGuid(previous_raid_members, guid)) {
        continue;
      }

      ui::game::DisplaySystemMessage(439, member.name.c_str());
    }
  }

  if (previous_group_type != current_group.group_type() || active_player_guid.IsEmpty()) {
    return;
  }

  for (const auto &member : previous_raid_members) {
    const auto guid = member.guid.GetRawValue();
    if (guid == 0 || member.name.empty() || HasRaidMemberGuid(current_group.members(), guid)) {
      continue;
    }

    ui::game::DisplaySystemMessage(440, member.name.c_str());
  }
}

void FireLfgRoleChosenEvent(WorldSession &session, const std::uint64_t guid,
                            const std::uint32_t roles) {
  auto *ui = session.world_ui_runtime();
  if (ui == nullptr) {
    return;
  }

  const auto name = ResolvePlayerName(session, guid);
  if (name.empty()) {
    return;
  }

  ui->frame_events().dispatcher().FireEventArgs(
      ui::game::events::LFG_ROLE_CHECK_ROLE_CHOSEN,
                             {name, (roles & kLfgRoleTank) != 0, (roles & kLfgRoleHealer) != 0,
                              (roles & kLfgRoleDps) != 0});
}

const data::dbc::LfgDungeonsEntry *FindLfgDungeonEntry(const data::dbc::DbcLoader *dbc,
                                                       const std::uint32_t packed_dungeon_id) {
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto dungeon_id = packed_dungeon_id & kPackedDungeonIdMask;
  for (const auto &entry : dbc->lfg_dungeons()) {
    if (entry.id == dungeon_id) {
      return &entry;
    }
  }

  return nullptr;
}

void DisplayLfgTeleportDeniedMessage(const std::uint32_t error_code) {
  switch (error_code) {
  case 1:
    ui::game::DisplaySystemMessage(135);
    return;
  case 2:
    ui::game::DisplaySystemMessage(662);
    return;
  case 4:
    ui::game::DisplaySystemMessage(663);
    return;
  case 6:
    ui::game::DisplaySystemMessage(711);
    ui::game::DisplaySystemMessage(136);
    return;
  case 0:
  case 5:
  case 7:
    return;
  default:
    ui::game::DisplaySystemMessage(136);
    return;
  }
}

std::uint32_t ResolveArenaTeamCommandMessageId(const ArenaCommandResult &result,
                                               std::vector<std::string> &args_out) {
  args_out.clear();

  if (result.error_type == 0) {

    switch (result.action) {
    case 0:

      args_out = {result.player_name};
      return 517;
    case 1:

      args_out = {result.team_name, result.player_name};
      return 518;
    case 3:

      args_out = {result.player_name};
      return 519;
    case 14:

      args_out = {result.player_name};
      return 520;
    default:
      return 0;
    }
  }

  switch (result.error_type) {
  case 1:  return 521;
  case 2:  return 522;
  case 3:

    args_out = {result.team_name};
    return 523;
  case 4:  return 524;
  case 5:

    args_out = {result.team_name};
    return 525;
  case 6:  return 526;
  case 7:

    args_out = {result.player_name};
    return 527;
  case 8:

    if (result.action == 3) {
      args_out = {result.player_name};
      return 528;
    }
    return 529;
  case 9:  return 530;
  case 10:

    args_out = {result.team_name, result.player_name};
    return 531;
  case 11:

    args_out = {result.team_name};
    return 532;
  case 12: return 533;
  case 19:

    args_out = {result.team_name};
    return 336;
  case 21:

    args_out = {result.team_name};
    return 541;
  case 22:

    args_out = {result.team_name};
    return 542;
  case 23:

    args_out = {result.player_name};
    return 543;
  case 27: return 545;
  case 30: return 546;
  default: return 0;
  }
}

void DisplayArenaTeamCommandMessage(const ArenaCommandResult &result) {
  std::vector<std::string> message_args;
  const auto message_id =
      ResolveArenaTeamCommandMessageId(result, message_args);

  if (message_id == 0) return;

  switch (message_args.size()) {
  case 0:
    ui::game::DisplaySystemMessage(static_cast<int>(message_id));
    return;
  case 1:
    ui::game::DisplaySystemMessage(static_cast<int>(message_id), message_args[0].c_str());
    return;
  case 2:
    ui::game::DisplaySystemMessage(static_cast<int>(message_id), message_args[0].c_str(),
                                   message_args[1].c_str());
    return;
  default:
    ui::game::DisplaySystemMessage(static_cast<int>(message_id), message_args[0].c_str(),
                                   message_args[1].c_str(), message_args[2].c_str());
    return;
  }
}

std::unordered_set<std::uint64_t> CollectIgnoredGuids(const SocialManager &social) {
  std::unordered_set<std::uint64_t> ignored_guids;
  for (const ContactInfo *contact : social.GetIgnored()) {
    if (contact == nullptr || contact->guid.IsEmpty()) {
      continue;
    }
    ignored_guids.insert(contact->guid.GetRawValue());
  }
  return ignored_guids;
}

std::string FormatSocialDisplayName(const PlayerNameInfo &name_info) {
  if (name_info.realm_name.empty()) {
    return name_info.name;
  }

  return name_info.name + "-" + name_info.realm_name;
}

std::string ResolveCachedSocialDisplayName(const WorldSession &session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *name_info = session.query_cache().GetPlayerName(guid.GetRawValue());
      name_info != nullptr && !name_info->name.empty()) {
    return FormatSocialDisplayName(*name_info);
  }

  if (const auto *contact = session.social().FindContact(guid);
      contact != nullptr && !contact->display_name.empty()) {
    return contact->display_name;
  }

  return session.objects().GetPlayerName(guid);
}

std::string ResolveStoredSocialDisplayName(const WorldSession &session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *name_info = session.query_cache().GetPlayerName(guid.GetRawValue());
      name_info != nullptr && !name_info->name.empty()) {
    return FormatSocialDisplayName(*name_info);
  }

  if (const auto *contact = session.social().FindContact(guid);
      contact != nullptr && !contact->display_name.empty()) {
    return contact->display_name;
  }

  return {};
}

const ContactInfo *FindContactByNameForRemoval(WorldSession &session,
                                               const std::string &name,
                                               const SocialFlag list) {
  const auto contacts = list == SocialFlag::kFriend
                            ? session.social().GetFriends()
                            : (list == SocialFlag::kIgnored
                                   ? session.social().GetIgnored()
                                   : session.social().GetMuted());
  for (const ContactInfo *contact : contacts) {
    if (contact == nullptr || contact->guid.IsEmpty()) {
      continue;
    }

    const std::string display_name =
        list == SocialFlag::kMuted
            ? ResolveStoredSocialDisplayName(session, contact->guid)
            : ResolveCachedSocialDisplayName(session, contact->guid);
    if (!display_name.empty() &&
        openwow::core::SStrCmpUTF8NoCase(display_name.c_str(), name.c_str(),
                                        0x7FFFFFFF) == 0) {
      return contact;
    }
  }

  return nullptr;
}
void DisplayNamedSocialSystemMessage(const std::uint32_t message_id, const std::string &name) {
  if (message_id == kSocialNoSystemMessage) {
    return;
  }

  ui::game::DisplaySystemMessage(static_cast<int>(message_id), name.c_str(), name.c_str());
}

}

std::vector<GroupSystemMember> WorldSession::BuildObservedGroupSystemMembers(
    bool *has_deferred_local_raid_member) const {
  if (has_deferred_local_raid_member != nullptr) {
    *has_deferred_local_raid_member = false;
  }

  std::vector<GroupSystemMember> observed_members;
  observed_members.reserve(group_.members().size() + (group_.IsRaid() ? 1u : 0u));

  for (const auto &member : group_.members()) {
    GroupSystemMember observed_member;
    observed_member.guid = member.guid.GetRawValue();
    observed_member.name = ResolveObservedGroupMemberName(*this, member.guid, member.name);
    observed_member.group_index = member.sub_group;
    observed_member.flags = member.flags;
    observed_member.is_online = (member.online_status & 0x01u) != 0;
    observed_member.online_status = member.online_status;
    observed_member.class_id = ResolveObservedGroupMemberClassId(*this, member.guid);
    observed_member.role = member.roles;
    observed_members.push_back(std::move(observed_member));
  }

  if (!group_.IsRaid()) {
    return observed_members;
  }

  const auto active_player_guid = objects().GetActivePlayerGuid();
  if (!active_player_guid.IsEmpty()) {
    const auto explicit_self_it =
        std::find_if(observed_members.begin(), observed_members.end(),
                     [active_player_guid](const GroupSystemMember &member) {
                       return member.guid == active_player_guid.GetRawValue();
                     });
    if (explicit_self_it != observed_members.end()) {
      return observed_members;
    }
  }

  GroupSystemMember self_member;
  self_member.guid = active_player_guid.GetRawValue();
  self_member.name = ResolveObservedGroupMemberName(*this, active_player_guid, {});
  self_member.group_index = group_.my_sub_group();
  self_member.flags = group_.my_flags();
  self_member.is_online = true;
  self_member.online_status = 0x01u;
  self_member.class_id = ResolveObservedGroupMemberClassId(*this, active_player_guid);
  self_member.role = group_.my_roles();
  observed_members.push_back(std::move(self_member));

  if (has_deferred_local_raid_member != nullptr && active_player_guid.IsEmpty()) {
    *has_deferred_local_raid_member = true;
  }

  return observed_members;
}

void WorldSession::SyncObservedGroupStateToGroupSystem() {
  auto &group_system = GroupSystem::Get();
  group_system.SetLocalPlayerGuid(objects().GetActivePlayerGuid());

  bool has_deferred_local_raid_member = false;
  const auto observed_members = BuildObservedGroupSystemMembers(&has_deferred_local_raid_member);
  group_system.SetGroupData(observed_members, group_.leader_guid().GetRawValue(), group_.loot_method(),
                            group_.master_looter_guid(), group_.loot_threshold(), group_.IsRaid(),
                            group_.my_sub_group(), objects().GetActivePlayerGuid().GetRawValue());
  pending_raid_roster_local_player_resolution_ = has_deferred_local_raid_member;
}

void WorldSession::DeleteIgnoredContactByName(const std::string &name) {
  if (name.empty()) {
    return;
  }

  if (const auto *contact =
          FindContactByNameForRemoval(*this, name, SocialFlag::kIgnored);
      contact != nullptr) {
    interaction_.SendDelIgnore(contact->guid.GetRawValue());
    return;
  }

  DisplaySocialApiError(kIgnoreNotFoundSystemMessageId);
}

void WorldSession::DisplaySocialApiError(const std::uint32_t message_id) const {
  DispatchSocialApiError(*this, message_id);
}

void WorldSession::DeleteFriendContactByName(const std::string &name) {
  if (const auto *contact =
          FindContactByNameForRemoval(*this, name, SocialFlag::kFriend);
      contact != nullptr) {
    interaction_.SendDelFriend(contact->guid.GetRawValue());
    return;
  }

  DisplaySocialApiError(278);
}

void WorldSession::DeleteMutedContactByName(const std::string &name) {
  if (const auto *contact =
          FindContactByNameForRemoval(*this, name, SocialFlag::kMuted);
      contact != nullptr) {
    interaction_.SendDelMute(contact->guid.GetRawValue());
    return;
  }

  DisplaySocialApiError(568);
}

void WorldSession::HandleContactList(const net::wotlk::WorldPacket &pkt) {
  PacketReader header_reader(pkt.payload.data(), pkt.payload.size());
  std::uint32_t requested_flags = 0;
  if (!header_reader.ReadU32(requested_flags)) {
    return;
  }

  std::vector<std::uint64_t> previous_friend_guids;
  previous_friend_guids.reserve(social_.GetFriends().size());
  for (const ContactInfo *contact : social_.GetFriends()) {
    if (contact != nullptr && !contact->guid.IsEmpty()) {
      previous_friend_guids.push_back(contact->guid.GetRawValue());
    }
  }

  if (!social_.HandleContactList(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto clear_contact_list_resolutions = [this](const PendingSocialListKind list_kind) {
    pending_social_name_resolutions_.erase(
        std::remove_if(pending_social_name_resolutions_.begin(),
                       pending_social_name_resolutions_.end(),
                       [list_kind](const PendingSocialNameResolution &resolution) {
                         return resolution.list_kind == list_kind &&
                                resolution.message_id == kSocialNoSystemMessage;
                       }),
        pending_social_name_resolutions_.end());
  };
  const auto rebuild_pending_name_queries = [this](const PendingSocialListKind list_kind) {
    auto &pending =
        list_kind == PendingSocialListKind::kFriend
            ? pending_friend_name_queries_
            : (list_kind == PendingSocialListKind::kIgnore ? pending_ignore_name_queries_
                                                           : pending_mute_name_queries_);
    pending.clear();
    for (const PendingSocialNameResolution &resolution : pending_social_name_resolutions_) {
      if (resolution.list_kind == list_kind) {
        pending.insert(resolution.guid);
      }
    }
  };
  const auto has_pending_refresh = [this](const PendingSocialListKind list_kind) {
    return std::any_of(
        pending_social_name_resolutions_.begin(), pending_social_name_resolutions_.end(),
        [list_kind](const PendingSocialNameResolution &resolution) {
          return resolution.list_kind == list_kind && resolution.refresh_visible_list;
        });
  };

  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kFriend)) != 0) {
    clear_contact_list_resolutions(PendingSocialListKind::kFriend);
    rebuild_pending_name_queries(PendingSocialListKind::kFriend);
  }
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kIgnored)) != 0) {
    clear_contact_list_resolutions(PendingSocialListKind::kIgnore);
    rebuild_pending_name_queries(PendingSocialListKind::kIgnore);
  }
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kMuted)) != 0) {
    clear_contact_list_resolutions(PendingSocialListKind::kMute);
    rebuild_pending_name_queries(PendingSocialListKind::kMute);
  }

  auto queue_contact_names = [this](const std::vector<const ContactInfo *> &contacts,
                                    const PendingSocialListKind list_kind) {
    auto &pending =
        list_kind == PendingSocialListKind::kFriend
            ? pending_friend_name_queries_
            : (list_kind == PendingSocialListKind::kIgnore ? pending_ignore_name_queries_
                                                           : pending_mute_name_queries_);

    for (const ContactInfo *contact : contacts) {
      if (contact == nullptr || contact->guid.IsEmpty()) {
        continue;
      }

      const std::string display_name = ResolveCachedSocialDisplayName(*this, contact->guid);
      if (!display_name.empty()) {
        social_.SetDisplayName(contact->guid, display_name);
        continue;
      }

      pending.insert(contact->guid.GetRawValue());
      pending_social_name_resolutions_.push_back({.guid = contact->guid.GetRawValue(),
                                                  .list_kind = list_kind,
                                                  .message_id = kSocialNoSystemMessage,
                                                  .refresh_visible_list = true});
      (void)query_cache_.RequestNameQuery(contact->guid.GetRawValue());
    }
  };

  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kFriend)) != 0) {
    queue_contact_names(social_.GetFriends(), PendingSocialListKind::kFriend);
  }
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kIgnored)) != 0) {
    queue_contact_names(social_.GetIgnored(), PendingSocialListKind::kIgnore);
  }
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kMuted)) != 0) {
    queue_contact_names(social_.GetMuted(), PendingSocialListKind::kMute);
  }

  const auto current_friends = social_.GetFriends();
  RefreshFriendAutoCompleteTargets(*this, current_friends);
  auto &autocomplete = ui::game::AutoComplete::Get();
  for (const auto raw_guid : previous_friend_guids) {
    const auto still_friend = std::any_of(
        current_friends.begin(), current_friends.end(), [&](const ContactInfo *contact) {
          return contact != nullptr && contact->guid.GetRawValue() == raw_guid;
        });
    if (!still_friend) {
      autocomplete.ClearRecentPlayerGuidContextBits(raw_guid, kAutoCompleteFriendFlag |
                                                                  kAutoCompleteOnlineFlag);
    }
  }
  if (CalendarSystem::Get().RefreshPendingInviteVisibility(CollectIgnoredGuids(social_))) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }
  RefreshWatchedChannelRosterLocalMuteFlags();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Received contact list: " + std::to_string(social_.contact_count()) +
                         " contacts");
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kFriend)) != 0 &&
      !has_pending_refresh(PendingSocialListKind::kFriend)) {
    ui::game::ScriptEventDispatch::Get().FireFriendListUpdate();
  }
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kIgnored)) != 0 &&
      !has_pending_refresh(PendingSocialListKind::kIgnore)) {
    ui::game::ScriptEventDispatch::Get().FireIgnoreListUpdate();
  }
  if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kMuted)) != 0 &&
      !has_pending_refresh(PendingSocialListKind::kMute)) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::MUTELIST_UPDATE);
  }
}

void WorldSession::HandleFriendStatus(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint8_t result_raw = 0;
  std::uint64_t raw_guid = 0;
  if (!reader.ReadU8(result_raw) || !reader.ReadU64(raw_guid)) {
    return;
  }

  const ObjectGuid guid(raw_guid);
  const bool was_friend_delete_pending = social_.IsDeletePending(guid, SocialFlag::kFriend);
  const bool was_ignore_delete_pending = social_.IsDeletePending(guid, SocialFlag::kIgnored);
  const bool was_mute_delete_pending = social_.IsDeletePending(guid, SocialFlag::kMuted);

  if (!social_.HandleFriendStatus(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto queue_named_result = [this, guid, raw_guid](const PendingSocialListKind list_kind,
                                                   const std::uint32_t message_id,
                                                   const bool refresh_visible_list) {
    const std::string display_name = ResolveCachedSocialDisplayName(*this, guid);
    if (!display_name.empty()) {
      social_.SetDisplayName(guid, display_name);
      if (refresh_visible_list) {
        switch (list_kind) {
        case PendingSocialListKind::kFriend:
          ui::game::ScriptEventDispatch::Get().FireFriendListUpdate();
          break;
        case PendingSocialListKind::kIgnore:
          ui::game::ScriptEventDispatch::Get().FireIgnoreListUpdate();
          break;
        case PendingSocialListKind::kMute:
          ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::MUTELIST_UPDATE);
          break;
        }
      }
      DisplayNamedSocialSystemMessage(message_id, display_name);
      return;
    }

    switch (list_kind) {
    case PendingSocialListKind::kFriend:
      pending_friend_name_queries_.insert(raw_guid);
      break;
    case PendingSocialListKind::kIgnore:
      pending_ignore_name_queries_.insert(raw_guid);
      break;
    case PendingSocialListKind::kMute:
      pending_mute_name_queries_.insert(raw_guid);
      break;
    }

    pending_social_name_resolutions_.push_back({.guid = raw_guid,
                                                .list_kind = list_kind,
                                                .message_id = message_id,
                                                .refresh_visible_list = refresh_visible_list});
    (void)query_cache_.RequestNameQuery(raw_guid);
  };

  auto &autocomplete = ui::game::AutoComplete::Get();
  switch (result_raw) {
  case 0x00:
  case 0x04:
    ui::game::DisplaySystemMessage(278);
    break;
  case 0x01:
    ui::game::DisplaySystemMessage(274);
    break;
  case 0x02:
    if (const auto *contact = social_.FindContact(guid);
        contact != nullptr && HasSocialFlag(contact->flags, SocialFlag::kFriend)) {
      TouchRecentPlayerAutoCompleteTarget(*this, guid, kAutoCompleteFriendFlag,
                                          contact->status != FriendStatus::kOffline, {}, false);
    }
    queue_named_result(PendingSocialListKind::kFriend, 276, true);
    break;
  case 0x03:
    autocomplete.ClearRecentPlayerGuidContextBits(guid.GetRawValue(), kAutoCompleteOnlineFlag);
    queue_named_result(PendingSocialListKind::kFriend, 277, true);
    break;
  case 0x05:
    autocomplete.ClearRecentPlayerGuidContextBits(guid.GetRawValue(), kAutoCompleteFriendFlag |
                                                                          kAutoCompleteOnlineFlag);
    if (was_friend_delete_pending) {
      ui::game::DisplaySystemMessage(284);
    } else {
      queue_named_result(PendingSocialListKind::kFriend, 280, true);
    }
    break;
  case 0x06:
  case 0x07:
    TouchRecentPlayerAutoCompleteTarget(*this, guid, kAutoCompleteFriendFlag, result_raw == 0x06,
                                        {}, false);
    queue_named_result(PendingSocialListKind::kFriend, 275, true);
    break;
  case 0x08:
    queue_named_result(PendingSocialListKind::kFriend, 282, true);
    break;
  case 0x09:
    ui::game::DisplaySystemMessage(283);
    break;
  case 0x0A:
    ui::game::DisplaySystemMessage(279);
    break;
  case 0x0B:
    ui::game::DisplaySystemMessage(285);
    break;
  case 0x0C:
    ui::game::DisplaySystemMessage(286);
    break;
  case 0x0D:
    ui::game::DisplaySystemMessage(kIgnoreNotFoundSystemMessageId);
    break;
  case 0x0E:
    queue_named_result(PendingSocialListKind::kIgnore, 288, true);
    break;
  case 0x0F:
    queue_named_result(PendingSocialListKind::kIgnore, 289, true);
    break;
  case 0x10:
    if (was_ignore_delete_pending) {
      ui::game::DisplaySystemMessage(292);
    } else {
      queue_named_result(PendingSocialListKind::kIgnore, 290, true);
    }
    break;
  case 0x11:
    ui::game::DisplaySystemMessage(291);
    break;
  case 0x12:
    ui::game::DisplaySystemMessage(566);
    break;
  case 0x13:
    ui::game::DisplaySystemMessage(567);
    break;
  case 0x14:
    ui::game::DisplaySystemMessage(568);
    break;
  case 0x15:
    queue_named_result(PendingSocialListKind::kMute, 569, true);
    break;
  case 0x16:
    queue_named_result(PendingSocialListKind::kMute, 570, true);
    break;
  case 0x17:
    if (was_mute_delete_pending) {
      ui::game::DisplaySystemMessage(573);
    } else {
      queue_named_result(PendingSocialListKind::kMute, 571, true);
    }
    break;
  case 0x18:
    ui::game::DisplaySystemMessage(572);
    break;
  case 0x19:
    break;
  case 0x1A:
  case 0x1B:
    if (const auto *contact = social_.FindContact(guid);
        contact != nullptr && HasSocialFlag(contact->flags, SocialFlag::kFriend)) {
      TouchRecentPlayerAutoCompleteTarget(*this, guid, kAutoCompleteFriendFlag,
                                          contact->status != FriendStatus::kOffline, {}, false);
    }
    ui::game::ScriptEventDispatch::Get().FireFriendListUpdate();
    break;
  default:
    ui::game::DisplaySystemMessage(281);
    break;
  }

  if (CalendarSystem::Get().RefreshPendingInviteVisibility(CollectIgnoredGuids(social_))) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }
  RefreshWatchedChannelRosterLocalMuteFlags();
}

void WorldSession::ResolvePendingSocialNameQueries(const std::uint64_t guid,
                                                   const bool name_unknown) {
  if (guid == 0) {
    return;
  }

  const ObjectGuid object_guid(guid);
  const std::string display_name =
      !name_unknown ? ResolveCachedSocialDisplayName(*this, object_guid) : std::string{};
  const auto has_pending_refresh = [this](const PendingSocialListKind list_kind) {
    return std::any_of(
        pending_social_name_resolutions_.begin(), pending_social_name_resolutions_.end(),
        [list_kind](const PendingSocialNameResolution &resolution) {
          return resolution.list_kind == list_kind && resolution.refresh_visible_list;
        });
  };
  const bool friend_refresh_was_pending = has_pending_refresh(PendingSocialListKind::kFriend);
  const bool ignore_refresh_was_pending = has_pending_refresh(PendingSocialListKind::kIgnore);
  const bool mute_refresh_was_pending = has_pending_refresh(PendingSocialListKind::kMute);

  pending_friend_name_queries_.erase(guid);
  pending_ignore_name_queries_.erase(guid);
  pending_mute_name_queries_.erase(guid);

  auto it = pending_social_name_resolutions_.begin();
  while (it != pending_social_name_resolutions_.end()) {
    if (it->guid != guid) {
      ++it;
      continue;
    }

    if (!display_name.empty()) {
      social_.SetDisplayName(object_guid, display_name);
      DisplayNamedSocialSystemMessage(it->message_id, display_name);
    } else {
      if (it->message_id != kSocialNoSystemMessage) {
        ui::game::DisplaySystemMessage(281);
      }

      switch (it->list_kind) {
      case PendingSocialListKind::kFriend:
        social_.MarkDeletePending(object_guid, SocialFlag::kFriend);
        interaction_.SendDelFriend(guid);
        ui::game::AutoComplete::Get().ClearRecentPlayerGuidContextBits(
            guid, kAutoCompleteFriendFlag | kAutoCompleteOnlineFlag);
        break;
      case PendingSocialListKind::kIgnore:
        social_.MarkDeletePending(object_guid, SocialFlag::kIgnored);
        interaction_.SendDelIgnore(guid);
        break;
      case PendingSocialListKind::kMute:
        social_.MarkDeletePending(object_guid, SocialFlag::kMuted);
        interaction_.SendDelMute(guid);
        break;
      }
    }

    it = pending_social_name_resolutions_.erase(it);
  }

  const bool fire_friend_update =
      friend_refresh_was_pending && !has_pending_refresh(PendingSocialListKind::kFriend);
  const bool fire_ignore_update =
      ignore_refresh_was_pending && !has_pending_refresh(PendingSocialListKind::kIgnore);
  const bool fire_mute_update =
      mute_refresh_was_pending && !has_pending_refresh(PendingSocialListKind::kMute);

  if (fire_friend_update) {
    ui::game::ScriptEventDispatch::Get().FireFriendListUpdate();
  }
  if (fire_ignore_update) {
    ui::game::ScriptEventDispatch::Get().FireIgnoreListUpdate();
  }
  if (fire_mute_update) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::MUTELIST_UPDATE);
  }

  if ((fire_friend_update || fire_ignore_update || fire_mute_update) &&
      CalendarSystem::Get().RefreshPendingInviteVisibility(CollectIgnoredGuids(social_))) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::CALENDAR_UPDATE_PENDING_INVITES);
  }
  if (fire_friend_update || fire_ignore_update || fire_mute_update) {
    RefreshWatchedChannelRosterLocalMuteFlags();
  }
}

void WorldSession::ApplyGroupLootSettingsUpdate(const std::uint8_t previous_loot_method,
                                                const std::uint64_t previous_master_looter,
                                                const std::uint8_t previous_loot_threshold,
                                                const bool force_announcements) {
  bool should_fire_loot_method_event = false;

  if (force_announcements || previous_loot_method != group_.loot_method()) {
    DisplayLootMethodSystemMessage(group_.loot_method());
    should_fire_loot_method_event = true;
  }

  if ((force_announcements || previous_master_looter != group_.master_looter_guid()) &&
      group_.loot_method() == kMasterLootMethod && group_.master_looter_guid() != 0) {
    QueueGroupLootMasterAnnouncement(group_.master_looter_guid());
  }

  if (force_announcements || previous_loot_threshold != group_.loot_threshold()) {
    const auto quality_name = ResolveLootThresholdLabel(group_.loot_threshold());
    ui::game::DisplaySystemMessage(kLootThresholdSystemMessageId, quality_name.c_str());
    should_fire_loot_method_event = true;
  }

  if (should_fire_loot_method_event) {
    ui::game::ScriptEventDispatch::Get().FirePartyLootMethodChanged();
  }
}

void WorldSession::HandleGroupList(const net::wotlk::WorldPacket &pkt) {
  const bool was_in_group = group_.IsInGroup();
  const bool was_in_raid = group_.IsRaid();

  const auto previous_my_roles = group_.my_roles();
  const auto previous_voice_selection = CaptureVoiceDisplaySelectionSnapshot();
  const auto previous_leader_guid = group_.leader_guid().GetRawValue();
  const auto previous_raid_members = was_in_raid ? group_.members() : std::vector<GroupMember>{};
  const auto previous_group_members = group_.members();
  const auto previous_group_type = group_.group_type();
  auto &gs = GroupSystem::Get();
  const auto previous_effective_dungeon = gs.GetDungeonDifficulty();
  const auto previous_effective_raid = gs.GetRaidDifficulty();
  const auto previous_player_difficulty = gs.GetPlayerDifficultyIndex();
  const auto previous_loot_method = static_cast<std::uint8_t>(gs.GetLootMethod());
  const auto previous_master_looter = gs.GetMasterLooter();
  const auto previous_loot_threshold = gs.GetLootThreshold();
  const auto previous_party_lfg_dungeon_id = gs.GetPartyLfgDungeonId();
  if (!group_.HandleGroupList(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto removed_member_count =
      CountRemovedGroupMembers(previous_group_members, group_.members());
  const bool lfg_restrictions_changed = gs.SetHasLfgRestrictions(group_.has_lfg_restrictions());
  const bool leader_changed = previous_leader_guid != group_.leader_guid().GetRawValue();
  RefreshPartyAutoCompleteTargets(*this, previous_group_members, group_);
  const bool refresh_lfg_status =
      ShouldRefreshLfgStatusAfterGroupList(was_in_group, was_in_raid, group_);
  const auto observed_group_members = BuildObservedGroupSystemMembers();

  if (group_.IsInGroup()) {
    SyncObservedGroupStateToGroupSystem();
    gs.UpdateRealGroupStateFromGroupList(group_.IsRaid(), group_.IsBattlegroundGroup(),
                                         static_cast<std::uint32_t>(group_.member_count()),
                                         gs.GetTrackedPartyMemberCount(),
                                         group_.leader_guid().GetRawValue());
    gs.SetPartyDungeonDifficulty(static_cast<DungeonDifficulty>(group_.dungeon_difficulty()));
    gs.SetCurrentRaidDifficulty(static_cast<RaidDifficulty>(group_.raid_difficulty()));
    gs.SetPlayerDifficultyIndex(group_.player_difficulty_index());
    gs.SetLocalPlayerPartyFlags(group_.my_flags());
    gs.SetLocalPlayerRoleFlags(group_.my_roles());
    gs.SetPartyLfgStatusFlags(group_.party_lfg_status_flags());
    gs.SetPartyLfgDungeonId(group_.party_lfg_dungeon_id());
    AnnounceRaidRosterChanges(previous_raid_members, previous_group_type, group_,
                              objects().GetActivePlayerGuid());
    ApplyGroupLootSettingsUpdate(previous_loot_method, previous_master_looter,
                                 previous_loot_threshold, !was_in_group);
  } else {
    pending_raid_roster_local_player_resolution_ = false;
    gs.ClearGroup();
    ApplyLootOptOutState(false, false);
  }

  if (!group_.IsInGroup() || leader_changed) {
    CancelLocalReadyCheck(true);
  } else {
    ReconcileReadyCheckAfterGroupListUpdate(removed_member_count, previous_leader_guid);
  }

  if (previous_effective_dungeon != gs.GetDungeonDifficulty() ||
      previous_effective_raid != gs.GetRaidDifficulty() ||
      previous_player_difficulty != gs.GetPlayerDifficultyIndex()) {
    RefreshGameObjectDifficultyVisibility();
  }

  if (lfg_restrictions_changed) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PARTY_LFG_RESTRICTED);
  }
  if (previous_my_roles != group_.my_roles()) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PLAYER_ROLES_ASSIGNED);
  }
  const bool entered_group = !was_in_group && group_.IsInGroup();
  const bool entered_raid_like = !was_in_raid && group_.IsRaid();

  const bool converted_party_to_raid =
      was_in_group && !was_in_raid && group_.IsRaid() && !group_.IsBattlegroundGroup();
  if (converted_party_to_raid) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PARTY_CONVERTED_TO_RAID);
  }
  if (entered_group || entered_raid_like) {
    const auto selection_type =
        group_.IsBattlegroundGroup()
            ? VoiceChatChannelType::kBattleground
            : (group_.IsRaid() ? VoiceChatChannelType::kRaid : VoiceChatChannelType::kParty);
    VoiceChat_SyncDisplaySelectionForSessionType(*this, selection_type, &previous_voice_selection);
  }
  if (entered_raid_like) {
    interaction().SendRequestAllRaidTargets();
  }

  const auto current_party_lfg_dungeon_id = group_.IsInGroup() ? group_.party_lfg_dungeon_id() : 0u;
  if (current_party_lfg_dungeon_id != previous_party_lfg_dungeon_id) {
    if (auto *ui = world_ui_runtime()) {
      ui->frame_events().dispatcher().FireEvent(ui::game::events::LFG_UPDATE);
    }
  }

  pending_raid_roster_name_queries_.clear();
  pending_raid_roster_local_player_resolution_ = false;
  if (group_.IsRaid()) {
    for (const auto &member : observed_group_members) {
      const auto raw_guid = member.guid;
      if (HasResolvedRaidRosterName(*this, raw_guid)) {
        continue;
      }

      pending_raid_roster_name_queries_.insert(raw_guid);
      (void)query_cache_.RequestNameQuery(raw_guid);
    }
    pending_raid_roster_local_player_resolution_ =
        !observed_group_members.empty() && observed_group_members.back().guid == 0;
  }

  if (leader_changed) {
    ui::game::ScriptEventDispatch::Get().FirePartyLeaderChanged();
  }
  ui::game::ScriptEventDispatch::Get().FirePartyMembersChanged();
  if (was_in_raid || group_.IsRaid()) {
    if (pending_raid_roster_name_queries_.empty() &&
        !pending_raid_roster_local_player_resolution_) {
      ui::game::ScriptEventDispatch::Get().FireRaidRosterUpdate();
    }
  }

  if (refresh_lfg_status) {
    RefreshLfgStateAfterGroupAcquisition(*this);
  }

  DisplayGroupDifficultyChangedMessagesIfNeeded(*this, previous_effective_dungeon,
                                                previous_effective_raid);
}

void WorldSession::HandleGroupInvite(const net::wotlk::WorldPacket &pkt) {
  group_.HandleGroupInvite(pkt.payload.data(), pkt.payload.size());
  const auto &invite = group_.pending_invite();
  if (group_.has_pending_invite()) {
    ui::game::ScriptEventDispatch::Get().FirePartyInviteRequest(invite.inviter_name);
    ui::game::DisplaySystemMessage(65, invite.inviter_name.c_str(),
                                   invite.inviter_name.c_str());
  } else {
    ui::game::DisplaySystemMessage(66, invite.inviter_name.c_str(),
                                   invite.inviter_name.c_str());
  }
}

void WorldSession::HandleGroupDecline(const net::wotlk::WorldPacket &pkt) {
  std::string decliner;
  if (!group_.HandleGroupDecline(pkt.payload.data(), pkt.payload.size(), decliner)) {
    return;
  }

  const auto active_player_name = ResolveActivePlayerName(*this);
  const bool declined_by_active_player =
      !active_player_name.empty() &&
      openwow::core::SStrCmpUTF8NoCase(active_player_name.c_str(), decliner.c_str(), 0x7FFFFFFF) ==
          0;
  const bool has_lfg_restrictions = GroupSystem::Get().HasLfgRestrictions();

  if (declined_by_active_player) {
    ui::game::DisplaySystemMessage(has_lfg_restrictions ? 72 : 70);
  } else {
    ui::game::DisplaySystemMessage(has_lfg_restrictions ? 71 : 69, decliner.c_str());
  }
  ui::game::ScriptEventDispatch::Get().FirePartyInviteCancel();
}

void WorldSession::HandleGroupSetLeader(const net::wotlk::WorldPacket &pkt) {
  if (!group_.HandleSetLeader(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& leader_name = group_.leader_name();
  const auto active_player_name = ResolveActivePlayerName(*this);
  const bool is_active_player =
      openwow::core::SStrCmpUTF8NoCase(active_player_name.c_str(),
                                      leader_name.c_str(), 0x7FFFFFFF) == 0;
  const bool is_lfg_guide = GroupSystem::Get().HasLfgRestrictions();
  if (is_active_player) {
    ui::game::DisplaySystemMessage(is_lfg_guide ? 72 : 70);
  } else {
    ui::game::DisplaySystemMessage(is_lfg_guide ? 71 : 69,
                                   leader_name.c_str());
  }
}

void WorldSession::HandlePartyCommandResult(const net::wotlk::WorldPacket &pkt) {
  PartyCommandResult result{};
  if (!GroupManager::ParsePartyCommandResult(pkt.payload.data(),
                                             pkt.payload.size(), result)) {
    return;
  }

  const auto operation = static_cast<std::uint32_t>(result.operation);
  const auto result_code = static_cast<std::uint32_t>(result.result);
  auto& dispatch = ui::game::ScriptEventDispatch::Get();

  if (result_code == 0) {
    if (operation == 0 && !result.member.empty()) {
      ui::game::DisplaySystemMessage(63, result.member.c_str());
      return;
    }

    if (operation != 2) {
      return;
    }

    const bool fire_proposal_failed =
        lfg_.proposal().has_value() && lfg_.proposal()->state == 2;
    const bool fire_role_check_hide = lfg_.role_check().has_value();

    group_.Clear();
    pending_raid_roster_name_queries_.clear();
    pending_raid_roster_local_player_resolution_ = false;
    GroupSystem::Get().ClearGroup();
    CancelLocalReadyCheck(true);

    ui::game::DisplaySystemMessage(74);

    lfg_.Clear();
    LFGSystem::Get().Reset();
    if (fire_proposal_failed) {
      dispatch.FireEvent(ui::game::events::LFG_PROPOSAL_FAILED);
    }
    if (fire_role_check_hide) {
      dispatch.FireEvent(ui::game::events::LFG_ROLE_CHECK_HIDE);
    }
    dispatch.FireCancelSummon();
    return;
  }

  switch (result_code) {
    case 1:
      ui::game::DisplaySystemMessage(79, result.member.c_str());
      break;
    case 2:
      ui::game::DisplaySystemMessage(81, result.member.c_str());
      break;
    case 3:
      ui::game::DisplaySystemMessage(82, result.member.c_str());
      break;
    case 4:
      ui::game::DisplaySystemMessage(83);
      break;
    case 5:
      ui::game::DisplaySystemMessage(67, result.member.c_str());
      break;
    case 6:
      ui::game::DisplaySystemMessage(80);
      break;
    case 7:
      ui::game::DisplaySystemMessage(84);
      break;
    case 8:
      ui::game::DisplaySystemMessage(269);
      break;
    case 9:
      ui::game::DisplaySystemMessage(336, result.member.c_str());
      break;
    case 12:
      ui::game::DisplaySystemMessage(515);
      break;
    case 13:
      ui::game::DisplaySystemMessage(565);
      break;
    case 14:
      if (operation == 4) {
        ui::game::DisplaySystemMessage(634);
        dispatch.FireRaidRosterUpdate();
      } else {
        ui::game::DisplaySystemMessage(635);
      }
      break;
    case 15:
      ui::game::DisplaySystemMessage(642);
      break;
    case 16:
      ui::game::DisplaySystemMessage(643);
      break;
    case 17:
      ui::game::DisplaySystemMessage(644);
      break;
    case 18:
      ui::game::DisplaySystemMessage(645);
      break;
    case 19:
      ui::game::DisplaySystemMessage(646, result.member.c_str());
      break;
    case 20:
      ui::game::DisplaySystemMessage(647);
      break;
    case 21:
    case 24: {
      const std::uint32_t seconds = result.value < 0
                                        ? 1u
                                        : static_cast<std::uint32_t>(result.value);
      const std::string duration =
          FormatRoundedSpellDurationText(seconds * 1000u);
      ui::game::DisplaySystemMessage(result_code == 21 ? 648 : 649,
                                     duration.c_str());
      break;
    }
    case 22:
      ui::game::DisplaySystemMessage(650);
      break;
    case 23:
      ui::game::DisplaySystemMessage(651);
      break;
    case 25:
      ui::game::DisplaySystemMessage(658);
      break;
    case 26:
      ui::game::DisplaySystemMessage(654);
      break;
    case 27:
      dispatch.FireGlobalEventWithArgs(
          ui::game::events::VOTE_KICK_REASON_NEEDED, {result.member});
      break;
    case 28:
      ui::game::DisplaySystemMessage(655);
      break;
    case 29:
      ui::game::DisplaySystemMessage(656);
      break;
    case 30:
      ui::game::DisplaySystemMessage(657);
      break;
    default:
      break;
  }
}

void WorldSession::HandleGuildQueryResponse(const net::wotlk::WorldPacket &pkt) {

  PacketReader response_reader(pkt.payload.data(), pkt.payload.size());
  std::uint32_t queried_guild_id = 0;
  if (!response_reader.ReadU32(queried_guild_id)) {
    return;
  }

  if (!guild_.HandleGuildQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto *player = objects().GetActivePlayer();
  if (player != nullptr) {
    if (const auto *info = guild_.FindCachedGuildInfo(player->GetGuildID());
        info != nullptr) {
      GuildSystem::Get().SetGuildIdentity(info->guild_id, info->name);
    }
  }

  if (queried_guild_id != 0) {
    auto &dispatch = ui::game::ScriptEventDispatch::Get();

    objects().ForEachPlayer(
        [&](const ObjectGuid &guid, CGPlayer_C &p) {
          if (p.GetGuildID() == queried_guild_id) {
            dispatch.FirePerUnitEvent(
                ui::game::events::PLAYER_GUILD_UPDATE, guid.GetRawValue());
            dispatch.FireEvent(ui::game::events::TABARD_CANSAVE_CHANGED);
          }
        });

    objects().ForEachObject(
        [&](const ObjectGuid&, CGObject_C& object) {
          auto* const corpse = dynamic_cast<CGCorpse_C*>(&object);
          if (corpse != nullptr &&
              corpse->GetGuildId() == queried_guild_id) {
            corpse->UpdateGuildTabard(*this, false);
          }
        });
  }
}

void WorldSession::HandleGuildRoster(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildRoster(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (guild_.has_roster()) {
    RefreshGuildAutoCompleteTargets(*this, guild_.roster());
  }

  auto &guild_system = GuildSystem::Get();
  guild_system.HandleRosterUpdate(pkt.payload.data(), pkt.payload.size());
  const auto *player = objects().GetActivePlayer();
  const auto *guild_info =
      player != nullptr ? guild_.FindCachedGuildInfo(player->GetGuildID()) : nullptr;
  if (guild_info != nullptr) {
    guild_system.SetGuildInfo(guild_info->guild_id, guild_info->name, guild_system.GetGuildMOTD(),
                              guild_system.GetGuildInfo());

    if (guild_.has_roster()) {
      const auto &roster = guild_.roster();
      std::vector<GuildRank> ranks;
      ranks.reserve(roster.ranks.size());
      for (std::size_t index = 0; index < roster.ranks.size(); ++index) {
        GuildRank rank;
        rank.id = static_cast<std::uint32_t>(index);
        rank.name = index < static_cast<std::size_t>(kGuildRanksMaxCount)
                        ? guild_info->rank_names[index]
                        : "";
        rank.rights = roster.ranks[index].flags;
        rank.money_per_day = roster.ranks[index].withdraw_gold_limit;
        for (std::size_t tab = 0; tab < rank.bank_tab_flags.size(); ++tab) {
          rank.bank_tab_flags[tab] = roster.ranks[index].tab_flags[tab];
          rank.bank_tab_withdraw_item_limits[tab] =
              roster.ranks[index].tab_withdraw_item_limit[tab];
        }
        ranks.push_back(std::move(rank));
      }
      guild_system.SetRanks(ranks);
    }
  }
  ui::game::ScriptEventDispatch::Get().FireGuildRosterUpdate();
  ui::game::detail::TryCompletePendingGuildRosterExport(*this);
}

void WorldSession::HandleGuildEvent(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildEvent(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto &guild_system = GuildSystem::Get();
  bool refresh_roster = false;
  if (const auto &evt = guild_.last_event(); evt.has_value()) {
    refresh_roster = GuildEventRefreshesRoster(evt->type);
    auto &autocomplete = ui::game::AutoComplete::Get();
    switch (evt->type) {
    case GuildEventType::kMotd: {
      const std::string motd_param = evt->params.empty() ? std::string{} : evt->params.front();
      ::openwow::ui::game::ScriptEventDispatch::Get().FireGuildMotd(motd_param);
      const auto stored_motd = detail::SanitizeGuildEventRosterMotd(motd_param);
      guild_system.SetGuildMOTD(stored_motd);
      guild_.UpdateCachedRosterMotd(stored_motd);
      break;
    }
    case GuildEventType::kJoined:
      if (!evt->params.empty()) {
        TouchRecentPlayerAutoCompleteTarget(*this, evt->guid, kAutoCompleteGuildFlag, true,
                                            evt->params.front(), false);
      }
      break;
    case GuildEventType::kLeft:
      autocomplete.ClearRecentPlayerGuidContextBits(evt->guid.GetRawValue(),
                                                    kAutoCompleteGuildFlag);
      break;
    case GuildEventType::kRemoved:
      if (!evt->params.empty()) {
        autocomplete.ClearRecentPlayerNameContextBits(evt->params.front(), kAutoCompleteGuildFlag);
      }
      break;
    case GuildEventType::kSignedOn:
      if (!evt->params.empty()) {
        TouchRecentPlayerAutoCompleteTarget(*this, evt->guid, 0u, true, evt->params.front(), false);
      }
      break;
    case GuildEventType::kSignedOff:
      autocomplete.ClearRecentPlayerGuidContextBits(evt->guid.GetRawValue(),
                                                    kAutoCompleteOnlineFlag);
      break;
    case GuildEventType::kBankTabUpdated:

      if (evt->params.size() >= 3) {
        char *end = nullptr;
        const long tab = std::strtol(evt->params[0].c_str(), &end, 10);
        if (end != evt->params[0].c_str() && tab >= 0 &&
            tab < static_cast<long>(GuildSystem::kGuildBankMaxTabs)) {
          guild_system.UpdateGuildBankTabCacheEntry(
              static_cast<std::uint8_t>(tab), evt->params[1], evt->params[2]);
        }
      }
      ::openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
          ::openwow::ui::game::events::GUILDBANK_UPDATE_TABS);
      break;
    case GuildEventType::kBankTextChanged:
      if (!evt->params.empty()) {
        char *end = nullptr;
        const long tab = std::strtol(evt->params.front().c_str(), &end, 10);
        if (end != evt->params.front().c_str() && tab >= 0 &&
            tab < static_cast<long>(GuildSystem::kGuildBankMaxTabs)) {
          GuildSystem::Get().SetGuildBankTabTextRefreshPending(static_cast<std::uint8_t>(tab),
                                                               true);
          ::openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
              ::openwow::ui::game::events::GUILDBANK_TEXT_CHANGED, {static_cast<int>(tab + 1)});
        }
      }
      break;
    default:
      break;
    }
  }

  if (refresh_roster) {
    NotifyGuildRosterServerRefresh(guild_system);
  }
}

void WorldSession::HandleGuildCommandResult(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildCommandResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &result = guild_.last_command_result();
  if (!result.has_value()) {
    return;
  }

  const auto command = result->command;
  const auto result_code = result->result;
  if (result_code == 0) {
    switch (command) {
      case 0:
        ui::game::DisplaySystemMessage(86, result->name.c_str());
        return;
      case 1:
        ui::game::DisplaySystemMessage(87, result->name.c_str());
        return;
      case 3:
        ui::game::AutoComplete::Get().ClearRecentPlayerContextBits(
            kAutoCompleteGuildFlag);
        ui::game::DisplaySystemMessage(100, result->name.c_str());
        ui::game::SetGuildBankInteractionTarget({});
        return;
      case 14:
        ui::game::DisplaySystemMessage(97, result->name.c_str());
        return;
      case 19:
      case 20:
        NotifyGuildRosterServerRefresh(GuildSystem::Get());
        return;
      default:
        return;
    }
  }

  switch (result_code) {
    case 1:
      ui::game::DisplaySystemMessage(114);
      break;
    case 2:
      ui::game::DisplaySystemMessage(92);
      break;
    case 3:
      ui::game::DisplaySystemMessage(89, result->name.c_str());
      break;
    case 4:
      ui::game::DisplaySystemMessage(91);
      break;
    case 5:
      ui::game::DisplaySystemMessage(90, result->name.c_str());
      break;
    case 6:

      ui::game::DisplaySystemMessage(598);
      break;
    case 7:
      ui::game::DisplaySystemMessage(124, result->name.c_str());
      break;
    case 8:
      ui::game::DisplaySystemMessage(command == 3 ? 119 : 95);
      break;
    case 9:
      ui::game::DisplaySystemMessage(110);
      break;
    case 10:
      ui::game::DisplaySystemMessage(109, result->name.c_str());
      break;
    case 11:
      ui::game::DisplaySystemMessage(108, result->name.c_str());
      break;
    case 12:
      ui::game::DisplaySystemMessage(118);
      break;
    case 13:
      ui::game::DisplaySystemMessage(122, result->name.c_str());
      break;
    case 14:
      ui::game::DisplaySystemMessage(123, result->name.c_str());
      break;
    case 17:
      ui::game::DisplaySystemMessage(120);
      break;
    case 18:
      ui::game::DisplaySystemMessage(121);
      break;
    case 19:
      ui::game::DisplaySystemMessage(336, result->name.c_str());
      break;
    case 20:
      if (command == 5) {
        NotifyGuildRosterServerRefresh(GuildSystem::Get());
      }
      break;
    case 25:
      ui::game::DisplaySystemMessage(125);
      break;
    case 26:
      ui::game::DisplaySystemMessage(126);
      break;
    case 28:
      ui::game::DisplaySystemMessage(132);
      break;
    case 29:
      ui::game::DisplaySystemMessage(25);
      break;
    default:
      break;
  }
}

void WorldSession::HandleGuildInvite(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildInvite(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &inviter = guild_.invite_from();
  const auto &guild_name = guild_.invite_guild_name();
  ui::game::ScriptEventDispatch::Get().FireGuildInviteRequest(inviter, guild_name);
  ui::game::DisplaySystemMessage(0x58, inviter.c_str(), inviter.c_str(),
                                 guild_name.c_str());
}

void WorldSession::HandleGuildPermissions(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildPermissions(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (guild_.has_permissions()) {
    GuildSystem::Get().SetGuildBankTabCount(
        static_cast<std::uint8_t>(guild_.permissions().num_tabs));
  }
  ui::game::ScriptEventDispatch::Get().FireGuildRosterUpdate();
}

void WorldSession::HandleGuildEventLogQuery(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildEventLogQuery(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const bool has_pending_refresh = guild_.BeginGuildEventLogRefresh(
      query_cache_, [this](std::uint64_t raw_guid) {
        (void)query_cache_.RequestNameQuery(raw_guid);
      });
  if (!has_pending_refresh) {
    ui::game::ScriptEventDispatch::Get().FireGuildEventLogUpdate();
  }
}

void WorldSession::HandleLfgJoinResult(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgJoinResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (const auto &join_result = lfg_.join_result(); join_result.has_value()) {
    constexpr int kLfgJoinResultRoleCheckFailedTimeoutMessage = 681;
    constexpr int kLfgJoinResultRoleCheckFailedDeclinedMessage = 682;
    constexpr std::pair<std::uint32_t, int> kLfgJoinResultMessages[] = {
        {1, 680},  {2, 683},  {4, 684},  {5, 685},  {6, 686},  {7, 687},
        {8, 688},  {9, 689},  {10, 690}, {11, 691}, {12, 692}, {13, 693},
        {14, 694}, {15, 695}, {16, 696}, {17, 710}, {18, 680},
    };
    if (join_result->result == 1 && join_result->state == 3) {
      ui::game::DisplaySystemMessage(kLfgJoinResultRoleCheckFailedTimeoutMessage);
    } else if (join_result->result == 1 && join_result->state == 4) {
      ui::game::DisplaySystemMessage(kLfgJoinResultRoleCheckFailedDeclinedMessage);
    } else {
      for (const auto &[result_code, message_index] : kLfgJoinResultMessages) {
        if (join_result->result == result_code) {
          ui::game::DisplaySystemMessage(message_index);
          break;
        }
      }
    }
  }
}

void WorldSession::HandleLfgQueueStatus(const net::wotlk::WorldPacket &pkt) {
  if (lfg_.HandleLfgQueueStatus(pkt.payload.data(), pkt.payload.size())) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_QUEUE_STATUS_UPDATE);
  }
}

void WorldSession::HandleLfgUpdatePlayer(const net::wotlk::WorldPacket &pkt) {
  const bool had_proposal = lfg_.proposal().has_value();
  const auto previous_update = lfg_.player_update();
  if (!lfg_.HandleLfgUpdatePlayer(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (lfg_.player_update()) {
    const auto &upd = *lfg_.player_update();
    const bool server_info_changed = DidServerLfgInfoChange(previous_update, upd);
    if (upd.queued) {
      LFGSystem::Get().SetState(LFGState::Queued);
    } else if (upd.update_type == LfgUpdateType::kRemovedFromQueue ||
               upd.update_type == LfgUpdateType::kDefault) {
      LFGSystem::Get().SetState(LFGState::None);
    }

    if (had_proposal && (!upd.has_extra || upd.queued) && !group_.IsInGroup()) {
      lfg_.ClearProposal();
      LFGSystem::Get().ClearProposal();
      FireLfgProposalResolutionEvent(*this, UpdatePlayerMarksProposalSuccess(upd));
    }

    if (!group_.IsInGroup() && server_info_changed) {
      if (!upd.has_extra || !upd.queued) {
        lfg_.ClearQueueStatus();
      }
      FireLfgUpdateEvent(*this, false);
    }
  }
}

void WorldSession::HandleLfgUpdateParty(const net::wotlk::WorldPacket &pkt) {
  const bool had_proposal = lfg_.proposal().has_value();
  const auto previous_update = lfg_.party_update();
  if (!lfg_.HandleLfgUpdateParty(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (!lfg_.party_update()) {
    return;
  }

  const auto &upd = *lfg_.party_update();
  const bool server_info_changed = DidServerLfgInfoChange(previous_update, upd);
  if (had_proposal && (!upd.has_extra || upd.queued) && group_.IsInGroup()) {
    lfg_.ClearProposal();
    LFGSystem::Get().ClearProposal();
    FireLfgProposalResolutionEvent(*this, UpdatePartyMarksProposalSuccess(upd));
  }

  if (group_.IsInGroup() && server_info_changed) {
    if (!upd.has_extra || !upd.queued) {
      lfg_.ClearQueueStatus();
    }
    FireLfgUpdateEvent(*this, false);
  }
}

void WorldSession::HandleLfgProposalUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgProposalUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (lfg_.proposal()) {
    const auto &prop = *lfg_.proposal();
    LFGProposal sys_prop;
    sys_prop.id = prop.proposal_id;
    sys_prop.dungeon_id = prop.dungeon_entry;
    sys_prop.encounter_completion_mask = prop.encounter_mask;
    sys_prop.state = prop.state;
    sys_prop.silent = prop.silent;
    for (const auto &p : prop.players) {
      LFGProposal::ProposalMember pm;
      pm.role = static_cast<uint8_t>(p.role);
      pm.accepted = p.has_answered ? std::optional<bool>(p.has_accepted) : std::nullopt;
      pm.self = p.is_current_player;
      pm.in_dungeon = p.in_dungeon;
      sys_prop.members.push_back(pm);
    }
    LFGSystem::Get().SetProposal(sys_prop);
    LFGSystem::Get().SetState(LFGState::Proposal);
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_PROPOSAL_UPDATE);
  if (lfg_.ConsumeProposalShowPending()) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_PROPOSAL_SHOW);
  }

  if (!lfg_.proposal()) {
    return;
  }

  const auto &proposal = *lfg_.proposal();
  if (ProposalHasPartyDecline(proposal)) {
    sound_runtime_.PlayVoiceChatToggle(kLfgFailureSoundKitId);
    ui::game::DisplaySystemMessage(699);
    lfg_.ClearProposal();
    LFGSystem::Get().ClearProposal();
    FireLfgProposalResolutionEvent(*this, false);
    return;
  }

  if (ProposalHasCurrentPlayerDecline(proposal)) {
    sound_runtime_.PlayVoiceChatToggle(kLfgFailureSoundKitId);
    ui::game::DisplaySystemMessage(698);
    lfg_.ClearProposal();
    LFGSystem::Get().ClearProposal();
    FireLfgProposalResolutionEvent(*this, false);
  }
}

void WorldSession::HandleLfgRoleCheckUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgRoleCheckUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (lfg_.role_check()) {
    const auto &role_check = *lfg_.role_check();

    for (const auto &p : role_check.players) {
      if (p.ready && p.roles != 0) {
        LFGSystem::Get().SetRoles(static_cast<uint8_t>(p.roles));
        FireLfgRoleChosenEvent(*this, p.guid, p.roles);
      }
    }

    if (role_check.is_beginning) {
      ui::game::DisplaySystemMessage(702);
      sound_runtime_.PlayVoiceChatToggle(kLfgAcceptSoundKitId);
    } else if (role_check.state == 5 || role_check.state == 6) {
      sound_runtime_.PlayVoiceChatToggle(kLfgFailureSoundKitId);
      if (role_check.state == 5) {
        ui::game::DisplaySystemMessage(708);
      }
    }

    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_ROLE_CHECK_UPDATE);

    const auto *visible_player =
        SelectVisibleRoleCheckMember(role_check, objects().GetLocalPlayerGuid());
    if (visible_player != nullptr && !visible_player->ready && role_check.state == 2) {
      ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_ROLE_CHECK_SHOW);
    } else {
      ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_ROLE_CHECK_HIDE);
    }
  }
}

void WorldSession::HandleLfgBootProposalUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgBootProposalUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (lfg_.boot_proposal()) {
    const auto &boot_proposal = *lfg_.boot_proposal();
    LFGBootVote boot_vote;
    boot_vote.in_progress = boot_proposal.in_progress;
    boot_vote.did_vote = boot_proposal.did_vote;
    boot_vote.my_vote = boot_proposal.agree;
    boot_vote.target_guid = boot_proposal.victim_guid;
    boot_vote.votes_total = boot_proposal.total_votes;
    boot_vote.agree_count = boot_proposal.agree_count;
    boot_vote.time_left = boot_proposal.time_left;
    boot_vote.votes_needed = boot_proposal.needed_votes;
    boot_vote.reason = boot_proposal.reason;
    LFGSystem::Get().SetBootVote(boot_vote);
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_BOOT_PROPOSAL_UPDATE);
}

void WorldSession::HandleLfgPlayerReward(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgPlayerReward(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (lfg_.player_reward()) {
    const auto &reward = *lfg_.player_reward();
    LFGReward system_reward;
    system_reward.dungeon_id = reward.completed_dungeon_entry != 0 ? reward.completed_dungeon_entry
                                                                   : reward.random_dungeon_entry;
    system_reward.is_first_reward = reward.is_first_reward;
    system_reward.reward_money = reward.base_money_reward;
    system_reward.reward_xp = reward.base_xp_reward;
    for (const auto &item : reward.items) {
      system_reward.reward_item_ids.push_back(item.item_id);
      system_reward.reward_item_counts.push_back(item.item_count);
    }
    LFGSystem::Get().SetRewards({system_reward});
  }

  if (auto *ui = world_ui_runtime()) {
    ui->frame_events().dispatcher().FireEvent(
        ui::game::events::LFG_COMPLETION_REWARD);
  }
}

void WorldSession::HandleLfgTeleportDenied(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgTeleportDenied(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  DisplayLfgTeleportDeniedMessage(lfg_.teleport_error());
}

void WorldSession::HandleLfgOfferContinue(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgOfferContinue(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto *ui = world_ui_runtime();
  if (ui == nullptr) {
    return;
  }

  const auto *entry = FindLfgDungeonEntry(GetDbcLoader(), lfg_.offer_continue_dungeon());
  if (entry == nullptr) {
    return;
  }

  ui->frame_events().dispatcher().FireEventArgs(
      ui::game::events::LFG_OFFER_CONTINUE,
      {std::string(entry->name), static_cast<int>(entry->id), static_cast<int>(entry->type_id)});
}

void WorldSession::HandleInitWorldStates(const net::wotlk::WorldPacket &pkt) {
  const std::uint64_t previous_revision = world_states_.world_state_ui_revision();
  if (!world_states_.HandleInitWorldStates(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (bg_instance_.IsActive()) {
    bg_instance_.OnInitWorldStates(world_states_.states());
  }

  if (world_states_.world_state_ui_revision() != previous_revision) {
    ui::game::NotifyWorldStatesChanged();
  }
}

void WorldSession::HandleUpdateWorldState(const net::wotlk::WorldPacket &pkt) {
  if (!world_states_.HandleUpdateWorldState(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (bg_instance_.IsActive() && pkt.payload.size() >= 8) {
    std::int32_t var_id = 0, value = 0;
    std::memcpy(&var_id, pkt.payload.data(), 4);
    std::memcpy(&value, pkt.payload.data() + 4, 4);
    bg_instance_.OnWorldStateUpdate(var_id, value);
  }

  ui::game::NotifyWorldStatesChanged();

  ui::game::RefreshZoneSoundsForActiveMover();
}

void WorldSession::HandleBattlefieldStatus(const net::wotlk::WorldPacket &pkt) {
  BattlefieldInfo::Get().HandleBattlefieldStatus(*this, pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleBattlefieldList(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandleBattlefieldList(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  DispatchBattlefieldListUi(*this, battleground_.battlefield_list());
}

void WorldSession::HandlePlayerPositions(const net::wotlk::WorldPacket &pkt) {
  (void)BattlefieldInfo::Get().HandleBGPlayerPositions(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandlePlayerJoined(const net::wotlk::WorldPacket &pkt) {
  (void)BattlefieldInfo::Get().HandleBGPlayerJoinLeave(
      pkt.payload.data(), pkt.payload.size(), kBgPlayerJoinedSystemMessageId, query_cache_,
      [this](const std::uint64_t guid) {
        (void)query_cache_.RequestNameQuery(guid);
      });
}

void WorldSession::HandlePlayerLeft(const net::wotlk::WorldPacket &pkt) {
  (void)BattlefieldInfo::Get().HandleBGPlayerJoinLeave(
      pkt.payload.data(), pkt.payload.size(), kBgPlayerLeftSystemMessageId, query_cache_,
      [this](const std::uint64_t guid) {
        (void)query_cache_.RequestNameQuery(guid);
      });
}

void WorldSession::HandlePvpLogData(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandlePvpLogData(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &pvp_log = battleground_.pvp_log();
  if (pvp_log.is_arena) {
    for (std::size_t team_index = 0; team_index < pvp_log.arena_teams.size(); ++team_index) {
      BattlefieldInfo::Get().SetArenaBattlefieldTeamInfo(
          team_index, pvp_log.arena_teams[team_index].name,
          pvp_log.arena_teams[team_index].raw_values);
    }
  }

  BattlefieldInfo::Get().SetBattlefieldWinnerValid(pvp_log.is_ended);
  if (pvp_log.is_arena || pvp_log.is_ended) {
    BattlefieldInfo::Get().SetBattlefieldWinnerRaw(pvp_log.winner);
  }

  std::vector<BGScoreEntry> ui_scores;
  ui_scores.reserve(pvp_log.scores.size());
  for (const auto &score : pvp_log.scores) {
    BGScoreEntry entry;
    entry.player_guid = ObjectGuid(score.player_guid);
    entry.killing_blows = score.killing_blows;
    entry.deaths = score.deaths;
    entry.honorable_kills = score.honorable_kills;
    entry.bonus_honor = score.bonus_honor;
    entry.damage_done = score.damage_done;
    entry.healing_done = score.healing_done;
    entry.faction = pvp_log.is_arena ? static_cast<std::int32_t>(score.pvp_team_id) : 0;
    entry.bg_stats = score.bg_objectives;
    ui_scores.push_back(std::move(entry));
  }

  BattlefieldInfo::Get().SetScoreEntries(
      std::move(ui_scores), pvp_log.is_arena, query_cache_, objects(),
      [this](const std::uint64_t raw_guid) {
        (void)query_cache_.RequestNameQuery(raw_guid);
      });
}

void WorldSession::HandlePvpCredit(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandlePvpCredit(pkt.payload.data(), pkt.payload.size()))
    return;

  const auto &credit = battleground_.last_pvp_credit();
  FormatHonorGain(*this, credit.victim_guid,
                  static_cast<int>(credit.victim_rank),
                  static_cast<int>(credit.honor));
}

void WorldSession::HandleArenaTeamRoster(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandleArenaTeamRoster(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireGlobalEvent(ui::game::events::ARENA_TEAM_ROSTER_UPDATE);
}

void WorldSession::HandleArenaTeamCommandResult(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandleArenaTeamCommandResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  DisplayArenaTeamCommandMessage(battleground_.arena_command());
  ResetArenaRosterRequestsAndNotify(*this);
}

void WorldSession::HandleArenaTeamStats(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandleArenaTeamStats(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &stats = battleground_.arena_stats();
  if (ArenaSystem::Get().UpdateTeamStatsById(stats.team_id, stats.rating, stats.week_games,
                                             stats.week_wins, stats.season_games, stats.season_wins,
                                             stats.rank)) {
    ui::game::ScriptEventDispatch::Get().FireGlobalEvent(ui::game::events::ARENA_TEAM_UPDATE);
  }
}

void WorldSession::HandleArenaTeamInvite(const net::wotlk::WorldPacket &pkt) {
  if (!battleground_.HandleArenaTeamInvite(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &invite = battleground_.arena_invite();
  ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
      ui::game::events::ARENA_TEAM_INVITE_REQUEST,
      {invite.inviter_name, invite.team_name});

  ui::game::DisplaySystemMessage(0x58, invite.inviter_name.c_str(),
                                 invite.inviter_name.c_str(),
                                 invite.team_name.c_str());
}

namespace {

void FireStockPartyMemberStatsEvents(const PartyMemberStats &previous,
                                      const PartyMemberStats &current,
                                      const std::uint32_t packet_mask) {
  const std::uint64_t guid = current.guid.GetRawValue();
  if (guid == 0) {
    return;
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  const std::uint32_t mask = packet_mask;

  bool cur_hp_consumed_by_status = false;
  bool cur_power_consumed_by_status = false;

  using namespace GroupUpdateFlag;

  if (mask & kStatus) {
    dispatch.FireUnitHealth(guid);
    dispatch.FireUnitPowerSpecific(guid, current.power_type);
    cur_hp_consumed_by_status = true;
    cur_power_consumed_by_status = true;
  }
  if (mask & kLevel) {
    dispatch.FireUnitLevel(guid);
  }
  if ((mask & kCurHp) && !cur_hp_consumed_by_status) {
    dispatch.FireUnitHealth(guid);
  }
  if (mask & kMaxHp) {
    dispatch.FireUnitMaxHealth(guid);
  }
  if (mask & kPowerType) {
    dispatch.FireUnitDisplayPower(guid);
  }
  if ((mask & kCurPower) && !cur_power_consumed_by_status) {
    dispatch.FireUnitPowerSpecific(guid, current.power_type);
  }
  if (mask & kMaxPower) {
    dispatch.FireUnitMaxPowerSpecific(guid, current.power_type);
  }

  constexpr std::uint16_t kPvpAndFfaStatusMask =
      GroupMemberStatus::kPvp | GroupMemberStatus::kPvpFfa;
  if ((previous.status & kPvpAndFfaStatusMask) !=
      (current.status & kPvpAndFfaStatusMask)) {
    dispatch.FireUnitFaction(guid);
  }
  if (mask & kAuras) {
    dispatch.FireUnitAura(guid);
  }
  if (mask & kPetGuid) {
    dispatch.FireUnitPet(guid);
  }

  if (current.pet_guid != 0) {
    const std::uint64_t pet_guid = current.pet_guid;
    if (mask & kPetName) {
      dispatch.FireUnitName(pet_guid);
    }
    if (mask & kPetModelId) {
      dispatch.FireUnitPortrait(pet_guid);
      dispatch.FireUnitModel(pet_guid);
    }
    if (mask & kPetCurHp) {
      dispatch.FireUnitHealth(pet_guid);
    }
    if (mask & kPetMaxHp) {
      dispatch.FireUnitMaxHealth(pet_guid);
    }
    if (mask & kPetPowerType) {
      dispatch.FireUnitDisplayPower(pet_guid);
    }
    if (mask & kPetCurPower) {
      dispatch.FireUnitPowerSpecific(pet_guid, current.pet_power_type);
    }
    if (mask & kPetMaxPower) {
      dispatch.FireUnitMaxPowerSpecific(pet_guid, current.pet_power_type);
    }
    if (mask & kPetAuras) {
      dispatch.FireUnitAura(pet_guid);
    }
  }

}

std::uint64_t PeekPartyMemberStatsGuid(const net::wotlk::WorldPacket &pkt,
                                       bool has_leading_byte) {
  PacketReader peek(pkt.payload.data(), pkt.payload.size());
  if (has_leading_byte) {
    std::uint8_t unused_leading_byte = 0;
    if (!peek.ReadU8(unused_leading_byte)) {
      return 0;
    }
  }
  ObjectGuid peeked_guid;
  if (!peek.ReadPackedGuid(peeked_guid)) {
    return 0;
  }
  return peeked_guid.GetRawValue();
}

}

void WorldSession::HandlePartyMemberStats(const net::wotlk::WorldPacket &pkt) {

  const std::uint64_t peeked_guid_raw =
      PeekPartyMemberStatsGuid(pkt, false);
  const auto previous_cached = party_stats_.GetCachedMember(peeked_guid_raw);
  const PartyMemberStats previous_stats =
      previous_cached.has_value() ? previous_cached->stats : PartyMemberStats{};

  party_stats_.HandlePartyMemberStats(pkt.payload.data(), pkt.payload.size());
  ApplyPartyMemberPetGuidToGroupSystem(party_stats_.last_update());
  const std::uint32_t packet_mask = party_stats_.last_update().update_mask;

  ui::game::ScriptEventDispatch::Get().FirePartyMembersChanged();

  if (const auto current_cached = party_stats_.GetCachedMember(peeked_guid_raw);
      current_cached.has_value()) {
    FireStockPartyMemberStatsEvents(previous_stats, current_cached->stats, packet_mask);
  }
}

void WorldSession::HandlePartyMemberStatsFull(const net::wotlk::WorldPacket &pkt) {

  const std::uint64_t peeked_guid_raw =
      PeekPartyMemberStatsGuid(pkt, true);
  const auto previous_cached = party_stats_.GetCachedMember(peeked_guid_raw);
  const PartyMemberStats previous_stats =
      previous_cached.has_value() ? previous_cached->stats : PartyMemberStats{};

  if (!party_stats_.HandlePartyMemberStatsFull(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ApplyPartyMemberPetGuidToGroupSystem(party_stats_.last_update());
  const std::uint32_t packet_mask = party_stats_.last_update().update_mask;

  if (party_stats_.last_update().is_arena_opponent_marker && peeked_guid_raw != 0) {
    const ObjectGuid opponent_guid(peeked_guid_raw);
    if (!battleground_.FindArenaOpponentSlot(peeked_guid_raw).has_value()) {
      for (std::size_t slot = 0; slot < kMaxArenaOpponents; ++slot) {
        const auto candidate = battleground_.GetArenaOpponent(slot);
        if (!candidate.guid.IsEmpty() || !candidate.pet_guid.IsEmpty()) {
          continue;
        }
        ArenaOpponentSlot opponent{};
        opponent.guid = opponent_guid;
        battleground_.SetArenaOpponent(objects(), slot, opponent);
        break;
      }
    }
    if (const auto owner_slot = battleground_.FindArenaOpponentSlot(peeked_guid_raw);
        owner_slot.has_value() && party_stats_.last_update().pet_guid != 0) {
      battleground_.SetArenaOpponentPet(
          objects(), *owner_slot, ObjectGuid(party_stats_.last_update().pet_guid));
    }
  }

  if (const auto current_cached = party_stats_.GetCachedMember(peeked_guid_raw);
      current_cached.has_value()) {
    FireStockPartyMemberStatsEvents(previous_stats, current_cached->stats, packet_mask);
  }
}

void WorldSession::HandleSummonRequest(const net::wotlk::WorldPacket &pkt) {
  if (!summon_.ApplyRequest(pkt.payload.data(), pkt.payload.size(),
                            CurrentClientTimeMs())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireConfirmSummon();
}

void WorldSession::HandleRaidTargetUpdate(const net::wotlk::WorldPacket &pkt) {
  const auto update =
      DecodeRaidTargetUpdate(pkt.payload.data(), pkt.payload.size());
  if (!update.has_value()) {
    return;
  }
  auto& group = GroupSystem::Get();
  if (update->replace_all) {
    group.ReplaceRaidTargets({});
  }
  for (const auto& icon : update->icons) {
    group.SetRaidTargetIcon(icon.icon_id, icon.target_guid);
  }
}

void WorldSession::HandleRaidReadyCheck(const net::wotlk::WorldPacket &pkt) {
  const auto ready_check =
      DecodeRaidReadyCheck(pkt.payload.data(), pkt.payload.size());
  if (!ready_check.has_value()) {
    return;
  }

  const auto now_tick = ResolveReadyCheckNowTick(*this);
  GroupSystem::Get().StartReadyCheck(ready_check->initiator_guid, now_tick);
  active_ready_check_initiator_guid_ = ready_check->initiator_guid;
  active_ready_check_end_tick_ = now_tick + 30000u;
  ready_check_finish_sent_ = false;

  const auto active_player_guid = objects().GetActivePlayerGuid().GetRawValue();
  if (ready_check->initiator_guid != 0 &&
      ready_check->initiator_guid == active_player_guid) {
    DisplayLocalReadyCheckStartedMessage(objects());
  }

  DispatchReadyCheckStart(ready_check->initiator_guid);
}

void WorldSession::HandleRaidReadyCheckConfirm(const net::wotlk::WorldPacket &pkt) {
  const auto confirmation =
      DecodeRaidReadyCheckConfirm(pkt.payload.data(), pkt.payload.size());
  if (!confirmation.has_value()) {
    return;
  }

  GroupSystem::Get().SetReadyCheckResponse(confirmation->player_guid,
                                          confirmation->ready);
  ui::game::ScriptEventDispatch::Get().FireReadyCheckConfirm(
      confirmation->player_guid, confirmation->ready);

  if (!confirmation->ready && active_ready_check_initiator_guid_ != 0 &&
      active_ready_check_initiator_guid_ == objects().GetActivePlayerGuid().GetRawValue()) {
    const auto player_name =
        ResolveReadyCheckPlayerName(*this, confirmation->player_guid);
    if (!player_name.empty()) {
      const auto format =
          Localization::Get().GetString("RAID_MEMBER_NOT_READY", "RAID_MEMBER_NOT_READY");
      DisplayReadyCheckChatMessage(objects(),
                                   Localization::Get().FormatString(format, {player_name}));
    }
  }

  if (!ready_check_finish_sent_ && active_ready_check_initiator_guid_ != 0 &&
      active_ready_check_initiator_guid_ == objects().GetActivePlayerGuid().GetRawValue() &&
      !BuildReadyCheckCompletionSummary(*this).has_pending) {
    FinalizeLocalReadyCheck();
  }
}

void WorldSession::HandleRaidReadyCheckFinished(const net::wotlk::WorldPacket &pkt) {
  if (!DecodeRaidReadyCheckFinished(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  active_ready_check_initiator_guid_ = 0;
  active_ready_check_end_tick_ = 0;
  ready_check_finish_sent_ = false;
  pending_ready_check_initiator_guid_ = 0;
  GroupSystem::Get().ClearReadyCheck();
  ui::game::ScriptEventDispatch::Get().FireReadyCheckFinished();
}

void WorldSession::DispatchReadyCheckStart(const std::uint64_t initiator_guid) {
  if (initiator_guid == 0) {
    pending_ready_check_initiator_guid_ = 0;
    return;
  }

  const std::string initiator_name = ResolveReadyCheckPlayerName(*this, initiator_guid);
  if (initiator_name.empty()) {
    pending_ready_check_initiator_guid_ = initiator_guid;
    (void)query_cache_.RequestNameQuery(initiator_guid);
    return;
  }

  pending_ready_check_initiator_guid_ = 0;
  const auto now_tick = ResolveReadyCheckNowTick(*this);
  const auto time_left_seconds =
      active_ready_check_end_tick_ == 0 ||
              static_cast<std::int32_t>(now_tick - active_ready_check_end_tick_) >= 0
          ? 0u
          : (active_ready_check_end_tick_ - now_tick) / 1000u;
  ui::game::ScriptEventDispatch::Get().FireReadyCheck(initiator_name, time_left_seconds);

  if (initiator_guid != objects().GetActivePlayerGuid().GetRawValue()) {
    ui::game::DisplaySystemMessage(499, initiator_name.c_str());
  }
}

void WorldSession::ResolvePendingReadyCheckNameQuery(const std::uint64_t guid,
                                                     const bool name_unknown) {
  if (guid == 0 || pending_ready_check_initiator_guid_ != guid) {
    return;
  }

  if (name_unknown) {
    pending_ready_check_initiator_guid_ = 0;
    return;
  }

  DispatchReadyCheckStart(guid);
}

void WorldSession::CancelLocalReadyCheck(const bool interrupted) {
  auto &group_system = GroupSystem::Get();
  const bool had_ready_check =
      active_ready_check_initiator_guid_ != 0 || pending_ready_check_initiator_guid_ != 0 ||
      ready_check_finish_sent_ || group_system.GetReadyCheckInitiatorGuid() != 0 ||
      group_system.IsReadyCheckInProgress();

  active_ready_check_initiator_guid_ = 0;
  active_ready_check_end_tick_ = 0;
  ready_check_finish_sent_ = false;
  pending_ready_check_initiator_guid_ = 0;
  group_system.ClearReadyCheck();

  if (interrupted && had_ready_check) {
    ui::game::ScriptEventDispatch::Get().FireReadyCheckFinished(true);
  }
}

void WorldSession::ReconcileReadyCheckAfterGroupListUpdate(
    const std::size_t removed_member_count, const std::uint64_t previous_leader_guid) {
  auto &group_system = GroupSystem::Get();
  const auto active_player_guid = objects().GetActivePlayerGuid().GetRawValue();

  if (active_ready_check_initiator_guid_ == 0 || ready_check_finish_sent_ ||
      active_ready_check_end_tick_ == 0 || active_player_guid == 0 ||
      previous_leader_guid != group_system.GetLeaderGuid()) {
    return;
  }

  if (removed_member_count == 0 || active_ready_check_initiator_guid_ != active_player_guid) {
    return;
  }

  const bool can_finalize = group_system.IsInRaid()
                                ? group_system.HasRaidOfficerRank(active_player_guid)
                                : group_system.GetLeaderGuid() == active_player_guid;
  if (!can_finalize || HasOnlineWaitingReadyCheckMembers(group_system)) {
    return;
  }

  FinalizeLocalReadyCheck();
}

void WorldSession::FinalizeLocalReadyCheck() {
  if (ready_check_finish_sent_ || active_ready_check_initiator_guid_ == 0 ||
      active_ready_check_initiator_guid_ != objects().GetActivePlayerGuid().GetRawValue()) {
    return;
  }

  GroupSystem::Get().ExpireReadyCheck();
  active_ready_check_end_tick_ = 0;
  const auto summary = BuildReadyCheckCompletionSummary(*this);
  DisplayReadyCheckCompletionMessage(objects(), summary);

  ready_check_finish_sent_ = true;
  interaction_.SendReadyCheckFinished();
}

void WorldSession::HandlePartyAssignment(const net::wotlk::WorldPacket &pkt) {
  const auto assignment =
      DecodePartyAssignment(pkt.payload.data(), pkt.payload.size());
  if (!assignment.has_value()) {
    return;
  }
  GroupSystem::Get().ApplyTrackedPartyAssignment(assignment->role,
                                                 assignment->apply,
                                                 assignment->target_guid,
                                                 objects().GetLocalPlayerGuid().GetRawValue());
}

void WorldSession::HandleGuildBankList(const net::wotlk::WorldPacket &pkt) {
  if (!guild_bank_.HandleGuildBankList(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &bank = guild_bank_.last_bank_list();
  auto &guild = GuildSystem::Get();
  guild.SetGuildBankMoney(bank.money);
  guild.SetLastGuildBankTabWithdrawalsRemaining(bank.withdrawals_remaining);
  guild.SetGuildBankTabContentsRefreshPending(bank.tab, false);

  if (bank.full_update && bank.tab == 0) {
    std::vector<GuildBankTab> sys_tabs;
    sys_tabs.reserve(bank.tabs.size());
    for (const auto &t : bank.tabs) {
      GuildBankTab st;
      st.name = t.name;
      st.icon = t.icon;
      sys_tabs.push_back(std::move(st));
    }
    guild.SetBankTabs(sys_tabs);
  }

  if (bank.full_update && bank.tab == 0) {
    guild.ClearGuildBankTabItems(bank.tab);
  }

  for (const auto &item : bank.items) {
    if (item.slot >= GuildSystem::kGuildBankSlotsPerTab) {
      continue;
    }

    const std::size_t linear_slot_index =
        static_cast<std::size_t>(bank.tab) *
            GuildSystem::kGuildBankSlotsPerTab +
        static_cast<std::size_t>(item.slot);
    if (linear_slot_index >
        std::numeric_limits<std::uint32_t>::max()) {
      continue;
    }
    const auto linear_slot =
        static_cast<std::uint32_t>(linear_slot_index);
    const auto* held_item =
        held_cursor_ != nullptr
            ? held_cursor_->get_if<actions::held_cursor::GuildBankItem>()
            : nullptr;
    if (held_item != nullptr && held_item->linear_slot == linear_slot) {
      const auto held_item_entry = held_item->item_entry;
      held_cursor_->Clear();

      ui::game::detail::guild_bank_cursor::
          ClearGuildBankItemLockAtLinearSlotAndNotify(
              held_item_entry, linear_slot);
    }

    if (item.item_id == 0) {
      guild.ClearGuildBankTabItem(bank.tab, item.slot);
      continue;
    }

    ItemInstance stored_item{};
    stored_item.entry = item.item_id;
    stored_item.flags = static_cast<std::uint32_t>(std::max(item.flags, 0));
    stored_item.random_property = item.random_property_id;
    stored_item.random_suffix = static_cast<std::uint32_t>(std::max(item.random_property_seed, 0));
    stored_item.count = static_cast<std::uint32_t>(std::max(item.count, 0));
    stored_item.enchantments[0].id = static_cast<std::uint32_t>(std::max(item.enchant_id, 0));
    stored_item.charges[0] = item.charges;
    for (const auto &socket_enchant : item.socket_enchants) {
      if (socket_enchant.socket_index >= 3) {
        continue;
      }

      stored_item
          .enchantments[static_cast<std::size_t>(EnchantmentSlot::Socket1) +
                        socket_enchant.socket_index]
          .id = static_cast<std::uint32_t>(std::max(socket_enchant.enchant_id, 0));
    }

    guild.SetGuildBankTabItem(bank.tab, item.slot, stored_item);
  }

  const bool has_pending_item_refresh = guild_bank_.BeginGuildBankListRefresh(query_cache_);
  if (guild.GetBankerGuid() == 0) {
    const auto pending_banker_guid = guild.GetPendingBankerGuid();
    if (pending_banker_guid == 0) {
      guild.CloseBankFrame();
      ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILDBANKFRAME_CLOSED);
      return;
    }

    auto promoted_banker_guid = pending_banker_guid;
    ui::game::SetNpcInteractionTarget(ObjectGuid(promoted_banker_guid));
    if (guild.PromotePendingBankerGuid() != 0) {
      ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILDBANKFRAME_OPENED);
    }
    return;
  }

  if (!has_pending_item_refresh) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILDBANKBAGSLOTS_CHANGED);
  }
}

void WorldSession::HandleGuildBankLogQuery(const net::wotlk::WorldPacket &pkt) {
  if (!guild_bank_.HandleGuildBankLogQuery(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  GuildSystem::Get().SetGuildBankLog(guild_bank_.last_bank_log());

  const bool has_pending_refresh = guild_bank_.BeginGuildBankLogRefresh(
      query_cache_, [this](std::uint64_t raw_guid) {
        (void)query_cache_.RequestNameQuery(raw_guid);
      });
  if (!has_pending_refresh && GuildSystem::Get().GetBankerGuid() != 0) {
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILDBANKLOG_UPDATE);
  }
}

void WorldSession::HandleGuildBankMoneyWithdrawn(const net::wotlk::WorldPacket &pkt) {
  if (!guild_bank_.HandleGuildBankMoneyWithdrawn(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  GuildSystem::Get().SetGuildBankMoneyWithdrawRemaining(
      guild_bank_.last_money_withdrawn().remaining);
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILDBANK_UPDATE_WITHDRAWMONEY);
}

void WorldSession::HandleQueryGuildBankText(const net::wotlk::WorldPacket &pkt) {
  if (!guild_bank_.HandleGuildBankText(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &bank_text = guild_bank_.last_bank_text();
  GuildSystem::Get().SetGuildBankTabText(bank_text.tab, bank_text.text);
  ui::game::ScriptEventDispatch::Get().FireEventArgs(ui::game::events::GUILDBANK_UPDATE_TEXT,
                                                     {static_cast<int>(bank_text.tab) + 1});
}

void WorldSession::HandleArenaTeamQueryResponse(const net::wotlk::WorldPacket &pkt) {
  arena_.HandleArenaTeamQueryResponse(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleArenaTeamEvent(const net::wotlk::WorldPacket &pkt) {
  if (!arena_.HandleArenaTeamEvent(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& event = arena_.last_team_event();
  std::vector<std::string> arguments = event.strings;
  int message_id = 114;
  switch (event.event_type) {
    case 3: {
      message_id = 534;
      const auto active_player_name = ResolveActivePlayerName(*this);
      if (!arguments.empty() &&
          openwow::core::SStrCmpUTF8NoCase(active_player_name.c_str(),
                                          arguments.front().c_str(),
                                          0x7FFFFFFF) == 0) {
        message_id = 535;
        arguments.front() = arguments.size() > 1 ? arguments[1] : std::string{};
        arguments.resize(1);
      }
      break;
    }
    case 4:
      message_id = 536;
      break;
    case 5:
      message_id = 539;
      break;
    case 6:
      message_id = 537;
      break;
    case 7:
      message_id = 538;
      break;
    case 8:
      message_id = 540;
      break;
    default:
      break;
  }

  switch (arguments.size()) {
    case 1:
      ui::game::DisplaySystemMessage(message_id, arguments[0].c_str());
      break;
    case 2:
      ui::game::DisplaySystemMessage(message_id, arguments[0].c_str(),
                                     arguments[1].c_str());
      break;
    case 3:
      ui::game::DisplaySystemMessage(message_id, arguments[0].c_str(),
                                     arguments[1].c_str(),
                                     arguments[2].c_str());
      break;
    default:
      ui::game::DisplaySystemMessage(message_id);
      break;
  }

  ResetArenaRosterRequestsAndNotify(*this);
}

void WorldSession::ApplyLootOptOutState(const bool opt_out, const bool announce_change) {
  loot_.state().SetOptOut(opt_out);
  interaction().SendOptOutOfLoot(opt_out);
  if (announce_change) {
    DisplayOptOutOfLootStateMessage(objects(), opt_out);
  }
}

void WorldSession::HandleGroupDestroyed(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  auto &group_system = GroupSystem::Get();
  if (group_.IsInGroup()) {
    ui::game::DisplaySystemMessage(0x4b);
  }
  const auto previous_effective_dungeon = group_system.GetDungeonDifficulty();
  const auto previous_effective_raid = group_system.GetRaidDifficulty();
  const auto previous_player_difficulty = group_system.GetPlayerDifficultyIndex();
  group_.HandleGroupDestroyed();
  pending_raid_roster_name_queries_.clear();
  pending_raid_roster_local_player_resolution_ = false;
  group_system.ClearGroup();
  CancelLocalReadyCheck(true);
  if (previous_effective_dungeon != group_system.GetDungeonDifficulty() ||
      previous_effective_raid != group_system.GetRaidDifficulty() ||
      previous_player_difficulty != group_system.GetPlayerDifficultyIndex()) {
    RefreshGameObjectDifficultyVisibility();
  }
  DisplayGroupDifficultyChangedMessagesIfNeeded(*this, previous_effective_dungeon,
                                                previous_effective_raid);
  ui::game::ScriptEventDispatch::Get().FirePartyMembersChanged();
}

void WorldSession::HandleGroupUninvite(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  auto &group_system = GroupSystem::Get();
  const auto previous_effective_dungeon = group_system.GetDungeonDifficulty();
  const auto previous_effective_raid = group_system.GetRaidDifficulty();
  const auto previous_player_difficulty = group_system.GetPlayerDifficultyIndex();
  group_.HandleGroupUninvite();
  ui::game::DisplaySystemMessage(0x4e);
  pending_raid_roster_name_queries_.clear();
  pending_raid_roster_local_player_resolution_ = false;
  group_system.ClearGroup();
  CancelLocalReadyCheck(true);
  if (previous_effective_dungeon != group_system.GetDungeonDifficulty() ||
      previous_effective_raid != group_system.GetRaidDifficulty() ||
      previous_player_difficulty != group_system.GetPlayerDifficultyIndex()) {
    RefreshGameObjectDifficultyVisibility();
  }
  DisplayGroupDifficultyChangedMessagesIfNeeded(*this, previous_effective_dungeon,
                                                previous_effective_raid);
  ui::game::ScriptEventDispatch::Get().FirePartyMembersChanged();
}

void WorldSession::HandleRealGroupUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!group_.HandleRealGroupUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &real_update = group_.last_real_group_update();
  if (!real_update.has_value()) {
    return;
  }

  GroupSystem::Get().ApplyRealGroupUpdate(real_update->group_flags, real_update->member_count,
                                          real_update->leader_guid);
  ui::game::ScriptEventDispatch::Get().FirePartyMembersChanged();
}

void WorldSession::HandleGroupActionThrottled(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  if (group_.HandleGroupActionThrottled()) {
    ui::game::DisplaySystemMessage(0x254);
  }
}

void WorldSession::HandleBattlefieldMgrEntryInvite(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleEntryInvite(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &invite = *battlefield_mgr_.last_entry_invite();
  BattlefieldInfo::Get().ApplyBfMgrEntryInvite(invite.battle_id, invite.accept_flag);
}

void WorldSession::HandleBattlefieldMgrEntered(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleEntered(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &entered = *battlefield_mgr_.last_entered();
  BattlefieldInfo::Get().ApplyBfMgrEntered(
      objects(), entered.battle_id, entered.area_id, entered.status_flag,
      entered.secondary_flag, entered.cleared_afk);
}

void WorldSession::HandleBattlefieldMgrQueueInvite(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleQueueInvite(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &invite = *battlefield_mgr_.last_queue_invite();
  BattlefieldInfo::Get().ApplyBfMgrQueueInvite(
      objects(), invite.queue_id, invite.invite_flag, invite.warmup, invite.cleared_afk);
}

void WorldSession::HandleGroupJoinedBattleground(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleGroupJoinedBattleground(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &joined = *battlefield_mgr_.last_group_joined();
  (void)BattlefieldInfo::Get().ApplyGroupJoinedBattleground(
      joined.result, joined.player_guid, joined.has_guid, query_cache_,
      [this](const std::uint64_t raw_guid) { interaction_.SendNameQuery(raw_guid); });
}

void WorldSession::HandleGuildInfoPacket(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildInfoPacket(pkt.payload.data(), pkt.payload.size()) ||
      !guild_.guild_info_data().has_value()) {
    return;
  }

  const auto& info = *guild_.guild_info_data();
  auto& localization = Localization::Get();
  const std::string name_format =
      localization.GetString("GUILD_NAME_TEMPLATE", "");
  DisplaySystemChatMessage(objects(),
      localization.FormatString(name_format, {info.name}));

  const std::string info_format =
      localization.GetString("GUILD_INFO_TEMPLATE", "");
  DisplaySystemChatMessage(objects(), localization.FormatString(
      info_format,
      {std::to_string(info.created_month() + 1u),
       std::to_string(info.created_day() + 1u),
       std::to_string(info.created_year() + 2000u),
       std::to_string(info.num_members), std::to_string(info.num_accounts)}));
}

void WorldSession::HandleGuildDeclinePacket(const net::wotlk::WorldPacket &pkt) {
  if (!guild_.HandleGuildDeclinePacket(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ui::game::DisplaySystemMessage(0x5e, guild_.declined_name().c_str());
}

void WorldSession::HandleGroupCancel(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::string cancelling_player;
  if (!reader.ReadCString(cancelling_player, 0x30u)) {
    return;
  }

  group_.HandleGroupCancel();
  ui::game::ScriptEventDispatch::Get().FirePartyInviteCancel();
}

void WorldSession::HandleBattlefieldMgrEjected(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleEjected(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &ejected = *battlefield_mgr_.last_ejected();
  BattlefieldInfo::Get().ApplyBfMgrEjected(ejected.queue_id, ejected.reason);
}

void WorldSession::HandleBattlefieldMgrEjectPending(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleEjectPending(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &pending = *battlefield_mgr_.last_eject_pending();
  BattlefieldInfo::Get().ApplyBfMgrEjectPending(
      pending.queue_id, pending.reason, pending.relocate_flag,
      pending.battleground_flag);
}

void WorldSession::HandleBattlefieldMgrQueueRequestResponse(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleQueueRequestResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &response = *battlefield_mgr_.last_queue_response();
  BattlefieldInfo::Get().ApplyBfMgrQueueResponse(response.queue_id,
                                                 response.accepted);
}

void WorldSession::HandleBattlefieldMgrStateChange(const net::wotlk::WorldPacket &pkt) {
  if (!battlefield_mgr_.HandleStateChange(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &state = *battlefield_mgr_.last_state_change();
  BattlefieldInfo::Get().ApplyBfMgrStateChange(
      state.battlefield_id, state.area_id, state.expiry_time);
}

void WorldSession::HandleInspectResultsUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!inspect_.HandleInspectResultsUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
}

void WorldSession::HandleInspectArenaTeams(const net::wotlk::WorldPacket &pkt) {
  arena_.HandleInspectArenaTeams(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleArenaError(const net::wotlk::WorldPacket &pkt) {
  if (!arena_.HandleArenaError(pkt.payload.data(), pkt.payload.size()) ||
      !arena_.last_arena_error().has_value()) {
    return;
  }

  const auto& error = *arena_.last_arena_error();
  switch (error.error_type) {
    case 0:
      ui::game::DisplaySystemMessage(516, error.unk, error.unk);
      break;
    case 1:
      ui::game::DisplaySystemMessage(595);
      break;
    case 2:
      ui::game::DisplaySystemMessage(709);
      break;
    default:
      break;
  }
}

void WorldSession::HandleArenaTeamChangeFailedQueued(const net::wotlk::WorldPacket &pkt) {

  std::uint32_t queued = 0;
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  (void)reader.ReadU32(queued);

  if (queued != 0) {
    ui::game::DisplaySystemMessage(kArenaTeamChangeFailedQueuedSystemMessageId);
  }
}

void WorldSession::HandleArenaUnitDestroyed(const net::wotlk::WorldPacket &pkt) {
  if (!arena_.HandleArenaUnitDestroyed(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (!IsActiveArenaBattlefield()) {
    return;
  }

  battleground_.NotifyArenaUnitDestroyed(ObjectGuid(arena_.arena_unit_destroyed_guid()), objects());
}

void WorldSession::HandleJoinedBattlegroundQueue(const net::wotlk::WorldPacket &pkt) {
  arena_.HandleJoinedBattlegroundQueue(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleBattlefieldPortDenied(const net::wotlk::WorldPacket &pkt) {
  if (!arena_.HandleBattlefieldPortDenied(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (objects().GetActivePlayer() == nullptr) {
    return;
  }

  ui::game::DisplaySystemMessage(kBattlefieldPortDeniedSystemMessageId);
}

void WorldSession::HandleBattlegroundInfoThrottled(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  ui::game::DisplaySystemMessage(kBattlegroundInfoThrottledSystemMessageId);
}

void WorldSession::HandleRemovedFromPvpQueue(const net::wotlk::WorldPacket &pkt) {
  arena_.HandleRemovedFromPvpQueue(pkt.payload.data(), pkt.payload.size());

  std::uint32_t reason = 0;
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  (void)reader.ReadU32(reason);

  switch (reason) {
    case kRemovedFromPvpQueueReasonXpGain:
      ui::game::DisplaySystemMessage(kRemovedFromPvpQueueXpGainSystemMessageId);
      break;
    case kRemovedFromPvpQueueReasonFactionChange:
      ui::game::DisplaySystemMessage(
          kRemovedFromPvpQueueFactionChangeSystemMessageId);
      break;
    case kRemovedFromPvpQueueReasonGrantLevel:
      ui::game::DisplaySystemMessage(
          kRemovedFromPvpQueueGrantLevelSystemMessageId);
      break;
    default:
      break;
  }
}

void WorldSession::HandleReportPvpAfkResult(const net::wotlk::WorldPacket &pkt) {
  arena_.HandleReportPvpAfkResult(pkt.payload.data(), pkt.payload.size());

  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint8_t result = 0;
  if (!reader.ReadU8(result)) {
    return;
  }

  const bool enable = result == kPvpAfkResultNotifySystemEnabled;
  if (!enable && result != kPvpAfkResultNotifySystemDisabled) {
    return;
  }

  auto &cvars = ui::game::CVarSystem::Instance();
  if ((cvars.GetCVarInt(kEnablePvpNotifyAfkCVar) != 0) == enable) {
    return;
  }
  cvars.SetCVar(kEnablePvpNotifyAfkCVar, enable ? "1" : "0", true);

  const char *const key = enable ? kPvpReportAfkSystemEnabledKey
                                 : kPvpReportAfkSystemDisabledKey;
  DisplaySystemChatMessage(objects(), Localization::Get().GetString(key, key));
}

void WorldSession::HandleLfgPlayerInfo(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgPlayerInfo(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  FireLfgLockAndRandomInfoEvents(*this);
}

void WorldSession::HandleLfgPartyInfo(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgPartyInfo(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  FireLfgLockAndRandomInfoEvents(*this);
}

void WorldSession::HandleLfgRoleChosen(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleLfgRoleChosen(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (!lfg_.last_role_chosen()) {
    return;
  }

  const auto &role_choice = *lfg_.last_role_chosen();
  if (role_choice.ready != 0) {
    FireLfgRoleChosenEvent(*this, role_choice.guid, role_choice.roles);
  } else {
    ui::game::DisplaySystemMessage(703);
  }
}

void WorldSession::HandleLfgUpdateSearch(const net::wotlk::WorldPacket &pkt) {
  const auto previous_generation = lfg_.published_search_generation();
  if (!lfg_.HandleLfgUpdateSearch(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  if (lfg_.published_search_generation() != previous_generation) {
    if (auto *ui = world_ui_runtime()) {
      ui->frame_events().dispatcher().FireEvent(ui::game::events::LFG_UPDATE);
    }
  }
}

void WorldSession::HandleLfgDisabled(const net::wotlk::WorldPacket &pkt) {
  lfg_.HandleLfgDisabled(pkt.payload.data(), pkt.payload.size());
  LFGSystem::Get().Reset();
  FireLfgUpdateEvent(*this, false);
  ui::game::DisplaySystemMessage(557);
}

void WorldSession::HandleOpenLfgDungeonFinder(const net::wotlk::WorldPacket &pkt) {
  if (!lfg_.HandleOpenLfgDungeonFinder(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::LFG_OPEN_FROM_GOSSIP, {static_cast<int>(lfg_.open_lfg_dungeon_id())});
}

void WorldSession::HandleUpdateLfgList(const net::wotlk::WorldPacket &pkt) {
  const auto previous_generation = lfg_.published_search_generation();
  if (!lfg_.HandleUpdateLfgList(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  lfg_.QueueMissingSearchPlayerNameQueries(
      this, [this](const std::uint64_t raw_guid) {
        (void)query_cache_.RequestNameQuery(raw_guid);
      });
  if (lfg_.published_search_generation() != previous_generation) {
    if (auto *ui = world_ui_runtime()) {
      ui->frame_events().dispatcher().FireEvent(ui::game::events::LFG_UPDATE);
    }
  }
}

}

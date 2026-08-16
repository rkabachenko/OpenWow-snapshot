#pragma once

#include "openwow/game/chat_types.h"
#include "openwow/game/movement_info.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"
#include "openwow/net/wotlk/movement.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {
struct CommentatorInstanceKey;
struct CommentatorMapInfo;
struct CommentatorInstanceInfo;
}

namespace openwow::net::wotlk {

struct AuthSessionPayload {
  std::uint32_t build{12340};
  std::uint32_t login_server_id{0};
  std::string_view account_name;
  std::uint32_t login_server_type{0};
  std::uint32_t client_seed{0};
  std::uint32_t region_id{0};
  std::uint32_t battlegroup_id{0};
  std::uint32_t realm_id{0};
  std::uint64_t proof_of_work_nonce{0};
};

struct CalendarAddEventInvite {
  game::ObjectGuid invitee{game::ObjectGuid(0)};
  std::uint8_t status = 0;
  std::uint8_t moderator_status = 0;
};

enum SpellTargetFlag : std::uint32_t {
  kTargetFlagSelf = 0x00000000,
  kTargetFlagUnused1 = 0x00000001,
  kTargetFlagUnit = 0x00000002,
  kTargetFlagUnitRaid = 0x00000004,
  kTargetFlagUnused2 = 0x00000008,
  kTargetFlagItem = 0x00000010,
  kTargetFlagSourceLocation = 0x00000020,
  kTargetFlagDestLocation = 0x00000040,
  kTargetFlagObjectUnk = 0x00000080,
  kTargetFlagUnitUnk = 0x00000100,
  kTargetFlagPvpCorpse = 0x00000200,
  kTargetFlagUnitCorpse = 0x00000400,
  kTargetFlagGameObject = 0x00000800,
  kTargetFlagTradeItem = 0x00001000,
  kTargetFlagString = 0x00002000,
  kTargetFlagLocked = 0x00004000,
  kTargetFlagCorpseAlly = 0x00008000,
  kTargetFlagUnitMinipet = 0x00010000,
  kTargetFlagGlyphSlot = 0x00020000,
  kTargetFlagDestTarget = 0x00040000,
  kTargetFlagExtraTargets = 0x00080000,
  kTargetFlagUnitPassenger = 0x00100000,
};

inline constexpr std::uint8_t kClientSpellCastFlagHasTrajectory = 0x02;
inline constexpr std::size_t kSpellTargetStringCapacity = 0x80;

struct SpellTargets {
  std::uint32_t target_mask{0};

  game::ObjectGuid unit_target;

  game::ObjectGuid go_target;

  game::ObjectGuid item_target;

  game::ObjectGuid src_transport;
  float src_x{0}, src_y{0}, src_z{0};

  game::ObjectGuid dst_transport;
  float dst_x{0}, dst_y{0}, dst_z{0};

  std::string str_target;

  float trajectory_pitch{0.0f};
  float trajectory_speed{0.0f};
};

struct WhoQuery {
  std::uint32_t min_level{0};
  std::uint32_t max_level{100};
  std::string player_name;
  std::string guild_name;
  std::uint32_t race_mask{0xFFFFFFFF};
  std::uint32_t class_mask{0xFFFFFFFF};
  std::vector<std::uint32_t> zones;
  std::vector<std::string> strings;
};

struct MailAttachment {
  std::uint8_t slot{0};
  game::ObjectGuid item_guid;
};

struct PetSetActionSlotState {
  std::uint32_t slot{0};
  std::uint32_t action_data{0};
};

struct AuctionSearchParams {
  static constexpr std::size_t kMaxSortColumns = 12;

  game::ObjectGuid auctioneer_guid;
  std::uint32_t list_from{0};
  std::string search_string;
  std::uint8_t level_min{0};
  std::uint8_t level_max{0};
  std::uint32_t inventory_type{0xFFFFFFFFu};
  std::uint32_t item_class{0xFFFFFFFFu};
  std::uint32_t item_sub_class{0xFFFFFFFFu};
  std::uint32_t quality{0xFFFFFFFFu};
  std::uint8_t usable{0};
  std::uint8_t get_all{0};

  struct SortEntry {
    std::uint8_t column{0};
    std::uint8_t reversed{0};
  };
  std::vector<SortEntry> sort_columns;
};

class PacketSender {
public:
  PacketSender() = delete;

  static WorldPacket BuildAuthSession(std::uint32_t build,
                                      std::string_view account_name,
                                      std::uint32_t client_seed,
                                      const std::uint8_t digest[20],
                                      const std::vector<std::uint8_t> &addon_data = {});

  static WorldPacket BuildAuthSession(const AuthSessionPayload &payload,
                                      const std::uint8_t digest[20],
                                      const std::vector<std::uint8_t> &addon_data = {});

  static WorldPacket BuildCharEnum();

  static WorldPacket BuildCharCreate(std::string_view name, std::uint8_t race, std::uint8_t cls,
                                     std::uint8_t gender, std::uint8_t skin, std::uint8_t face,
                                     std::uint8_t hair_style, std::uint8_t hair_color,
                                     std::uint8_t facial_hair, std::uint8_t outfit_id = 0);

  static WorldPacket BuildCharDelete(std::uint64_t guid);

  static WorldPacket BuildPlayerLogin(std::uint64_t guid);

  static WorldPacket BuildVoiceChatEnable(bool voice_enabled, bool microphone_enabled);

  static WorldPacket BuildPing(std::uint32_t ping, std::uint32_t latency);

  static WorldPacket BuildLogoutRequest();

  static WorldPacket BuildLogoutCancel();

  static WorldPacket BuildKeepAlive();

  static WorldPacket BuildMovement(Opcode opcode, const game::ObjectGuid &mover,
                                   const game::MovementInfo &info);

  static WorldPacket BuildMoveHeartbeat(const game::ObjectGuid &mover,
                                        const game::MovementInfo &info);

  static WorldPacket BuildMoveSetFly(const game::ObjectGuid &mover, const game::MovementInfo &info);

  static WorldPacket BuildMoveTimeSkipped(const game::ObjectGuid &mover,
                                          std::uint32_t skipped_time_ms);

  static WorldPacket BuildForceSpeedChangeAck(game::SpeedType type, const game::ObjectGuid &mover,
                                              std::uint32_t counter, const game::MovementInfo &info,
                                              float new_speed);

  static WorldPacket BuildForceSpeedChangeAck(Opcode ack_opcode, const game::ObjectGuid &mover,
                                              std::uint32_t counter, const game::MovementInfo &info,
                                              float new_speed);

  static WorldPacket BuildForceMoveRootAck(const game::ObjectGuid &mover, std::uint32_t counter,
                                           const game::MovementInfo &info);

  static WorldPacket BuildForceMoveUnrootAck(const game::ObjectGuid &mover, std::uint32_t counter,
                                             const game::MovementInfo &info);

  static WorldPacket BuildMoveKnockBackAck(const game::ObjectGuid &mover, std::uint32_t counter,
                                           const game::MovementInfo &info);

  static WorldPacket BuildMoveSetCanFlyAck(const game::ObjectGuid &mover, std::uint32_t counter,
                                           const game::MovementInfo &info, bool enabled);

  static WorldPacket BuildMoveFeatherFallAck(const game::ObjectGuid &mover,
                                             std::uint32_t counter,
                                             const game::MovementInfo &info, bool enabled);

  static WorldPacket BuildMoveWaterWalkAck(const game::ObjectGuid &mover,
                                           std::uint32_t counter,
                                           const game::MovementInfo &info, bool enabled);

  static WorldPacket BuildMoveHoverAck(const game::ObjectGuid &mover, std::uint32_t counter,
                                       const game::MovementInfo &info, bool enabled);

  static WorldPacket BuildMoveSetCanTransitionBetweenSwimAndFlyAck(
      const game::ObjectGuid &mover, std::uint32_t counter, const game::MovementInfo &info,
      bool enabled);

  static WorldPacket BuildMoveGravityDisableAck(const game::ObjectGuid &mover,
                                                std::uint32_t counter,
                                                const game::MovementInfo &info);

  static WorldPacket BuildMoveGravityEnableAck(const game::ObjectGuid &mover,
                                               std::uint32_t counter,
                                               const game::MovementInfo &info);

  static WorldPacket BuildMoveSetCollisionHeightAck(const game::ObjectGuid &mover,
                                                    std::uint32_t counter,
                                                    const game::MovementInfo &info,
                                                    float collision_height);

  static WorldPacket BuildMoveSplineDone(const game::ObjectGuid &mover,
                                         const game::MovementInfo &info,
                                         std::uint32_t spline_id);

  static WorldPacket BuildTimeSyncResponse(std::uint32_t counter, std::uint32_t client_time_ms);

  static WorldPacket BuildSetSelection(std::uint64_t target_guid);

  static WorldPacket BuildAttackSwing(std::uint64_t target_guid);

  static WorldPacket BuildAttackStop();

  static WorldPacket BuildCastSpell(std::uint8_t cast_count, std::uint32_t spell_id,
                                    std::uint8_t cast_flags,
                                    const SpellTargets &targets);

  static WorldPacket BuildPetCastSpell(std::uint64_t pet_guid,
                                       std::uint8_t cast_count,
                                       std::uint32_t spell_id,
                                       std::uint8_t cast_flags,
                                       const SpellTargets& targets);

  static WorldPacket BuildCancelCast(std::uint8_t cast_count,
                                     std::uint32_t spell_id);

  static WorldPacket BuildCancelAura(std::uint32_t spell_id);

  static WorldPacket BuildCancelChannelling(std::uint32_t spell_id);

  static WorldPacket BuildCancelAutoRepeat();

  static WorldPacket BuildUpdateProjectilePosition(std::uint64_t caster_guid,
                                                   std::uint32_t spell_id,
                                                   std::uint8_t cast_count,
                                                   float x, float y, float z);

  static WorldPacket BuildChatMessage(game::ChatMsg type, game::Language language,
                                      std::string_view message,
                                      std::string_view target_or_channel = {});

  static WorldPacket BuildJoinChannel(std::uint32_t channel_id, std::string_view channel_name,
                                      std::string_view password = {},
                                      bool has_voice = false,
                                      std::uint8_t join_flag = 0);

  static WorldPacket BuildLeaveChannel(std::uint32_t channel_id, std::string_view channel_name);

  static WorldPacket BuildGossipHello(std::uint64_t npc_guid);

  static WorldPacket BuildGossipSelectOption(std::uint64_t npc_guid, std::uint32_t menu_id,
                                             std::uint32_t gossip_list_id,
                                             std::string_view code_text = {});

  static WorldPacket BuildQuestgiverHello(std::uint64_t npc_guid);

  static WorldPacket BuildQuestgiverAcceptQuest(std::uint64_t npc_guid, std::uint32_t quest_id,
                                                std::uint32_t accept_packet_value = 0);

  static WorldPacket BuildQuestgiverCompleteQuest(std::uint64_t npc_guid, std::uint32_t quest_id);

  static WorldPacket BuildQuestgiverChooseReward(std::uint64_t npc_guid, std::uint32_t quest_id,
                                                 std::uint32_t reward_index);

  static WorldPacket BuildQuestgiverRequestReward(std::uint64_t npc_guid, std::uint32_t quest_id);

  static WorldPacket BuildQuestPushResult(std::uint64_t receiver_guid, std::uint32_t quest_id,
                                          std::uint8_t result);

  static WorldPacket BuildWorldStateUiTimerUpdate();

  static WorldPacket BuildPushQuestToParty(std::uint32_t quest_id);

  static WorldPacket BuildQuestlogRemoveQuest(std::uint8_t slot);

  static WorldPacket BuildQuestQuery(std::uint32_t quest_id);

  static WorldPacket BuildQuestgiverStatusQuery(std::uint64_t guid);

  static WorldPacket BuildQuestgiverStatusMultipleQuery();

  static WorldPacket BuildQuestPoiQuery(const std::vector<std::uint32_t> &quest_ids);

  static WorldPacket BuildSwapInvItem(std::uint8_t dst_slot, std::uint8_t src_slot);

  static WorldPacket BuildSwapItem(std::uint8_t dst_bag, std::uint8_t dst_slot,
                                   std::uint8_t src_bag, std::uint8_t src_slot);

  static WorldPacket BuildAutoEquipItem(std::uint8_t src_bag, std::uint8_t src_slot);

  static WorldPacket BuildDestroyItem(std::uint8_t bag, std::uint8_t slot,
                                      std::uint32_t count);

  static WorldPacket BuildOpenItem(std::uint8_t bag, std::uint8_t slot);

  static WorldPacket BuildWrapItem(std::uint8_t source_bag, std::uint8_t source_slot,
                                   std::uint8_t target_bag, std::uint8_t target_slot);

  static WorldPacket BuildReadItem(std::uint8_t bag, std::uint8_t slot);

  static WorldPacket BuildItemTextQuery(std::uint64_t item_guid);

  static WorldPacket BuildUseItem(std::uint8_t bag_index, std::uint8_t slot,
                                  std::uint8_t cast_count, std::uint32_t spell_id,
                                  std::uint64_t item_guid, std::uint32_t glyph_index,
                                  std::uint8_t cast_flags,
                                  const SpellTargets &targets);

  static WorldPacket BuildSplitItem(std::uint8_t src_bag, std::uint8_t src_slot,
                                    std::uint8_t dst_bag, std::uint8_t dst_slot,
                                    std::uint32_t count);

  static WorldPacket BuildSellItem(std::uint64_t vendor_guid, std::uint64_t item_guid,
                                   std::uint32_t count);
  static WorldPacket BuildItemRefundInfo(std::uint64_t item_guid);
  static WorldPacket BuildItemRefund(std::uint64_t item_guid);
  static WorldPacket BuildSelfResurrect();

  static WorldPacket BuildBuyItem(std::uint64_t vendor_guid, std::uint32_t item_entry,
                                  std::uint32_t slot, std::uint32_t count,
                                  std::uint8_t bag);

  static WorldPacket BuildBuyItemInSlot(std::uint64_t vendor_guid, std::uint32_t item_entry,
                                        std::uint32_t vendor_slot, std::uint64_t target_guid,
                                        std::uint8_t target_slot, std::uint32_t count);

  static WorldPacket BuildLoot(std::uint64_t target_guid);

  static WorldPacket BuildAutoStoreLootItem(std::uint8_t loot_slot);

  static WorldPacket BuildGroupInvite(std::string_view player_name, std::uint32_t role_flags = 0);

  static WorldPacket BuildGroupAccept(std::uint32_t role_flags = 0);

  static WorldPacket BuildGroupDecline();

  static WorldPacket BuildGroupDisband();

  static WorldPacket BuildGroupSetLeader(std::uint64_t target_guid);

  static WorldPacket BuildGroupUninviteByGuid(std::uint64_t target_guid,
                                              std::string_view reason = "");

  static WorldPacket BuildGroupAssistantLeader(std::uint64_t target_guid, bool set);

  static WorldPacket BuildLootMethod(std::uint32_t method, std::uint64_t master_guid,
                                     std::uint32_t threshold);

  static WorldPacket BuildReadyCheck();

  static WorldPacket BuildReadyCheckFinished();

  static WorldPacket BuildRequestPartyMemberStats(std::uint64_t target_guid);

  static WorldPacket BuildAddFriend(std::string_view name, std::string_view note = {});

  static WorldPacket BuildDelFriend(std::uint64_t guid);

  static WorldPacket BuildAddIgnore(std::string_view name);

  static WorldPacket BuildDelIgnore(std::uint64_t guid);

  static WorldPacket BuildWho(const WhoQuery &query);

  static WorldPacket BuildLearnTalent(std::uint32_t talent_id, std::uint32_t talent_rank);

  static WorldPacket BuildLearnPetTalent(std::uint64_t pet_guid,
                                         std::uint32_t talent_id,
                                         std::uint32_t talent_rank);

  static WorldPacket BuildBuySkillStep(std::uint32_t skill_id);

  static WorldPacket
  BuildBuySkillRanks(const std::vector<std::pair<std::uint32_t, std::uint32_t>> &queued_ranks);

  static WorldPacket BuildSetActiveMover(std::uint64_t mover_guid);

  static WorldPacket BuildFarSight(bool enable);

  static WorldPacket BuildSetActionBarToggles(std::uint8_t toggles);

  static WorldPacket BuildBattlefieldPort(std::uint64_t battlefield_instance_guid, bool accepted);

  static WorldPacket BuildInstanceLockResponse(bool accept);

  static WorldPacket BuildBattlemasterJoin(std::uint64_t battlemaster_guid,
                                           std::uint32_t bg_type_id, std::uint32_t instance_id,
                                           bool join_as_group);

  static WorldPacket BuildBattlemasterJoinArena(std::uint64_t battlemaster_guid, std::uint8_t slot,
                                                bool as_group, bool is_rated);

  static WorldPacket BuildCommentatorEnable(std::uint32_t mode);

  static WorldPacket BuildCommentatorGetMapInfo(std::string_view zone);

  static WorldPacket
  BuildCommentatorGetPlayerInfo(const game::CommentatorInstanceKey &instance_key);

  static WorldPacket BuildCommentatorEnterInstance(const game::CommentatorInstanceKey &instance_key,
                                                   std::uint64_t instance_guid);

  static WorldPacket BuildCommentatorStartInstance(std::uint64_t battlemaster_guid,
                                                   std::uint32_t map_id,
                                                   std::uint32_t team_size,
                                                   std::uint32_t min_level,
                                                   std::uint32_t max_level);

  static WorldPacket BuildCommentatorAddPlayer(const game::CommentatorMapInfo& map,
                                               const game::CommentatorInstanceInfo& instance,
                                               std::uint64_t player_guid,
                                               std::uint64_t battlemaster_guid,
                                               std::uint32_t team_index);

  static WorldPacket BuildCommentatorRemovePlayer(const game::CommentatorMapInfo& map,
                                                  const game::CommentatorInstanceInfo& instance,
                                                  std::uint64_t player_guid,
                                                  std::uint64_t battlemaster_guid,
                                                  std::uint32_t team_index);

  static WorldPacket BuildCommentatorExitInstance();

  static WorldPacket BuildCommentatorSetSkirmishMatchmakingMode(std::uint8_t mode);

  static WorldPacket BuildCommentatorRequestSkirmishQueueData();

  static WorldPacket BuildCommentatorStartSkirmishMatch(std::uint64_t first_guid,
                                                        std::uint64_t second_guid,
                                                        std::int32_t match_size);

  static WorldPacket BuildCommentatorRequestSkirmishMode();

  static WorldPacket BuildArenaTeamAccept();

  static WorldPacket BuildArenaTeamDecline();

  static WorldPacket BuildLeaveBattlefield(game::ObjectGuid battlefield_guid);

  static WorldPacket BuildRequestVehicleExit();

  static WorldPacket BuildRequestVehicleSwitchSeat(std::uint64_t vehicle_guid,
                                                   std::uint8_t seat_id);

  static WorldPacket BuildRequestVehicleNextSeat();

  static WorldPacket BuildRequestVehiclePrevSeat();

  static WorldPacket BuildSpellClick(std::uint64_t unit_guid);

  static WorldPacket BuildPlayerVehicleEnter(std::uint64_t unit_guid);

  static WorldPacket BuildControllerEjectPassenger(std::uint64_t passenger_guid);

  static WorldPacket BuildGuildCreate(std::string_view guild_name);

  static WorldPacket BuildGuildInvite(std::string_view player_name);

  static WorldPacket BuildGuildAccept();

  static WorldPacket BuildGuildDecline();

  static WorldPacket BuildGuildLeave();

  static WorldPacket BuildGuildDisband();

  static WorldPacket BuildGuildMotd(std::string_view motd);

  static WorldPacket BuildGuildInfoText(std::string_view info_text);

  static WorldPacket BuildGuildRoster();

  static WorldPacket BuildGuildRank(std::uint32_t rank_id, std::uint32_t rights,
                                    std::string_view rank_name, std::uint32_t money_per_day,
                                    const std::array<std::uint32_t, 6> &bank_tab_flags,
                                    const std::array<std::uint32_t, 6> &bank_tab_withdraw);

  static WorldPacket BuildGuildPromote(std::string_view player_name);

  static WorldPacket BuildGuildDemote(std::string_view player_name);

  static WorldPacket BuildGuildRemove(std::string_view player_name);

  static WorldPacket BuildBattlemasterHello(std::uint64_t guid);

  static WorldPacket BuildBankerActivate(std::uint64_t banker_guid);

  static WorldPacket BuildBuyBankSlot(std::uint64_t banker_guid);

  static WorldPacket BuildAutoBankItem(std::uint8_t bag, std::uint8_t slot);

  static WorldPacket BuildAutoStoreBankItem(std::uint8_t bag, std::uint8_t slot);

  static WorldPacket BuildGuildBankQueryTab(std::uint64_t banker_guid, std::uint8_t tab_id,
                                            bool full_update);

  static WorldPacket BuildGuildBankDepositMoney(std::uint64_t banker_guid, std::uint32_t money);

  static WorldPacket BuildGuildBankWithdrawMoney(std::uint64_t banker_guid, std::uint32_t money);

  static WorldPacket
  BuildGuildBankSwapItemsPlayerToBank(std::uint64_t banker_guid, std::uint8_t bank_tab,
                                      std::uint8_t bank_slot, std::uint32_t destination_item_entry,
                                      std::uint8_t player_bag, std::uint8_t player_slot,
                                      std::uint32_t stack_count);

  static WorldPacket
  BuildGuildBankSwapItemsBankToPlayer(std::uint64_t banker_guid, std::uint8_t source_tab,
                                      std::uint8_t source_slot, std::uint32_t source_item_entry,
                                      std::uint8_t player_bag, std::uint8_t player_slot,
                                      std::uint32_t stack_count);

  static WorldPacket
  BuildGuildBankSwapItemsBankToBank(std::uint64_t banker_guid, std::uint8_t source_tab,
                                    std::uint8_t source_slot, std::uint32_t destination_item_entry,
                                    std::uint8_t destination_tab, std::uint8_t destination_slot,
                                    std::uint32_t held_item_entry, std::uint32_t stack_count);

  static WorldPacket BuildCalendarGetCalendar();

  static WorldPacket BuildCalendarGetNumPending();

  static WorldPacket BuildCalendarAddEvent(std::string_view title, std::string_view description,
                                           std::uint8_t event_type, std::uint8_t repeat_type,
                                           std::uint32_t max_invites, std::int32_t dungeon_id,
                                           std::uint32_t event_time, std::uint32_t time_zone_time,
                                           std::uint32_t flags,
                                           const std::vector<CalendarAddEventInvite> &invites);

  static WorldPacket BuildCalendarRemoveEvent(std::uint64_t event_id, std::uint64_t invite_id,
                                              std::uint32_t flags);

  static WorldPacket BuildCalendarRemoveEventBuffer(std::uint64_t event_id,
                                                    std::uint64_t invite_id,
                                                    bool uses_guild_calendar);

  static WorldPacket BuildCalendarEventSignUp(std::uint64_t event_id, std::uint8_t tentative);

  static WorldPacket BuildCalendarEventRsvp(std::uint64_t event_id, std::uint64_t invite_id,
                                            std::uint32_t status);

  static WorldPacket BuildCalendarEventRemoveInvite(std::uint64_t target_invitee_guid,
                                                    std::uint64_t event_id,
                                                    std::uint64_t target_invite_id,
                                                    std::uint64_t self_invite_id);

  static WorldPacket BuildCalendarEventModeratorStatus(std::uint64_t target_invitee_guid,
                                                       std::uint64_t event_id,
                                                       std::uint64_t target_invite_id,
                                                       std::uint64_t self_invite_id,
                                                       std::uint32_t status);

  static WorldPacket BuildCalendarEventStatus(std::uint64_t target_invitee_guid,
                                              std::uint64_t event_id,
                                              std::uint64_t target_invite_id,
                                              std::uint64_t self_invite_id, std::uint32_t status);

  static WorldPacket BuildLfgSetRoles(std::uint8_t roles);

  static WorldPacket BuildLfgSetNeeds();

  static WorldPacket BuildLfgJoin(std::uint32_t roles, const std::vector<std::uint32_t> &dungeons,
                                  std::string_view comment);

  static WorldPacket BuildLfgLeave();

  static WorldPacket BuildLfgProposalResult(std::uint32_t proposal_id, bool accept);

  static WorldPacket BuildLfgSetBootVote(bool agree);

  static WorldPacket BuildPetAction(std::uint64_t pet_guid, std::uint32_t action_data,
                                    std::uint64_t target_guid);

  static WorldPacket BuildPetSetAction(std::uint64_t pet_guid,
                                       std::optional<PetSetActionSlotState> secondary_slot,
                                       PetSetActionSlotState target_slot);

  static WorldPacket BuildPetRename(std::uint64_t pet_guid, std::string_view new_name,
                                    const std::array<std::string, 5> *declined_names = nullptr);

  static WorldPacket BuildPetitionRename(std::uint64_t petition_guid, std::string_view new_name);

  static WorldPacket BuildPetitionSign(std::uint64_t petition_guid, std::uint8_t petition_choice);

  static WorldPacket BuildPetitionBuy(std::uint64_t npc_guid, std::uint32_t petition_type,
                                      std::string_view petition_name);

  static WorldPacket BuildTurnInPetition(std::uint64_t petition_guid,
                                         const std::array<std::uint32_t, 5>& extra_fields);

  static WorldPacket BuildPetitionQuery(std::uint32_t petition_id, std::uint64_t petition_guid);

  static WorldPacket BuildPetitionDecline(std::uint64_t petition_guid);

  static WorldPacket BuildTabardVendorActivate(std::uint64_t vendor_guid);

  static WorldPacket BuildPetitionShowList(std::uint64_t npc_guid);

  static WorldPacket BuildPetAbandon(std::uint64_t pet_guid);

  static WorldPacket BuildDismissCritter(std::uint64_t critter_guid);

  static WorldPacket BuildPetSpellAutocast(std::uint64_t pet_guid, std::uint32_t spell_id,
                                           bool enabled);

  static WorldPacket BuildRequestPetInfo();

  static WorldPacket BuildRepopRequest(bool auto_release = false);

  static WorldPacket BuildResurrectResponse(std::uint64_t resurrecter_guid, bool accept);

  static WorldPacket BuildSpiritHealerActivate(std::uint64_t healer_guid);

  static WorldPacket BuildAreaSpiritHealerQuery(std::uint64_t healer_guid);

  static WorldPacket BuildAreaSpiritHealerQueue(std::uint64_t healer_guid);

  static WorldPacket BuildReclaimCorpse(std::uint64_t corpse_guid);

  static WorldPacket BuildHearthAndResurrect();

  static WorldPacket BuildCompleteMovie();

  static WorldPacket BuildReadyForAccountDataTimes();

  static WorldPacket BuildRealmSplit(std::uint32_t split_state);

  static WorldPacket BuildInspect(std::uint64_t target_guid);

  static WorldPacket BuildItemQuerySingle(std::uint32_t item_entry);

  static WorldPacket BuildCreatureQuery(std::uint32_t entry, std::uint64_t guid);

  static WorldPacket BuildGameObjectQuery(std::uint32_t entry, std::uint64_t guid);

  static WorldPacket BuildSetPvP(std::uint8_t flag);

  static WorldPacket BuildSetSheathed(std::uint32_t sheath_state);

  static WorldPacket BuildEmote(std::uint32_t emote_id);

  static WorldPacket BuildTextEmote(std::uint32_t text_emote, std::uint32_t emote_num,
                                    std::uint64_t target_guid);

  static WorldPacket BuildPlayDance(std::uint32_t dance_id, std::uint32_t sequence_id);

  static WorldPacket BuildCompleteCinematic();

  static WorldPacket BuildNextCinematicCamera();

  static WorldPacket BuildQueryTime();

  static WorldPacket BuildZoneUpdate(std::uint32_t zone_id);

  static WorldPacket BuildSetActiveVoiceChannel(std::uint32_t channel_type,
                                                std::string_view channel_name);

};

}

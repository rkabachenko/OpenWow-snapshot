
#pragma once

#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/object_guid.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openwow/game/world_session_fwd.h"

namespace openwow::game {

struct TalentEntry {
  std::uint32_t talent_id = 0;
  std::uint8_t rank = 0;
};

struct EquipmentSetSave;
struct EquipmentSetUse;

class InteractionSender {
public:
  using ProposalResponseClockFn = std::function<double()>;

  explicit InteractionSender(WorldSession& session);
  void SetProposalResponseClockFn(ProposalResponseClockFn fn) {
    proposal_response_clock_fn_ = std::move(fn);
  }

  void SendGossipHello(std::uint64_t guid);
  void SendGossipSelectOption(std::uint64_t guid, std::uint32_t menu_id, std::uint32_t option_id,
                              const std::string &code = "");
  void SendNpcTextQuery(std::uint64_t guid, std::uint32_t text_id);

  void SendQuestGiverHello(std::uint64_t guid);

  void SendQuestGiverQueryQuest(std::uint64_t guid, std::uint32_t quest_id);
  void SendQuestGiverAcceptQuest(std::uint64_t guid, std::uint32_t quest_id,
                                 std::uint32_t accept_packet_value = 0);
  void SendQuestGiverCompleteQuest(std::uint64_t guid, std::uint32_t quest_id);
  void SendQuestGiverRequestReward(std::uint64_t guid, std::uint32_t quest_id);
  void SendQuestGiverChooseReward(std::uint64_t guid, std::uint32_t quest_id,
                                  std::uint32_t reward_index);
  void SendQuestPushResult(std::uint64_t receiver_guid, std::uint32_t quest_id,
                           std::uint8_t result);
  void SendQuestQuery(std::uint32_t quest_id);
  void SendQuestPoiQuery(const std::vector<std::uint32_t> &quest_ids);
  void SendQuestConfirmAccept(std::uint32_t quest_id);
  void SendQuestLogRemoveQuest(std::uint8_t slot);
  void SendQueryTime();

  void SendListInventory(std::uint64_t guid);
  void SendBuyItem(std::uint64_t vendor_guid, std::uint32_t item_id, std::uint32_t slot,
                   std::uint32_t count, std::uint8_t bag);
  void SendBuyItemInSlot(std::uint64_t vendor_guid, std::uint32_t item_id,
                         std::uint32_t vendor_slot, std::uint64_t target_guid,
                         std::uint8_t target_slot, std::uint32_t count);
  void SendSellItem(std::uint64_t vendor_guid, std::uint64_t item_guid, std::uint32_t count);
  [[nodiscard]] bool SendItemRefundInfo(std::uint64_t item_guid);

  [[nodiscard]] bool SendItemRefund(std::uint64_t item_guid);
  [[nodiscard]] bool SendSelfResurrect();
  void SendBuybackItem(std::uint64_t vendor_guid, std::uint32_t slot);
  void SendRepairItem(std::uint64_t vendor_guid, std::uint64_t item_guid, bool guild_bank);

  void SendTrainerList(std::uint64_t guid);
  void SendTrainerBuySpell(std::uint64_t guid, std::uint32_t spell_id);

  void SendLoot(std::uint64_t guid);
  void SendAutoStoreLootItem(std::uint8_t slot);
  void SendLootRelease(std::uint64_t guid);
  void SendLootMoney();
  void SendAutoEquipItem(std::uint8_t bag, std::uint8_t slot);

  void SendGetMailList(std::uint64_t mailbox_guid);
  void SendSendMail(std::uint64_t mailbox_guid, const std::string &recipient,
                    const std::string &subject, const std::string &body, std::uint32_t money,
                    std::uint32_t cod, std::uint32_t stationery = 0,
                    const std::vector<MailSendAttachment> &attachments = {},
                    std::uint32_t package_id = 0);
  void SendMailTakeMoney(std::uint64_t mailbox_guid, std::uint32_t mail_id);
  void SendMailTakeItem(std::uint64_t mailbox_guid, std::uint32_t mail_id,
                        std::uint32_t item_low_guid);
  void SendMailDelete(std::uint64_t mailbox_guid, std::uint32_t mail_id, MailDeleteReason reason);
  void SendMailReturnToSender(std::uint64_t mailbox_guid, std::uint32_t mail_id,
                              std::uint64_t original_sender_guid);
  void SendMailMarkAsRead(std::uint64_t mailbox_guid, std::uint32_t mail_id);
  void SendMailCreateTextItem(std::uint64_t mailbox_guid, std::uint32_t mail_id);
  void SendMailFollowup(const MailFollowupCommand& command);
  void SendQueryNextMailTime();

  void SendBankerActivate(std::uint64_t guid);
  void SendBuyBankSlot(std::uint64_t banker_guid);
  void SendAutoBankItem(std::uint8_t bag, std::uint8_t slot);
  void SendAutoStoreBankItem(std::uint8_t bag, std::uint8_t slot);

  void SendBinderActivate(std::uint64_t guid);

  void SendAuctionHello(std::uint64_t guid);
  void SendAuctionListItems(const net::wotlk::AuctionSearchParams &params);
  void SendAuctionPlaceBid(std::uint64_t auctioneer, std::uint32_t auction_id, std::uint32_t bid);
  void SendAuctionListOwnerItems(std::uint64_t auctioneer);
  void SendAuctionListPendingSales(std::uint64_t auctioneer);
  void SendAuctionListBidderItems(std::uint64_t auctioneer, std::uint32_t list_from,
                                  const std::vector<std::uint32_t> &outbidded_ids);
  void SendAuctionSellItem(std::uint64_t auctioneer, std::uint64_t item_guid, std::uint32_t count,
                           std::uint32_t start_bid, std::uint32_t buyout, std::uint32_t runtime);
  void SendAuctionSellItems(
      std::uint64_t auctioneer,
      const std::vector<std::pair<std::uint64_t, std::uint32_t>>& items,
      std::uint32_t start_bid, std::uint32_t buyout,
      std::uint32_t duration_minutes);
  void SendAuctionRemoveItem(std::uint64_t auctioneer, std::uint32_t auction_id);

  void SendCastSpell(std::uint32_t spell_id, std::uint8_t cast_flags, std::uint64_t target_guid);

  [[nodiscard]] bool TrySendCastSpell(std::uint32_t spell_id, std::uint8_t cast_flags,
                                      std::uint64_t target_guid);
  void SendCastSpellOnItem(std::uint32_t spell_id, std::uint8_t cast_flags,
                           std::uint64_t item_guid);
  [[nodiscard]] bool SendCastSpellOnUnitAndItem(std::uint32_t spell_id,
                                                std::uint8_t cast_flags,
                                                std::uint64_t target_guid,
                                                std::uint64_t item_guid);
  void SendCastSpellOnTradeEnchantSlot(std::uint32_t spell_id, std::uint8_t cast_flags);
  void SendCastSpellOnTradeItem(std::uint32_t spell_id, std::uint8_t cast_flags,
                                std::uint64_t item_guid);
  void SendCancelCast(std::uint32_t spell_id);
  void SendCancelAura(std::uint32_t spell_id);

  void SendTotemDestroyed(std::uint8_t slot);

  bool SendAttackSwing(std::uint64_t guid);
  void SendAttackStop();

  bool TryQueueBindOnUseConfirmation(std::uint64_t item_guid, std::uint32_t item_entry,
                                     std::uint32_t item_flags, std::uint64_t target_guid);
  bool ConfirmPendingBindOnUse();
  bool SendUseItemByGuid(std::uint64_t item_guid, std::uint8_t cast_flags,
                         std::uint64_t target_guid = 0);
  bool SendUseItemByGuidToGlyphSlot(std::uint64_t item_guid, std::uint32_t glyph_index);
  bool SendOpenItem(std::uint8_t bag, std::uint8_t slot, bool play_wrapped_gift_sound = false);
  bool SendWrapItem(std::uint8_t source_bag, std::uint8_t source_slot, std::uint8_t target_bag,
                    std::uint8_t target_slot);
  bool SendReadItem(std::uint8_t bag, std::uint8_t slot);
  bool SendUseItem(std::uint8_t bag, std::uint8_t slot,
                   std::uint8_t cast_flags);
  bool SendUseItem(std::uint8_t bag, std::uint8_t slot,
                   std::uint8_t cast_flags, std::uint64_t target_guid);
  void SendDestroyItem(std::uint8_t bag, std::uint8_t slot, std::uint32_t count);
  void SendSwapInvItem(std::uint8_t dst_slot, std::uint8_t src_slot);
  void SendSwapItem(std::uint8_t dst_bag, std::uint8_t dst_slot, std::uint8_t src_bag,
                    std::uint8_t src_slot);
  void SendSplitItem(std::uint8_t src_bag, std::uint8_t src_slot, std::uint8_t dst_bag,
                     std::uint8_t dst_slot, std::uint32_t count);
  void SendAutoStoreBagItem(std::uint8_t src_bag, std::uint8_t src_slot, std::uint8_t dst_bag);
  void SendSetAmmo(std::uint32_t item_entry);
  void SendSaveEquipmentSet(const EquipmentSetSave& request);
  void SendDeleteEquipmentSet(ObjectGuid set);
  void SendUseEquipmentSet(const EquipmentSetUse& request);

  void SendSocketGems(std::uint64_t item_guid, std::uint64_t gem_guid_0, std::uint64_t gem_guid_1,
                      std::uint64_t gem_guid_2);

  void SendResurrectResponse(std::uint64_t guid, std::uint8_t status);
  void SendRepopRequest(bool auto_release = false);
  void SendReclaimCorpse();

  void SendHearthAndResurrect();

  void SendAreaTrigger(std::uint32_t trigger_id);
  void SendZoneUpdate(std::uint32_t zone_id);

  void SendCreatureQuery(std::uint32_t entry, std::uint64_t guid);
  void SendGameObjectQuery(std::uint32_t entry, std::uint64_t guid);
  void SendGameObjectUse(std::uint64_t guid);
  void SendGameObjectReportUse(std::uint64_t guid);
  void SendItemQuery(std::uint32_t entry);
  void SendNameQuery(std::uint64_t guid);
  void SendItemTextQuery(std::uint64_t item_guid);
  void SendPageTextQuery(std::uint32_t page_text_id);
  void SendAcceptLevelGrant(std::uint64_t guid);
  void SendGrantLevel(std::uint64_t guid);

  void SendSetSelection(std::uint64_t guid);
  void SendSetActionButton(std::uint8_t slot, const ActionPresentationEntry &button);
  void SendClearActionButton(std::uint8_t slot);

  void SendTaxiNodeStatusQuery(std::uint64_t guid);
  void SendTaxiQueryAvailableNodes(std::uint64_t guid);
  void SendActivateTaxi(std::uint64_t npc_guid, std::uint32_t source_node, std::uint32_t dest_node);
  void SendActivateTaxiExpress(std::uint64_t npc_guid, const std::vector<std::uint32_t> &nodes);
  void SendEnableTaxi(std::uint64_t npc_guid);
  void SendSetTaxiBenchmarkMode(bool enabled);

  void SendAreaSpiritHealerQuery(std::uint64_t guid);
  void SendAreaSpiritHealerQueue(std::uint64_t guid);
  void SendSpiritHealerActivate(std::uint64_t guid);

  void SendLearnTalent(std::uint32_t talent_id, std::uint32_t rank);
  void SendLearnPetTalent(std::uint64_t pet_guid, std::uint32_t talent_id,
                          std::uint32_t rank);
  void SendLearnPreviewTalents(const std::vector<TalentEntry>& talents,
                               std::optional<ObjectGuid> pet_guid = std::nullopt);
  void SendTalentWipeConfirm(const ObjectGuid &trainer_guid);
  void SendSetActiveTalentGroup(std::uint8_t group_index);

  void SendSetGlyphSlot(std::uint32_t slot);
  void SendRemoveGlyph(std::uint32_t slot);

  void SendStandStateChange(std::uint8_t stand_state);

  void SendAddFriend(const std::string &name, const std::string &note);
  void SendDelFriend(std::uint64_t guid);
  void SendAddIgnore(const std::string &name);
  void SendDelIgnore(std::uint64_t guid);
  void SendAddMute(const std::string &name);
  void SendDelMute(std::uint64_t guid);

  void SendBattlefieldPort(std::uint64_t battlefield_instance_guid, bool accepted);

  void SendGuildRoster();
  void SendGuildRosterRefresh();
  void SendGuildInvite(const std::string &name);
  void SendGuildLeave();
  void SendGuildDisband();
  void SendGuildAccept();
  void SendGuildDecline();
  void SendGuildInfo();
  void SendGuildPromote(const std::string &name);
  void SendGuildDemote(const std::string &name);
  void SendGuildSetLeader(const std::string &name);
  void SendGuildSetMOTD(const std::string &motd);
  void SendGuildRemove(const std::string &name);
  void SendGuildSetPublicNote(const std::string &name, const std::string &note);
  void SendGuildSetOfficerNote(const std::string &name, const std::string &note);
  void SendGuildPermissionsQuery();
  void SendGuildBankMoneyWithdrawnQuery();
  void SendGuildBankDepositMoney(std::uint64_t banker_guid, std::uint32_t amount);
  void SendGuildBankWithdrawMoney(std::uint64_t banker_guid, std::uint32_t amount);
  void SendGuildBankerActivate(std::uint64_t guid);
  void SendGuildBankQueryTab(std::uint64_t guid, std::uint8_t tab);
  void SendGuildBankBuyTab(std::uint64_t guid, std::uint8_t tab);
  void SendGuildBankUpdateTab(std::uint64_t guid, std::uint8_t tab, const std::string &name,
                              const std::string &icon);
  void SendGuildBankSetTabText(std::uint8_t tab, const std::string &text);
  void SendGuildBankSwapItemsAutoStore(std::uint64_t banker_guid, std::uint8_t tab,
                                       std::uint8_t slot, std::uint32_t item_entry,
                                       std::uint32_t item_count);
  void SendGuildBankSwapItemsPlayerToBank(std::uint64_t banker_guid, std::uint8_t tab,
                                          std::uint8_t slot, std::uint32_t destination_item_entry,
                                          std::uint8_t player_bag, std::uint8_t player_slot,
                                          std::uint32_t count);
  void SendGuildBankSwapItemsBankToPlayer(std::uint64_t banker_guid, std::uint8_t source_tab,
                                          std::uint8_t source_slot, std::uint32_t source_item_entry,
                                          std::uint8_t player_bag, std::uint8_t player_slot,
                                          std::uint32_t count);
  void SendGuildBankSwapItemsBankToBank(std::uint64_t banker_guid, std::uint8_t source_tab,
                                        std::uint8_t source_slot,
                                        std::uint32_t destination_item_entry, std::uint8_t dest_tab,
                                        std::uint8_t dest_slot, std::uint32_t held_item_entry,
                                        std::uint32_t split_count);
  void SendGuildBankSwapItemsBankToCursor(std::uint64_t banker_guid, std::uint8_t tab,
                                          std::uint8_t slot);
  void SendGuildBankLogQuery(std::uint8_t tab);
  void SendGuildEventLogQuery();
  void SendGuildQueryBankText(std::uint8_t tab);
  void SendGuildInfoText(const std::string &text);
  void SendGuildSetRank(const std::string &name, std::uint32_t rank_id, std::uint32_t flags,
                        std::uint32_t withdraw_limit, std::uint32_t tab_flags_count,
                        const std::uint32_t *tab_flags, const std::uint32_t *tab_slots);

  void SendInitiateTrade(std::uint64_t target_guid,
                         bool defer_cursor_item_placement = false);
  void SendBeginTrade();
  void SendBusyTrade();
  void SendIgnoreTrade();
  void SendAcceptTrade(std::uint32_t accept_cookie = 1);
  void SendUnacceptTrade();
  void SendCancelTrade();
  void SendSetTradeGold(std::uint32_t copper);
  [[nodiscard]] bool SendSetTradeItem(
      std::uint8_t trade_slot, std::uint8_t bag, std::uint8_t bag_slot);
  [[nodiscard]] bool SendClearTradeItem(std::uint8_t trade_slot);

  void SendLootRoll(std::uint64_t loot_guid, std::uint32_t slot, std::uint8_t roll_type);
  void SendLootMethod(std::uint32_t method, std::uint64_t master_guid, std::uint32_t threshold);
  void SendLootMasterGive(std::uint64_t loot_guid, std::uint8_t slot, std::uint64_t target_guid);
  void SendOptOutOfLoot(bool opt_out);

  void SendDuelAccepted(std::uint64_t guid);
  void SendDuelCancelled(std::uint64_t guid);

  void SendAlterAppearance(std::uint32_t hair_style_id, std::uint32_t hair_color,
                           std::uint32_t facial_hair_style_id, std::uint32_t skin_color_style_id);

  void SendSetTitle(std::uint32_t title_id);

  void SendOfferPetition(std::uint32_t petition_type_field,
                         std::uint64_t petition_guid,
                         std::uint64_t target_guid);
  void SendPetitionBuy(std::uint64_t npc_guid, const std::string &petition_name);
  void SendPetitionBuy(std::uint64_t npc_guid, std::uint32_t petition_type,
                       const std::string &petition_name);
  void SendTabardVendorActivate(std::uint64_t vendor_guid);
  void SendPetitionShowList(std::uint64_t npc_guid);

  void SendShowingHelm(bool show);
  void SendShowingCloak(bool show);

  void SendSetAllowLowLevelRaid(bool allow);

  void SendPushQuestToParty(std::uint32_t quest_id);

  void SendEquipItem(std::uint64_t item_guid, std::uint8_t dst_slot);
  void SendEquipItem(std::uint8_t src_bag, std::uint8_t src_slot, std::uint8_t dst_slot);

  void SendSetActionBarToggles(std::uint8_t flags);

  void SendTogglePetAutocast(std::uint32_t spell_id, bool enabled);

  bool SendPetAction(std::uint64_t pet_guid, std::uint32_t action_data,
                     std::uint64_t target_guid);
  void SendPetSetAction(std::uint64_t pet_guid,
                        std::optional<net::wotlk::PetSetActionSlotState> secondary_slot,
                        net::wotlk::PetSetActionSlotState target_slot);
  void SendPetAbandon(std::uint64_t pet_guid);

  void SendPetCancelAura(std::uint64_t pet_guid, std::uint32_t spell_id);

  void SendDismissCritter(std::uint64_t critter_guid);
  void SendPetRename(std::uint64_t pet_guid, const std::string &name,
                     const std::array<std::string, 5> *declined_names = nullptr);
  void SendPetStopAttack(std::uint64_t pet_guid);
  void SendListStabledPets(std::uint64_t npc_guid);
  void SendStablePet(std::uint64_t npc_guid);
  void SendUnstablePet(std::uint64_t npc_guid, std::uint32_t pet_number);
  void SendBuyStableSlot(std::uint64_t npc_guid);
  void SendStableSwapPet(std::uint64_t npc_guid, std::uint32_t slot);
  void SendStableRevivePet(std::uint64_t npc_guid);

  void SendBugReport(const std::string &description);
  void SendSuggestionReport(const std::string &description);

  void SendGMReportLag(std::int32_t lag_type_argument);
  void SendGMTicketSystemStatus();
  void SendGMTicketGetTicket();
  void SendGMTicketCreate(const std::string &description, std::uint8_t need_response);
  void SendGMResponseNeedMoreHelp(const std::string &description);
  void SendGMTicketUpdateText(const std::string &text);
  void SendGMTicketDelete();
  void SendGMResponseResolve();
  void SendGMSurveySubmit();

  void SendTogglePvP();
  void SendSetPvP(std::uint8_t flag);
  void SendBattlemasterHello(std::uint64_t guid);
  void SendBattlefieldStatus();
  void SendBattlefieldList(std::uint32_t bg_type, std::uint8_t from_where = 0,
                          std::uint8_t xp_locked = 0);
  void SendBattlemasterJoin(std::uint32_t bg_type, std::uint32_t instance_id, bool as_group);
  void SendBattlemasterJoinArena(std::uint8_t slot, bool as_group, bool is_rated);
  void SendBattlefieldMgrEntryInviteResponse(std::uint32_t battlefield_id, bool accepted);
  void SendBattlefieldMgrQueueInviteResponse(std::uint32_t battlefield_id, bool accepted);
  void SendBattlefieldMgrQueueRequest(std::uint32_t battlefield_id);
  void SendBattlefieldMgrExitRequest(std::uint32_t battlefield_id);
  void SendLeaveBattlefield();
  void SendPvpLogData();
  void SendReportPvpAfk(std::uint64_t target_guid);

  void SendGroupInvite(const std::string &name, std::uint32_t role_flags = 0);
  void SendGroupAccept(std::uint32_t role_flags = 0);
  void SendGroupDecline();
  void SendGroupUninvite(const std::string &name);
  void SendGroupUninviteByGuid(std::uint64_t target_guid, std::string_view reason = "");
  void SendGroupDisband();

  void SendWho(const std::string &filter, std::uint32_t level_min = 0,
               std::uint32_t level_max = 100, std::uint32_t race_mask = 0xFFFFFFFF,
               std::uint32_t class_mask = 0xFFFFFFFF);

  void SendCancelMountAura();

  void SendSetSheathed(std::uint32_t sheath_state);
  void SendTextEmote(std::uint32_t emote_id, std::uint64_t target_guid);

  void SendRandomRoll(std::uint32_t min_val, std::uint32_t max_val);

  void SendGroupSetLeader(std::uint64_t target_guid);

  void SendGroupAssistantLeader(std::uint64_t target_guid, bool set);

  void SendGroupRaidConvert(bool to_raid = true);

  void SendGroupChangeSubGroup(const std::string &name, std::uint8_t sub_group);

  void SendGroupSwapSubGroup(const std::string &name1, const std::string &name2);

  void SendReadyCheck();

  void SendReadyCheckConfirm(bool is_ready);

  void SendReadyCheckFinished();

  void SendContactList(std::uint32_t flags = 0x07);

  void SendSetContactNotes(std::uint64_t guid, const std::string &note);

  void SendSummonResponse(std::uint64_t summoner_guid, bool accept);

  void SendComplain(std::uint8_t spam_type, std::uint64_t spammer_guid,
                    std::uint32_t auxiliary_word, std::uint32_t mail_message_id,
                    std::uint32_t trailing_word);
  void SendChatComplain(std::uint64_t spammer_guid, std::uint32_t aux_value,
                        std::uint32_t chat_type, std::uint32_t channel_lookup_id,
                        std::uint32_t recorded_at, const std::string &formatted_line);

  void SendChatFiltered(std::uint64_t spammer_guid);

  void SendChatIgnored(std::uint64_t sender_guid, bool commentator_squelch);

  void SendChannelCommand(std::uint16_t opcode_raw, const std::string &channel,
                          const std::string &target = "");

  void SendClearChannelWatch();

  void SendChannelStringPairCommand(std::uint16_t opcode_raw, const std::string &channel,
                                    const std::string &argument);

  void SendChannelTargetCommand(std::uint16_t opcode_raw, const std::string &channel,
                                const std::string &target);

  void SendGroupVoiceSilence(std::uint64_t target_guid, bool silence, bool battleground_group);

  void SendSetActiveVoiceChannel(std::uint32_t channel_type, std::string_view channel_name);

  void SendJoinChannel(std::uint32_t channel_id, const std::string &name,
                       const std::string &password = "", bool has_voice = false,
                       std::uint8_t join_flag = 0);

  void SendLeaveChannel(std::uint32_t channel_id, const std::string &name);

  void SendAddonMessage(std::uint32_t chat_type, const std::string &message,
                        const std::string &target = "");

  void SendLfgJoin(std::uint32_t roles, const std::vector<std::uint32_t> &dungeons,
                   const std::string &comment);
  void SendLfgLeave();
  void SendLfgSetRoles(std::uint8_t roles);
  void SendLfgSetComment(const std::string &comment);
  void SendLfgGetStatus();
  void SendLfdPlayerLockInfoRequest();
  void SendLfgSearchJoin(std::uint32_t packed_search_id);
  void SendLfgSearchLeave();

  void SendGuildAddRank(const std::string &name);
  void SendGuildDeleteRank();

  void SendToggleXPGain();

  void SendPlayedTime(bool show_in_chat);
  void SendOpeningCinematic();

  void SendCalendarGetCalendar();

  void SendCalendarGetNumPending();

  void SendCalendarGetEvent(std::uint64_t event_id);

  void SendCalendarAddEvent(const std::string &title, const std::string &description,
                            std::uint8_t type, std::uint8_t repeat_option,
                            std::uint32_t max_invites, std::int32_t dungeon_id,
                            std::uint32_t event_time, std::uint32_t secondary_time,
                            std::uint32_t flags,
                            const std::vector<net::wotlk::CalendarAddEventInvite> &invites);

  void SendCalendarUpdateEvent(std::uint64_t event_id, std::uint64_t invite_id,
                               const std::string &title, const std::string &description,
                               std::uint8_t type, std::uint8_t repeat_option,
                               std::uint32_t max_invites, std::int32_t dungeon_id,
                               std::uint32_t event_time, std::uint32_t secondary_time,
                               std::uint32_t flags);

  void SendCalendarRemoveEvent(std::uint64_t event_id, std::uint64_t invite_id,
                               std::uint32_t flags);

  void SendCalendarRemoveEventBuffer(std::uint64_t event_id, std::uint64_t invite_id,
                                     bool uses_guild_calendar);

  [[nodiscard]] bool SendCalendarEventInvite(std::uint64_t event_id, std::uint64_t invite_id,
                                              const std::string &name, bool is_pre_invite,
                                              bool is_guild);

  void SendCalendarEventRsvp(std::uint64_t event_id, std::uint64_t invite_id, std::uint32_t status);

  void SendCalendarEventRemoveInvite(std::uint64_t target_invitee_guid, std::uint64_t event_id,
                                     std::uint64_t target_invite_id, std::uint64_t self_invite_id);

  void SendCalendarEventSignUp(std::uint64_t event_id, std::uint8_t tentative);

  void SendCalendarEventModeratorStatus(std::uint64_t target_invitee_guid, std::uint64_t event_id,
                                        std::uint64_t target_invite_id,
                                        std::uint64_t self_invite_id, std::uint32_t status);

  void SendCalendarEventStatus(std::uint64_t target_invitee_guid, std::uint64_t event_id,
                               std::uint64_t target_invite_id, std::uint64_t self_invite_id,
                               std::uint32_t status);

  void SendCalendarComplain(std::uint64_t creator_guid, std::uint64_t event_id,
                            std::uint64_t self_invite_id);

  void SendCalendarCopyEvent(std::uint64_t event_id, std::uint64_t invite_id,
                             std::uint32_t event_time);

  void SendCalendarGuildFilter(std::uint32_t min_level, std::uint32_t max_level,
                               std::uint32_t rank);

  void SendCalendarArenaTeam(std::uint32_t team_id);

  bool SendRawPacket(net::wotlk::WorldPacket &pkt);

  void SendRequestVehicleExit();

  void SendRequestVehicleNextSeat();
  void SendRequestVehiclePrevSeat();

  void SendRequestVehicleSwitchSeat(std::uint64_t vehicle_guid,
                                    std::uint8_t seat_index);

  void SendChangeSeatOnVehicle(std::uint8_t seat_index);

  void SendSpellClick(std::uint64_t unit_guid);

  void SendPlayerVehicleEnter(std::uint64_t unit_guid);

  void SendChangePlayerDifficulty(std::uint32_t difficulty);

  void SendSetDungeonDifficulty(std::uint32_t difficulty);

  void SendSetRaidDifficulty(std::uint32_t difficulty);

  void SendResetInstances();

  void SendSetSavedInstanceExtend(std::uint32_t map_id, std::uint32_t difficulty, bool extended);

  void SendRaidTargetUpdate(std::uint64_t guid, std::uint8_t icon);

  void SendRequestAllRaidTargets();

  void SendRequestRaidInfo();

  void SendPartyAssignment(std::uint8_t role, bool apply, std::uint64_t target_guid);

  void SendArenaTeamAccept();
  void SendArenaTeamDecline();
  void SendArenaTeamLeave(std::uint32_t team_id);
  void SendArenaTeamDisband(std::uint32_t team_id);
  void SendArenaTeamInvite(std::uint32_t team_id, const std::string &name);
  void SendArenaTeamRemove(std::uint32_t team_id, const std::string &name);
  void SendArenaTeamLeader(std::uint32_t team_id, const std::string &name);
  void SendArenaTeamRoster(std::uint32_t team_id);
  void SendArenaTeamQuery(std::uint32_t team_id);

  void SendLfgBootVote(bool accept);
  void SendLfgProposalResult(bool accept);
  void SendLfgTeleport(bool teleport_argument);

  void SendPetitionRename(std::uint64_t petition_guid, const std::string &new_name);
  void SendPetitionSign(std::uint64_t petition_guid, std::uint8_t petition_choice);
  void SendTurnInPetition(std::uint64_t petition_guid,
                          const std::array<std::uint32_t, 5> &extra_fields = {
                              0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu});
  void SendPetitionShowSignatures(std::uint64_t petition_guid);

  void SendInspect(std::uint64_t guid);
  void SendInspectHonorDataRequests(std::uint64_t guid);

  void SendCancelTempEnchantment(std::uint32_t slot);
  void SendUnlearnSkill(std::uint32_t skill_id);
  void SendBuySkillStep(std::uint32_t skill_id);
  void SendBuySkillRanks(const std::vector<std::pair<std::uint32_t, std::uint32_t>> &queued_ranks);

private:
  struct PendingBindOnUseState {
    std::uint64_t item_guid = 0;
    std::uint64_t target_guid = 0;
  };

  WorldSession *session_ = nullptr;
  PendingBindOnUseState pending_bind_on_use_{};
  ProposalResponseClockFn proposal_response_clock_fn_{};
  std::uint32_t proposal_response_attempt_count_ = 0;
  double proposal_response_window_anchor_seconds_ = 0.0;

  bool Send(net::wotlk::WorldPacket &pkt);
  bool Send(net::wotlk::WorldPacket &&pkt) { return Send(pkt); }
  bool SendUseItemPacket(std::uint8_t bag, std::uint8_t slot, std::uint8_t cast_flags,
                         std::uint64_t target_guid, std::uint32_t glyph_index = 0);
  [[nodiscard]] bool CanSendProposalResponse();
  [[nodiscard]] double GetProposalResponseTimeSeconds() const;
};

}

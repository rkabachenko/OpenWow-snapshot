#pragma once

#include "openwow/ui/game/event_dispatcher.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {
struct CombatLogEntry;
class WorldSession;
}

namespace openwow::ui::game {

class EventDispatcher;

class UnitTokenRegistry {
public:
  static UnitTokenRegistry &Get();

  void SetToken(const std::string &token, std::uint64_t guid);
  void ClearToken(const std::string &token);

  [[nodiscard]] std::uint64_t GuidForToken(const std::string &token) const;

  [[nodiscard]] std::string TokenForGuid(std::uint64_t guid) const;
  [[nodiscard]] std::string TokenForGuid(std::uint64_t guid,
                                         const openwow::game::WorldSession *session) const;

  [[nodiscard]] std::string TokenForAuraCasterGuid(
      std::uint64_t guid, const openwow::game::WorldSession *session) const;

  [[nodiscard]] std::vector<std::string> AllTokensForGuid(std::uint64_t guid) const;
  [[nodiscard]] std::vector<std::string> AllTokensForGuid(
      std::uint64_t guid, const openwow::game::WorldSession *session) const;

  void SetPlayer(std::uint64_t guid);
  void SetTarget(std::uint64_t guid);
  void SetFocus(std::uint64_t guid);
  void SetPartyMember(std::uint8_t index, std::uint64_t guid);
  void SetRaidMember(std::uint8_t index, std::uint64_t guid);
  void SetBossFrame(std::uint8_t index, std::uint64_t guid);
  void SetPet(std::uint64_t guid);
  void SetVehicle(std::uint64_t guid);
  void SetRaidPetMember(std::uint8_t index, std::uint64_t guid);
  void SetArenaOpponent(std::uint8_t index, std::uint64_t guid);
  void SetArenaPet(std::uint8_t index, std::uint64_t guid);
  void SetCommentatorUnit(std::uint8_t index, std::uint64_t guid);
  void SetNpc(std::uint64_t guid);
  void SetQuestNpc(std::uint64_t guid);
  void SetMouseover(std::uint64_t guid);
  void ClearBossFrames();
  void ClearArenaOpponents();
  void ClearCommentatorUnits();

  void Reset();

  static bool IsValidToken(const std::string &token);

  [[nodiscard]] std::size_t token_count() const;

private:
  UnitTokenRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::uint64_t> token_to_guid_;
};

class ScriptEventDispatch {
public:
  static ScriptEventDispatch &Get();

  void Initialize(EventDispatcher *dispatcher,
                  openwow::game::WorldSession *session = nullptr);
  void BindWorldSession(openwow::game::WorldSession *session);

  void Shutdown();

  [[nodiscard]] bool IsInitialized() const {
    return dispatcher_ != nullptr;
  }

  [[nodiscard]] lua_State *GetLuaState() const {
    return dispatcher_ != nullptr ? dispatcher_->GetLuaState() : nullptr;
  }

  void FireEvent(const char *event_name);
  void FireEventArgs(const char *event_name, std::initializer_list<EventArg> args);
  void FireEventV(const char *event_name, const std::vector<EventArg>& args);

  void FireUnitHealth(std::uint64_t guid);
  void FireUnitMaxHealth(std::uint64_t guid);
  void FireUnitPowerSpecific(std::uint64_t guid, std::uint8_t power_type);
  void FireUnitMaxPowerSpecific(std::uint64_t guid, std::uint8_t power_type);
  void FireUnitLevel(std::uint64_t guid);
  void FireUnitAura(std::uint64_t guid);
  void FireUnitTarget(std::uint64_t guid);
  void FireUnitName(std::uint64_t guid);
  void FireUnitFlags(std::uint64_t guid);
  void FireUnitPortrait(std::uint64_t guid);
  void FireUnitModel(std::uint64_t guid);
  void FireUnitFaction(std::uint64_t guid);
  void FireUnitClassification(std::uint64_t guid);
  void FireUnitStats(std::uint64_t guid);
  void FireUnitAttack(std::uint64_t guid);
  void FireUnitDefense(std::uint64_t guid);
  void FireUnitSpellHaste(std::uint64_t guid);
  void FireUnitDisplayPower(std::uint64_t guid);
  void FireUnitInventoryChanged(std::uint64_t guid);
  void FireUnitComboPoints(std::uint64_t guid);
  void FireUnitAttackPower(std::uint64_t guid);
  void FireUnitAttackSpeed(std::uint64_t guid);
  void FireUnitRangedDamage(std::uint64_t guid);
  void FireUnitRangedAttackPower(std::uint64_t guid);
  void FireUnitResistances(std::uint64_t guid);
  void FireUnitPet(std::uint64_t guid);

  void FirePlayerXP();
  void FirePlayerMoney();
  void FirePlayerFlags();
  void FirePlayerEnteringWorld();
  void FirePlayerLeavingWorld();
  void FirePlayerLogin();
  void FirePlayerAlive();
  void FirePlayerDead();
  void FirePlayerUnghost();
  void FireAreaSpiritHealerInRange();
  void FirePlayerRegenEnabled();
  void FirePlayerRegenDisabled();

  void FirePlayerEquipmentChanged(std::uint8_t inventory_slot, bool has_item);

  void FirePlayerBankSlotsChanged(std::uint8_t bank_slot);
  void FirePlayerBankBagSlotsChanged();
  void FirePlayerLevelUp(std::uint32_t new_level);
  void FirePetSpellPowerUpdate();

  void FirePlayerCombatStatEvents();

  void FirePlayerUpdateResting();
  void FirePlayerControlGained();
  void FirePlayerControlLost();
  void FirePlayerTotemUpdate(std::uint8_t slot);

  void FirePlayerTargetChanged();
  void FirePlayerFocusChanged();

  void FireRaidRosterUpdate();
  void FirePartyMemberEnable(std::uint8_t index);
  void FirePartyMemberDisable(std::uint8_t index);
  void FirePartyLeaderChanged();
  void FirePartyMembersChanged();

  void FirePlayerEnterCombat();
  void FirePlayerLeaveCombat();
  void FireUnitCombat(std::uint64_t guid,
                      const std::string &damage_school,
                      const std::string &descriptor,
                      int amount,
                      int extra_amount);

  void FireCombatLogEvents(const openwow::game::CombatLogEntry &entry);

  void FireUnitSpellcastStart(std::uint64_t guid);
  void FireUnitSpellcastStop(std::uint64_t guid);
  void FireUnitSpellcastSucceeded(std::uint64_t guid);
  void FireUnitSpellcastFailed(std::uint64_t guid);
  void FireUnitSpellcastInterrupted(std::uint64_t guid);
  void FireUnitSpellcastChannelStart(std::uint64_t guid);
  void FireUnitSpellcastChannelUpdate(std::uint64_t guid);
  void FireUnitSpellcastChannelStop(std::uint64_t guid);
  void FireUnitSpellcastSent(std::uint64_t guid);
  void FireUnitSpellcastFailedQuiet(std::uint64_t guid);
  void FireUnitSpellcastDelayed(std::uint64_t guid);

  void FireZoneChanged();
  void FireZoneChangedNewArea();
  void FireSubzoneChanged();

  void FireBagOpen(int bag);
  void FireBagUpdate(int bag);
  void FireBagClosed(std::uint8_t bag);

  void FirePlayerBagUpdates();
  void FireBagUpdateCooldown();
  void FireCursorUpdate();
  void FireActionbarShowGrid();
  void FireActionbarHideGrid();
  void FireActionbarUpdate();
  void FireActionbarSlotChanged(std::uint8_t slot);
  void FireActionbarPageChanged();
  void FireActionbarUpdateCooldown();
  void FireActionbarSpellAndShapeshiftCooldownUpdates(bool has_shapeshift_forms);
  void FireActionbarUpdateUsable();
  void FireInventoryCooldownsChanged();
  void FireUpdateBonusActionbar();
  void FireSpellsChanged();
  void FireCombatRatingUpdate();
  void FireSpellUpdateCooldown();
  void FireLanguageListChanged();

  void FirePetBarHideGrid();
  void FirePetBarShowGrid();
  void FirePetBarUpdate();
  void FirePetBarUpdateCooldown();
  void FirePetBarUpdateUsable();
  void FirePetRenameable();
  void FireWearEquipmentSet(const std::string &set_name);

  void FireReadyCheck(const std::string &initiator, std::uint32_t time_left_seconds);
  void FireReadyCheckConfirm(std::uint64_t guid, bool ready);
  void FireReadyCheckFinished(bool interrupted = false);
  void FireRaidTargetUpdate();
  void FireConfirmSummon();
  void FireCancelSummon();

  void FireConfirmTalentWipe(std::uint32_t cost);

  void FireCombatTextUpdate(const std::string &type);
  void FireCombatTextUpdate(const std::string &type, int amount);
  void FireCombatTextUpdate(const std::string &type, int amount, int extra_amount);
  void FireCombatTextUpdate(const std::string &type, int amount,
                            const std::string &suffix);
  void FireCombatTextUpdate(const std::string &type, const std::string &name);
  void FireCombatTextUpdate(const std::string &type, const std::string &name,
                            int amount);
  void FireCombatTextUpdate(const std::string &type, const std::string &name,
                            int amount, int extra_amount);

  void FireStartLootRoll(int roll_id, int countdown_ms);
  void FireCancelLootRoll(int roll_id);
  void FireConfirmLootRoll(int roll_id, int roll_type);
  void FireConfirmDisenchantRoll(int roll_id, int roll_type);
  void FireChatPlayerNotFound(const std::string &name);
  void FireChatServerMessage(const std::string &message);
  void FireUiErrorMessage(const std::string &message);
  void FireUiInfoMessage(const std::string &message);

  void FireChannelUiUpdate();

  void FireChannelRosterUpdate(int display_slot, int member_count);

  void FireGossipShow();
  void FireGossipClosed();
  void FireTrainerShow();
  void FireTrainerClosed();
  void FireMerchantShow();
  void FireMerchantUpdate();
  void FireMerchantClosed();

  void FireQuestDetail();
  void FireQuestComplete();
  void FireQuestProgress();
  void FireQuestFinished();
  void FireQuestGreeting();
  void FireQuestAccepted(int quest_log_index, int quest_id);
  void FireQuestLogUpdate();
  void FireQuestQueryComplete();
  void FireQuestAcceptConfirm(const std::string &sharer_name, const std::string &quest_title);

  void FireLootOpened(bool auto_loot = false);
  void FireLootClosed();
  void FireLootSlotCleared(int slot);
  void FireLootSlotChanged(int slot);
  void FireLootBindConfirm(int slot);
  void FireOpenMasterLootList();
  void FireUpdateMasterLootList();

  void FireFriendListUpdate();
  void FireIgnoreListUpdate();
  void FireWhoListUpdate();

  void FireGuildRosterUpdate();
  void FireGuildRosterUpdate(int update_type);
  void FireGuildEventLogUpdate();
  void FireGuildInviteRequest(const std::string &inviter, const std::string &guild_name);
  void FireGuildMotd(const std::string &motd);
  void FirePlayerGuildUpdate();
  void FireVehicleAngleUpdate(double raw_pitch, double min_pitch, double max_pitch);

  void FireEvent(const std::string &event_name);
  void FireEventArgs(const std::string &event_name, std::initializer_list<EventArg> args);

  void FireMailShow();
  void FireMailClosed();
  void FireMailInboxUpdate();
  void FireMailSendSuccess();
  void FireUpdatePendingMail();

  void FireAuctionHouseShow();
  void FireAuctionHouseClosed();
  void FireAuctionItemListUpdate();
  void FireAuctionOwnedListUpdate();
  void FireAuctionBidderListUpdate();
  void FireAuctionMultiSellStart(int total_count);
  void FireAuctionMultiSellUpdate(int completed_count, int total_count);
  void FireAuctionMultiSellFailure();

  void FirePartyInviteRequest(const std::string &inviter);
  void FirePartyInviteCancel();
  void FirePartyLootMethodChanged();

  void FireRaidInstanceWelcome(const std::string &name, int reset_time, int flag1, int flag2);

  void FireBankFrameOpened();
  void FireBankFrameClosed();

  void FireDuelRequested(const std::string &challenger);
  void FireDuelOutOfBounds();
  void FireDuelInBounds();
  void FireDuelFinished();

  void FireTradeShow();
  void FireTradeClosed();
  void FireTradeAcceptUpdate();
  void FireTradeAcceptUpdate(int player_accepted, int trader_accepted);
  void FireTradePlayerItemChanged(int trade_slot);
  void FireTradePotentialBindEnchant(bool enabled);
  void FireTradeTargetItemChanged(int trade_slot);
  void FirePlayerTradeMoney();
  void FireTradeMoneyChanged();
  void FireTradeUpdate();
  void FireTradeRequest();
  void FireTradeRequestCancel();

  void FireInspectHonorUpdate();

  void FireTaxiMapOpened();
  void FireTaxiMapClosed();

  void FireBarberShopClose();
  void FireBarberShopOpen();
  void FireBarberShopSuccess();
  void FireBarberShopAppearanceApplied();

  void FireCommentatorMapUpdate();
  void FireCommentatorEnterWorld();
  void FireCommentatorPlayerUpdate();
  void FireCommentatorSkirmishQueueRequest();

  void FireVoiceSessionsUpdate();
  void FireVoiceLeftSession();
  void FireVoiceStart(std::uint64_t guid, const std::string &speaker_name);
  void FireVoiceStop(std::uint64_t guid, const std::string &speaker_name);

  void FireStartMinigame();
  void FireMinigameUpdate();

  void FireTimePlayedMsg(std::uint32_t total_time, std::uint32_t level_time);

  void FirePerUnitEvent(const char *event_name, std::uint64_t guid);

  void FirePerUnitEventWithArgs(const char *event_name, std::uint64_t guid,
                                const std::vector<EventArg> &extra_args);

  void QueuePerUnitEvent(const char *event_name, std::uint64_t guid);

  void QueueTrackedUnitEvent(const char *event_name);

  void QueueGlobalEvent(const char *event_name);

  void FlushQueuedEvents();

  void ClearQueuedEvents();

  [[nodiscard]] std::size_t queued_event_count() const;

  void FireGlobalEvent(const char *event_name);

  void FireGlobalEventArgs(const char *event_name, const std::vector<EventArg> &args);

  void FireGlobalEventWithArgs(const char *event_name, const std::vector<std::string> &args);

private:
  ScriptEventDispatch() = default;

  struct QueuedScriptEvent {
    std::uint64_t guid{0};
    std::string event_name;
  };

  void QueueEvent(std::uint64_t guid, const char *event_name);

  EventDispatcher *dispatcher_{nullptr};
  openwow::game::WorldSession *session_{nullptr};
  std::vector<QueuedScriptEvent> queued_events_;
};

}

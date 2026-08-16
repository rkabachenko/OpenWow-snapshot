
#include "openwow/game/session_event_bridge.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_companion.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/threat_warning_state.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstddef>

namespace openwow::ui::game {

using namespace openwow::game;

SessionEventBridge::SessionEventBridge() = default;
SessionEventBridge::~SessionEventBridge() { Shutdown(); }

void SessionEventBridge::Initialize(WorldSession* session, GameUIManager* ui) {
  session_ = session;
  ui_ = ui;
  initialized_ = (session_ != nullptr && ui_ != nullptr);

  if (initialized_) {
    ThreatWarningState::Get().EnsureBinding(session_);
  }

  Reset();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SessionEventBridge: initialized");
}

void SessionEventBridge::Reset() {
  UnitTokenRegistry::Get().Reset();
  if (session_ != nullptr) {
    session_->action_assignments().ResetUsabilityStates();
  }
  prev_player_ = {};
  prev_target_ = {};
  prev_target_guid_ = 0;
  prev_quests_ = {};
  prev_inventory_ = {};
  prev_social_ = {};
  prev_loot_ = {};
  prev_gossip_ = {};
  prev_spells_ = {};
  prev_trade_ = {};
  prev_mail_ = {};
  prev_combat_ = {};
  if (session_ != nullptr) {
    prev_combat_.combat_log_serial = session_->combat_log().log_entry_serial();
  }
  prev_talents_ = {};
  prev_auras_ = {};
  prev_player_state_ = {};
  prev_spellcast_ = {};
  prev_world_state_ui_ = {};
}

void SessionEventBridge::Shutdown() {
  UnitTokenRegistry::Get().Reset();
  session_ = nullptr;
  ui_ = nullptr;
  initialized_ = false;
}

bool SessionEventBridge::SynchronizePlayerUnitToken() {
  if (!initialized_ || session_ == nullptr) {
    return false;
  }
  const auto *const player = session_->objects().GetLocalPlayer();
  if (player == nullptr || player->GetGuid().IsEmpty()) {
    return false;
  }
  UnitTokenRegistry::Get().SetPlayer(player->GetGuid().GetRawValue());
  return true;
}

void SessionEventBridge::Poll(float elapsed_seconds) {
  if (!initialized_ || !session_ || !ui_) return;

  PollWorldStateUi();

  const auto& objects = session_->objects();
  const auto* player = objects.GetLocalPlayerTyped();
  if (!player) return;

  PollPlayerState();
  PollTargetState();

  PollQuestState();
  PollInventoryState();
  PollSocialState();
  PollLootState();
  PollGossipState();
  PollSpellState();
  PollTradeState();
  PollMailState(elapsed_seconds);
  PollCombatState();
  PollTalentState();
  PollPlayerExtendedState();
  PollPetState();
  PollCompanionState();
}

void SessionEventBridge::PollWorldStateUi() {
  WorldStateUiSnapshot cur;
  cur.map_id = session_->current_map_id();

  if (cur.map_id != prev_world_state_ui_.map_id) {
    ThreatWarningState::Get().Refresh(*session_);
  }

  prev_world_state_ui_ = cur;
}

void SessionEventBridge::PollPlayerState() {

  (void)SynchronizePlayerUnitToken();
  const auto& objects = session_->objects();
  const auto* player = objects.GetLocalPlayer();

  UnitSnapshot cur_player = SnapshotUnit("player");

  auto& sed = ScriptEventDispatch::Get();

  const auto pguid = player ? player->GetGuid().GetRawValue() : 0ULL;

  if (cur_player.stat0 != prev_player_.stat0 && prev_player_.stat0 != 0) {
    if (pguid) {
      sed.FireUnitStats(pguid);
      sed.FireUnitAttack(pguid);
      sed.FireUnitDefense(pguid);
    }
  }

  if (cur_player.attack_power != prev_player_.attack_power &&
      prev_player_.attack_power != 0) {
    if (pguid) {
      sed.FireUnitAttackPower(pguid);
      sed.FireUnitAttackSpeed(pguid);
    }
  }

  if (cur_player.ranged_attack_power != prev_player_.ranged_attack_power &&
      prev_player_.ranged_attack_power != 0) {
    if (pguid) {
      sed.FireUnitRangedAttackPower(pguid);
      sed.FireUnitRangedDamage(pguid);
    }
  }

  if (cur_player.resistances != prev_player_.resistances &&
      prev_player_.resistances != 0) {
    if (pguid) sed.FireUnitResistances(pguid);
  }

  if (cur_player.combo_points != prev_player_.combo_points) {
    if (pguid) sed.FireUnitComboPoints(pguid);
  }

  if (cur_player.flags != prev_player_.flags && prev_player_.flags != 0) {
    if (pguid) sed.FireUnitFlags(pguid);
    sed.FirePlayerFlags();
  }

  prev_player_ = cur_player;
}

void SessionEventBridge::PollTargetState() {
  const auto& objects = session_->objects();
  const auto target_guid = objects.GetTargetGuid();
  const auto target_raw = target_guid.IsEmpty() ? 0ULL : target_guid.GetRawValue();

  if (target_raw != prev_target_guid_) {

    UnitTokenRegistry::Get().SetTarget(target_raw);
    ScriptEventDispatch::Get().FirePlayerTargetChanged();
    prev_target_guid_ = target_raw;
    prev_target_ = target_guid.IsEmpty() ? UnitSnapshot{} : SnapshotUnit("target");
  }

  if (!target_guid.IsEmpty()) {
    UnitSnapshot cur_target = SnapshotUnit("target");
    auto& sed = ScriptEventDispatch::Get();

    if (cur_target.target_guid != prev_target_.target_guid) {
      sed.FireUnitTarget(target_raw);
    }
    prev_target_ = cur_target;
  }
}

void SessionEventBridge::PollQuestState() {
  const auto& quests = session_->quests();
  const std::size_t cur_quest_count = quests.quest_log_count();

  prev_quests_.quest_count = cur_quest_count;

  auto& runtime_quest_log = QuestLog::Get();
  runtime_quest_log.ExpireTimedWatches();

  const auto watch_update_serial = runtime_quest_log.GetWatchUpdateSerial();
  if (watch_update_serial != prev_quests_.watch_update_serial) {
    ui_->frame_events().dispatcher().FireEvent(events::QUEST_WATCH_UPDATE);
    prev_quests_.watch_update_serial = watch_update_serial;
  }

  prev_quests_.has_active_details = quests.has_active_details();
  prev_quests_.has_active_reward = quests.has_active_reward();
  prev_quests_.has_active_request = quests.has_active_request();
}

void SessionEventBridge::PollInventoryState() {
  const auto& objects = session_->objects();
  const auto* player = objects.GetLocalPlayer();
  if (!player) return;

  const std::uint32_t cur_money = player->GetUInt32(PLAYER_FIELD_COINAGE);
  prev_inventory_.money = cur_money;
}

void SessionEventBridge::PollSocialState() {

  const std::size_t cur_party = session_->group().members().size();
  const bool cur_is_raid = session_->group().IsRaid();
  if (cur_party != prev_social_.party_count || cur_is_raid != prev_social_.is_raid) {

    prev_social_.party_count = cur_party;
    prev_social_.is_raid = cur_is_raid;
    ThreatWarningState::Get().Refresh(*session_);
  }

  const std::size_t cur_friends = session_->social().contact_count();
  if (cur_friends != prev_social_.friend_count) {
    ui_->frame_events().dispatcher().FireEvent(events::FRIENDLIST_UPDATE);
    prev_social_.friend_count = cur_friends;
  }

  const auto& guild = session_->guild();
  const std::size_t cur_guild_size = guild.has_roster() ? guild.roster().members.size() : 0;
  if (cur_guild_size != prev_social_.guild_count) {
    ui_->frame_events().dispatcher().FireEvent(events::GUILD_ROSTER_UPDATE);
    prev_social_.guild_count = cur_guild_size;
  }
}

void SessionEventBridge::PollLootState() {

  prev_loot_.loot_open = session_->loot().is_looting();
}

void SessionEventBridge::PollGossipState() {

  prev_gossip_.gossip_open = session_->gossip().has_gossip();
}

void SessionEventBridge::PollSpellState() {
  const auto& spellbook = session_->spell_book();
  const std::size_t cur_spells = spellbook.spell_count();
  const std::size_t cur_cooldowns = spellbook.cooldowns().size();
  auto& sed = ScriptEventDispatch::Get();

  for (const auto& event :
       openwow::game::SpellbookSystem::Get().ConsumeUiEvents()) {
    switch (event.type) {
      case openwow::game::SpellbookUiEventType::SpellsChanged:
        sed.FireSpellsChanged();
        if (openwow::ui::game::detail::RefreshAllActionSlotValidation(
                *session_)) {
          sed.FireActionbarUpdateUsable();
        }
        break;
      case openwow::game::SpellbookUiEventType::LearnedSpellInTab:
        ui_->frame_events().dispatcher().FireEvent(events::LEARNED_SPELL_IN_TAB,
                                static_cast<int>(event.argument));
        break;
      case openwow::game::SpellbookUiEventType::CompanionLearned:
        ui_->frame_events().dispatcher().FireEvent(events::COMPANION_LEARNED);
        break;
      case openwow::game::SpellbookUiEventType::CompanionUnlearned:
        ui_->frame_events().dispatcher().FireEvent(events::COMPANION_UNLEARNED);
        break;
      case openwow::game::SpellbookUiEventType::CompanionUpdate:
        ui_->frame_events().dispatcher().FireEvent(events::COMPANION_UPDATE);
        break;
    }
  }
  prev_spells_.spell_count = cur_spells;

  if (cur_cooldowns != prev_spells_.cooldown_count) {
    sed.FireActionbarSpellAndShapeshiftCooldownUpdates(
        session_->HasAvailableShapeshiftForms());
    if (openwow::ui::game::detail::RefreshAllActionSlotValidation(*session_)) {
      sed.FireActionbarUpdateUsable();
    }
    sed.FireActionbarUpdate();
    prev_spells_.cooldown_count = cur_cooldowns;
  }
}

void SessionEventBridge::PollTradeState() {
  const bool cur_trade_open = session_->trade().is_open();
  if (cur_trade_open && !prev_trade_.trade_open) {
    ui_->frame_events().dispatcher().FireEvent(events::TRADE_SHOW);
  } else if (!cur_trade_open && prev_trade_.trade_open) {
    ui_->frame_events().dispatcher().FireEvent(events::TRADE_CLOSED);
  }
  prev_trade_.trade_open = cur_trade_open;
}

void SessionEventBridge::PollMailState(float elapsed_seconds) {
  auto& mail = session_->mail();
  mail.UpdatePendingMailTimers(elapsed_seconds);

  if (mail.ConsumePendingMailUpdateEvent()) {
    ui_->frame_events().dispatcher().FireEvent(events::UPDATE_PENDING_MAIL);
  }

  if (mail.ConsumeMailInboxUpdateEvent()) {
    ui_->frame_events().dispatcher().FireEvent(events::MAIL_INBOX_UPDATE);
  }

  if (mail.ConsumeSendInfoUpdateEvent()) {
    ui_->frame_events().dispatcher().FireEvent(events::MAIL_SEND_INFO_UPDATE);
  }

  const std::size_t cur_inbox_count = mail.inbox().size();
  if (cur_inbox_count != prev_mail_.inbox_count) {
    ui_->frame_events().dispatcher().FireEvent(events::MAIL_INBOX_UPDATE);
    prev_mail_.inbox_count = cur_inbox_count;
  }
}

void SessionEventBridge::PollCombatState() {
  auto& sed = ScriptEventDispatch::Get();
  const bool cur_in_combat = session_->combat().in_combat();

  if (cur_in_combat && !prev_combat_.in_combat) {
    sed.FirePlayerEnterCombat();
  } else if (!cur_in_combat && prev_combat_.in_combat) {
    sed.FirePlayerLeaveCombat();
  }
  prev_combat_.in_combat = cur_in_combat;

  prev_combat_.combat_log_serial = session_->combat_log().log_entry_serial();
}

void SessionEventBridge::PollTalentState() {

  prev_talents_.active_spec = TalentInfoStore::Get().GetActiveGroupIndex();
}

SessionEventBridge::UnitSnapshot SessionEventBridge::SnapshotUnit(
    const std::string& unit_id) const {
  UnitSnapshot snap;
  if (!session_) return snap;

  const auto& objects = session_->objects();
  const WorldObject* obj = nullptr;

  if (unit_id == "player") {
    obj = objects.GetLocalPlayer();
  } else if (unit_id == "target") {
    const auto target_guid = objects.GetTargetGuid();
    if (!target_guid.IsEmpty()) {
      obj = objects.Get(target_guid);
    }
  }

  if (!obj) return snap;

  snap.health = obj->GetUInt32(UNIT_FIELD_HEALTH);
  snap.max_health = obj->GetUInt32(UNIT_FIELD_MAXHEALTH);
  snap.power = obj->GetUInt32(UNIT_FIELD_POWER1);
  snap.max_power = obj->GetUInt32(UNIT_FIELD_MAXPOWER1);
  snap.level = obj->GetUInt32(UNIT_FIELD_LEVEL);
  snap.flags = obj->GetUInt32(UNIT_FIELD_FLAGS);
  snap.stat0 = obj->GetUInt32(UNIT_FIELD_STAT0);
  snap.attack_power = obj->GetUInt32(UNIT_FIELD_ATTACK_POWER);
  snap.ranged_attack_power = obj->GetUInt32(UNIT_FIELD_RANGED_ATTACK_POWER);
  snap.resistances = obj->GetUInt32(UNIT_FIELD_RESISTANCES);
  if (obj->GetTypeId() == TypeID::kPlayer) {
    snap.combo_points = static_cast<const CGPlayer_C *>(obj)->GetComboPoints();
  }

  auto target_g = obj->GetGuidField(UNIT_FIELD_TARGET);
  snap.target_guid = target_g.IsEmpty() ? 0 : target_g.GetRawValue();

  auto pet_g = obj->GetGuidField(UNIT_FIELD_SUMMON);
  snap.pet_guid = pet_g.IsEmpty() ? 0 : pet_g.GetRawValue();

  return snap;
}

void SessionEventBridge::FirePlayerFlagTransitionEvents(
    const std::uint32_t player_flags) {

  constexpr std::uint32_t kPlayerFlagResting = 0x00000020u;
  constexpr std::uint32_t kPlayerFlagsPlayedTime = 0x00003000u;
  constexpr std::uint32_t kPlayerFlagAllowLowLevelRaid = 0x00010000u;
  constexpr std::uint32_t kPlayerFlagTaxiBenchmark = 0x00020000u;
  constexpr std::uint32_t kPlayerFlagNoXpGain = 0x02000000u;

  constexpr std::uint32_t kTutorialRestedFirst = 0x1Du;
  constexpr std::uint32_t kTutorialRestedSecond = 0x1Eu;

  if (!prev_player_state_.player_flags_seen) {
    prev_player_state_.player_flags = player_flags;
    prev_player_state_.player_flags_seen = true;
    return;
  }

  const std::uint32_t changed =
      player_flags ^ prev_player_state_.player_flags;
  prev_player_state_.player_flags = player_flags;
  if (changed == 0) {
    return;
  }

  auto& sed = ScriptEventDispatch::Get();

  if ((changed & kPlayerFlagResting) != 0) {
    if ((player_flags & kPlayerFlagResting) != 0) {
      auto& tutorials = TutorialSystem::Instance();
      tutorials.TriggerTutorial(kTutorialRestedFirst);
      tutorials.TriggerTutorial(kTutorialRestedSecond);
    }
    sed.FirePlayerUpdateResting();
  }

  if ((changed & kPlayerFlagsPlayedTime) != 0) {
    sed.FireEvent(ui::game::events::PLAYTIME_CHANGED);
  }

  if ((changed & kPlayerFlagTaxiBenchmark) != 0) {
    sed.FireEvent((player_flags & kPlayerFlagTaxiBenchmark) != 0
                      ? ui::game::events::ENABLE_TAXI_BENCHMARK
                      : ui::game::events::DISABLE_TAXI_BENCHMARK);
  }

  if ((changed & kPlayerFlagNoXpGain) != 0) {
    sed.FireEvent((player_flags & kPlayerFlagNoXpGain) != 0
                      ? ui::game::events::DISABLE_XP_GAIN
                      : ui::game::events::ENABLE_XP_GAIN);
  }

  if ((changed & kPlayerFlagAllowLowLevelRaid) != 0) {
    sed.FireEvent((player_flags & kPlayerFlagAllowLowLevelRaid) != 0
                      ? ui::game::events::ENABLE_LOW_LEVEL_RAID
                      : ui::game::events::DISABLE_LOW_LEVEL_RAID);
  }
}

void SessionEventBridge::PollPlayerExtendedState() {
  const auto& objects = session_->objects();
  const auto* player = objects.GetLocalPlayerTyped();
  if (!player) return;

  auto& sed = ScriptEventDispatch::Get();

  session_->scene_state().SetRestState(player->IsResting());

  const std::uint32_t player_flags = player->GetUInt32(PLAYER_FLAGS);
  const bool cur_ghost = (player_flags & 0x10) != 0;
  if (!cur_ghost && prev_player_state_.is_ghost) {

    sed.FirePlayerUnghost();
  }
  prev_player_state_.is_ghost = cur_ghost;

  FirePlayerFlagTransitionEvents(player_flags);

  const std::uint32_t uflags = player->GetUInt32(UNIT_FIELD_FLAGS);
  const auto charmed_by = player->GetGuidField(UNIT_FIELD_CHARMEDBY);
  const bool has_control = !charmed_by.IsEmpty() ? false :
      ((uflags & (0x01000000 | 0x00400000)) == 0);
  if (has_control != prev_player_state_.has_control) {
    if (has_control) {
      sed.FirePlayerControlGained();
    } else {
      sed.FirePlayerControlLost();
    }
    prev_player_state_.has_control = has_control;
  }

}

void SessionEventBridge::PollPetState() {
  const auto& objects = session_->objects();
  const auto* player = objects.GetLocalPlayer();
  if (!player) return;

  auto& sed = ScriptEventDispatch::Get();
  const auto pguid = player->GetGuid().GetRawValue();

  auto summon_guid = player->GetGuidField(UNIT_FIELD_SUMMON);
  const std::uint64_t cur_pet_guid = summon_guid.IsEmpty() ? 0ULL : summon_guid.GetRawValue();

  if (cur_pet_guid != prev_player_state_.pet_guid) {

    if (pguid) sed.FireUnitPet(pguid);
    sed.FirePetBarUpdate();

    UnitTokenRegistry::Get().SetPet(cur_pet_guid);

    prev_player_state_.pet_guid = cur_pet_guid;
  }

  if (cur_pet_guid != 0) {
    const auto& pet_mgr = session_->pet();
    if (!pet_mgr.pet_bar().spells.empty()) {

    }
  }
}

void SessionEventBridge::PollCompanionState() {
  const auto& objects = session_->objects();
  const auto* player = objects.GetLocalPlayer();
  if (!player) {
    return;
  }

  const auto critter_guid = player->GetGuidField(UNIT_FIELD_CRITTER);
  const auto current_critter_guid =
      critter_guid.IsEmpty() ? 0ULL : critter_guid.GetRawValue();

  if (session_->ConsumeActivePlayerCritterDescriptorRefresh()) {
    prev_player_state_.critter_guid = current_critter_guid;
    return;
  }
  if (current_critter_guid == prev_player_state_.critter_guid) {
    return;
  }

  const auto current_entry = critter_guid.IsEmpty() ? 0U : critter_guid.GetEntry();
  const auto previous_entry =
      prev_player_state_.critter_guid == 0
          ? 0U
          : openwow::game::ObjectGuid(prev_player_state_.critter_guid).GetEntry();

  detail::HandleCritterCompanionEntryChanged(
      *session_, current_entry, previous_entry);
  ui_->frame_events().dispatcher().FireEvent(events::COMPANION_UPDATE,
                                             std::string("CRITTER"));
  prev_player_state_.critter_guid = current_critter_guid;
}

}

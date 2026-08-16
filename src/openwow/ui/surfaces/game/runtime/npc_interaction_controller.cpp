#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace openwow::ui::game {
namespace {

namespace game = ::openwow::game;

constexpr std::size_t kNpcInteractionCloseSoundSlot = 1;

constexpr std::size_t kNpcInteractionGreetingSoundSlot = 0;
constexpr std::uint32_t kNpcFlagTabardVendor = 0x00080000u;
constexpr std::uint32_t kUnitFlagCannotInteract = 0x02000000u;

constexpr std::uint32_t kUnitFlag2AllowEnemyInteract = 0x00004000u;
constexpr std::uint32_t kNpcFlagGossip = 0x00000001u;

constexpr std::uint32_t kCreatureTypeFlagInteractWhileDead = 0x00000080u;
constexpr std::uint32_t kCreatureTypeFlagPlayerOwned = 0x00800000u;

constexpr std::uint32_t kCreatureTypeFlagInteractWhileInCombat = 0x00040000u;

constexpr std::uint32_t kNpcFlagSpellClick = 0x01000000u;

constexpr float kNpcInteractionRangeUnitPadding = 4.0f;

void ResetMailCompose(game::MailInteraction& mail, const bool present) {
  mail.ResetCompose();
  if (present) {
    auto& events = ScriptEventDispatch::Get();
    events.FireEvent(events::SEND_MAIL_MONEY_CHANGED);
    events.FireEvent(events::SEND_MAIL_COD_CHANGED);
    events.FireMailSendSuccess();
  }
}

void CloseMail(game::WorldSession& session) {
  auto& mail = session.mail();
  const bool was_open = mail.mailbox_guid() != 0;
  mail.CloseMailbox(false);
  if (mail.ConsumeNextMailTimeQueryRequest()) {
    session.interaction().SendQueryNextMailTime();
  }
  if (was_open) {
    ScriptEventDispatch::Get().FireMailClosed();
  }
}

const openwow::data::dbc::NPCSoundsEntry* ResolveNpcInteractionSounds(
    const game::WorldSession& session, const game::CGUnit_C& unit) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto* display =
      dbc->creature_display_info().LookupEntry(unit.Presentation().CurrentDisplayId());
  if (display == nullptr || display->sound_id == 0) {
    return nullptr;
  }

  return dbc->npc_sounds().LookupEntry(display->sound_id);
}

void PlayNpcInteractionSound(game::WorldSession& session,
                             const game::CGUnit_C& unit,
                             const std::size_t sound_slot) {
  const auto* sounds = ResolveNpcInteractionSounds(session, unit);
  if (sounds == nullptr || sound_slot >= sounds->sound.size()) {
    return;
  }

  const auto sound_kit_id = sounds->sound[sound_slot];
  if (sound_kit_id == 0) {
    return;
  }

  const auto position = unit.GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};
  (void)session.sound_runtime().PlaySoundKit(
      sound_kit_id, sound_position);
}

void ResetNpcInteractionAnimation(game::WorldSession& session,
                                  game::CGUnit_C& unit) {
  unit.Animation().ResetEmoteState();
  unit.Animation().UpdateStandAnimation(session, 0, unit.Animation().GetStandState());
}

void ResetMerchantCursorState(game::WorldSession& session) {
  constexpr std::uint32_t kRetailDefaultCursorType = 1;

  auto* cursor = session.held_cursor();
  if (cursor != nullptr &&
      cursor->kind() ==
          game::actions::held_cursor::Kind::MerchantItem) {
    cursor->Clear();
  }

  auto* cursor_manager = game::GetActiveCursorSurface();
  if (cursor_manager == nullptr ||
      cursor_manager->GetBaseCursorType() != game::CursorType::kRepair) {
    return;
  }

  cursor_manager->SetBaseCursor(game::CursorType::kDefault);
  cursor_manager->SetImmediateCursorType(kRetailDefaultCursorType);
}

void AppendInteractionGuid(std::vector<game::ObjectGuid>& guids,
                           const game::ObjectGuid guid) {
  if (!guid.IsEmpty() &&
      std::find(guids.begin(), guids.end(), guid) == guids.end()) {
    guids.push_back(guid);
  }
}

std::vector<game::ObjectGuid> CollectActiveNpcInteractionGuids(
    const game::WorldSession& session) {
  std::vector<game::ObjectGuid> guids;
  const auto& gossip = session.gossip();

  AppendInteractionGuid(guids, gossip.interaction_guid());
  if (gossip.merchant().active()) {
    AppendInteractionGuid(guids, gossip.merchant().snapshot().vendor_guid);
  }
  if (gossip.has_trainer()) {
    AppendInteractionGuid(guids, gossip.trainer().trainer_guid);
  }

  const auto& guild = game::GuildSystem::Get();
  if (guild.IsBankFrameOpen()) {
    AppendInteractionGuid(guids, game::ObjectGuid(guild.GetBankerGuid()));
  }

  AppendInteractionGuid(
      guids, game::ObjectGuid(session.bank_npc_guid()));
  AppendInteractionGuid(guids, game::ObjectGuid(session.mail().mailbox_guid()));
  AppendInteractionGuid(
      guids, game::ObjectGuid(session.petition().guild_registrar_guid()));
  AppendInteractionGuid(
      guids, game::ObjectGuid(session.petition().petition_vendor_guid()));
  AppendInteractionGuid(
      guids, game::ObjectGuid(session.petition().tabard_vendor_guid()));

  if (session.auction().enabled()) {
    AppendInteractionGuid(
        guids, game::ObjectGuid(session.auction().auctioneer_guid()));
  }
  if (session.taxi().IsTaxiMapOpen()) {
    AppendInteractionGuid(
        guids, game::ObjectGuid(session.taxi().GetFlightMasterGuid()));
  }
  if (session.trade().is_open()) {
    AppendInteractionGuid(
        guids, game::ObjectGuid(session.trade().begin_trade_guid()));
  }

  AppendInteractionGuid(guids, session.pet().stable_list().npc_guid);
  AppendInteractionGuid(
      guids,
      game::ObjectGuid(
          session.battleground().GetBattlefieldListBattlemasterGuid()));
  return guids;
}

bool IsValidPlayerInteractionTarget(const game::CGPlayer_C& active_player,
                                    const game::CGUnit_C& target) {
  return target.GetGuid() == active_player.GetGuid() ||
         active_player.Interaction().CanInteractWithFriendlyPlayerTarget(target);
}

bool IsTabardVendorInteractionTarget(const game::WorldSession& session,
                                     const game::ObjectGuid guid) {
  return session.petition().tabard_vendor_guid() == guid.GetRawValue();
}

game::WorldSession* GetUiWorldSession() {
  auto* manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  return manager != nullptr ? manager->world_session() : nullptr;
}

void CloseNpcInteractionTarget(game::WorldSession& session,
                               const game::ObjectGuid unit,
                               const NpcInteractionClosureCause cause,
                               const NpcInteractionFeedback feedback) {
  if (unit.IsEmpty()) {
    return;
  }

  if (session.objects().GetNpcGuid() != unit) {
    return;
  }
  const auto unit_guid = unit.GetRawValue();

  if (feedback == NpcInteractionFeedback::Apply) {
    ApplyNpcInteractionCloseFeedback(session, unit, cause);
  }

  session.objects().SetNpcGuid({});
  UnitTokenRegistry::Get().SetNpc(0);

  auto& gossip = session.gossip();
  auto& guild = game::GuildSystem::Get();

  if (session.trade().is_open() &&
      session.trade().begin_trade_guid() == unit_guid) {
    const auto trade_result = session.trade().HandleTradeOpenClose(0, 0);
    const auto trade_changes = session.trade().TakeChanges();
    for (const auto guid : trade_changes.released_item_guids) {
      GameUI_OnMouseoverUnitLeave(guid);
    }
    if (trade_changes.closed) {
      ScriptEventDispatch::Get().FireTradeClosed();
    }
    if (trade_result.npc_death_guid != 0) {
      HandleNpcInteractionLoss(
          session, game::ObjectGuid(trade_result.npc_death_guid),
          NpcInteractionClosureCause::UnitUnavailable);
    }
    if (trade_result.send_cancel_packet) {
      session.interaction().SendCancelTrade();
    }
  } else if (gossip.interaction_guid().GetRawValue() == unit_guid) {

    gossip.DismissAll();
    ScriptEventDispatch::Get().FireGossipClosed();
  } else if (session.quests()
                 .quest_frame_interaction_state()
                 .interaction_guid.GetRawValue() == unit_guid) {

    session.quests().CloseQuestFrameInteraction();
  } else if (gossip.merchant().active() &&
             gossip.merchant().snapshot().vendor_guid.GetRawValue() ==
                 unit_guid) {
    CloseMerchantInteraction(session, unit, cause,
                             NpcInteractionFeedback::Suppress);
  } else if (session.taxi().IsTaxiMapOpen() &&
             session.taxi().GetFlightMasterGuid() == unit_guid) {
    session.taxi().CloseTaxiMap();
    ScriptEventDispatch::Get().FireTaxiMapClosed();
  } else if (gossip.has_trainer() &&
             gossip.trainer().trainer_guid.GetRawValue() == unit_guid) {
    CloseTrainerInteraction(session, unit, cause,
                            NpcInteractionFeedback::Suppress);
  } else if (session.bank_npc_guid() == unit_guid) {
    SetBankInteractionTarget(session, {});
  } else if (guild.GetBankerGuid() == unit_guid) {
    SetGuildBankInteractionTarget({});
  } else if (session.petition().guild_registrar_guid() == unit_guid) {
    session.CloseGuildRegistrarInteraction();
  } else if (session.petition().tabard_vendor_guid() == unit_guid) {
    session.CloseTabardVendorInteraction();
  } else if (session.mail().mailbox_guid() == unit_guid) {
    CloseMail(session);
  } else if (session.auction().enabled() &&
             session.auction().auctioneer_guid() == unit_guid) {
    CloseAuctionInteraction(session, unit, cause,
                            NpcInteractionFeedback::Suppress);
  } else if (session.pet().stable_list().npc_guid.GetRawValue() == unit_guid) {
    session.pet().CloseStableList();
    ScriptEventDispatch::Get().FireEvent(events::PET_STABLE_CLOSED);
  } else if (session.petition().petition_vendor_guid() == unit_guid) {
    session.ClosePetitionVendorInteraction();
  } else if (session.battleground().GetBattlefieldListBattlemasterGuid() ==
             unit_guid) {
    CloseBattlefieldList(session);
  }
}

}

namespace detail {

bool IsValidNpcUnitInteractionTarget(
    const game::CGPlayer_C& active_player, const game::CGUnit_C& target,
    const game::QueryCache* query_cache) {
  if ((target.State().GetUnitFlags() & kUnitFlagCannotInteract) != 0u ||
      target.State().GetNpcFlags() == 0u) {
    return false;
  }

  if (target.State().GetHealth() == 0u) {
    const auto* creature_template =
        query_cache != nullptr
            ? query_cache->GetCreatureTemplate(target.GetEntry())
            : nullptr;
    if (creature_template == nullptr ||
        (creature_template->type_flags & kCreatureTypeFlagInteractWhileDead) ==
            0u) {
      return false;
    }
  }

  const bool mutually_neutral_or_better =
      target.Interaction().GetReaction(active_player) >= game::ReactionType::kNeutral &&
      active_player.Interaction().GetReaction(target) >= game::ReactionType::kNeutral;
  const bool explicitly_interactable =
      (target.State().GetUnitFlags2() & kUnitFlag2AllowEnemyInteract) != 0u;
  if (!mutually_neutral_or_better && !explicitly_interactable) {
    return false;
  }

  if ((target.State().GetNpcFlags() & kNpcFlagGossip) != 0u &&
      query_cache != nullptr) {
    if (const auto* creature_template =
            query_cache->GetCreatureTemplate(target.GetEntry());
        creature_template != nullptr &&
        (creature_template->type_flags & kCreatureTypeFlagPlayerOwned) != 0u &&
        target.State().GetCreatedBy() != active_player.GetGuid()) {
      return false;
    }
  }
  return true;
}

bool CanActivePlayerInteractWithNpcUnits(
    const game::CGPlayer_C& active_player) {

  if (active_player.State().IsTaxiFlight() ||
      !active_player.State().GetCharmedBy().IsEmpty() ||
      active_player.State().GetHealth() == 0u) {
    return false;
  }

  const auto form_id =
      active_player.State().SuppressesCurrentFormSpellQueries()
          ? std::uint8_t{0}
          : active_player.Animation().GetShapeshiftForm();
  if (form_id == 0u) {
    return true;
  }

  const auto* const dbc = active_player.dbc_loader();
  const auto* const form =
      dbc != nullptr ? dbc->spell_shapeshift_form().LookupEntry(form_id)
                     : nullptr;
  constexpr std::uint32_t kFormsAllowingNpcInteraction =
      openwow::data::dbc::kShapeshiftFormFlagIsStance |
      openwow::data::dbc::kShapeshiftFormFlagAllowsNpcInteraction;
  if (form != nullptr && (form->flags & kFormsAllowingNpcInteraction) != 0u) {
    return true;
  }
  return active_player.Interaction().CanAutoCancelShapeshiftFormForAction();
}

bool PickPathAllowsNpcUnitTarget(const game::CGPlayer_C& active_player,
                                 const game::CGUnit_C& target,
                                 const game::QueryCache* query_cache) {

  if (!target.State().GetCharmedBy().IsEmpty() &&
      target.Vehicle().GetVehicleEntry() == nullptr) {
    return false;
  }

  if (target.Interaction().IsAttackingOrLatched()) {
    const auto* const creature_template =
        query_cache != nullptr
            ? query_cache->GetCreatureTemplate(target.GetEntry())
            : nullptr;
    const bool creature_allows_combat_interaction =
        creature_template != nullptr &&
        (creature_template->type_flags &
         kCreatureTypeFlagInteractWhileInCombat) != 0u;
    if (!creature_allows_combat_interaction &&
        (active_player.State().GetUnitFlags2() &
         kUnitFlag2AllowEnemyInteract) == 0u) {
      return false;
    }
  }

  if ((target.State().GetNpcFlags() & kNpcFlagSpellClick) != 0u &&
      !target.Interaction().IsSpellClickAccessible()) {
    return false;
  }

  return true;
}

double GetUnitInteractionRangeSquared(
    const game::CGPlayer_C& active_player,
    const game::CGUnit_C& target) {
  return static_cast<double>(
      game::interaction_range::ComputeUnitInteractionRangeSquared(
          active_player.State().GetCombatReach(), target.State().GetCombatReach()));
}

}

void ApplyNpcInteractionCloseFeedback(
    game::WorldSession& session, const game::ObjectGuid unit_guid,
    const NpcInteractionClosureCause cause) {
  if (unit_guid.IsEmpty()) {
    return;
  }

  if (session.objects().GetNpcGuid() != unit_guid) {
    return;
  }

  auto* unit = session.objects().GetMutableUnit(unit_guid);
  if (unit == nullptr || unit->IsPlayer()) {
    return;
  }

  if (cause != NpcInteractionClosureCause::TargetChanged) {
    PlayNpcInteractionSound(session, *unit, kNpcInteractionCloseSoundSlot);
  }
  ResetNpcInteractionAnimation(session, *unit);
}

void CloseAuctionInteraction(
    game::WorldSession& session, const game::ObjectGuid unit,
    const NpcInteractionClosureCause cause,
    const NpcInteractionFeedback feedback) {
  if (unit.IsEmpty() ||
      session.auction().auctioneer_guid() != unit.GetRawValue()) {
    return;
  }

  ScriptEventDispatch::Get().FireAuctionHouseClosed();
  session.auction().state().SetAtAH(false);
  if (feedback == NpcInteractionFeedback::Apply) {
    ApplyNpcInteractionCloseFeedback(session, unit, cause);
  }
  session.auction().CloseAuctionHouse();
}

void CloseMerchantInteraction(
    game::WorldSession& session, const game::ObjectGuid unit,
    const NpcInteractionClosureCause cause,
    const NpcInteractionFeedback feedback) {
  auto& gossip = session.gossip();
  if (unit.IsEmpty() || !gossip.merchant().active() ||
      gossip.merchant().snapshot().vendor_guid != unit) {
    return;
  }

  if (feedback == NpcInteractionFeedback::Apply) {
    ApplyNpcInteractionCloseFeedback(session, unit, cause);
  }
  gossip.DismissAll();
  ScriptEventDispatch::Get().FireMerchantClosed();
  ResetMerchantCursorState(session);
}

void CloseTrainerInteraction(
    game::WorldSession& session, const game::ObjectGuid unit,
    const NpcInteractionClosureCause cause,
    const NpcInteractionFeedback feedback) {
  auto& gossip = session.gossip();
  if (unit.IsEmpty() || !gossip.has_trainer() ||
      gossip.trainer().trainer_guid != unit) {
    return;
  }

  ScriptEventDispatch::Get().FireTrainerClosed();
  if (feedback == NpcInteractionFeedback::Apply) {
    ApplyNpcInteractionCloseFeedback(session, unit, cause);
  }
  gossip.DismissAll();
}

void HandleNpcInteractionLoss(
    game::WorldSession& session, const game::ObjectGuid unit,
    const NpcInteractionClosureCause cause) {
  CloseNpcInteractionTarget(
      session, unit, cause, NpcInteractionFeedback::Apply);
}

void CloseStableInteraction(
    game::WorldSession& session,
    const StableCloseEventPolicy event_policy) {
  const auto stable_master_guid =
      session.pet().stable_list().npc_guid;
  if (stable_master_guid.IsEmpty() &&
      event_policy == StableCloseEventPolicy::ActiveInteractionOnly) {
    return;
  }
  if (!stable_master_guid.IsEmpty()) {
    HandleNpcInteractionLoss(
        session, stable_master_guid,
        NpcInteractionClosureCause::UnitUnavailable);
  }
  session.pet().CloseStableList();
  ScriptEventDispatch::Get().FireEvent(events::PET_STABLE_CLOSED);
}

void CloseNpcInteractionTarget(game::WorldSession& session,
                               const game::ObjectGuid unit) {

  CloseNpcInteractionTarget(
      session, unit, NpcInteractionClosureCause::TargetChanged,
      NpcInteractionFeedback::Apply);
}

void CloseGossipInteraction(game::WorldSession& session) {

  auto& gossip = session.gossip();
  const auto gossip_guid = gossip.interaction_guid();
  if (gossip_guid.IsEmpty()) {
    return;
  }

  if (session.quests()
          .quest_frame_interaction_state()
          .interaction_guid.IsEmpty()) {
    CloseNpcInteractionTarget(session, gossip_guid,
                              NpcInteractionClosureCause::UnitUnavailable,
                              NpcInteractionFeedback::Apply);
  }
  gossip.DismissAll();
  ScriptEventDispatch::Get().FireGossipClosed();
}

void CaptureNpcInteractionRange(game::WorldSession& session,
                                const game::ObjectGuid new_target) {
  const auto* const active_player = session.objects().GetLocalPlayerTyped();
  if (const auto* game_object = session.objects().GetGameObject(new_target);
      game_object != nullptr) {
    const double interact_distance =
        static_cast<double>(game_object->GetInteractDistance());
    session.objects().SetNpcInteractionRangeSquared(interact_distance *
                                                     interact_distance);
    return;
  }

  const auto* unit = session.objects().GetUnit(new_target);
  if (unit == nullptr || active_player == nullptr) {
    return;
  }

  if (unit->IsPlayer()) {
    session.objects().SetNpcInteractionRangeSquared(
        detail::GetUnitInteractionRangeSquared(*active_player, *unit));
    return;
  }

  const double reach =
      static_cast<double>(unit->State().GetCombatReach()) +
      static_cast<double>(kNpcInteractionRangeUnitPadding);
  session.objects().SetNpcInteractionRangeSquared(reach * reach);
}

void StoreNpcInteractionTarget(game::WorldSession& session,
                               const game::ObjectGuid new_target) {
  const auto interaction_guids = CollectActiveNpcInteractionGuids(session);
  for (const auto guid : interaction_guids) {
    if (guid != new_target) {
      CloseNpcInteractionTarget(session, guid);
    }
  }

  const auto previous_target = session.objects().GetNpcGuid();
  if (new_target != previous_target) {
    if (auto* unit = session.objects().GetMutableUnit(new_target);
        unit != nullptr && !unit->IsPlayer()) {
      PlayNpcInteractionSound(session, *unit, kNpcInteractionGreetingSoundSlot);
      ResetNpcInteractionAnimation(session, *unit);
    }
  }
  session.objects().SetNpcGuid(new_target);

  UnitTokenRegistry::Get().SetNpc(new_target.GetRawValue());

  CaptureNpcInteractionRange(session, new_target);
}

void SetNpcInteractionTarget(const game::ObjectGuid new_target) {
  auto* session = GetUiWorldSession();
  if (session == nullptr) {
    return;
  }
  StoreNpcInteractionTarget(*session, new_target);
}

void RequestTrainerInteraction(game::WorldSession& session,
                               const game::ObjectGuid trainer) {
  if (trainer.IsEmpty()) {
    return;
  }

  StoreNpcInteractionTarget(session, trainer);

  session.gossip().BeginTrainerRequest(trainer);
  session.interaction().SendTrainerList(trainer.GetRawValue());
}

void CloseBattlefieldList(game::WorldSession& session) {
  session.battleground().SetBattlefieldListBattlemasterGuid(0);
  ScriptEventDispatch::Get().FireEvent(events::BATTLEFIELDS_CLOSED);
}

void ShowBattlefieldList(game::WorldSession& session,
                         const game::ObjectGuid battlemaster) {
  const auto guid = battlemaster.GetRawValue();
  const auto previous_guid =
      session.battleground().GetBattlefieldListBattlemasterGuid();
  if (guid == 0) {
    CloseBattlefieldList(session);
    ScriptEventDispatch::Get().FireEvent(events::BATTLEFIELDS_SHOW);
    return;
  }
  if (previous_guid != 0 && previous_guid != guid) {
    CloseBattlefieldList(session);
  }
  session.battleground().SetBattlefieldListBattlemasterGuid(guid);
  ScriptEventDispatch::Get().FireEvent(events::BATTLEFIELDS_SHOW);
}

void ShowMailbox(game::MailInteraction& mail) {
  ResetMailCompose(mail, true);
  ScriptEventDispatch::Get().FireMailShow();
}

void SetBankInteractionTarget(
    game::WorldSession& session, const game::ObjectGuid banker) {
  if (banker.IsEmpty()) {
    session.CloseBank();
    ScriptEventDispatch::Get().FireBankFrameClosed();
    return;
  }

  StoreNpcInteractionTarget(session, banker);
  session.SetBankNpcGuid(banker.GetRawValue());
  ScriptEventDispatch::Get().FireBankFrameOpened();
}

void SetGuildBankInteractionTarget(const game::ObjectGuid banker) {
  auto& guild = game::GuildSystem::Get();
  if (banker.IsEmpty()) {
    guild.CloseBankFrame();
    ScriptEventDispatch::Get().FireEvent(events::GUILDBANKFRAME_CLOSED);
    return;
  }

  SetNpcInteractionTarget(banker);
  guild.SetBankerGuid(banker.GetRawValue());
  ScriptEventDispatch::Get().FireEvent(events::GUILDBANKFRAME_OPENED);
}

NpcInteractionValidation ValidateNpcInteractionTarget(
    const game::CGPlayer_C& active_player,
    const game::CGObject_C& target,
    const game::QueryCache* query_cache) {
  if (target.IsPendingRemoval()) {
    return {.should_keep = false, .distance_squared = 0.0};
  }

  const double distance_sq =
      active_player.GetSquaredDistanceToPosition(target.GetPosition());

  if (const auto* game_object =
          dynamic_cast<const game::CGGameObject_C*>(&target);
      game_object != nullptr) {
    const double interaction_range =
        static_cast<double>(game_object->GetInteractDistance());
    return {
        .should_keep =
            distance_sq <= interaction_range * interaction_range &&
            game_object->PassesInteractionFlagGate(),
        .distance_squared = distance_sq,
    };
  }

  const auto* unit = dynamic_cast<const game::CGUnit_C*>(&target);
  if (unit == nullptr) {
    return {.should_keep = false, .distance_squared = distance_sq};
  }

  bool should_keep = false;
  if (unit->IsPlayer()) {
    should_keep =
        IsValidPlayerInteractionTarget(active_player, *unit) &&
        distance_sq <=
            detail::GetUnitInteractionRangeSquared(active_player, *unit);
  } else {
    should_keep =
        detail::IsValidNpcUnitInteractionTarget(
            active_player, *unit, query_cache) &&
        distance_sq <=
            detail::GetUnitInteractionRangeSquared(active_player, *unit);
  }
  return {
      .should_keep = should_keep,
      .distance_squared = distance_sq,
  };
}

void ValidateNpcInteractionTargets(game::WorldSession& session) {
  const auto guid = session.objects().GetNpcGuid();
  if (guid.IsEmpty()) {
    return;
  }

  const double range_sq = session.objects().GetNpcInteractionRangeSquared();
  if (range_sq <= 0.0) {
    return;
  }

  auto* const unit = session.objects().GetMutableUnit(guid);
  const auto* const game_object =
      unit == nullptr ? session.objects().GetGameObject(guid) : nullptr;
  const game::CGObject_C* const target =
      unit != nullptr ? static_cast<const game::CGObject_C*>(unit)
                       : static_cast<const game::CGObject_C*>(game_object);
  if (target == nullptr || target->IsPendingRemoval()) {
    HandleNpcInteractionLoss(session, guid,
                             NpcInteractionClosureCause::UnitUnavailable);
    return;
  }

  const auto* const active_player = session.objects().GetLocalPlayerTyped();
  if (active_player == nullptr) {
    HandleNpcInteractionLoss(session, guid,
                             NpcInteractionClosureCause::UnitUnavailable);
    return;
  }

  if (game_object != nullptr) {

    if (!game_object->PassesInteractionFlagGate() ||
        !game_object->PassesInteractionPointRangeTest(session)) {
      HandleNpcInteractionLoss(session, guid,
                               NpcInteractionClosureCause::UnitUnavailable);
    }
    return;
  }

  if (IsTabardVendorInteractionTarget(session, guid) &&
      (unit->State().GetNpcFlags() & kNpcFlagTabardVendor) == 0u) {
    HandleNpcInteractionLoss(session, guid,
                             NpcInteractionClosureCause::UnitUnavailable);
    return;
  }

  const bool should_keep =
      unit->IsPlayer()
          ? IsValidPlayerInteractionTarget(*active_player, *unit)
          : detail::IsValidNpcUnitInteractionTarget(*active_player, *unit,
                                                     &session.query_cache());
  const double distance_sq =
      active_player->GetSquaredDistanceToPosition(unit->GetPosition());
  if (!should_keep || distance_sq > range_sq) {
    HandleNpcInteractionLoss(session, guid,
                             NpcInteractionClosureCause::UnitUnavailable);
  }
}

}

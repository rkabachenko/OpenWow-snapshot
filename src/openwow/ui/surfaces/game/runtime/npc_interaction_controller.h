#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {

class CGObject_C;
class CGPlayer_C;
class CGUnit_C;
class QueryCache;
class MailInteraction;
class WorldSession;

}

namespace openwow::ui::game {

enum class NpcInteractionClosureCause : std::uint8_t {
  TargetChanged,
  UnitUnavailable,
  UnitDespawned,
};

enum class NpcInteractionFeedback : std::uint8_t {
  Suppress,
  Apply,
};

enum class StableCloseEventPolicy : std::uint8_t {
  ActiveInteractionOnly,
  Always,
};

struct NpcInteractionValidation {
  bool should_keep{false};
  double distance_squared{0.0};
};

void StoreNpcInteractionTarget(openwow::game::WorldSession& session,
                               openwow::game::ObjectGuid guid);

void SetNpcInteractionTarget(openwow::game::ObjectGuid guid);
void RequestTrainerInteraction(openwow::game::WorldSession& session,
                               openwow::game::ObjectGuid trainer);
void ShowBattlefieldList(openwow::game::WorldSession& session,
                         openwow::game::ObjectGuid battlemaster);
void CloseBattlefieldList(openwow::game::WorldSession& session);
void ShowMailbox(openwow::game::MailInteraction& mail);
void SetBankInteractionTarget(openwow::game::WorldSession& session,
                              openwow::game::ObjectGuid banker);
void SetGuildBankInteractionTarget(openwow::game::ObjectGuid banker);

void HandleNpcInteractionLoss(openwow::game::WorldSession& session,
                              openwow::game::ObjectGuid unit,
                              NpcInteractionClosureCause cause);
void CloseNpcInteractionTarget(openwow::game::WorldSession& session,
                               openwow::game::ObjectGuid unit);

void CloseGossipInteraction(openwow::game::WorldSession& session);
void CloseStableInteraction(openwow::game::WorldSession& session,
                            StableCloseEventPolicy event_policy);
void ApplyNpcInteractionCloseFeedback(
    openwow::game::WorldSession& session, openwow::game::ObjectGuid unit,
    NpcInteractionClosureCause cause);
void CloseAuctionInteraction(
    openwow::game::WorldSession& session, openwow::game::ObjectGuid unit,
    NpcInteractionClosureCause cause,
    NpcInteractionFeedback feedback = NpcInteractionFeedback::Apply);
void CloseMerchantInteraction(
    openwow::game::WorldSession& session, openwow::game::ObjectGuid unit,
    NpcInteractionClosureCause cause,
    NpcInteractionFeedback feedback = NpcInteractionFeedback::Apply);
void CloseTrainerInteraction(
    openwow::game::WorldSession& session, openwow::game::ObjectGuid unit,
    NpcInteractionClosureCause cause,
    NpcInteractionFeedback feedback = NpcInteractionFeedback::Apply);

void ValidateNpcInteractionTargets(openwow::game::WorldSession& session);
NpcInteractionValidation ValidateNpcInteractionTarget(
    const openwow::game::CGPlayer_C& active_player,
    const openwow::game::CGObject_C& target,
    const openwow::game::QueryCache* query_cache = nullptr);

namespace detail {

bool IsValidNpcUnitInteractionTarget(
    const openwow::game::CGPlayer_C& active_player,
    const openwow::game::CGUnit_C& target,
    const openwow::game::QueryCache* query_cache);

bool CanActivePlayerInteractWithNpcUnits(
    const openwow::game::CGPlayer_C& active_player);

bool PickPathAllowsNpcUnitTarget(
    const openwow::game::CGPlayer_C& active_player,
    const openwow::game::CGUnit_C& target,
    const openwow::game::QueryCache* query_cache);
double GetUnitInteractionRangeSquared(
    const openwow::game::CGPlayer_C& active_player,
    const openwow::game::CGUnit_C& target);

}

}

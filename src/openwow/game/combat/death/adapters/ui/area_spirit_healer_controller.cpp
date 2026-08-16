#include "openwow/game/combat/death/adapters/ui/area_spirit_healer_controller.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <bit>
#include <cmath>
#include <limits>

namespace openwow::game::combat::death::ui {
namespace {

constexpr std::uint32_t kAreaSpiritHealerUnitFlag = 0x00008000u;
constexpr std::uint32_t kPlayerGhostFlag = 0x00000010u;
constexpr std::uint32_t kCancelAreaSpiritHealSpellId = 2584u;
constexpr float kSelectionRangeSquared = 400.0f;
constexpr float kRetentionRangeSquared = 484.000030517578125f;
constexpr float kInteractionPadding = 4.0f;
constexpr float kXpLossFraction = 0.05F;

float GetAreaSpiritHealerSquaredDistance(const CGUnit_C& healer,
                                         const CGPlayer_C& player) {
  const float delta_x = healer.GetX() - player.GetX();
  const float delta_y = healer.GetY() - player.GetY();
  const float delta_z = healer.GetZ() - player.GetZ();
  return delta_x * delta_x + delta_y * delta_y + delta_z * delta_z;
}

bool CanSelectAreaSpiritHealer(const CGUnit_C& healer, const CGPlayer_C& player) {
  return (healer.State().GetUnitFlags2() & kAreaSpiritHealerUnitFlag) != 0 &&
         healer.Interaction().CanAssistSpellTarget(player, false) &&

         !(GetAreaSpiritHealerSquaredDistance(healer, player) >
           kSelectionRangeSquared);
}

bool CanRetainAreaSpiritHealer(const CGUnit_C& healer, const CGPlayer_C& player) {

  return !(GetAreaSpiritHealerSquaredDistance(healer, player) >
           kRetentionRangeSquared);
}

bool IsSpiritHealerInConfirmRange(const CGPlayer_C& player,
                                  const CGUnit_C& healer) {
  const float dx = healer.GetX() - player.GetX();
  const float dy = healer.GetY() - player.GetY();
  const float dz = healer.GetZ() - player.GetZ();
  const float distance_squared = dx * dx + dy * dy + dz * dz;
  const float interaction_range =
      healer.State().GetBoundingRadius() + kInteractionPadding;

  return !(distance_squared > interaction_range * interaction_range);
}

std::int32_t TruncateNativeSpiritHealerXpLoss(const float value) {
  if (!std::isfinite(value) ||
      value < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
      value >= -static_cast<float>(std::numeric_limits<std::int32_t>::min())) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(value);
}

bool UseCancelAreaSpiritHealAction(WorldSession& session) {
  if (session.objects().GetActivePlayer() == nullptr ||
      !SpellQueryBridge::Get().Query(kCancelAreaSpiritHealSpellId).has_value()) {
    return false;
  }

  UseSpellAction(session, 0, kCancelAreaSpiritHealSpellId);
  return true;
}

void ApplySelectionChange(WorldSession& session,
                          const AreaSpiritHealerSelectionChange& change) {
  if (!change.changed) {
    return;
  }
  if (change.cancel_pending_resurrection) {
    (void)UseCancelAreaSpiritHealAction(session);
  }
  if (!change.healer_to_query.IsEmpty()) {
    session.interaction().SendAreaSpiritHealerQuery(
        change.healer_to_query.GetRawValue());
  }
}

void SelectAreaSpiritHealer(WorldSession& session, const ObjectGuid healer) {
  ApplySelectionChange(session,
                       session.area_spirit_healer().SelectHealer(healer));
}

}

void RefreshAreaSpiritHealer(WorldSession& session) {
  auto* player = session.objects().GetActivePlayer();
  if (player == nullptr ||
      (player->GetUInt32(PLAYER_FLAGS) & kPlayerGhostFlag) == 0u) {
    SelectAreaSpiritHealer(session, {});
    return;
  }

  auto& state = session.area_spirit_healer();
  if (!state.active_healer().IsEmpty()) {
    const auto* healer = session.objects().GetUnit(state.active_healer());
    if (healer != nullptr && CanRetainAreaSpiritHealer(*healer, *player)) {
      return;
    }
    SelectAreaSpiritHealer(session, {});
  }

  session.objects().ForEachUnit(
      [&](const ObjectGuid& guid, CGUnit_C& unit) {

        if (CanSelectAreaSpiritHealer(unit, *player)) {
          SelectAreaSpiritHealer(session, guid);
        }
      });
}

bool AcceptAreaSpiritHeal(WorldSession& session) {
  RefreshAreaSpiritHealer(session);
  const auto healer = session.area_spirit_healer().active_healer();
  if (healer.IsEmpty()) {
    return false;
  }

  session.interaction().SendAreaSpiritHealerQueue(healer.GetRawValue());
  return true;
}

void CancelAreaSpiritHeal(WorldSession& session) {
  (void)UseCancelAreaSpiritHealAction(session);
}

void SetAreaSpiritHealerCountdown(WorldSession& session,
                                  const ObjectGuid healer,
                                  const std::chrono::milliseconds delay) {
  RefreshAreaSpiritHealer(session);
  if (!session.area_spirit_healer().StartCountdown(
          healer, delay, core::GameClock::GetTickCount32())) {
    return;
  }

  ::openwow::ui::game::ScriptEventDispatch::Get()
      .FireAreaSpiritHealerInRange();
}

std::chrono::seconds GetAreaSpiritHealerRemainingTime(
    const WorldSession& session) {
  return session.area_spirit_healer().RemainingTime(
      core::GameClock::GetTickCount32());
}

void InteractWithSpiritGuide(WorldSession& session,
                             const ObjectGuid spirit_guide) {
  SelectAreaSpiritHealer(session, {});
  SelectAreaSpiritHealer(session, spirit_guide);
}

bool AcceptSpiritHealerXpLoss(WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr ||
      (player->GetUInt32(PLAYER_FLAGS) & kPlayerGhostFlag) == 0u) {
    return false;
  }

  const ObjectGuid healer_guid =
      session.combat_handler().last_spirit_healer_guid();
  if (healer_guid.IsEmpty()) {
    return false;
  }

  const auto* healer = session.objects().GetUnit(healer_guid);
  if (healer == nullptr || !IsSpiritHealerInConfirmRange(*player, *healer)) {
    return false;
  }

  session.interaction().SendSpiritHealerActivate(healer_guid.GetRawValue());
  return true;
}

std::optional<std::int32_t> HandleSpiritHealerConfirm(
    WorldSession& session, const ObjectGuid healer_guid) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr ||
      (player->GetUInt32(PLAYER_FLAGS) & kPlayerGhostFlag) == 0u) {
    return std::nullopt;
  }

  const auto* healer = session.objects().GetUnit(healer_guid);
  if (healer == nullptr || !IsSpiritHealerInConfirmRange(*player, *healer)) {
    return std::nullopt;
  }

  session.combat_handler().StoreSpiritHealerConfirm(healer_guid);
  return GetSpiritHealerXpLoss(session);
}

std::int32_t GetSpiritHealerXpLoss(const WorldSession& session) {
  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr ||
      session.objects().GetActivePlayerGuid() != player->GetGuid()) {
    return 0;
  }

  const auto next_level_xp = std::bit_cast<std::int32_t>(player->GetNextLevelXP());
  return TruncateNativeSpiritHealerXpLoss(
      static_cast<float>(next_level_xp) * kXpLossFraction);
}

}

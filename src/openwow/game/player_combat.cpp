#include "openwow/game/player_combat.h"

#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/world_session.h"

#include <cstdint>

namespace openwow::game {

namespace {

constexpr std::uint8_t kMainHandEquipSlot = 15;
constexpr std::uint8_t kOffHandEquipSlot = 16;
constexpr std::uint8_t kRangedEquipSlot = 17;
constexpr std::uint32_t kDisarmedUnitFlag = 0x40000u;
constexpr std::uint32_t kCastAllowsSheatheToggleFlag = 0x10000000u;

[[nodiscard]] bool ClassUsesRelicSlot(const std::uint8_t class_id) {
  switch (class_id) {
    case 2:
    case 6:
    case 7:
    case 11:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool IsBlockedAnimation(const std::int32_t animation_id) {
  switch (animation_id) {
    case 46:
    case 49:
    case 105:
    case 106:
    case 107:
    case 108:
    case 109:
    case 110:
    case 111:
    case 112:
      return true;
    default:
      return false;
  }
}

}

void TogglePlayerSheathe(WorldSession& session) {
  auto* player = session.objects().GetActivePlayer();
  if (player == nullptr || !player->IsActivePlayer() ||
      !player->Movement().CanControlCharacter() ||
      (player->Casts().IsCasting() &&
       (player->State().GetSpellStateFlags() & kCastAllowsSheatheToggleFlag) == 0u) ||
      (player->State().GetUnitFlags() & kDisarmedUnitFlag) != 0u ||
      player->State().GetHealth() == 0u || player->Casts().IsChanneling() ||
      player->Animation().GetShapeshiftForm() != 0u ||
      player->Animation().GetEmoteState() != 0u ||
      IsBlockedAnimation(player->Animation().GetCurrentAnimationGroup()) ||
      player->Vehicle().GetVehiclePassengerComponent() != nullptr) {
    return;
  }

  const auto current_state = player->Animation().GetCachedSheatheState();
  const bool has_melee_weapon =
      !player->GetEquippedItem(kMainHandEquipSlot).IsEmpty() ||
      !player->GetEquippedItem(kOffHandEquipSlot).IsEmpty();
  const bool has_ranged_weapon =
      !ClassUsesRelicSlot(player->State().GetClass()) &&
      !player->GetEquippedItem(kRangedEquipSlot).IsEmpty();

  std::int32_t next_state = current_state;
  if (current_state == 0) {
    if (has_melee_weapon) {
      next_state = 1;
    } else if (has_ranged_weapon) {
      next_state = 2;
    }
  } else if (current_state == 1) {
    next_state = has_ranged_weapon ? 2 : 0;
  } else if (current_state == 2) {
    next_state = 0;
  } else {
    return;
  }

  if (next_state != current_state) {
    player->Animation().ChangeSheatheStateAndNotifyServer(next_state, false, false);
  }

  if (current_state == 0 &&
      player->Interaction().IsActivePlayerAutoAttacking()) {
    player->Interaction().CompleteAutoAttackInteraction(false, true);
  }
}

}

#include "openwow/game/targeting/adapters/ui/world_click_controller.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/inventory/adapters/ui/held_cursor_item_use_controller.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/targeting/adapters/ui/target_interaction_controller.h"
#include "openwow/game/targeting/application/target_selection_service.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"

#include <cmath>
#include <optional>

namespace openwow::game::targeting::ui {
namespace {
namespace held_cursor = ::openwow::game::actions::held_cursor;

bool HasActionObjectPayload(const held_cursor::HeldCursor& cursor) {
  if (const auto* item = cursor.get_if<held_cursor::MerchantItem>()) {
    return item->item_entry != 0;
  }
  if (const auto* item = cursor.get_if<held_cursor::ActionBarItem>()) {
    return item->item_entry != 0;
  }
  if (const auto* item = cursor.get_if<held_cursor::AmmoItem>()) {
    return item->item_entry != 0;
  }
  if (const auto* item = cursor.get_if<held_cursor::GuildBankItem>()) {
    return item->item_entry != 0;
  }
  return false;
}

bool CanEffectiveMoverAutoWalk(const WorldSession &session,
                               const CGUnit_C &moving_unit) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto &vehicle_system = VehicleSystem::Get();
  const auto *moving_passenger = moving_unit.Vehicle().GetVehiclePassengerComponent();
  const auto current_seat = vehicle_system.GetSeat(vehicle_system.GetSeatIndex());
  const bool seat_allows_free_movement =
      !vehicle_system.IsInVehicle() || !current_seat.has_value() ||
      HasFlag(current_seat->flags, VehicleSeatFlags::StaticPassenger);

  PlayerMovementGateState gate_state{};
  gate_state.health = static_cast<std::int32_t>(moving_unit.State().GetHealth());
  gate_state.unit_flags = moving_unit.State().GetUnitFlags();
  gate_state.has_knockdown_animation = moving_unit.Mount().HasKnockdownAnimation(moving_unit);
  gate_state.is_active_player = moving_unit.GetGuid() == player->GetGuid();
  gate_state.vehicle_control_allows_free_movement = seat_allows_free_movement;
  gate_state.is_in_vehicle_transition =
      UnitVehicle_IsActivePlayerInVehicle(&moving_unit);
  gate_state.has_non_static_vehicle_seat =
      moving_passenger != nullptr &&
      UnitVehicle_HasNonStaticSeat(
          session,
          moving_passenger->GetPrimaryVehicleGuid() & 0xFFFFFFFFu,
          moving_passenger->GetPrimaryVehicleGuid() >> 32,
          moving_passenger->GetPrimarySeatIndex());

  gate_state.has_movement_restriction_flags =
      (moving_unit.Movement().Data().GetRuntimeFlags() & 0x00100A00u) != 0u;
  gate_state.is_power_type_locked = moving_unit.State().GetPowerType() == 7;
  gate_state.is_on_vehicle = moving_unit.Vehicle().HasValidVehicleUnitGuid();
  return CInputControl::CanPlayerMove(gate_state);
}

void ClearTargetAndCancelAutoRepeat(WorldSession &session) {
  const auto target_guid = session.objects().GetTargetGuid();
  if (target_guid.IsEmpty()) {
    return;
  }

  auto *targeting_system = session.targeting_system();
  if (targeting_system == nullptr) {
    return;
  }

  TargetSelectionService target_selection(*targeting_system);
  (void)target_selection.ClearTarget(session, target_guid);
  if (session.spells().GetAutoRepeatSpellId() != 0) {
    session.spells().CancelSpell(session, SpellSlotType::kAutoRepeat);
  }
}

std::optional<WorldClickPoint> ResolveWorldPoint(
    const WorldSession &session, const WorldTerrainClick &click) {
  if (click.reference_object.IsEmpty()) {
    return click.local_position;
  }

  const auto *object = session.objects().Get(click.reference_object);
  if (object == nullptr) {
    return std::nullopt;
  }

  const float orientation = object->GetOrientation();
  const float cos_orientation = std::cos(orientation);
  const float sin_orientation = std::sin(orientation);
  return WorldClickPoint{
      .x = object->GetX() +
           click.local_position.x * cos_orientation -
           click.local_position.y * sin_orientation,
      .y = object->GetY() +
           click.local_position.x * sin_orientation +
           click.local_position.y * cos_orientation,
      .z = object->GetZ() + click.local_position.z,
  };
}

}

void HandleWorldObjectClick(WorldSession &session,
                            const WorldObjectClick &click) {
  auto* cursor = session.held_cursor();
  if (cursor != nullptr && HasActionObjectPayload(*cursor)) {
    cursor->Clear();
  }

  auto *targeting_system = session.targeting_system();
  if (targeting_system == nullptr) {
    return;
  }

  if (click.button == WorldClickButton::kSecondary) {
    TargetInteractionController target_interaction(*targeting_system);
    (void)target_interaction.InteractWithTarget(session, click.object);
    return;
  }

  const auto* const clicked = session.objects().Get(click.object);
  const auto* const active_player = session.objects().GetActivePlayer();
  if (clicked == nullptr || active_player == nullptr) {
    return;
  }

  const auto* const held_item = cursor != nullptr ? cursor->live_item() : nullptr;
  const bool has_held_item = held_item != nullptr && held_item->item.guid != 0 &&
                             held_item->source_container_guid != 0;

  if (clicked->IsPlayer()) {
    HandleHeldCursorLeftClickOnPlayer(
        session, *active_player, static_cast<const CGUnit_C&>(*clicked));
  }

  if (clicked->IsUnit() && !clicked->IsPlayer()) {

    static_cast<const CGUnit_C&>(*clicked).sound_runtime().PlayAmbientIdleSound(
        click.object.GetRawValue());
  }

  if (clicked->IsGameObject() && has_held_item) {
    (void)::openwow::game::inventory::ui::PromptDeleteHeldCursorItem(
        session.objects(), session.query_cache(),
        {
            .item = ObjectGuid(held_item->item.guid),
            .container = ObjectGuid(held_item->source_container_guid),
        });
  }

  TargetSelectionService target_selection(*targeting_system);
  (void)target_selection.SelectTarget(session, click.object);
}

void HandleWorldTerrainClick(WorldSession &session,
                             const WorldTerrainClick &click) {
  auto* cursor = session.held_cursor();
  if (cursor != nullptr &&
      (click.button == WorldClickButton::kSecondary ||
       HasActionObjectPayload(*cursor))) {
    cursor->Clear();
  }

  if (click.button == WorldClickButton::kPrimary) {
    if (ConfirmSpellGroundTarget(session, SpellGroundClickData{
            .object_guid = click.reference_object,
            .x = click.local_position.x,
            .y = click.local_position.y,
            .z = click.local_position.z,
        })) {
      return;
    }

    const auto* held_item = cursor != nullptr ? cursor->live_item() : nullptr;
    if (held_item != nullptr && held_item->item.guid != 0 &&
        held_item->source_container_guid != 0) {

      (void)::openwow::game::inventory::ui::PromptDeleteHeldCursorItem(
          session.objects(), session.query_cache(),
          {
              .item = ObjectGuid(held_item->item.guid),
              .container = ObjectGuid(held_item->source_container_guid),
          });
      return;
    }

    if (::openwow::ui::game::CVarSystem::Instance().GetCVarBool(
            "deselectOnClick")) {
      ClearTargetAndCancelAutoRepeat(session);
    }
    return;
  }

  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr ||
      static_cast<std::int32_t>(active_player->State().GetHealth()) <= 0 ||
      !active_player->IsActiveMover() ||
      !::openwow::ui::game::CVarSystem::Instance().GetCVarBool(
          "autoInteract")) {
    return;
  }

  const auto world_point = ResolveWorldPoint(session, click);
  if (world_point.has_value()) {
    session.click_to_move().MoveTo(
        world_point->x, world_point->y, world_point->z);
  }
}

void HandleEmptyWorldClick(WorldSession &session,
                           const EmptyWorldClick &click) {
  auto* cursor = session.held_cursor();
  const bool cursor_was_clear = cursor == nullptr || cursor->empty();
  const WorldClickPoint direction{
      .x = click.ray_end.x - click.ray_start.x,
      .y = click.ray_end.y - click.ray_start.y,
      .z = click.ray_end.z - click.ray_start.z,
  };
  const float direction_length_squared =
      direction.x * direction.x + direction.y * direction.y +
      direction.z * direction.z;

  const auto *moving_unit = ResolveEffectiveMovingUnit(session);
  const bool can_auto_walk =
      cursor_was_clear && direction_length_squared != 0.0f &&
      moving_unit != nullptr &&
      CanEffectiveMoverAutoWalk(session, *moving_unit);
  bool cleared_cursor = false;

  if (can_auto_walk && click.button == WorldClickButton::kSecondary) {

    session.click_to_move().Stop();
    const float raw_direction[3] = {direction.x, direction.y, direction.z};
    const_cast<CGUnit_C *>(moving_unit)->Movement().GameUIAutoWalk(
        session, raw_direction);
    return;
  }

  if (!can_auto_walk && click.button == WorldClickButton::kSecondary) {
    if (cursor != nullptr) {
      cursor->Clear();
    }
    cleared_cursor = true;
  }

  const auto* held_item = cursor != nullptr ? cursor->live_item() : nullptr;
  if ((held_item == nullptr || held_item->item.guid == 0 ||
       held_item->source_container_guid == 0) &&
      !cleared_cursor && cursor != nullptr) {
    cursor->Clear();
  }

  if (click.button == WorldClickButton::kPrimary) {
    if (held_item != nullptr && held_item->item.guid != 0 &&
        held_item->source_container_guid != 0) {

      (void)::openwow::game::inventory::ui::PromptDeleteHeldCursorItem(
          session.objects(), session.query_cache(),
          {
              .item = ObjectGuid(held_item->item.guid),
              .container = ObjectGuid(held_item->source_container_guid),
          });
    } else if (cursor_was_clear &&
               ::openwow::ui::game::CVarSystem::Instance().GetCVarBool(
                   "deselectOnClick")) {
      ClearTargetAndCancelAutoRepeat(session);
    }
  }

}

}

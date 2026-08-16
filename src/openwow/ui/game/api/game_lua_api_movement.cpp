
#include "openwow/ui/game/api/game_lua_api_movement.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/unit/unit_movement_runtime.h"
#include "openwow/game/player_combat.h"
#include "openwow/game/targeting/adapters/ui/world_click_controller.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/world_session.h"
#include "openwow/input/input_control.h"
#include "openwow/input/input_manager.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/ui/game/camera_lua_bindings.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/api/game_lua_api_misc_ui.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/world/camera/world_camera.h"

#include <cmath>
#include <string>
#include <utility>

namespace openwow::ui::game::detail {

namespace {

static_assert(openwow::world::kControlTurnOrActionButton ==
              openwow::game::kCtrlTurnOrAction);
static_assert(openwow::world::kControlCameraOrSelectButton ==
              openwow::game::kCtrlCameraOrSelect);
static_assert(openwow::world::kControlMaskStrafeKeys ==
              openwow::game::kMaskStrafe);
static_assert(openwow::world::kControlMaskTurnKeys ==
              openwow::game::kMaskTurn);
static_assert(openwow::world::kControlMaskForwardBackwardAutoRun ==
              openwow::game::kMaskFwdBwdAutoRun);
static_assert(openwow::world::kControlMaskVehicleSeats ==
              openwow::game::kMaskClickToMove);
static_assert(openwow::world::kControlMaskMouseSteer ==
              openwow::game::kMaskMoveAndSteer);

WorldSession *g_process_movement_session = nullptr;

enum class LuaWorldClickSnapshotKind : std::uint8_t {
  kNone,
  kObject,
  kTerrain,
  kEmpty,
};

struct LuaWorldClickSnapshot final {
  WorldSession *session = nullptr;
  LuaWorldClickSnapshotKind kind = LuaWorldClickSnapshotKind::kNone;
  ::openwow::game::ObjectGuid object;
  ::openwow::game::targeting::WorldTerrainClick terrain;
  ::openwow::game::targeting::EmptyWorldClick empty;
};

LuaWorldClickSnapshot g_lua_world_click_snapshot;

constexpr std::uint32_t kCommentatorSpectatorFlags2 = 0x00080000u;
constexpr std::uint32_t kCommentatorAdminFlags2 = 0x00400000u;
constexpr std::uint32_t kCommentatorArenaMapType = 4u;
constexpr std::uint32_t kStandStateBlockedUnitFlags = 0x00100000u;
constexpr std::uint32_t kStandStateBlockedUnitFlags2 = 0x00000001u;
constexpr std::uint32_t kStandStateVehicleSeatOverride = 0x10000000u;
constexpr std::uint32_t kStandStateAllowedCurrentCastAttribute = 0x08000000u;
constexpr std::uint32_t kStandStateSitBlockedMovementMask =
    ::openwow::game::kMoveFlagForward | ::openwow::game::kMoveFlagBackward |
    ::openwow::game::kMoveFlagStrafeLeft | ::openwow::game::kMoveFlagStrafeRight |
    ::openwow::game::kMoveFlagFalling | ::openwow::game::kMoveFlagSwimming |
    ::openwow::game::kMoveFlagAscending | ::openwow::game::kMoveFlagDescending |
    ::openwow::game::kMoveFlagFlying;
constexpr std::uint32_t kStandStateSitBlockedTurnMask =
    ::openwow::game::kMoveFlagTurnLeft | ::openwow::game::kMoveFlagTurnRight;
constexpr std::uint8_t kStandStateStanding = 0u;
constexpr std::uint8_t kStandStateSit = 1u;

constexpr std::uint8_t kStandStateSleep = 3u;

class ScopedLuaInputControl {
public:
  explicit ScopedLuaInputControl(::openwow::game::CInputControl &control)
      : restore_needed_(::openwow::game::GetInputControlSingleton() == nullptr) {
    if (restore_needed_) {
      ::openwow::game::SetInputControlSingleton(&control);
    }
  }

  ~ScopedLuaInputControl() {
    if (restore_needed_) {
      ::openwow::game::SetInputControlSingleton(nullptr);
    }
  }

private:
  bool restore_needed_ = false;
};

bool CanPerformMovementProtectedAction() {
  return GameUI_CanPerformProtectedAction(protected_action_kind::kMovement) != 0;
}

class ScopedLuaMovementProtectedAction {
public:
  ScopedLuaMovementProtectedAction()
      : previous_(::openwow::game::GetProtectedActionCheck()) {
    ::openwow::game::SetProtectedActionCheck(CanPerformMovementProtectedAction);
  }

  ~ScopedLuaMovementProtectedAction() {
    ::openwow::game::SetProtectedActionCheck(previous_);
  }

private:
  ::openwow::game::ProtectedActionCheck previous_ = nullptr;
};

::openwow::game::CInputControl &GetLuaInputControl() {

  if (auto *const singleton = ::openwow::game::GetInputControlSingleton();
      singleton != nullptr) {
    return *singleton;
  }
  static ::openwow::game::CInputControl control;
  return control;
}

bool QueryLuaProcessMovementRuntimeState(
    ::openwow::game::ProcessMovementRuntimeState &state);

bool QueryLuaMouseDeltaRuntimeState(
    ::openwow::game::MouseDeltaRuntimeState &state) {
  ::openwow::game::ProcessMovementRuntimeState movement_state;
  if (!QueryLuaProcessMovementRuntimeState(movement_state)) {
    return false;
  }

  state.frame_tick = ::openwow::core::GameClock::GetTickCount32();
  state.timestamp = state.frame_tick;
  state.apply_distance_sensitivity =
      g_process_movement_session != nullptr &&
      g_process_movement_session->world_camera() != nullptr;
  state.can_mouse_steer =
      GetLuaInputControl().CanMouseSteerUnit(movement_state.gate_state);
  state.player_can_move =
      ::openwow::game::CInputControl::CanPlayerMove(movement_state.gate_state);
  return true;
}

bool QueryLuaActiveCameraDistance(float &distance) {
  const auto *camera =
      g_process_movement_session != nullptr
          ? g_process_movement_session->world_camera()
          : nullptr;
  if (camera == nullptr) {
    return false;
  }
  distance = camera->distance();
  return true;
}

void ApplyLuaCameraMouseInput(float dx, float dy, float *pitch_out) {
  auto* camera = g_process_movement_session != nullptr
                     ? g_process_movement_session->world_camera()
                     : nullptr;
  if (camera == nullptr) {
    return;
  }
  camera->HandleMouseDelta(dx, dy);
  if (pitch_out != nullptr) {
    *pitch_out = camera->target_pitch();
  }
}

void RefreshLuaCameraPitchClamp() {}

class ScopedLuaMouseDeltaCallbacks {
public:
  ScopedLuaMouseDeltaCallbacks() {
    ::openwow::game::SetActiveWorldCameraDistanceQuery(
        QueryLuaActiveCameraDistance);
    ::openwow::game::SetMouseDeltaRuntimeStateQuery(
        QueryLuaMouseDeltaRuntimeState);
    ::openwow::game::SetCameraMouseInputCallback(ApplyLuaCameraMouseInput);
    ::openwow::game::SetCameraPitchClampCallback(
        RefreshLuaCameraPitchClamp);
  }

  ~ScopedLuaMouseDeltaCallbacks() {
    ::openwow::game::SetCameraPitchClampCallback(nullptr);
    ::openwow::game::SetCameraMouseInputCallback(nullptr);
    ::openwow::game::SetMouseDeltaRuntimeStateQuery(nullptr);
    ::openwow::game::SetActiveWorldCameraDistanceQuery(nullptr);
  }
};

bool CanUseCommentatorMovementRates(const WorldSession &session,
                                    const ::openwow::game::CGPlayer_C &player) {
  const auto flags2 = player.State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u) {
    return false;
  }

  if ((flags2 & kCommentatorAdminFlags2) != 0u) {
    return true;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto *map_entry = dbc->map().LookupEntry(session.current_map_id());
  return map_entry != nullptr && map_entry->map_type == kCommentatorArenaMapType;
}

bool BuildLuaProcessMovementRuntimeState(
    WorldSession &session,
    ::openwow::game::ProcessMovementRuntimeState &state) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return false;
  }

  const auto *moving_unit =
      ::openwow::game::ResolveEffectiveMovingUnit(session);
  if (moving_unit == nullptr) {
    return false;
  }

  const auto &movement_data = moving_unit->Movement().Data();
  const std::uint32_t runtime_movement_flags = movement_data.GetRuntimeFlags();
  const auto &vehicle_system = ::openwow::game::VehicleSystem::Get();
  const auto *moving_passenger = moving_unit->Vehicle().GetVehiclePassengerComponent();
  const auto current_seat = vehicle_system.GetSeat(vehicle_system.GetSeatIndex());
  const bool seat_allows_free_movement =
      !vehicle_system.IsInVehicle() || !current_seat.has_value() ||
      ::openwow::game::HasFlag(current_seat->flags,
                               ::openwow::game::VehicleSeatFlags::StaticPassenger);

  state.gate_state.health = static_cast<std::int32_t>(moving_unit->State().GetHealth());
  state.gate_state.unit_flags = moving_unit->State().GetUnitFlags();
  state.gate_state.has_knockdown_animation =
      moving_unit->Mount().HasKnockdownAnimation(*moving_unit);
  state.gate_state.is_active_player = moving_unit->GetGuid() == player->GetGuid();
  state.gate_state.vehicle_control_allows_free_movement = seat_allows_free_movement;
  state.gate_state.is_in_vehicle_transition =
      ::openwow::game::UnitVehicle_IsActivePlayerInVehicle(moving_unit);
  state.gate_state.has_non_static_vehicle_seat =
      moving_passenger != nullptr &&
      ::openwow::game::UnitVehicle_HasNonStaticSeat(
          session,
          static_cast<std::uint32_t>(moving_passenger->GetPrimaryVehicleGuid()),
          static_cast<std::uint32_t>(moving_passenger->GetPrimaryVehicleGuid() >> 32),
          moving_passenger->GetPrimarySeatIndex());
  state.gate_state.has_movement_restriction_flags =
      (runtime_movement_flags & 0x00100A00u) != 0;
  state.gate_state.is_power_type_locked = moving_unit->State().GetPowerType() == 7;
  state.gate_state.is_on_vehicle = moving_unit->Vehicle().HasValidVehicleUnitGuid();
  state.commentator_controls_enabled =
      CanUseCommentatorMovementRates(session, *player);
  state.camera_bound_to_mover =
      session.world_camera() != nullptr;
  state.camera_bound_alpha_visible =
      session.world_camera() != nullptr &&
      session.world_camera()->resolved_distance() > 0.1f;
  state.allow_keyboard_turn_in_move_and_steer =
      ::openwow::game::InputControl_CheckTurnFlag(moving_unit) != 0;
  if (const auto *vehicle_entry = moving_unit->Vehicle().GetVehicleEntry();
      vehicle_entry != nullptr) {
    state.vehicle_allows_ground_pitch =
        (vehicle_entry->flags & 0x00040000u) != 0u;
  }
  state.movement_flags = runtime_movement_flags;
  state.movement_flags2 = movement_data.GetRuntimeFlags2();
  state.should_use_movement_rates = state.commentator_controls_enabled;

  constexpr std::uint32_t kUnitFlags2ForceMovement = 0x40u;
  state.has_force_forward_override =
      (moving_unit->State().GetUnitFlags2() & kUnitFlags2ForceMovement) != 0 ||
      moving_unit->Vehicle().HasVehicleSeatMovementBlock(*moving_unit);

  std::uint32_t timestamp_floor_ms = 0;
  if (::openwow::core::TryGetMovementRuntimeTimestampFloor(timestamp_floor_ms)) {
    state.has_timestamp_floor = true;
    state.timestamp_floor = timestamp_floor_ms;
  }
  return true;
}

bool QueryLuaProcessMovementRuntimeState(
    ::openwow::game::ProcessMovementRuntimeState &state) {
  return g_process_movement_session != nullptr &&
         BuildLuaProcessMovementRuntimeState(*g_process_movement_session,
                                             state);
}

bool QueryLuaMovementRatesRuntimeState(
    ::openwow::game::MovementRatesRuntimeState& state) {
  if (g_process_movement_session == nullptr) {
    return false;
  }

  state.has_active_player =
      g_process_movement_session->objects().GetLocalPlayerTyped() != nullptr;
  state.has_active_camera =
      g_process_movement_session->world_camera() != nullptr;
  return true;
}

void ClearLuaMovementAfkState() {
  if (g_process_movement_session == nullptr) {
    return;
  }

  g_process_movement_session->chat_sender().ClearLocalAfkIfNeeded(false);
}

void CaptureLuaWorldClick() {

  g_lua_world_click_snapshot = {};
  auto *const session = g_process_movement_session;
  auto *const world_frame =
      session != nullptr ? session->world_frame() : nullptr;
  if (world_frame == nullptr) {
    return;
  }

  g_lua_world_click_snapshot.session = session;
  const auto [mouse_x, mouse_y] =
      openwow::input::InputManager::Get().GetMousePosition();
  const auto pick = world_frame->Pick(mouse_x, mouse_y);
  if (pick.hit &&
      pick.type != openwow::render::PickResult::HitType::kTerrain &&
      pick.type != openwow::render::PickResult::HitType::kNone &&
      !pick.guid.IsEmpty()) {
    g_lua_world_click_snapshot.kind = LuaWorldClickSnapshotKind::kObject;
    g_lua_world_click_snapshot.object = pick.guid;
    return;
  }

  if (pick.hit &&
      pick.type == openwow::render::PickResult::HitType::kTerrain &&
      world_frame->TryBuildTerrainClickInput(
          pick, &g_lua_world_click_snapshot.terrain)) {
    g_lua_world_click_snapshot.kind = LuaWorldClickSnapshotKind::kTerrain;
    return;
  }

  const auto ray = world_frame->ScreenToWorldRay(mouse_x, mouse_y);
  g_lua_world_click_snapshot.kind = LuaWorldClickSnapshotKind::kEmpty;
  g_lua_world_click_snapshot.empty = {
      .ray_start = {ray.origin_x, ray.origin_y, ray.origin_z},
      .ray_end = {
          ray.origin_x + ray.dir_x,
          ray.origin_y + ray.dir_y,
          ray.origin_z + ray.dir_z,
      },
  };
}

void ProcessLuaInputControlMovement(::openwow::game::CInputControl &control,
                                    const ::openwow::game::ProcessMovementDecision &decision) {
  if (g_process_movement_session == nullptr) {
    return;
  }

  if (decision.stop_auto_attack) {
    if (auto *player = const_cast<openwow::game::CGPlayer_C*>(g_process_movement_session->objects().GetLocalPlayerTyped());
        player != nullptr) {
      player->Interaction().CompleteAutoAttackInteraction(false, true);
    }
  }

  if (decision.can_turn) {
    const int net_turn = control.ComputeNetTurn(
        decision.allow_keyboard_turn_in_move_and_steer);
    if (net_turn != 0) {
      if (auto *player =
              const_cast<openwow::game::CGPlayer_C*>(g_process_movement_session->objects().GetLocalPlayerTyped());
          player != nullptr && player->Animation().GetStandState() != 0) {
        player->Animation().MaybeStandUpIfPlayer(*g_process_movement_session, 0);
      }
      g_process_movement_session->chat_sender().ClearLocalAfkIfNeeded(false);
    }
  }

  if (decision.can_move) {

    const auto vertical = control.ResolveVerticalMovementDecision(
        decision.movement_flags, decision.movement_flags2);
    if (vertical.clear_afk) {
      ClearLuaMovementAfkState();
    }
  }

  const auto mover_guid =
      g_process_movement_session->player_control_runtime().ActiveMoverGuid();
  auto *const mover = g_process_movement_session->objects().GetMutableUnit(
      mover_guid);
  if (mover != nullptr) {
    mover->Movement().ApplyInputControlMovement(
        *g_process_movement_session, control, decision);
  }
}

void DispatchLuaWorldClick(
    WorldSession &session,
    const ::openwow::game::targeting::WorldClickButton button) {
  namespace click = ::openwow::game::targeting;
  if (g_lua_world_click_snapshot.session != &session) {
    return;
  }

  switch (g_lua_world_click_snapshot.kind) {
  case LuaWorldClickSnapshotKind::kObject:
    click::ui::HandleWorldObjectClick(
        session,
        click::WorldObjectClick{
            .object = g_lua_world_click_snapshot.object,
            .button = button,
        });
    return;
  case LuaWorldClickSnapshotKind::kTerrain: {
    auto terrain = g_lua_world_click_snapshot.terrain;
    terrain.button = button;
    click::ui::HandleWorldTerrainClick(session, terrain);
    return;
  }
  case LuaWorldClickSnapshotKind::kEmpty: {
    auto empty = g_lua_world_click_snapshot.empty;
    empty.button = button;
    click::ui::HandleEmptyWorldClick(session, empty);
    return;
  }
  case LuaWorldClickSnapshotKind::kNone:
    return;
  }
}

void ApplyLuaControlFlagSideEffects(
    const ::openwow::game::ControlFlagSideEffects &effects) {
  auto* const active_camera =
      g_process_movement_session != nullptr
          ? g_process_movement_session->world_camera()
          : nullptr;

  if (effects.check_auto_freelook && active_camera != nullptr) {
    if (!active_camera->IsTaxiFlightInputSuppressed() &&
        !active_camera->IsFreelooking()) {
      active_camera->EnterFreelook();
    }
  }

  if (effects.clear_mouse_modes && active_camera != nullptr) {
    active_camera->ExitFreelook();
  }

  if (effects.pop_yaw_offset && active_camera != nullptr) {
    active_camera->PopYawOffset();
  }

  if (effects.mouse_button_set) {
    openwow::input::OnMouseButtonSet();
  }

  if (effects.mouse_button_clear) {
    openwow::input::OnMouseButtonClear();
  }

  (void)effects.update_pitch_clamp;
  (void)effects.update_bobbing;

  if (effects.click_to_move_flags_changed && active_camera != nullptr &&
      g_process_movement_session != nullptr) {
    active_camera->SetTrackingCamera(
        effects.click_to_move_flags != 0u,
        g_process_movement_session->click_to_move()
            .CameraFollowsUnitDuringDrive(),
        effects.control_flags, effects.timestamp);
  }

  if (effects.evaluate_camera_smoothing && active_camera != nullptr) {
    active_camera->ApplyControlFlagSmoothing(
        effects.control_flags, effects.camera_smoothing_stop_event,
        effects.timestamp);
  }

  if (effects.dispatch_world_click_type != 0 &&
      g_process_movement_session != nullptr) {
    DispatchLuaWorldClick(
        *g_process_movement_session,
        effects.dispatch_world_click_type == 1
            ? ::openwow::game::targeting::WorldClickButton::kPrimary
            : ::openwow::game::targeting::WorldClickButton::kSecondary);
  }
}

class ScopedLuaProcessMovementSession {
public:
  explicit ScopedLuaProcessMovementSession(WorldSession *session)
      : previous_session_(g_process_movement_session) {
    g_process_movement_session = session;
    ::openwow::game::SetMovementRatesRuntimeStateQuery(
        QueryLuaMovementRatesRuntimeState);
    ::openwow::game::SetClearLocalAfkForMovementCallback(
        ClearLuaMovementAfkState);
    ::openwow::game::SetSaveCursorPosCallback(CaptureLuaWorldClick);
    ::openwow::game::SetProcessMovementRuntimeStateQuery(QueryLuaProcessMovementRuntimeState);
    ::openwow::game::SetProcessMovementCallback(ProcessLuaInputControlMovement);
    ::openwow::game::SetControlFlagSideEffectCallback(
        ApplyLuaControlFlagSideEffects);
  }

  ~ScopedLuaProcessMovementSession() {
    g_process_movement_session = previous_session_;
    if (previous_session_ == nullptr) {
      ::openwow::game::SetMovementRatesRuntimeStateQuery(nullptr);
      ::openwow::game::SetClearLocalAfkForMovementCallback(nullptr);
      ::openwow::game::SetSaveCursorPosCallback(nullptr);
      ::openwow::game::SetProcessMovementRuntimeStateQuery(nullptr);
      ::openwow::game::SetProcessMovementCallback(nullptr);
      ::openwow::game::SetControlFlagSideEffectCallback(nullptr);
    }
  }

private:
  WorldSession *previous_session_ = nullptr;
};

std::uint32_t CurrentInputTimestamp() {

  return ::openwow::core::GameClock::GetTickCount32();
}

float GetMoveViewRateScale(lua_State *L) {
  float rate_scale = 1.0f;
  if (lua_isnumber(L, 1) != 0) {
    rate_scale = static_cast<float>(lua_tonumber(L, 1));
  }
  return rate_scale;
}

int LuaMoveViewStart(lua_State *L, const ::openwow::world::ScriptedCameraMoveChannel channel) {
  const std::uint32_t timestamp = CurrentInputTimestamp();
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    manager->world_camera().StartScriptedMoveChannel(
        channel, timestamp, GetMoveViewRateScale(L));
  }
  return 0;
}

int LuaMoveViewStop(lua_State* state,
                    const ::openwow::world::ScriptedCameraMoveChannel channel) {
  const std::uint32_t timestamp = CurrentInputTimestamp();
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(state); manager != nullptr) {
    manager->world_camera().StopScriptedMoveChannel(channel, timestamp);
  }
  return 0;
}

template <typename Fn>
int InvokeInputControlAtTimestamp(lua_State *L, Fn &&fn,
                                  const std::uint32_t timestamp) {
  auto &control = GetLuaInputControl();
  ScopedLuaInputControl bind(control);
  ScopedLuaProcessMovementSession bridge(GetWorldSession(L));
  ScopedLuaMovementProtectedAction protected_action;
  return fn(timestamp);
}

template <typename Fn> int InvokeInputControl(lua_State *L, Fn &&fn) {
  return InvokeInputControlAtTimestamp(
      L, std::forward<Fn>(fn), CurrentInputTimestamp());
}

bool IsInputControlMouselooking() {
  const auto flags = GetLuaInputControl().GetControlFlags();

  return (flags & ::openwow::game::kMaskMoveAndSteer) != 0;
}

void CancelActiveClickToMoveOnMovementInput(lua_State *L) {
  if (auto *const session = GetWorldSession(L); session != nullptr) {
    session->click_to_move().Stop();
  }
}

void CancelActiveClickToMoveIfBothMouseButtonsNowHeld(lua_State *L) {
  const auto flags = GetLuaInputControl().GetControlFlags();
  if ((flags & ::openwow::game::kMaskMouseButtons) ==
      ::openwow::game::kMaskMouseButtons) {
    CancelActiveClickToMoveOnMovementInput(L);
  }
}

}

int LuaAscendStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_AscendStop);
  return 0;
}

int LuaJumpOrAscendStart(lua_State *L) {
  auto *const session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  CancelActiveClickToMoveOnMovementInput(L);

  const auto mover_guid =
      session->player_control_runtime().ActiveMoverGuid();
  auto *const mover = session->objects().GetMutableUnit(mover_guid);
  if (mover == nullptr) {
    return 0;
  }

  auto &control = GetLuaInputControl();
  ScopedLuaInputControl bind(control);
  ScopedLuaProcessMovementSession bridge(session);

  if (!CanPerformMovementProtectedAction()) {
    return 0;
  }

  const std::uint32_t timestamp = CurrentInputTimestamp();
  (void)::openwow::game::InputControl_ApplyControlFlagChange(
      ::openwow::game::kCtrlAscend, true, timestamp);

  if ((mover->GetMovementInfo().flags &
       (::openwow::game::kMoveFlagSwimming |
        ::openwow::game::kMoveFlagFlying)) != 0u) {
    return 0;
  }

  ::openwow::game::InputControl_NotifyMovementActivated();

  ::openwow::game::ProcessMovementRuntimeState runtime_state;
  if (!BuildLuaProcessMovementRuntimeState(*session, runtime_state)) {
    return 0;
  }
  const auto &gate = runtime_state.gate_state;
  if (gate.health <= 0 ||
      !gate.vehicle_control_allows_free_movement ||
      gate.is_in_vehicle_transition || gate.has_knockdown_animation ||
      !::openwow::game::CInputControl::CanUnitWalk(gate)) {
    return 0;
  }

  ClearLuaMovementAfkState();

  if ((mover->GetMovementInfo().flags & ::openwow::game::kMoveFlagCanFly) !=
      0u) {
    mover->Movement().SwimToFly(timestamp, true);
    return 0;
  }

  mover->Movement().SendJump(*session, timestamp);
  return 0;
}

int LuaToggleAutoRun(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_ToggleAutoRun);
  return 0;
}

bool CurrentCastAllowsStandState(const WorldSession &session,
                                 const ::openwow::game::CGUnit_C &player) {
  const auto spell_id = player.Casts().GetCurrentCast().spell_id;
  if (spell_id == 0u) {
    return true;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  return spell != nullptr &&
         (spell->attributes & kStandStateAllowedCurrentCastAttribute) != 0u;
}

bool CanSendStandStateToggle(const WorldSession &session,
                             const ::openwow::game::CGPlayer_C &player,
                             const std::uint8_t target_state) {
  if ((player.State().GetUnitFlags() & kStandStateBlockedUnitFlags) != 0u) {
    return false;
  }
  if (player.State().GetHealth() == 0u) {
    return false;
  }
  if ((player.State().GetUnitFlags2() & kStandStateBlockedUnitFlags2) != 0u) {
    return false;
  }
  if (player.Vehicle().GetVehiclePassengerComponent() != nullptr &&
      (player.State().GetSpellStateFlags() & kStandStateVehicleSeatOverride) == 0u) {
    return false;
  }
  if (!CurrentCastAllowsStandState(session, player)) {
    return false;
  }
  if (player.Vehicle().HasAttachedVehiclePassenger()) {
    return false;
  }
  if (player.Animation().HasChannelingActionLock()) {
    return false;
  }
  if (player.Casts().GetChannelCast().cast_id != 0u) {
    return false;
  }
  if (!player.Movement().CanControlCharacter()) {
    return false;
  }

  if (target_state != kStandStateStanding) {
    const auto movement_flags = player.GetMovementInfo().flags;
    if ((movement_flags & kStandStateSitBlockedMovementMask) != 0u) {
      return false;
    }
    if ((target_state == kStandStateSit || target_state == kStandStateSleep) &&
        (movement_flags & kStandStateSitBlockedTurnMask) != 0u) {
      return false;
    }
  }

  return true;
}

int LuaSitStandOrDescendStart(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (session == nullptr || player == nullptr) {
    return 0;
  }
  CancelActiveClickToMoveOnMovementInput(L);

  InvokeInputControl(L, ::openwow::game::InputControl_DescendStart);

  if ((player->GetMovementInfo().flags & ::openwow::game::kMoveFlagFlying) != 0u) {
    return 0;
  }

  const auto target_state =
      player->Animation().GetStandState() == kStandStateStanding ? kStandStateSit : kStandStateStanding;
  if (CanSendStandStateToggle(*session, *player, target_state)) {
    session->interaction().SendStandStateChange(target_state);
  }
  return 0;
}

int LuaDescendStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_DescendStop);
  return 0;
}

int LuaCameraOrSelectOrMoveStart(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_CameraOrSelectOrMoveStart);

  CancelActiveClickToMoveIfBothMouseButtonsNowHeld(L);
  return 0;
}

int LuaCameraOrSelectOrMoveStop(lua_State *L) {
  const auto lock_camera =
      static_cast<std::uint32_t>(ScriptReadBoolArgOrDefault(L, 1, false));
  InvokeInputControl(L, [lock_camera](std::uint32_t timestamp) {
    return ::openwow::game::InputControl_CameraOrSelectOrMoveStop(
        timestamp, lock_camera);
  });
  return 0;
}

int LuaDetectWowMouse([[maybe_unused]] lua_State *L) {
  auto& control = GetLuaInputControl();
  ScopedLuaInputControl bind(control);
  (void)control.DetectWowMouse();
  return 0;
}

int LuaIsMouselooking(lua_State *L) {
  lua_pushwowbool(L, IsInputControlMouselooking());
  return 1;
}

int LuaMouselookStart([[maybe_unused]] lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_MouselookStart);
  openwow::input::InputManager::Get().SetMouseLooking(true);
  return 0;
}

int LuaMouselookStop([[maybe_unused]] lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_MouselookStop);
  openwow::input::InputManager::Get().SetMouseLooking(false);
  return 0;
}

int LuaMoveAndSteerStart(lua_State *L) {

  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_MoveAndSteerStart);
  return 0;
}

int LuaMoveAndSteerStop([[maybe_unused]] lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_MoveAndSteerStop);
  return 0;
}

int LuaMoveBackwardStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_MoveBackwardStart);
  return 0;
}

int LuaMoveBackwardStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_MoveBackwardStop);
  return 0;
}

int LuaMoveViewDownStart(lua_State *L) {
  return LuaMoveViewStart(L, ::openwow::world::ScriptedCameraMoveChannel::kPitchDown);
}

int LuaMoveViewDownStop([[maybe_unused]] lua_State *L) {
  return LuaMoveViewStop(L, ::openwow::world::ScriptedCameraMoveChannel::kPitchDown);
}

int LuaMoveViewInStart(lua_State *L) {
  return LuaMoveViewStart(L, ::openwow::world::ScriptedCameraMoveChannel::kZoomIn);
}

int LuaMoveViewInStop([[maybe_unused]] lua_State *L) {
  return LuaMoveViewStop(L, ::openwow::world::ScriptedCameraMoveChannel::kZoomIn);
}

int LuaMoveViewLeftStart(lua_State *L) {
  return LuaMoveViewStart(L, ::openwow::world::ScriptedCameraMoveChannel::kYawLeft);
}

int LuaMoveViewLeftStop([[maybe_unused]] lua_State *L) {
  return LuaMoveViewStop(L, ::openwow::world::ScriptedCameraMoveChannel::kYawLeft);
}

int LuaMoveViewOutStart(lua_State *L) {
  return LuaMoveViewStart(L, ::openwow::world::ScriptedCameraMoveChannel::kZoomOut);
}

int LuaMoveViewOutStop([[maybe_unused]] lua_State *L) {
  return LuaMoveViewStop(L, ::openwow::world::ScriptedCameraMoveChannel::kZoomOut);
}

int LuaMoveViewRightStart(lua_State *L) {
  return LuaMoveViewStart(L, ::openwow::world::ScriptedCameraMoveChannel::kYawRight);
}

int LuaMoveViewRightStop([[maybe_unused]] lua_State *L) {
  return LuaMoveViewStop(L, ::openwow::world::ScriptedCameraMoveChannel::kYawRight);
}

int LuaMoveViewUpStart(lua_State *L) {
  return LuaMoveViewStart(L, ::openwow::world::ScriptedCameraMoveChannel::kPitchUp);
}

int LuaMoveViewUpStop([[maybe_unused]] lua_State *L) {
  return LuaMoveViewStop(L, ::openwow::world::ScriptedCameraMoveChannel::kPitchUp);
}

int LuaMoveForwardStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_MoveForwardStart);
  return 0;
}

int LuaMoveForwardStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_MoveForwardStop);
  return 0;
}

int LuaPitchDownStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_PitchDownStart);
  return 0;
}

int LuaPitchDownStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_PitchDownStop);
  return 0;
}

int LuaPitchUpStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_PitchUpStart);
  return 0;
}

int LuaPitchUpStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_PitchUpStop);
  return 0;
}

int LuaStrafeLeftStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_StrafeLeftStart);
  return 0;
}

int LuaStrafeLeftStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_StrafeLeftStop);
  return 0;
}

int LuaStrafeRightStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_StrafeRightStart);
  return 0;
}

int LuaStrafeRightStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_StrafeRightStop);
  return 0;
}

int LuaToggleRun(lua_State *L) {
  if (GameUI_CanPerformProtectedAction(protected_action_kind::kMovement) == 0) {
    return 0;
  }

  auto *const session = GetWorldSession(L);
  const auto *const player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (session == nullptr || player == nullptr) {
    return 0;
  }
  const auto *const moving_unit =
      ::openwow::game::ResolveEffectiveMovingUnit(*session);
  if (moving_unit == nullptr) {
    return 0;
  }

  ::openwow::game::ProcessMovementRuntimeState runtime_state;
  if (!BuildLuaProcessMovementRuntimeState(*session, runtime_state)) {
    return 0;
  }
  const auto &gate = runtime_state.gate_state;
  if (gate.health <= 0 ||
      !gate.vehicle_control_allows_free_movement ||
      gate.is_in_vehicle_transition ||
      !::openwow::game::CInputControl::CanUnitWalk(gate)) {
    return 0;
  }

  auto *const mutable_mover =
      session->objects().GetMutableUnit(moving_unit->GetGuid());
  if (mutable_mover != nullptr) {
    mutable_mover->Movement().ToggleRun(CurrentInputTimestamp());
  }
  return 0;
}

int LuaToggleSheath(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  (void)::openwow::game::TogglePlayerSheathe(*session);
  return 0;
}

int LuaTurnLeftStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_TurnLeftStart);
  return 0;
}

int LuaTurnLeftStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_TurnLeftStop);
  return 0;
}

int LuaSetMouselookOverrideBinding(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(
        L, "Usage: SetMouselookOverrideBinding(\"key\", \"binding\" or nil)");
  }

  if (!GameUI_CanPerformProtectedAction(
          protected_action_kind::kKeyBinding)) {
    return 0;
  }
  const char *key = lua_tostring(L, 1);
  const char *command = lua_tostring(L, 2);
  auto &control = GetLuaInputControl();
  ScopedLuaInputControl bind(control);
  ::openwow::game::InputControl_SetStoredMouselookOverrideBinding(key, command);
  return 0;
}

int LuaTurnOrActionStart(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_TurnOrActionStart);
  openwow::input::InputManager::Get().SetMouseLooking(true);

  CancelActiveClickToMoveIfBothMouseButtonsNowHeld(L);
  return 0;
}

int LuaTurnOrActionStop([[maybe_unused]] lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_TurnOrActionStop);
  openwow::input::InputManager::Get().SetMouseLooking(false);
  return 0;
}

int LuaTurnRightStart(lua_State *L) {
  CancelActiveClickToMoveOnMovementInput(L);
  InvokeInputControl(L, ::openwow::game::InputControl_TurnRightStart);
  return 0;
}

int LuaTurnRightStop(lua_State *L) {
  InvokeInputControl(L, ::openwow::game::InputControl_TurnRightStop);
  return 0;
}

int LuaFlipCameraYaw(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: FlipCameraYaw(degrees)");
  }
  const double degrees = lua_tonumber(L, 1);
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    FlipActiveCameraYawDegrees(manager->world_camera(), degrees);
  }
  return 0;
}

int LuaResetView(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: ResetView(viewModeIndex)");
  }
  const int view_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (view_index < 1 || view_index > 5)
    return 0;
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    ResetCameraViewPreset(manager->world_camera(), view_index);
  }
  return 0;
}

int LuaApi_NextView(lua_State *L) {
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    StepCameraViewPreset(manager->world_camera(), 1);
  }
  return 0;
}

int LuaApi_VehicleCameraZoomIn(lua_State *L) {
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    ZoomActiveCamera(manager->world_camera(), true,
                     ReadOptionalCameraZoomIncrement(L));
  }
  return 0;
}

int LuaApi_VehicleCameraZoomOut(lua_State *L) {
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    ZoomActiveCamera(manager->world_camera(), false,
                     ReadOptionalCameraZoomIncrement(L));
  }
  return 0;
}

WorldMouseDeltaResult HandleWorldMouseDelta(lua_State *L, const float dx,
                                             const float dy) {
  auto &control = GetLuaInputControl();
  if (control.GetControlFlags() == 0u) {
    return {};
  }

  ScopedLuaInputControl bind(control);
  ScopedLuaProcessMovementSession bridge(GetWorldSession(L));
  ScopedLuaMouseDeltaCallbacks callbacks;
  ::openwow::game::ProcessMovementRuntimeState movement_state;
  const bool steers_mover =
      QueryLuaProcessMovementRuntimeState(movement_state) &&
      control.CanMouseSteerUnit(movement_state.gate_state);
  control.HandleMouseDelta(dx, dy);
  if (steers_mover && g_process_movement_session != nullptr) {
    if (const auto *const camera =
            g_process_movement_session->world_camera();
        camera != nullptr) {
      (void)control.ApplyCameraFacing(
          *g_process_movement_session, CurrentInputTimestamp(),
          camera->yaw());

      (void)control.ApplyCameraPitch(
          *g_process_movement_session, CurrentInputTimestamp(),
          camera->pitch());
    }
  }
  return {.handled = true, .steers_mover = steers_mover};
}

void CancelWorldMouseInput(lua_State *L) {
  auto &control = GetLuaInputControl();
  if (control.GetControlFlags() == 0u) {
    return;
  }

  ScopedLuaInputControl bind(control);
  ScopedLuaProcessMovementSession bridge(GetWorldSession(L));
  control.SetLastMouseButtonType(0u);
  control.ResetControlFlagsAndJoystick(CurrentInputTimestamp());
}

void HandleWorldApplicationActivation(lua_State *L, const bool active) {
  auto &control = GetLuaInputControl();
  ScopedLuaInputControl bind(control);
  ScopedLuaProcessMovementSession bridge(GetWorldSession(L));
  ::openwow::game::GameUiMovementGateContext event_record;
  event_record.gate = active ? 1u : 0u;
  (void)control.ProcessGameUiMovementState(event_record,
                                           CurrentInputTimestamp());

  if (auto *const manager = runtime::WorldUiRuntimeContext::FromLua(L);
      manager != nullptr) {
    manager->input_router().SetApplicationActive(active);
  }
}

}

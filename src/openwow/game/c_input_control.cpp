
#include "openwow/game/c_input_control.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/game/commentator_state.h"
#include "openwow/game/frame_timer.h"
#include "openwow/game/input_control.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/persistence/retail_binding_text_codec.h"
#include "openwow/game/actions/bindings/model/binding_types.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/world_session.h"
#include "openwow/input/hid_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

float g_camera_aspect_normalize_x = 0.800000011920929f;
float g_camera_aspect_normalize_y = 0.6000000238418579f;
constexpr char kMouselookOverrideOwner[] = "__openwow_mouselook";
constexpr std::size_t kStormFullStringCompare = 0x7FFFFFFFu;

void ProcessMovement(CInputControl& control, std::uint32_t timestamp,
                     bool movement_active);
void ApplyCursorVisibility(bool visible);
bool IsCinematicJoystickEnabled();
void ApplyJoystickFreeLookMode(bool enabled);
std::uint32_t CurrentInputTickCount();
std::uint32_t CurrentInputFrameTick();
bool QueryCanMouseSteerUnitForControlFlagsImpl(const CInputControl& control);

struct CameraControlGateResult {
  bool allowed = false;
  bool commentator_controls = false;
};

CameraControlGateResult QueryCanPlayerMoveForCameraPitchImpl();
CameraControlGateResult QueryCanApplyCameraFacingImpl(
    const CInputControl& control);

struct CaseInsensitiveStringHash {
  std::size_t operator()(const std::string& value) const {
    return openwow::core::SStrHashCI(value.c_str());
  }
};

struct CaseInsensitiveStringEqual {
  bool operator()(const std::string& left, const std::string& right) const {
    return openwow::core::SStrCmpNoCase(left.c_str(), right.c_str(),
                                        kStormFullStringCompare) == 0;
  }
};

using MouselookBindingStore =
    std::unordered_map<std::string, std::string, CaseInsensitiveStringHash,
                       CaseInsensitiveStringEqual>;

bool StoredOverrideBindingEqualsNoCase(const std::string& existing_binding,
                                       const char* new_binding) {
  return openwow::core::SStrCmpNoCase(existing_binding.c_str(), new_binding,
                                      kStormFullStringCompare) == 0;
}

bool AreBothMouseButtonsHeld(const std::uint32_t flags) {
  return (flags & kCtrlTurnOrAction) != 0 &&
         (flags & kCtrlCameraOrSelect) != 0;
}

bool IsMoveAndSteerTurnActive(const std::uint32_t flags) {
  return (flags & kMaskMoveAndSteer) != 0 && (flags & kMaskTurn) != 0;
}

bool AreControlFlagsIdle(const std::uint32_t flags) {
  return (flags & kMaskFwdBwdAutoRun) == 0 &&
         (flags & kMaskStrafe) == 0 &&
         !IsMoveAndSteerTurnActive(flags) &&
         ((flags & kMaskTurn) == 0 || (flags & kMaskMoveAndSteer) != 0) &&
         (flags & kMaskClickToMove) == 0;
}

bool AreCollisionDirectionalControlsIdle(const std::uint32_t flags) {
  return (flags & kMaskFwdBwdAutoRun) == 0u &&
         (flags & kMaskStrafe) == 0u &&
         (flags & kMaskTurn) == 0u &&
         (flags & kMaskClickToMove) == 0u;
}

bool ShouldEnableMovementBobbing(const std::uint32_t flags) {
  return (flags & kMaskActionMovement) != 0 ||
         AreBothMouseButtonsHeld(flags) ||
         IsMoveAndSteerTurnActive(flags);
}
}

class InputControlBindingRuntime {
 public:
  BindingProfiles* profiles{nullptr};
  MouselookBindingStore staged_bindings;
  BindingProfiles::OverrideOwner override_owner =
      BindingProfiles::OverrideOwner::FromStableTag(kMouselookOverrideOwner);

  MissileTrajectoryInputRefreshCallback missile_trajectory_input_refresh;
};

namespace {
void EmitControlFlagSideEffects(const ControlFlagSideEffects& effects);
bool ShouldNotifyMovementActivatedForFlagSet(std::uint32_t flags_to_reapply,
                                              std::uint32_t prior_flags);
void NotifyMovementActivated();
std::uint32_t CurrentMouseDeltaTimestamp();
extern MouseDeltaRuntimeStateQuery g_mouse_delta_runtime_state_query;
extern CameraMouseInputCallback g_camera_mouse_input_callback;
extern CameraPitchClampCallback g_camera_pitch_clamp_callback;
extern VehicleAimRequestCallback g_vehicle_aim_request_callback;
}

CInputControl::CInputControl()
    : binding_runtime_(std::make_unique<InputControlBindingRuntime>()) {
  Reset();
  reserved0_ = openwow::core::GameClock::GetTickCount32();
  cursor_visibility_flags_ = 3;
  joystick_free_look_flags_ = 3;
}

CInputControl::CInputControl(BindingProfiles& binding_profiles)
    : CInputControl() {
  BindBindingProfiles(binding_profiles);
}

CInputControl::~CInputControl() {
  ClearMouselookOverrideBindings();
  SetJoystickEnabled(false);
  binding_runtime_->missile_trajectory_input_refresh = {};
}

void CInputControl::BindBindingProfiles(BindingProfiles& binding_profiles) {
  if (binding_runtime_->profiles == &binding_profiles) {
    return;
  }
  if (binding_runtime_->profiles != nullptr) {
    binding_runtime_->profiles->ClearOverrideBindings(
        binding_runtime_->override_owner);
  }
  binding_runtime_->profiles = &binding_profiles;
}

void CInputControl::BindMissileTrajectoryInputRefresh(
    MissileTrajectoryInputRefreshCallback callback) {
  binding_runtime_->missile_trajectory_input_refresh = std::move(callback);
}

void CInputControl::LatchMissileTrajectoryInputRefresh() {
  if (binding_runtime_->missile_trajectory_input_refresh) {
    binding_runtime_->missile_trajectory_input_refresh();
  }
}

void CInputControl::SetMouselookOverrideBinding(
    const std::string_view key, const char* binding) {
  const std::string stored_key =
      actions::bindings::adapters::persistence::
          NormalizeRetailBindingChord(key);
  if (binding == nullptr) {
    binding_runtime_->staged_bindings.erase(stored_key);
    return;
  }

  const auto it = binding_runtime_->staged_bindings.find(stored_key);
  if (it == binding_runtime_->staged_bindings.end()) {
    binding_runtime_->staged_bindings.emplace(stored_key, binding);
  } else if (!StoredOverrideBindingEqualsNoCase(it->second, binding)) {
    it->second = binding;
  }
}

void CInputControl::ApplyMouselookOverrideBindings() {
  auto* profiles = binding_runtime_->profiles;
  if (profiles == nullptr) {
    return;
  }

  for (const auto& [key, command] : binding_runtime_->staged_bindings) {
    std::optional<BindingCommand> override_command;
    if (!command.empty()) {
      override_command.emplace(command);
    }
    profiles->SetOverrideBinding(
        binding_runtime_->override_owner, true,
        BindingChord(key),
        std::move(override_command));
  }
}

void CInputControl::ClearMouselookOverrideBindings() {
  binding_runtime_->staged_bindings.clear();
  DeactivateMouselookOverrideBindings();
}

void CInputControl::DeactivateMouselookOverrideBindings() {
  if (binding_runtime_->profiles != nullptr) {
    binding_runtime_->profiles->ClearOverrideBindings(
        binding_runtime_->override_owner);
  }
}

void CInputControl::Reset() {
  SetJoystickEnabled(false);
  reserved0_ = 0;
  control_flags_ = 0;
  accu_abs_dx_ = 0.0f;
  accu_abs_dy_ = 0.0f;
  last_mouse_delta_frame_tick_ = 0;
  last_click_timestamp_ = 0;
  last_mouse_button_type_ = 0;
  for (auto& r : reserved2_) r = 0;
  yaw_pushed_ = 0;
  saved_yaw_ = 0.0f;
  pitch_lock_active_ = 0;
  pitch_lock_value_ = 0.0f;
  both_buttons_active_ = 0;
  cursor_visibility_flags_ = 0;
  cursor_visibility_hold_count_ = 0;
  joystick_free_look_flags_ = 0;
  joystick_x_rate_ = 0.0f;
  joystick_y_rate_ = 0.0f;
  joystick_obj_ = nullptr;
}

bool CInputControl::IsIdle() const {
  return AreControlFlagsIdle(control_flags_);
}

bool CInputControl::HasDoubleClickElapsed(std::uint32_t now_ms) const {
  const int elapsed = static_cast<int>(now_ms - last_click_timestamp_);

  if (elapsed >= 800) return true;

  if (accu_abs_dx_ >= 8.0f || accu_abs_dy_ >= 8.0f) {
    return elapsed >= 200;
  }

  return false;
}

bool CInputControl::HasRecentMouseDeltaAfterDoubleClickElapsed(
    std::uint32_t now_ms) const {
  if (!HasDoubleClickElapsed(now_ms)) {
    return false;
  }

  return (CurrentInputFrameTick() - last_mouse_delta_frame_tick_) < 2u;
}

bool CInputControl::IsPlayerAliveAndFree(const PlayerMovementGateState& state) {
  if (state.health <= 0) return false;
  if (!state.vehicle_control_allows_free_movement) return false;
  if (state.has_knockdown_animation) return false;
  if (state.is_active_player && state.active_player_turn_locked) return false;
  if (state.is_in_vehicle_transition) return false;
  return true;
}

bool CInputControl::CanPlayerTurn(const PlayerMovementGateState& state) {
  if (!IsPlayerAliveAndFree(state)) return false;
  if ((state.unit_flags & kUnitFlagStunned) != 0) return false;
  if (state.has_non_static_vehicle_seat) return false;
  return true;
}

bool CInputControl::CanUnitWalk(const PlayerMovementGateState& state) {
  if (state.health <= 0) return false;
  if (state.has_movement_restriction_flags) return false;
  if (state.is_power_type_locked) return false;
  return true;
}

bool CInputControl::CanPlayerMove(const PlayerMovementGateState& state) {
  if (!IsPlayerAliveAndFree(state)) return false;
  if (state.health <= 0) return false;
  if (state.has_movement_restriction_flags) return false;
  if (state.is_power_type_locked) return false;
  if (state.is_on_vehicle) return false;
  return true;
}

bool CInputControl::CanMouseSteerUnit(const PlayerMovementGateState& state) const {
  if (!CanPlayerTurn(state)) return false;
  if ((control_flags_ & kMaskMoveAndSteer) == 0) return false;
  if (state.mouse_steering_blocked) return false;
  return true;
}

bool CInputControl::CanAutoRun(
    const ProcessMovementRuntimeState& runtime_state) const {
  const bool commentator_override = runtime_state.commentator_controls_enabled;
  const bool mover_passes_auto_run_gate =
      runtime_state.gate_state.health > 0 &&
      runtime_state.gate_state.vehicle_control_allows_free_movement &&
      (runtime_state.gate_state.unit_flags & kUnitFlagStunned) == 0 &&
      !runtime_state.gate_state.is_in_vehicle_transition &&
      !runtime_state.gate_state.mouse_steering_blocked &&
      runtime_state.camera_bound_to_mover;

  if (!commentator_override && !mover_passes_auto_run_gate) {
    return false;
  }

  if (!runtime_state.camera_bound_alpha_visible) {
    return false;
  }

  return (control_flags_ & kMaskMoveAndSteer) != 0;
}

bool CInputControl::CheckPlayerCanMove(
    const PlayerCanMoveRuntimeState& runtime_state) {
  if (runtime_state.commentator_controls_enabled) {
    return true;
  }

  const auto& gate_state = runtime_state.gate_state;
  if (gate_state.health <= 0) {
    return false;
  }
  if (!gate_state.vehicle_control_allows_free_movement) {
    return false;
  }
  if (gate_state.is_in_vehicle_transition) {
    return false;
  }
  if ((gate_state.unit_flags & kUnitFlagStunned) != 0) {
    return false;
  }
  if (gate_state.mouse_steering_blocked) {
    return false;
  }

  const bool always_allow_pitching =
      (runtime_state.movement_flags2 & kMoveFlag2AlwaysAllowPitching) != 0;
  const bool swimming_or_flying =
      (runtime_state.movement_flags &
       (kMoveFlagSwimming | kMoveFlagFlying)) != 0;
  if (always_allow_pitching && !swimming_or_flying &&
      !runtime_state.vehicle_allows_ground_pitch) {
    return false;
  }

  return runtime_state.camera_bound_to_mover;
}

bool CInputControl::SetControlFlag(std::uint32_t flag, std::uint32_t timestamp) {
  if ((flag & control_flags_) != 0) return false;

  const std::uint32_t old_flags = control_flags_;
  const bool was_idle = AreCollisionDirectionalControlsIdle(old_flags);
  const bool had_any_mouse_mode = (old_flags & kMaskAllMouseModes) != 0;
  const bool had_any_mouse_button = (old_flags & kMaskMouseButtons) != 0;
  const bool had_both_mouse_buttons = AreBothMouseButtonsHeld(old_flags);
  const bool had_camera_or_vehicle =
      (old_flags & kMaskCameraOrVehicle) != 0;
  const bool had_move_and_steer = (old_flags & kMaskMoveAndSteer) != 0;
  const bool had_move_and_steer_turn = IsMoveAndSteerTurnActive(old_flags);

  control_flags_ = old_flags | flag;
  ControlFlagSideEffects side_effects;
  side_effects.input_control = this;

  if (!had_any_mouse_mode && (control_flags_ & kMaskAllMouseModes) != 0) {
    ResetClickState(timestamp);
    side_effects.check_auto_freelook = true;
  }

  if (!had_any_mouse_button && (control_flags_ & kMaskMouseButtons) != 0) {

    ApplyCursorVisibility(false);
    side_effects.mouse_button_set = true;
  }

  if (!had_both_mouse_buttons && AreBothMouseButtonsHeld(control_flags_)) {
    if (QueryCanMouseSteerUnitForControlFlagsImpl(*this)) {
      both_buttons_active_ = 1;
      side_effects.update_pitch_clamp = true;
    }
  } else if (!AreBothMouseButtonsHeld(control_flags_)) {
    both_buttons_active_ = 0;
  }

  if ((flag & kMaskClickToMove) != 0) {
    side_effects.click_to_move_flags_changed = true;
    side_effects.click_to_move_flags = control_flags_ & kMaskClickToMove;
  }

  if ((flag & kMaskActionMovement) != 0 ||
      (!had_both_mouse_buttons && AreBothMouseButtonsHeld(control_flags_)) ||
      (!had_move_and_steer_turn && IsMoveAndSteerTurnActive(control_flags_))) {
    side_effects.update_bobbing = true;
    side_effects.bobbing_enabled = ShouldEnableMovementBobbing(control_flags_);
  }

  if (yaw_pushed_ != 0 && (flag & kMaskTurn) != 0) {
    side_effects.pop_yaw_offset = ClearYawPushState();
  }

  if ((flag & kMaskDirectionalAll) != 0 ||
      (!had_camera_or_vehicle &&
       (control_flags_ & kMaskCameraOrVehicle) != 0) ||
      (!had_move_and_steer &&
       (control_flags_ & kMaskMoveAndSteer) != 0) ||
      (!had_move_and_steer_turn &&
       IsMoveAndSteerTurnActive(control_flags_))) {
    side_effects.evaluate_camera_smoothing = true;
    side_effects.camera_smoothing_stop_event =
        !was_idle && AreCollisionDirectionalControlsIdle(control_flags_);
  }

  side_effects.control_flags = control_flags_;
  side_effects.timestamp = timestamp;
  EmitControlFlagSideEffects(side_effects);

  if ((flag & kMaskForwardBackward) != 0) {
    control_flags_ &= ~kCtrlAutoRun;
  }

  if (!had_both_mouse_buttons && AreBothMouseButtonsHeld(control_flags_)) {
    control_flags_ &= ~kCtrlAutoRun;
  }

  if ((flag & kMaskMouseButtons) != 0 &&
      (control_flags_ & kMaskMouseButtons) != flag) {
    last_mouse_button_type_ = 0;
  }

  return true;
}

bool CInputControl::ClearControlFlag(std::uint32_t flag, std::uint32_t timestamp,
                                      std::uint32_t lock_camera) {
  if ((flag & control_flags_) == 0) return false;

  const std::uint32_t old_flags = control_flags_;
  const bool was_idle = AreCollisionDirectionalControlsIdle(old_flags);
  const std::uint32_t old_mouse_modes = old_flags & kMaskAllMouseModes;
  const std::uint32_t old_buttons = old_flags & kMaskMouseButtons;
  const bool had_both_mouse_buttons = AreBothMouseButtonsHeld(old_flags);
  const std::uint32_t old_camera_or_vehicle = old_flags & kMaskCameraOrVehicle;
  const std::uint32_t old_move_and_steer = old_flags & kMaskMoveAndSteer;
  const bool had_move_and_steer_turn = IsMoveAndSteerTurnActive(old_flags);

  control_flags_ = old_flags & ~flag;
  ControlFlagSideEffects side_effects;
  side_effects.input_control = this;

  if (old_mouse_modes != 0 && (control_flags_ & kMaskAllMouseModes) == 0) {
    side_effects.clear_mouse_modes = true;
    side_effects.lock_camera_on_clear = lock_camera != 0;
    if ((control_flags_ & kMaskTurn) != 0 && ClearYawPushState()) {
      side_effects.pop_yaw_offset = true;
    }
  }

  if (old_buttons != 0 && (control_flags_ & kMaskMouseButtons) == 0) {

    if ((cursor_visibility_flags_ & kCursorVisibilityPresented) != 0) {
      ApplyCursorVisibility(true);
    }
    side_effects.mouse_button_clear = true;
    if (!HasDoubleClickElapsed(timestamp)) {
      if (last_mouse_button_type_ == 1u) {
        side_effects.dispatch_world_click_type = 1u;
      } else if (last_mouse_button_type_ == 2u) {
        side_effects.dispatch_world_click_type = 4u;
      }
    }
    last_mouse_button_type_ = 0;
  }

  if ((flag & kMaskClickToMove) != 0) {
    side_effects.click_to_move_flags_changed = true;
    side_effects.click_to_move_flags = control_flags_ & kMaskClickToMove;
  }

  if ((flag & kMaskActionMovement) != 0 ||
      (had_both_mouse_buttons && !AreBothMouseButtonsHeld(control_flags_)) ||
      (had_move_and_steer_turn && !IsMoveAndSteerTurnActive(control_flags_))) {
    side_effects.update_bobbing = true;
    side_effects.bobbing_enabled = ShouldEnableMovementBobbing(control_flags_);
  }

  if ((flag & kMaskDirectionalAll) != 0 ||
      (old_camera_or_vehicle != 0 &&
       (control_flags_ & kMaskCameraOrVehicle) == 0) ||
      (old_move_and_steer != 0 &&
       (control_flags_ & kMaskMoveAndSteer) == 0) ||
      (had_move_and_steer_turn && !IsMoveAndSteerTurnActive(control_flags_))) {
    side_effects.evaluate_camera_smoothing = true;
    side_effects.camera_smoothing_stop_event =
        !was_idle && AreCollisionDirectionalControlsIdle(control_flags_);
  }

  side_effects.control_flags = control_flags_;
  side_effects.timestamp = timestamp;
  EmitControlFlagSideEffects(side_effects);
  return true;
}

void CInputControl::ProcessMovementNow(const std::uint32_t timestamp,
                                       const bool movement_active) {
  ProcessMovement(*this, timestamp, movement_active);
}

void CInputControl::ReapplyDirectionalControlState(
    const std::uint32_t timestamp) {
  const std::uint32_t flags_to_reapply =
      control_flags_ & kMaskDirectionalReapply;

  if (ClearControlFlag(flags_to_reapply, timestamp, 0)) {
    ProcessMovement(*this, timestamp, true);
  }

  ProcessMovement(*this, timestamp, true);

  if (SetControlFlag(flags_to_reapply, timestamp)) {

    control_flags_ |= flags_to_reapply & kCtrlAutoRun;

    if (ShouldNotifyMovementActivatedForFlagSet(flags_to_reapply,
                                                control_flags_)) {
      NotifyMovementActivated();
    }

    ProcessMovement(*this, timestamp, true);
  }
}

void CInputControl::ResetClickState(std::uint32_t timestamp) {
  accu_abs_dx_ = 0.0f;
  accu_abs_dy_ = 0.0f;
  last_click_timestamp_ = timestamp;
}

void CInputControl::AccumulateClickDelta(float dx, float dy) {
  accu_abs_dx_ += std::fabs(dx);
  accu_abs_dy_ += std::fabs(dy);
}

bool CInputControl::ClearYawPushState() {
  if (yaw_pushed_ == 0) {
    return false;
  }

  yaw_pushed_ = 0;
  return true;
}

bool CInputControl::PopYawIfNeeded() {
  if (!ClearYawPushState()) {
    return false;
  }

  ControlFlagSideEffects side_effects;
  side_effects.input_control = this;
  side_effects.pop_yaw_offset = true;
  EmitControlFlagSideEffects(side_effects);
  return true;
}

int CInputControl::ComputeNetForward() const {
  const std::uint32_t f = control_flags_;
  int net = 0;
  if (f & kCtrlAutoRun)     ++net;
  if (f & kCtrlMoveForward) ++net;
  if ((f & kCtrlTurnOrAction) && (f & kCtrlCameraOrSelect)) ++net;
  if (f & kCtrlMoveBackward) --net;
  return net;
}

int CInputControl::ComputeNetStrafe() const {
  const std::uint32_t f = control_flags_;
  int net = 0;
  if (f & kCtrlStrafeLeft)  ++net;
  if ((f & kMaskMoveAndSteer) != 0 && (f & kCtrlTurnLeft) != 0) ++net;
  if (f & kCtrlStrafeRight) --net;
  if ((f & kMaskMoveAndSteer) != 0 && (f & kCtrlTurnRight) != 0) --net;
  return net;
}

int CInputControl::ComputeNetTurn(
    const bool allow_turn_in_move_and_steer) const {
  const std::uint32_t f = control_flags_;
  if ((f & kMaskMoveAndSteer) != 0 && !allow_turn_in_move_and_steer) {
    return 0;
  }

  int net = 0;
  if (f & kCtrlTurnLeft)  ++net;
  if (f & kCtrlTurnRight) --net;
  return net;
}

int CInputControl::ComputeNetPitch() const {
  const std::uint32_t f = control_flags_;
  if ((f & kMaskMoveAndSteer) != 0) return 0;
  int net = 0;
  if (f & kCtrlPitchUp)   ++net;
  if (f & kCtrlPitchDown) --net;
  return net;
}

int CInputControl::ComputeNetVertical() const {
  const std::uint32_t f = control_flags_;
  int net = 0;
  if (f & kCtrlAscend)  ++net;
  if (f & kCtrlDescend) --net;
  return net;
}

VerticalMovementDecision CInputControl::ResolveVerticalMovementDecision(
    const std::uint32_t movement_flags,
    const std::uint16_t movement_flags2) const {
  VerticalMovementDecision decision;
  decision.sent_state_after = IsVerticalSent();

  if ((movement_flags & (kMoveFlagSwimming | kMoveFlagFlying)) == 0) {
    decision.sent_state_after = false;
    return decision;
  }

  const int net_vertical = ComputeNetVertical();
  const bool sent = IsVerticalSent();
  const bool full_speed_pitching =
      (movement_flags2 & kMoveFlag2FullSpeedPitching) != 0;

  if (full_speed_pitching) {
    if (net_vertical > 0) {
      if (!sent || (movement_flags & kMoveFlagPitchDown) != 0) {
        decision.command = VerticalMovementCommand::kStartPitchUp;
        decision.sent_state_after = true;
      }
      return decision;
    }

    if (net_vertical < 0) {
      if (!sent || (movement_flags & kMoveFlagPitchUp) != 0) {
        decision.command = VerticalMovementCommand::kStartPitchDown;
        decision.sent_state_after = true;
      }
      return decision;
    }

    if (sent) {
      decision.command = VerticalMovementCommand::kStopPitch;
      decision.sent_state_after = false;
      decision.fire_pitch_event = true;
    }
    return decision;
  }

  if (net_vertical != 0) {
    decision.clear_afk = true;
  }

  if (net_vertical > 0) {
    if (!sent || (movement_flags & kMoveFlagDescending) != 0) {
      decision.command = VerticalMovementCommand::kStartAscend;
      decision.sent_state_after = true;
    }
    return decision;
  }

  if (net_vertical < 0) {
    if (!sent || (movement_flags & kMoveFlagAscending) != 0) {
      decision.command = VerticalMovementCommand::kStartDescend;
      decision.sent_state_after = true;
    }
    return decision;
  }

  if (sent) {
    decision.command = VerticalMovementCommand::kStopVertical;
    decision.sent_state_after = false;
  }
  return decision;
}

void CInputControl::MarkForwardSent(bool sent) {
  if (sent)
    control_flags_ |= kCtrlForwardSent;
  else
    control_flags_ &= ~kCtrlForwardSent;
}

void CInputControl::MarkStrafeSent(bool sent) {
  if (sent)
    control_flags_ |= kCtrlStrafeSent;
  else
    control_flags_ &= ~kCtrlStrafeSent;
}

void CInputControl::MarkTurnSent(bool sent) {
  if (sent)
    control_flags_ |= kCtrlTurnSent;
  else
    control_flags_ &= ~kCtrlTurnSent;
}

void CInputControl::MarkPitchSent(bool sent) {
  if (sent)
    control_flags_ |= kCtrlPitchSent;
  else
    control_flags_ &= ~kCtrlPitchSent;
}

void CInputControl::MarkVerticalSent(bool sent) {
  if (sent)
    control_flags_ |= kCtrlVerticalSent;
  else
    control_flags_ &= ~kCtrlVerticalSent;
}

void CInputControl::ToggleAutoRun() {
  control_flags_ ^= kCtrlAutoRun;
}

void CInputControl::HandleMouseDelta(float dx, float dy) {
  if (control_flags_ == 0) {
    return;
  }

  MouseDeltaRuntimeState state;
  state.frame_tick = CurrentInputFrameTick();
  state.timestamp = CurrentMouseDeltaTimestamp();
  if (g_mouse_delta_runtime_state_query) {
    g_mouse_delta_runtime_state_query(state);
  }

  if (state.apply_distance_sensitivity) {
    Camera_ApplyMouseSensitivity(&dx, &dy);
  }

  last_mouse_delta_frame_tick_ = state.frame_tick;
  accu_abs_dx_ += std::fabs(dx);
  accu_abs_dy_ += std::fabs(dy);

  Camera_ConvertMouseDeltaToRadians(&dx, &dy);

  if (g_camera_mouse_input_callback) {
    const bool route_pitch_into_vehicle_aim =
        state.can_mouse_steer && state.route_pitch_into_vehicle_aim;
    if (route_pitch_into_vehicle_aim) {
      float pitch_out = 0.0f;
      g_camera_mouse_input_callback(dx, dy, &pitch_out);
      if (state.player_can_move && g_vehicle_aim_request_callback) {
        const float pitch_base =
            IsPitchLockActive() ? pitch_lock_value_ : state.vehicle_aim_pitch_base;
        g_vehicle_aim_request_callback(state.timestamp, pitch_base - pitch_out);
        control_flags_ &= ~kCtrlPitchSent;
      }
    } else {
      g_camera_mouse_input_callback(dx, dy, nullptr);
    }
  }

  if (state.can_mouse_steer && g_camera_pitch_clamp_callback) {
    g_camera_pitch_clamp_callback();
  }
}

void CInputControl::ResetControlFlagsAndJoystick(std::uint32_t timestamp) {
  (void)ClearControlFlag(0xFFFFFFFFu, timestamp, 0);
  reserved0_ = timestamp;
  joystick_x_rate_ = 0.0f;
  joystick_y_rate_ = 0.0f;
}

namespace {

struct JoystickCameraScreenState {
  float width{800.0f};
  float height{600.0f};
  bool initialized{false};
};

JoystickCameraScreenState g_joystick_camera_screen;

JoystickCameraRuntimeStateQuery g_joystick_camera_runtime_state_query = nullptr;
PitchEventCallback g_pitch_event_callback = nullptr;
float g_vehicle_aim_normalized_power = 0.0f;

}

bool CInputControl::RefreshCursorVisibility(bool force) {
  const std::uint32_t flags = cursor_visibility_flags_;

  std::uint32_t desired = 0;
  if ((flags & kCursorVisibilityAppInactiveForce) != 0) {
    desired = 1;
  } else if ((flags & kCursorVisibilityCinematicHide) == 0) {
    if (!IsCinematicJoystickEnabled() &&
        (flags & kCursorVisibilitySpellTargetingForce) == 0 &&
        cursor_visibility_hold_count_ == 0) {
      desired = flags & kCursorVisibilityRequestMask;
    } else {
      desired = 1;
    }
  }

  const bool enabled = desired != 0;
  const bool was_enabled = (flags & kCursorVisibilityPresented) != 0;
  if (force || enabled != was_enabled) {
    if (enabled) {
      cursor_visibility_flags_ = flags | kCursorVisibilityPresented;
      if ((control_flags_ & kMaskMouseButtons) == 0) {
        ApplyCursorVisibility(true);
      }
    } else {
      cursor_visibility_flags_ = flags & ~kCursorVisibilityPresented;
      ApplyCursorVisibility(false);
    }
  }

  const bool joystick_enabled =
      input::IsJoystickMouseConfigEnabled() &&
      !IsCinematicJoystickEnabled() &&
      (joystick_free_look_flags_ & 0x2u) != 0 &&
      (cursor_visibility_flags_ & kCursorVisibilityPresented) != 0;
  const bool joystick_was_enabled =
      (joystick_free_look_flags_ & 0x1u) != 0;
  if (joystick_enabled != joystick_was_enabled) {
    if (joystick_enabled) {
      joystick_free_look_flags_ |= 0x1u;
    } else {
      joystick_free_look_flags_ &= ~0x1u;
    }

    ApplyJoystickFreeLookMode(joystick_enabled);
  }

  return enabled;
}

int CInputControl::ProcessGameUiMovementState(GameUiMovementGateContext& context,
                                              std::uint32_t timestamp) {
  if (context.gate == 0) {
    control_flags_ &= 0xFFFFF00Fu;
    ProcessMovement(*this, timestamp, true);
  }

  if (context.gate != 0) {
    cursor_visibility_flags_ &= ~kCursorVisibilityAppInactiveForce;
  } else {
    cursor_visibility_flags_ |= kCursorVisibilityAppInactiveForce;
  }

  (void)RefreshCursorVisibility(false);
  return 1;
}

namespace {
CInputControl* g_input_control_singleton = nullptr;
ProtectedActionCheck g_protected_action_check = nullptr;
OnMovementActivated g_on_movement_activated = nullptr;
SaveCursorPosCallback g_save_cursor_pos = nullptr;
ActiveWorldCameraDistanceQuery g_active_world_camera_distance_query = nullptr;
MovementRatesRuntimeStateQuery g_movement_rates_runtime_state_query = nullptr;
ClearLocalAfkForMovementCallback g_clear_local_afk_for_movement = nullptr;
ProcessMovementRuntimeStateQuery g_process_movement_runtime_state_query = nullptr;
ProcessMovementCallback g_process_movement = nullptr;
CursorVisibilityCallback g_cursor_visibility_callback = nullptr;
CinematicJoystickCVarQuery g_cinematic_joystick_cvar_query = nullptr;
JoystickFreeLookModeCallback g_joystick_free_look_mode = nullptr;
InputTickCountProvider g_input_tick_count_provider = nullptr;
InputFrameTickProvider g_input_frame_tick_provider = nullptr;
MouseDeltaRuntimeStateQuery g_mouse_delta_runtime_state_query = nullptr;
CameraMouseInputCallback g_camera_mouse_input_callback = nullptr;
CameraPitchClampCallback g_camera_pitch_clamp_callback = nullptr;
ControlFlagSideEffectCallback g_control_flag_side_effect_callback = nullptr;
VehicleAimRequestCallback g_vehicle_aim_request_callback = nullptr;

constexpr double kCameraMouseSensitivityDistanceScale = 0.81851113;
constexpr double kCameraMouseSensitivityMinimumRatio = 0.05;
constexpr double kCameraMouseSensitivityRangeScale = 0.95;

double ComputeCameraMouseSensitivityScale(double camera_distance) {
  const double scaled_distance =
      camera_distance * kCameraMouseSensitivityDistanceScale;

  double clamped_distance = 1.0;
  if (scaled_distance < 1.0) {
    clamped_distance = kCameraMouseSensitivityMinimumRatio;
    if (scaled_distance > kCameraMouseSensitivityMinimumRatio) {
      clamped_distance = scaled_distance;
    }
  }

  return kCameraMouseSensitivityMinimumRatio +
         clamped_distance * kCameraMouseSensitivityRangeScale;
}

bool QueryCanMouseSteerUnitForControlFlagsImpl(const CInputControl& control) {
  if (g_process_movement_runtime_state_query == nullptr) {
    return false;
  }

  ProcessMovementRuntimeState runtime_state;
  if (!g_process_movement_runtime_state_query(runtime_state)) {
    return false;
  }

  return control.CanMouseSteerUnit(runtime_state.gate_state);
}

CameraControlGateResult QueryCanPlayerMoveForCameraPitchImpl() {
  if (g_process_movement_runtime_state_query == nullptr) {
    return {};
  }

  ProcessMovementRuntimeState process_state;
  if (!g_process_movement_runtime_state_query(process_state)) {
    return {};
  }

  PlayerCanMoveRuntimeState player_state;
  player_state.gate_state = process_state.gate_state;
  player_state.movement_flags = process_state.movement_flags;
  player_state.movement_flags2 = process_state.movement_flags2;
  player_state.commentator_controls_enabled =
      process_state.commentator_controls_enabled;
  player_state.camera_bound_to_mover = process_state.camera_bound_to_mover;
  player_state.vehicle_allows_ground_pitch =
      process_state.vehicle_allows_ground_pitch;
  return {
      .allowed = CInputControl::CheckPlayerCanMove(player_state),
      .commentator_controls = process_state.commentator_controls_enabled,
  };
}

CameraControlGateResult QueryCanApplyCameraFacingImpl(
    const CInputControl& control) {
  if (g_process_movement_runtime_state_query == nullptr) {
    return {};
  }

  ProcessMovementRuntimeState state;
  if (!g_process_movement_runtime_state_query(state)) {
    return {};
  }

  const auto& gate = state.gate_state;
  const bool mover_allowed =
      state.commentator_controls_enabled ||
      (gate.health > 0 && gate.vehicle_control_allows_free_movement &&
       !gate.is_in_vehicle_transition &&
       (gate.unit_flags & kUnitFlagStunned) == 0u &&
       !gate.mouse_steering_blocked && state.camera_bound_to_mover);
  if (!mover_allowed ||
      (control.GetControlFlags() & kMaskMoveAndSteer) == 0u) {
    return {};
  }

  return {
      .allowed = true,
      .commentator_controls = state.commentator_controls_enabled,
  };
}

float ClampInputPitchForVehicle(const CGUnit_C& mover, const float pitch) {
  const auto* const vehicle_entry = mover.Vehicle().GetVehicleEntry();
  if (vehicle_entry == nullptr) {
    return pitch;
  }

  constexpr std::uint32_t kVehicleCustomPitchBoundsFlag = 0x40u;
  constexpr float kDefaultMinPitch = -1.5707964f;
  constexpr float kDefaultMaxPitch = 1.5707964f;
  float min_pitch = kDefaultMinPitch;
  float max_pitch = kDefaultMaxPitch;
  if ((vehicle_entry->flags & kVehicleCustomPitchBoundsFlag) != 0u) {
    min_pitch = vehicle_entry->pitch_min;
    max_pitch = vehicle_entry->pitch_max;
  }

  if (!(min_pitch <= pitch)) {
    return min_pitch;
  }
  if (max_pitch <= pitch) {
    return max_pitch;
  }
  return pitch;
}

void DispatchVehicleAngleUpdateIfVisible(const CGUnit_C& mover,
                                         const float pitch) {
  const auto* const vehicle_entry = mover.Vehicle().GetVehicleEntry();
  constexpr std::uint32_t kVehicleAngleDisplayFlag = 0x400u;
  if (vehicle_entry == nullptr ||
      (vehicle_entry->flags & kVehicleAngleDisplayFlag) == 0u) {
    return;
  }

  constexpr std::uint32_t kVehicleCustomPitchBoundsFlag = 0x40u;
  float min_pitch = -1.5707964f;
  float max_pitch = 1.5707964f;
  if ((vehicle_entry->flags & kVehicleCustomPitchBoundsFlag) != 0u) {
    min_pitch = vehicle_entry->pitch_min;
    max_pitch = vehicle_entry->pitch_max;
  }
  openwow::ui::game::ScriptEventDispatch::Get().FireVehicleAngleUpdate(
      pitch, min_pitch, max_pitch);
}

void EmitControlFlagSideEffects(const ControlFlagSideEffects& effects) {
  if (g_control_flag_side_effect_callback != nullptr) {
    g_control_flag_side_effect_callback(effects);
  }
}
}

bool QueryCanMouseSteerUnitForControlFlags(const CInputControl& control) {
  return QueryCanMouseSteerUnitForControlFlagsImpl(control);
}

void InputControl_RefreshViewportAspect() {

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  float aspect = 4.0f / 3.0f;
  if (cvars.GetCVarInt("widescreen") != 0) {
    const std::string resolution = cvars.GetCVar("gxResolution");
    int parsed_width = 4;
    int parsed_height = 3;
    char separator = 0;
    if (std::sscanf(resolution.c_str(), "%d%c%d", &parsed_width, &separator,
                    &parsed_height) == 3 &&
        parsed_width > 0 && parsed_height > 0) {
      aspect = static_cast<float>(parsed_width) /
               static_cast<float>(parsed_height);
    }
  }
  const float inv_len = 1.0f / std::sqrt(aspect * aspect + 1.0f);
  g_camera_aspect_normalize_y = inv_len;
  g_camera_aspect_normalize_x = inv_len * aspect;
}

void InputControl_UpdatePitchEvent(const float pitch) {
  if (g_pitch_event_callback != nullptr) {
    g_pitch_event_callback(pitch);
  }
}

void InputControl_UpdatePitchEventForUnit(const CGUnit_C& unit,
                                          const float pitch) {
  DispatchVehicleAngleUpdateIfVisible(unit, pitch);
  InputControl_UpdatePitchEvent(pitch);
}

float InputControl_GetVehicleAimNormalizedPower() noexcept {
  return g_vehicle_aim_normalized_power;
}

void InputControl_SetVehicleAimNormalizedPower(
    const float normalized_power) noexcept {
  g_vehicle_aim_normalized_power =
      std::clamp(normalized_power, 0.0f, 1.0f);
}

void CInputControl::HandleJoystickCameraInput(float dt) {
  if (g_joystick_camera_runtime_state_query == nullptr) {
    return;
  }

  JoystickCameraRuntimeState state;
  if (!g_joystick_camera_runtime_state_query(*this, state)) {
    return;
  }

  if (!state.has_active_mover) {
    return;
  }

  if (state.has_joystick_device &&
      (control_flags_ & kMaskJoystickCameraCtrl) != 0) {
    if (state.has_active_camera) {

      if (!g_joystick_camera_screen.initialized) {
        g_joystick_camera_screen.initialized = true;
        g_joystick_camera_screen.width = 800.0f;
        g_joystick_camera_screen.height = 600.0f;
      }

      float dx = joystick_x_rate_ * g_joystick_camera_screen.width * dt;
      float dy = joystick_y_rate_ * g_joystick_camera_screen.height * dt;

      Camera_ConvertMouseDeltaToRadians(&dx, &dy);

      if (g_camera_mouse_input_callback) {
        g_camera_mouse_input_callback(dx, dy, nullptr);
      }

      if (QueryCanMouseSteerUnitForControlFlagsImpl(*this) &&
          g_camera_pitch_clamp_callback) {
        g_camera_pitch_clamp_callback();
      }
    }
  }

  if (state.mover_has_pitch_flags) {
    if (state.active_mover != nullptr) {
      InputControl_UpdatePitchEventForUnit(*state.active_mover,
                                           state.mover_pitch);
    } else {
      InputControl_UpdatePitchEvent(state.mover_pitch);
    }
  }
}

void SetInputControlSingleton(CInputControl* ptr) {
  g_input_control_singleton = ptr;
}

CInputControl* GetInputControlSingleton() {
  return g_input_control_singleton;
}

void InputControl_SetStoredMouselookOverrideBinding(std::string_view key,
                                                    const char* binding) {
  auto* input = GetInputControlSingleton();
  if (input == nullptr) {
    return;
  }

  InputControl_SetStoredMouselookOverrideBindingForControl(*input, key, binding);
}

void InputControl_SetStoredMouselookOverrideBindingForControl(
    CInputControl& input,
    std::string_view key,
    const char* binding) {
  input.SetMouselookOverrideBinding(key, binding);
}

void InputControl_ApplyStoredMouselookOverrideBindingsForControl(
    CInputControl& input) {
  input.ApplyMouselookOverrideBindings();
}

void InputControl_ResetStoredMouselookOverrideBindings() {
  if (auto* input = GetInputControlSingleton(); input != nullptr) {
    input->ClearMouselookOverrideBindings();
  }
}

void ShutdownInputControlRuntime() {
  if (auto* input = GetInputControlSingleton(); input != nullptr) {
    input->SetJoystickEnabled(false);
    input->ClearMouselookOverrideBindings();
  }

  SetInputControlSingleton(nullptr);
}

void SetCinematicInputBlocked(bool active) {
  auto* input = GetInputControlSingleton();
  if (input == nullptr) {
    return;
  }

  std::uint32_t flags = input->GetCursorVisibilityFlags();
  if (active) {
    flags |= kCursorVisibilityCinematicHide;
  } else {
    flags &= ~kCursorVisibilityCinematicHide;
  }

  input->SetCursorVisibilityFlagsRaw(flags);
  (void)input->RefreshCursorVisibility(false);
}

void SetSpellTargetingCursorForce(bool active) {
  auto* input = GetInputControlSingleton();
  if (input == nullptr) {
    return;
  }

  std::uint32_t flags = input->GetCursorVisibilityFlags();
  if (active) {
    flags |= kCursorVisibilitySpellTargetingForce;
  } else {
    flags &= ~kCursorVisibilitySpellTargetingForce;
  }

  input->SetCursorVisibilityFlagsRaw(flags);
  (void)input->RefreshCursorVisibility(false);
}

void InputControl_ResetCursorVisibilityForUiInit() {
  auto* input = GetInputControlSingleton();
  if (input == nullptr) {
    return;
  }

  input->SetCursorVisibilityFlagsRaw(kCursorVisibilityUiInitFlags);
  input->SetCursorVisibilityHoldCount(0);
  (void)input->RefreshCursorVisibility(true);
}

void ResetInputAfterCinematicTransition() {
  auto* input = GetInputControlSingleton();
  if (input == nullptr) {
    return;
  }

  const std::uint32_t timestamp =
      g_input_tick_count_provider ? CurrentInputTickCount()
                                  : openwow::core::GameClock::GetTickCount32();
  input->ResetControlFlagsAndJoystick(timestamp);
}

void SetProtectedActionCheck(ProtectedActionCheck fn) {
  g_protected_action_check = fn;
}

ProtectedActionCheck GetProtectedActionCheck() {
  return g_protected_action_check;
}

void SetOnMovementActivatedCallback(OnMovementActivated fn) {
  g_on_movement_activated = fn;
}

void SetSaveCursorPosCallback(SaveCursorPosCallback fn) {
  g_save_cursor_pos = fn;
}

void SetActiveWorldCameraDistanceQuery(ActiveWorldCameraDistanceQuery fn) {
  g_active_world_camera_distance_query = fn;
}

void SetMovementRatesRuntimeStateQuery(MovementRatesRuntimeStateQuery fn) {
  g_movement_rates_runtime_state_query = fn;
}

void SetClearLocalAfkForMovementCallback(ClearLocalAfkForMovementCallback fn) {
  g_clear_local_afk_for_movement = fn;
}

void SetProcessMovementRuntimeStateQuery(ProcessMovementRuntimeStateQuery fn) {
  g_process_movement_runtime_state_query = fn;
}

void SetProcessMovementCallback(ProcessMovementCallback fn) {
  g_process_movement = fn;
}

void SetCursorVisibilityCallback(CursorVisibilityCallback fn) {
  g_cursor_visibility_callback = fn;
}

void SetCinematicJoystickCVarQuery(CinematicJoystickCVarQuery fn) {
  g_cinematic_joystick_cvar_query = fn;
}

void SetJoystickFreeLookModeCallback(JoystickFreeLookModeCallback fn) {
  g_joystick_free_look_mode = fn;
}

void SetInputTickCountProvider(InputTickCountProvider fn) {
  g_input_tick_count_provider = fn;
}

void SetInputFrameTickProvider(InputFrameTickProvider fn) {
  g_input_frame_tick_provider = fn;
}

void SetMouseDeltaRuntimeStateQuery(MouseDeltaRuntimeStateQuery fn) {
  g_mouse_delta_runtime_state_query = fn;
}

void SetCameraMouseInputCallback(CameraMouseInputCallback fn) {
  g_camera_mouse_input_callback = fn;
}

void SetCameraPitchClampCallback(CameraPitchClampCallback fn) {
  g_camera_pitch_clamp_callback = fn;
}

void SetJoystickCameraRuntimeStateQuery(JoystickCameraRuntimeStateQuery fn) {
  g_joystick_camera_runtime_state_query = fn;
}

void SetPitchEventCallback(PitchEventCallback fn) {
  g_pitch_event_callback = fn;
}

void ResetJoystickCameraScreenState() {
  g_joystick_camera_screen = JoystickCameraScreenState{};
}

void SetControlFlagSideEffectCallback(ControlFlagSideEffectCallback fn) {
  g_control_flag_side_effect_callback = fn;
}

void SetVehicleAimRequestCallback(VehicleAimRequestCallback fn) {
  g_vehicle_aim_request_callback = fn;
}

namespace {

void ApplyJoystickFreeLookMode(bool enabled) {
  if (g_joystick_free_look_mode) {
    g_joystick_free_look_mode(enabled);
  }
}

bool CanPerformProtectedAction() {
  if (g_protected_action_check) return g_protected_action_check();
  return true;
}

void NotifyMovementActivated() {
  if (g_on_movement_activated) g_on_movement_activated();
}

void SaveCursorPos() {
  if (g_save_cursor_pos) g_save_cursor_pos();
}

bool ShouldNotifyMovementActivatedForFlagSet(
    const std::uint32_t flag,
    const std::uint32_t control_flags) {
  if ((flag & kMaskDirectionalMove) != 0) {
    return true;
  }

  return (flag & kMaskMouseButtons) != 0 &&
         (control_flags & kMaskMouseButtons) == kMaskMouseButtons;
}

void ProcessMovement(CInputControl& control, std::uint32_t timestamp,
                     bool movement_active) {

  static bool processing = false;
  if (processing) {
    return;
  }
  processing = true;
  struct Guard {
    bool& active;
    ~Guard() { active = false; }
  } guard{processing};

  ProcessMovementDecision decision;
  decision.timestamp = timestamp;
  decision.movement_active = movement_active;

  ProcessMovementRuntimeState runtime_state;
  if (g_process_movement_runtime_state_query &&
      g_process_movement_runtime_state_query(runtime_state)) {
    if (runtime_state.should_use_movement_rates) {
      decision.used_movement_rates = true;
      decision.movement_rates_updated =
          ComputeMovementRates(control.GetControlFlags(), movement_active,
                               decision.movement_rates);
    } else {
      if (runtime_state.has_timestamp_floor &&
          decision.timestamp < runtime_state.timestamp_floor) {
        decision.timestamp = runtime_state.timestamp_floor;
      }

      decision.can_move =
          movement_active && CInputControl::CanPlayerMove(runtime_state.gate_state);
      decision.can_turn =
          movement_active && CInputControl::CanPlayerTurn(runtime_state.gate_state);
      decision.stop_auto_attack = !decision.can_move && !decision.can_turn;
      decision.vehicle_control_allows_free_movement =
          runtime_state.gate_state.vehicle_control_allows_free_movement;
      decision.allow_keyboard_turn_in_move_and_steer =
          runtime_state.allow_keyboard_turn_in_move_and_steer;
      decision.movement_flags = runtime_state.movement_flags;
      decision.movement_flags2 = runtime_state.movement_flags2;

      decision.force_forward_override = runtime_state.has_force_forward_override;

      if (!decision.can_move &&
          (control.GetControlFlags() & kCtrlAutoRun) != 0) {
        control.SetControlFlagsRaw(control.GetControlFlags() & ~kCtrlAutoRun);
      }
    }
  }

  if (g_process_movement) {
    g_process_movement(control, decision);
  }
}

void ApplyCursorVisibility(bool visible) {
  if (g_cursor_visibility_callback) {
    g_cursor_visibility_callback(visible);
  }
}

bool IsCinematicJoystickEnabled() {
  if (g_cinematic_joystick_cvar_query) {
    return g_cinematic_joystick_cvar_query();
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  return cvars.Exists("CinematicJoystick") &&
         cvars.GetCVarBool("CinematicJoystick");
}

std::uint32_t CurrentInputTickCount() {
  if (g_input_tick_count_provider) {
    return g_input_tick_count_provider();
  }
  return 0;
}

std::uint32_t CurrentInputFrameTick() {
  if (g_input_frame_tick_provider) {
    return g_input_frame_tick_provider();
  }

  return static_cast<std::uint32_t>(FrameTimer::Get().GetFrameCount());
}

std::uint32_t CurrentMouseDeltaTimestamp() {
  if (g_input_tick_count_provider) {
    return g_input_tick_count_provider();
  }

  return openwow::core::GameClock::GetTickCount32();
}

bool ApplyControlFlagChange(CInputControl& control,
                            const std::uint32_t flag,
                            const bool set_flag,
                            const std::uint32_t timestamp,
                            const std::uint32_t clear_aux_arg) {
  const bool changed = set_flag
                           ? control.SetControlFlag(flag, timestamp)
                           : control.ClearControlFlag(flag, timestamp,
                                                      clear_aux_arg);
  if (!changed) {
    return false;
  }

  if (set_flag &&
      ShouldNotifyMovementActivatedForFlagSet(flag, control.GetControlFlags())) {
    NotifyMovementActivated();
  }

  ProcessMovement(control, timestamp, true);
  return true;
}

}

bool InputControl_ApplyControlFlagChange(const std::uint32_t flag,
                                         const bool set_flag,
                                         const std::uint32_t timestamp,
                                         const std::uint32_t clear_aux_arg) {
  auto* control = GetInputControlSingleton();
  if (control == nullptr) {
    return false;
  }

  return ApplyControlFlagChange(*control, flag, set_flag, timestamp,
                                clear_aux_arg);
}

void InputControl_NotifyMovementActivated() {
  NotifyMovementActivated();
}

int InputControl_MoveForwardStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlMoveForward, true, timestamp, 0);
  return 0;
}

int InputControl_MoveForwardStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlMoveForward, false, timestamp, 0);
  return 0;
}

int InputControl_MoveBackwardStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlMoveBackward, true, timestamp, 0);
  return 0;
}

int InputControl_MoveBackwardStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlMoveBackward, false, timestamp, 0);
  return 0;
}

int InputControl_TurnLeftStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlTurnLeft, true, timestamp, 0);
  return 0;
}

int InputControl_TurnLeftStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlTurnLeft, false, timestamp, 0);
  return 0;
}

int InputControl_TurnRightStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlTurnRight, true, timestamp, 0);
  return 0;
}

int InputControl_TurnRightStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlTurnRight, false, timestamp, 0);
  return 0;
}

int InputControl_StrafeLeftStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlStrafeLeft, true, timestamp, 0);
  return 0;
}

int InputControl_StrafeLeftStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlStrafeLeft, false, timestamp, 0);
  return 0;
}

int InputControl_StrafeRightStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlStrafeRight, true, timestamp, 0);
  return 0;
}

int InputControl_StrafeRightStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlStrafeRight, false, timestamp, 0);
  return 0;
}

int InputControl_PitchUpStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlPitchUp, true, timestamp, 0);
  return 0;
}

int InputControl_PitchUpStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlPitchUp, false, timestamp, 0);
  return 0;
}

int InputControl_PitchDownStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlPitchDown, true, timestamp, 0);
  return 0;
}

int InputControl_PitchDownStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlPitchDown, false, timestamp, 0);
  return 0;
}

int InputControl_JumpOrAscendStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlAscend, true, timestamp, 0);

  return 0;
}

int InputControl_AscendStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlAscend, false, timestamp, 0);
  return 0;
}

int InputControl_DescendStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic) return 0;
  if (CanPerformProtectedAction()) {
    if (ic->SetControlFlag(kCtrlDescend, timestamp)) {
      ProcessMovement(*ic, timestamp, true);
    }
  }
  NotifyMovementActivated();
  return 0;
}

int InputControl_DescendStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  if (ic->ClearControlFlag(kCtrlDescend, timestamp, 0)) {
    ProcessMovement(*ic, timestamp, true);
  }
  return 0;
}

int InputControl_TurnOrActionStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  const std::uint32_t f = ic->GetControlFlags();
  if ((f & kMaskMouseButtons) == 0) {
    ic->SetLastMouseButtonType(2);
    SaveCursorPos();
  }
  ApplyControlFlagChange(*ic, kCtrlTurnOrAction, true, timestamp, 0);
  return 0;
}

int InputControl_TurnOrActionStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlTurnOrAction, false, timestamp, 0);
  return 0;
}

int InputControl_CameraOrSelectOrMoveStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  const std::uint32_t f = ic->GetControlFlags();
  if ((f & kMaskMouseButtons) == 0) {
    ic->SetLastMouseButtonType(1);
    SaveCursorPos();
  }
  ApplyControlFlagChange(*ic, kCtrlCameraOrSelect, true, timestamp, 0);
  return 0;
}

int InputControl_CameraOrSelectOrMoveStop(std::uint32_t timestamp,
                                           std::uint32_t lock_camera) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlCameraOrSelect, false, timestamp,
                         lock_camera);
  return 0;
}

int InputControl_MoveAndSteerStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  const std::uint32_t f = ic->GetControlFlags();

  if ((f & kMaskMouseButtons) == 0) {
    ic->SetLastMouseButtonType(1);
    SaveCursorPos();
  }
  ApplyControlFlagChange(*ic, kCtrlCameraOrSelect, true, timestamp, 0);

  if ((ic->GetControlFlags() & kMaskMouseButtons) == 0) {
    ic->SetLastMouseButtonType(2);
    SaveCursorPos();
  }
  ApplyControlFlagChange(*ic, kCtrlTurnOrAction, true, timestamp, 0);
  return 0;
}

int InputControl_MoveAndSteerStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  ApplyControlFlagChange(*ic, kCtrlCameraOrSelect, false, timestamp, 0);
  ApplyControlFlagChange(*ic, kCtrlTurnOrAction, false, timestamp, 0);
  return 0;
}

int InputControl_MouselookStart(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic) return 0;
  ic->SetLastMouseButtonType(0);
  ApplyControlFlagChange(*ic, kCtrlTurnOrAction, true, timestamp, 0);
  ic->ApplyMouselookOverrideBindings();
  return 0;
}

int InputControl_MouselookStop(std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic) return 0;
  ic->SetLastMouseButtonType(0);
  ApplyControlFlagChange(*ic, kCtrlTurnOrAction, false, timestamp, 0);
  ic->DeactivateMouselookOverrideBindings();
  return 0;
}

int InputControl_ToggleAutoRun(const std::uint32_t timestamp) {
  auto* ic = GetInputControlSingleton();
  if (!ic || !CanPerformProtectedAction()) return 0;
  const bool was_enabled = (ic->GetControlFlags() & kCtrlAutoRun) != 0;
  ic->ToggleAutoRun();
  if (!was_enabled && (ic->GetControlFlags() & kCtrlAutoRun) != 0) {
    NotifyMovementActivated();
  }
  ProcessMovement(*ic, timestamp, true);
  return 0;
}

int GameUI_ProcessMovementState(GameUiMovementGateContext& context) {
  return GetInputControlSingleton()->ProcessGameUiMovementState(
      context, CurrentInputTickCount());
}

int InputControl_VehicleExit(WorldSession& session) {
  if (ResolveVehicleControlBoundUnit(session) == nullptr) {
    return 0;
  }

  if (!CanUseVehicleControlAction(session, VehicleControlSeatFlag::kCanExit)) {
    ui::game::DisplaySystemMessage(48);
    return 0;
  }

  const auto* const active_mover =
      session.objects().GetUnit(
          session.player_control_runtime().ActiveMoverGuid());
  if (active_mover != nullptr) {
    UnitVehicle_RequestExit(session, active_mover);
  }
  return 0;
}

int InputControl_VehiclePrevSeat(WorldSession& session) {
  if (!CanUseVehicleControlAction(session, VehicleControlSeatFlag::kCanSwitch)) {
    return 0;
  }

  const auto* const active_mover =
      session.objects().GetUnit(
          session.player_control_runtime().ActiveMoverGuid());
  if (active_mover != nullptr) {
    UnitVehicle_RequestPrevSeat(session, active_mover);
  }
  return 0;
}

int InputControl_VehicleNextSeat(WorldSession& session) {
  if (!CanUseVehicleControlAction(session, VehicleControlSeatFlag::kCanSwitch)) {
    return 0;
  }

  const auto* const active_mover =
      session.objects().GetUnit(
          session.player_control_runtime().ActiveMoverGuid());
  if (active_mover != nullptr) {
    UnitVehicle_RequestNextSeat(session, active_mover);
  }
  return 0;
}

void CInputControl::SetJoystickEnabled(bool enabled) {
  if (joystick_obj_) {
    if (!enabled) {
      delete joystick_obj_;
      joystick_obj_ = nullptr;
    }
    return;
  }

  if (!enabled) {
    return;
  }

  joystick_obj_ = openwow::input::InputPeripheralBridge::CreateForActiveWindow();
}

bool CInputControl::IsWowMouseActive() const {
  return joystick_obj_ != nullptr && joystick_obj_->HasDetectedDevice();
}

bool CInputControl::DetectWowMouse() {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists("enableWowMouse")) {
    cvars.RegisterCVar("enableWowMouse", "0",
                       openwow::ui::game::CVarFlags::Archive,
                       "Enable Steelseries World of Warcraft Mouse");
  }

  SetJoystickEnabled(false);
  (void)cvars.SetCVar("enableWowMouse", "1");
  if (joystick_obj_ == nullptr) {
    SetJoystickEnabled(true);
  }

  if (joystick_obj_ != nullptr && joystick_obj_->HasDetectedDevice()) {
    return true;
  }

  openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
      openwow::ui::game::events::WOW_MOUSE_NOT_FOUND);
  SetJoystickEnabled(false);
  (void)cvars.SetCVar("enableWowMouse", "0");
  return false;
}

bool CInputControl::ApplyCameraFacing(WorldSession& session,
                                      const std::uint32_t timestamp,
                                      float facing) {
  if (session.world_camera() == nullptr) {
    return false;
  }

  const CameraControlGateResult gate = QueryCanApplyCameraFacingImpl(*this);
  if (!gate.allowed) {
    return false;
  }

  auto *const mover = session.objects().GetMutableUnit(
      session.player_control_runtime().ActiveMoverGuid());
  if (mover == nullptr) {
    return false;
  }

  if (gate.commentator_controls) {
    auto& commentator = CommentatorState::Get();
    const auto position = commentator.GetCameraPosition();
    commentator.SetCamera(position.x, position.y, position.z, facing,
                          commentator.GetPitch());
    return true;
  }

  auto* const camera = session.world_camera();
  if (camera == nullptr) {
    return false;
  }
  Vehicle_TransformCameraTargetFacingToLocal(session.objects(), *mover,
                                             &facing);
  constexpr std::uint16_t kVehicleConstrainedFacingFlag = 0x0008u;
  if ((mover->GetMovementInfo().flags2 & kVehicleConstrainedFacingFlag) != 0u) {
    if (!IsYawPushed() || GetSavedYaw() != facing) {
      mover->Movement().SendBoundedTurnFacing(session, timestamp, facing);
      SetSavedYaw(facing);
      if (!IsYawPushed()) {

        camera->PushYawOffset();
        SetYawPushed(true);
      }
    }
  } else {
    mover->Movement().SendSetFacing(session, timestamp, facing);
    Vehicle_RecordCameraFacingMouseYawOverride(session.objects(), *mover,
                                               *camera, facing);
  }
  MarkTurnSent(false);
  return true;
}

bool CInputControl::ApplyCameraPitch(WorldSession& session,
                                     const std::uint32_t timestamp,
                                     const float camera_pitch) {
  const CameraControlGateResult gate = QueryCanPlayerMoveForCameraPitchImpl();
  if (!gate.allowed) {
    return false;
  }

  auto *const mover = session.objects().GetMutableUnit(
      session.player_control_runtime().ActiveMoverGuid());
  if (mover == nullptr) {
    return false;
  }

  if (gate.commentator_controls) {
    auto& commentator = CommentatorState::Get();
    const auto position = commentator.GetCameraPosition();
    commentator.SetCamera(position.x, position.y, position.z,
                          commentator.GetYaw(), camera_pitch);
    return true;
  }

  float mover_pitch = -camera_pitch;
  if (mover->Vehicle().GetVehicleData() != nullptr) {
    if (const auto *const vehicle_entry = mover->Vehicle().GetVehicleEntry();
        vehicle_entry != nullptr) {
      mover_pitch += vehicle_entry->mouse_look_offset_pitch;
    }
  }

  mover_pitch = ClampInputPitchForVehicle(*mover, mover_pitch);
  constexpr std::uint16_t kVehicleConstrainedPitchFlag = 0x0010u;
  if ((mover->GetMovementInfo().flags2 & kVehicleConstrainedPitchFlag) != 0u) {
    if (!IsPitchLockActive() || GetPitchLockValue() != mover_pitch) {
      mover->Movement().SendSetVehiclePitch(session, timestamp, mover_pitch);
      SetPitchLock(true, mover_pitch);
    }
  } else {
    mover->Movement().SendSetPitch(session, timestamp, mover_pitch);
    InputControl_UpdatePitchEventForUnit(*mover, mover_pitch);
  }
  LatchMissileTrajectoryInputRefresh();
  MarkPitchSent(false);
  return true;
}

bool CInputControl::RequestVehicleAimPitch(WorldSession& session,
                                            const std::uint32_t timestamp,
                                            CGUnit_C& mover,
                                            const float requested_pitch) {
  const float clamped_pitch =
      ClampInputPitchForVehicle(mover, requested_pitch);
  constexpr std::uint16_t kVehicleConstrainedPitchFlag = 0x0010u;
  if ((mover.GetMovementInfo().flags2 & kVehicleConstrainedPitchFlag) == 0u) {
    mover.Movement().SendSetPitch(session, timestamp, clamped_pitch);
    InputControl_UpdatePitchEventForUnit(mover, clamped_pitch);
  } else if (!IsPitchLockActive() || GetPitchLockValue() != clamped_pitch) {
    mover.Movement().SendSetVehiclePitch(session, timestamp, clamped_pitch);
    SetPitchLock(true, clamped_pitch);
  }
  LatchMissileTrajectoryInputRefresh();
  return true;
}

void CInputControl::ResetVehicleAimPitchLock(WorldSession& session) {
  SetPitchLock(false);

  auto* const mover = session.objects().GetMutableUnit(
      session.player_control_runtime().ActiveMoverGuid());
  if (mover == nullptr) {
    return;
  }

  const float pitch = mover->GetMovementInfo().pitch;
  InputControl_UpdatePitchEventForUnit(*mover, pitch);
}

bool CInputControl::IsUnitInMovementMode(std::uint8_t flags, int movement_type) {
  if ((flags & 0x10) == 0) return false;
  return movement_type == 1 || movement_type == 2 || movement_type == 3;
}

void Camera_ApplyMouseSensitivity(float* dx, float* dy) {
  float camera_distance = 0.0f;
  if (!g_active_world_camera_distance_query ||
      !g_active_world_camera_distance_query(camera_distance)) {
    return;
  }

  const double scale =
      ComputeCameraMouseSensitivityScale(static_cast<double>(camera_distance));
  *dx = static_cast<float>(*dx * scale);
  *dy = static_cast<float>(*dy * scale);
}

void Camera_ConvertMouseDeltaToRadians(float* dx, float* dy) {
  constexpr float kDegreesToRadians = 0.01745329238474369f;
  constexpr float kYawPixelSpan = 800.0f;
  constexpr float kPitchPixelSpan = 600.0f;
  constexpr float kYawSignDefault = -1.0f;
  constexpr float kYawSignInverted = 1.0f;
  constexpr float kPitchSignDefault = 1.0f;
  constexpr float kPitchSignInverted = -1.0f;

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const float yaw_sign = cvars.GetCVarInt("mouseInvertYaw") != 0
                             ? kYawSignInverted
                             : kYawSignDefault;
  const float pitch_sign = cvars.GetCVarInt("mouseInvertPitch") != 0
                               ? kPitchSignInverted
                               : kPitchSignDefault;
  const float yaw_speed_degrees = cvars.GetCVarFloat("cameraYawMoveSpeed");
  const float pitch_speed_degrees = cvars.GetCVarFloat("cameraPitchMoveSpeed");

  if (dx != nullptr) {
    *dx = yaw_sign * (*dx / g_camera_aspect_normalize_x / kYawPixelSpan) *
          yaw_speed_degrees * kDegreesToRadians;
  }
  if (dy != nullptr) {
    *dy = pitch_sign * (*dy / g_camera_aspect_normalize_y / kPitchPixelSpan) *
          pitch_speed_degrees * kDegreesToRadians;
  }
}

bool ComputeMovementRates(std::uint32_t flags, const bool movement_active,
                          MovementRates& out) {
  MovementRatesRuntimeState runtime_state;
  if (!g_movement_rates_runtime_state_query ||
      !g_movement_rates_runtime_state_query(runtime_state) ||
      !runtime_state.has_active_player || !runtime_state.has_active_camera) {
    return false;
  }

  if (!movement_active) {
    out.forward = 0.0f;
    out.strafe = 0.0f;
    out.vertical = 0.0f;
    out.turn_rate = 0.0f;
    return true;
  }

  {
    int net = 0;
    if (flags & kCtrlAutoRun)     ++net;
    if (flags & kCtrlMoveForward) ++net;
    if ((flags & kCtrlTurnOrAction) && (flags & kCtrlCameraOrSelect)) ++net;
    if (flags & kCtrlMoveBackward) --net;
    if (net > 0) {
      if (g_clear_local_afk_for_movement) {
        g_clear_local_afk_for_movement();
      }
      out.forward = 1.0f;
    } else if (net < 0) {
      if (g_clear_local_afk_for_movement) {
        g_clear_local_afk_for_movement();
      }
      out.forward = -1.0f;
    } else {
      out.forward = 0.0f;
    }
  }

  {
    int net = 0;
    if (flags & kCtrlStrafeLeft) ++net;

    if ((flags & kMaskMoveAndSteer) && (flags & kCtrlTurnLeft)) ++net;
    if (flags & kCtrlStrafeRight) --net;

    if ((flags & kMaskMoveAndSteer) && (flags & kCtrlTurnRight)) --net;
    if (net > 0) {
      if (g_clear_local_afk_for_movement) {
        g_clear_local_afk_for_movement();
      }
      out.strafe = 1.0f;
    } else if (net < 0) {
      if (g_clear_local_afk_for_movement) {
        g_clear_local_afk_for_movement();
      }
      out.strafe = -1.0f;
    } else {
      out.strafe = 0.0f;
    }
  }

  {
    int net = 0;
    if (flags & kCtrlAscend)  ++net;
    if (flags & kCtrlDescend) --net;
    out.vertical = static_cast<float>(net);
  }

  {
    int net = 0;
    if (flags & kCtrlTurnLeft)  ++net;
    if (flags & kCtrlTurnRight) --net;
    if (flags & kMaskMoveAndSteer) net = 0;
    out.turn_rate = static_cast<float>(static_cast<double>(net) * 0.05);
  }

  return true;
}

}

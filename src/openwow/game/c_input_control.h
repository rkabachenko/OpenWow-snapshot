#pragma once

#include "openwow/game/object_types.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace openwow::input {
class InputPeripheralBridge;
}

namespace openwow::game {

class CInputControl;
class CGUnit_C;
class WorldSession;
class BindingProfiles;
class InputControlBindingRuntime;

using MissileTrajectoryInputRefreshCallback = std::function<void()>;

enum ControlFlag : std::uint32_t {
  kCtrlNone            = 0x00000000,

  kCtrlTurnOrAction    = 0x00000001,
  kCtrlCameraOrSelect  = 0x00000002,

  kCtrlMoveForward     = 0x00000010,
  kCtrlMoveBackward    = 0x00000020,
  kCtrlStrafeLeft      = 0x00000040,
  kCtrlStrafeRight     = 0x00000080,
  kCtrlTurnLeft        = 0x00000100,
  kCtrlTurnRight       = 0x00000200,
  kCtrlPitchUp         = 0x00000400,
  kCtrlPitchDown       = 0x00000800,
  kCtrlAutoRun         = 0x00001000,
  kCtrlAscend          = 0x00002000,
  kCtrlDescend         = 0x00004000,

  kCtrlForwardSent     = 0x00010000,
  kCtrlStrafeSent      = 0x00020000,
  kCtrlTurnSent        = 0x00040000,
  kCtrlPitchSent       = 0x00080000,
  kCtrlVerticalSent    = 0x00100000,

  kCtrlClickToMoveForward = 0x00200000,
  kCtrlClickToMovePending = 0x00400000,
  kCtrlClickToMoveUnused  = 0x00800000,
  kCtrlClickToMoveFacing  = 0x01000000,

  kCtrlMoveAndSteer    = 0x02000000,
  kCtrlVehicleCtrl     = 0x04000000,
};

inline constexpr std::uint32_t kMaskMouseButtons      = 0x00000003;
inline constexpr std::uint32_t kMaskForwardBackward    = 0x00000030;
inline constexpr std::uint32_t kMaskStrafe             = 0x000000C0;
inline constexpr std::uint32_t kMaskTurn               = 0x00000300;
inline constexpr std::uint32_t kMaskPitch              = 0x00000C00;
inline constexpr std::uint32_t kMaskVertical           = 0x00006000;
inline constexpr std::uint32_t kMaskAutoRun            = 0x00001000;

inline constexpr std::uint32_t kMaskClickToMove        = 0x01E00000;
inline constexpr std::uint32_t kMaskMoveAndSteer       = 0x02000001;
inline constexpr std::uint32_t kMaskCameraOrVehicle    = 0x04000002;
inline constexpr std::uint32_t kMaskAllMouseModes      = 0x06000003;
inline constexpr std::uint32_t kMaskFwdBwdAutoRun      = 0x00001030;
inline constexpr std::uint32_t kMaskDirectionalMove    = 0x000010F0;
inline constexpr std::uint32_t kMaskDirectionalAll     = 0x000013F0;
inline constexpr std::uint32_t kMaskDirectionalReapply = 0x00001330;
inline constexpr std::uint32_t kMaskActionMovement     = 0x00A010F0;
inline constexpr std::uint32_t kMaskJoystickCameraCtrl = 0x06000000;
inline constexpr std::uint32_t kUnitFlagStunned        = 0x00040000;

inline constexpr std::uint32_t kCursorVisibilityPresented           = 0x00000001;

inline constexpr std::uint32_t kCursorVisibilityDefaultRequest      = 0x00000002;

inline constexpr std::uint32_t kCursorVisibilityToggledRequest      = 0x00000004;
inline constexpr std::uint32_t kCursorVisibilityRequestMask         = 0x00000006;

inline constexpr std::uint32_t kCursorVisibilitySpellTargetingForce = 0x00000010;

inline constexpr std::uint32_t kCursorVisibilityAppInactiveForce    = 0x00000020;

inline constexpr std::uint32_t kCursorVisibilityCinematicHide       = 0x00000040;

inline constexpr std::uint32_t kCursorVisibilityUiInitFlags =
    kCursorVisibilityPresented | kCursorVisibilityDefaultRequest;

struct GameUiMovementGateContext {
  std::uint32_t reserved0{0};
  std::uint32_t reserved4{0};
  std::uint32_t reserved8{0};
  std::uint32_t reservedC{0};
  std::uint32_t gate{0};
};

struct PlayerMovementGateState {
  std::int32_t  health{0};
  std::uint32_t unit_flags{0};
  bool          has_mounted_display{false};
  bool          is_active_player{false};
  bool          active_player_turn_locked{false};
  bool          vehicle_control_allows_free_movement{true};
  bool          is_in_vehicle_transition{false};
  bool          has_non_static_vehicle_seat{false};
  bool          has_movement_restriction_flags{false};
  bool          is_power_type_locked{false};
  bool          is_on_vehicle{false};
  bool          mouse_steering_blocked{false};
};

struct ProcessMovementRuntimeState {
  PlayerMovementGateState gate_state{};
  std::uint32_t movement_flags{0};
  std::uint16_t movement_flags2{0};
  bool should_use_movement_rates{false};
  bool commentator_controls_enabled{false};
  bool camera_bound_to_mover{false};
  bool camera_bound_alpha_visible{false};
  bool allow_keyboard_turn_in_move_and_steer{false};
  bool vehicle_allows_ground_pitch{false};
  bool has_timestamp_floor{false};
  std::uint32_t timestamp_floor{0};

  bool has_force_forward_override{false};
};

struct PlayerCanMoveRuntimeState {
  PlayerMovementGateState gate_state{};
  std::uint32_t movement_flags{0};
  std::uint16_t movement_flags2{0};
  bool commentator_controls_enabled{false};
  bool camera_bound_to_mover{false};
  bool vehicle_allows_ground_pitch{false};
};

struct ControlFlagSideEffects {
  const CInputControl* input_control{nullptr};
  bool check_auto_freelook{false};
  bool clear_mouse_modes{false};
  bool lock_camera_on_clear{false};
  bool mouse_button_set{false};
  bool mouse_button_clear{false};
  bool update_pitch_clamp{false};

  std::uint32_t click_to_move_flags{0};

  bool click_to_move_flags_changed{false};
  bool update_bobbing{false};
  bool bobbing_enabled{false};
  bool pop_yaw_offset{false};

  bool evaluate_camera_smoothing{false};

  bool camera_smoothing_stop_event{false};

  std::uint32_t control_flags{0};
  std::uint32_t timestamp{0};
  std::uint32_t dispatch_world_click_type{0};
};

enum class VerticalMovementCommand : std::uint8_t {
  kNoChange = 0,
  kStartPitchUp,
  kStartPitchDown,
  kStopPitch,
  kStartAscend,
  kStartDescend,
  kStopVertical,
};

struct VerticalMovementDecision {
  VerticalMovementCommand command{VerticalMovementCommand::kNoChange};
  bool sent_state_after{false};
  bool clear_afk{false};
  bool fire_pitch_event{false};
};

struct MovementRates;

struct ProcessMovementDecision;

struct MouseDeltaRuntimeState {
  std::uint32_t frame_tick{0};
  std::uint32_t timestamp{0};
  bool apply_distance_sensitivity{false};
  bool can_mouse_steer{false};
  bool route_pitch_into_vehicle_aim{false};
  bool player_can_move{false};
  float vehicle_aim_pitch_base{0.0f};
};

struct JoystickCameraRuntimeState {
  bool has_active_mover{false};
  bool has_joystick_device{false};
  bool has_active_camera{false};
  bool mover_has_pitch_flags{false};
  float mover_pitch{0.0f};
  const CGUnit_C* active_mover{nullptr};
};

struct MovementRatesRuntimeState {
  bool has_active_player{false};
  bool has_active_camera{false};
};

class CInputControl {
 public:
  CInputControl();
  explicit CInputControl(BindingProfiles& binding_profiles);
  ~CInputControl();

  void BindBindingProfiles(BindingProfiles& binding_profiles);
  void SetMouselookOverrideBinding(std::string_view key,
                                   const char* binding);
  void ApplyMouselookOverrideBindings();
  void DeactivateMouselookOverrideBindings();
  void ClearMouselookOverrideBindings();

  bool SetControlFlag(std::uint32_t flag, std::uint32_t timestamp);
  bool ClearControlFlag(std::uint32_t flag, std::uint32_t timestamp,
                        std::uint32_t lock_camera);
  void ReapplyDirectionalControlState(std::uint32_t timestamp);

  [[nodiscard]] bool IsIdle() const;

  [[nodiscard]] bool HasDoubleClickElapsed(std::uint32_t now_ms) const;

  [[nodiscard]] bool HasRecentMouseDeltaAfterDoubleClickElapsed(
      std::uint32_t now_ms) const;

  [[nodiscard]] static bool IsPlayerAliveAndFree(
      const PlayerMovementGateState& state);

  [[nodiscard]] static bool CanPlayerTurn(
      const PlayerMovementGateState& state);

  [[nodiscard]] static bool CanUnitWalk(
      const PlayerMovementGateState& state);

  [[nodiscard]] static bool CanPlayerMove(
      const PlayerMovementGateState& state);

  [[nodiscard]] bool CanMouseSteerUnit(
      const PlayerMovementGateState& state) const;

  [[nodiscard]] bool CanAutoRun(
      const ProcessMovementRuntimeState& runtime_state) const;

  [[nodiscard]] static bool CheckPlayerCanMove(
      const PlayerCanMoveRuntimeState& runtime_state);

  void ClearForwardSentAndAutoRun() {
    control_flags_ &= ~(kCtrlForwardSent | kCtrlAutoRun);
  }

  void ClearAxisSentFlags() {
    control_flags_ &= ~(kCtrlForwardSent | kCtrlStrafeSent |
                         kCtrlTurnSent | kCtrlPitchSent);
  }

  [[nodiscard]] std::uint32_t GetControlFlags() const { return control_flags_; }
  void SetControlFlagsRaw(std::uint32_t flags) { control_flags_ = flags; }

  void ResetClickState(std::uint32_t timestamp);
  void AccumulateClickDelta(float dx, float dy);
  [[nodiscard]] float GetAccumulatedAbsMouseDx() const { return accu_abs_dx_; }
  [[nodiscard]] float GetAccumulatedAbsMouseDy() const { return accu_abs_dy_; }
  [[nodiscard]] std::uint32_t GetLastMouseDeltaFrameTick() const {
    return last_mouse_delta_frame_tick_;
  }
  void SetLastMouseButtonType(std::uint32_t type) { last_mouse_button_type_ = type; }
  [[nodiscard]] std::uint32_t GetLastMouseButtonType() const { return last_mouse_button_type_; }

  [[nodiscard]] bool IsYawPushed() const { return yaw_pushed_ != 0; }
  void SetYawPushed(bool pushed) { yaw_pushed_ = pushed ? 1 : 0; }
  void SetSavedYaw(float yaw) { saved_yaw_ = yaw; }
  [[nodiscard]] float GetSavedYaw() const { return saved_yaw_; }

  [[nodiscard]] bool IsBothButtonsActive() const { return both_buttons_active_ != 0; }
  void SetBothButtonsActive(bool active) { both_buttons_active_ = active ? 1 : 0; }

  [[nodiscard]] std::uint32_t GetCursorVisibilityFlags() const { return cursor_visibility_flags_; }
  void SetCursorVisibilityFlagsRaw(std::uint32_t flags) { cursor_visibility_flags_ = flags; }
  [[nodiscard]] std::uint32_t GetCursorVisibilityHoldCount() const {
    return cursor_visibility_hold_count_;
  }
  void SetCursorVisibilityHoldCount(std::uint32_t value) {
    cursor_visibility_hold_count_ = value;
  }
  [[nodiscard]] std::uint32_t GetJoystickFreeLookFlags() const {
    return joystick_free_look_flags_;
  }
  void SetJoystickFreeLookFlagsRaw(std::uint32_t value) {
    joystick_free_look_flags_ = value;
  }

  [[nodiscard]] bool RefreshCursorVisibility(bool force);

  int ProcessGameUiMovementState(GameUiMovementGateContext& context,
                                 std::uint32_t timestamp);

  [[nodiscard]] bool IsPitchLockActive() const { return pitch_lock_active_ != 0; }
  void SetPitchLock(bool active, float value = 0.0f) {
    pitch_lock_active_ = active ? 1 : 0;
    pitch_lock_value_ = value;
  }
  [[nodiscard]] float GetPitchLockValue() const { return pitch_lock_value_; }

  [[nodiscard]] int ComputeNetForward() const;

  [[nodiscard]] int ComputeNetStrafe() const;

  [[nodiscard]] int ComputeNetTurn(
      bool allow_turn_in_move_and_steer = false) const;

  [[nodiscard]] int ComputeNetPitch() const;

  [[nodiscard]] int ComputeNetVertical() const;

  [[nodiscard]] VerticalMovementDecision ResolveVerticalMovementDecision(
      std::uint32_t movement_flags,
      std::uint16_t movement_flags2) const;

  [[nodiscard]] bool IsForwardSent() const { return (control_flags_ & kCtrlForwardSent) != 0; }
  [[nodiscard]] bool IsStrafeSent() const { return (control_flags_ & kCtrlStrafeSent) != 0; }
  [[nodiscard]] bool IsTurnSent() const { return (control_flags_ & kCtrlTurnSent) != 0; }
  [[nodiscard]] bool IsPitchSent() const { return (control_flags_ & kCtrlPitchSent) != 0; }
  [[nodiscard]] bool IsVerticalSent() const { return (control_flags_ & kCtrlVerticalSent) != 0; }

  void MarkForwardSent(bool sent);
  void MarkStrafeSent(bool sent);
  void MarkTurnSent(bool sent);
  void MarkPitchSent(bool sent);
  void MarkVerticalSent(bool sent);

  void ToggleAutoRun();

  bool PopYawIfNeeded();

  void SetJoystickRates(float x_rate, float y_rate) {
    joystick_x_rate_ = x_rate;
    joystick_y_rate_ = y_rate;
  }
  [[nodiscard]] float GetJoystickXRate() const { return joystick_x_rate_; }
  [[nodiscard]] float GetJoystickYRate() const { return joystick_y_rate_; }
  [[nodiscard]] bool HasJoystickBridge() const { return joystick_obj_ != nullptr; }

  void SetJoystickEnabled(bool enabled);

  [[nodiscard]] bool IsWowMouseActive() const;

  [[nodiscard]] bool DetectWowMouse();

  [[nodiscard]] bool ApplyCameraFacing(WorldSession& session,
                                       std::uint32_t timestamp, float facing);

  [[nodiscard]] bool ApplyCameraPitch(WorldSession& session,
                                      std::uint32_t timestamp,
                                      float camera_pitch);

  [[nodiscard]] bool RequestVehicleAimPitch(WorldSession& session,
                                            std::uint32_t timestamp,
                                            CGUnit_C& mover,
                                            float requested_pitch);

  void ResetVehicleAimPitchLock(WorldSession& session);

  void BindMissileTrajectoryInputRefresh(
      MissileTrajectoryInputRefreshCallback callback);

  void ResetControlFlagsAndJoystick(std::uint32_t timestamp);

  [[nodiscard]] static bool IsUnitInMovementMode(std::uint8_t flags, int movement_type);

  void Reset();

  void HandleMouseDelta(float dx, float dy);

  void HandleJoystickCameraInput(float dt);

  void ProcessMovementNow(std::uint32_t timestamp, bool movement_active = true);

 private:

  bool ClearYawPushState();
  void LatchMissileTrajectoryInputRefresh();

  std::uint32_t reserved0_{0};
  std::uint32_t control_flags_{0};
  float         accu_abs_dx_{0.0f};
  float         accu_abs_dy_{0.0f};
  std::uint32_t last_mouse_delta_frame_tick_{0};
  std::uint32_t last_click_timestamp_{0};
  std::uint32_t last_mouse_button_type_{0};
#if UINTPTR_MAX == UINT32_MAX
  std::unique_ptr<InputControlBindingRuntime> binding_runtime_;
  std::uint32_t reserved2_[9]{};
#else
  std::uint32_t reserved2_[10]{};
#endif
  std::uint32_t yaw_pushed_{0};
  float         saved_yaw_{0.0f};
  std::uint32_t pitch_lock_active_{0};
  float         pitch_lock_value_{0.0f};
  std::uint32_t both_buttons_active_{0};
  std::uint32_t cursor_visibility_flags_{0};
  std::uint32_t cursor_visibility_hold_count_{0};
  std::uint32_t joystick_free_look_flags_{0};
  float         joystick_x_rate_{0.0f};
  float         joystick_y_rate_{0.0f};
  openwow::input::InputPeripheralBridge* joystick_obj_{nullptr};
#if UINTPTR_MAX != UINT32_MAX
  std::unique_ptr<InputControlBindingRuntime> binding_runtime_;
#endif
};

static_assert(sizeof(CInputControl) == (sizeof(void*) == 4 ? 0x70 : sizeof(CInputControl)),
              "CInputControl must match the 112-byte IDA layout");

void              SetInputControlSingleton(CInputControl* ptr);
CInputControl*    GetInputControlSingleton();

void InputControl_SetStoredMouselookOverrideBinding(std::string_view key,
                                                    const char* binding);
void InputControl_SetStoredMouselookOverrideBindingForControl(
    CInputControl& input,
    std::string_view key,
    const char* binding);
void InputControl_ApplyStoredMouselookOverrideBindingsForControl(
    CInputControl& input);
void InputControl_ResetStoredMouselookOverrideBindings();

void ShutdownInputControlRuntime();

void SetCinematicInputBlocked(bool active);

void ResetInputAfterCinematicTransition();

using ProtectedActionCheck = bool(*)();
void SetProtectedActionCheck(ProtectedActionCheck fn);
[[nodiscard]] ProtectedActionCheck GetProtectedActionCheck();

using OnMovementActivated = std::function<void()>;
void SetOnMovementActivatedCallback(OnMovementActivated fn);

using SaveCursorPosCallback = void(*)();
void SetSaveCursorPosCallback(SaveCursorPosCallback fn);

using ActiveWorldCameraDistanceQuery = bool(*)(float& distance);
void SetActiveWorldCameraDistanceQuery(ActiveWorldCameraDistanceQuery fn);

using MovementRatesRuntimeStateQuery = bool(*)(MovementRatesRuntimeState& state);
void SetMovementRatesRuntimeStateQuery(MovementRatesRuntimeStateQuery fn);

using ClearLocalAfkForMovementCallback = void(*)();
void SetClearLocalAfkForMovementCallback(ClearLocalAfkForMovementCallback fn);

using MouseDeltaRuntimeStateQuery = bool(*)(MouseDeltaRuntimeState& state);
void SetMouseDeltaRuntimeStateQuery(MouseDeltaRuntimeStateQuery fn);

using CameraMouseInputCallback = void(*)(float dx, float dy, float* pitch_out);
void SetCameraMouseInputCallback(CameraMouseInputCallback fn);

using CameraPitchClampCallback = void(*)();
void SetCameraPitchClampCallback(CameraPitchClampCallback fn);

using JoystickCameraRuntimeStateQuery =
    bool(*)(const CInputControl& ctrl, JoystickCameraRuntimeState& state);
void SetJoystickCameraRuntimeStateQuery(JoystickCameraRuntimeStateQuery fn);

using PitchEventCallback = void(*)(float pitch);
void SetPitchEventCallback(PitchEventCallback fn);

void InputControl_RefreshViewportAspect();

void InputControl_UpdatePitchEvent(float pitch);

void InputControl_UpdatePitchEventForUnit(const CGUnit_C& unit, float pitch);

[[nodiscard]] float InputControl_GetVehicleAimNormalizedPower() noexcept;
void InputControl_SetVehicleAimNormalizedPower(float normalized_power) noexcept;

void ResetJoystickCameraScreenState();

using ControlFlagSideEffectCallback = void(*)(const ControlFlagSideEffects& effects);
void SetControlFlagSideEffectCallback(ControlFlagSideEffectCallback fn);

using VehicleAimRequestCallback = void(*)(std::uint32_t timestamp,
                                          float requested_pitch);
void SetVehicleAimRequestCallback(VehicleAimRequestCallback fn);

using ProcessMovementRuntimeStateQuery =
    bool(*)(ProcessMovementRuntimeState& state);
void SetProcessMovementRuntimeStateQuery(ProcessMovementRuntimeStateQuery fn);

[[nodiscard]] bool QueryCanMouseSteerUnitForControlFlags(const CInputControl& control);

using ProcessMovementCallback = void(*)(CInputControl& control,
                                        const ProcessMovementDecision& decision);
void SetProcessMovementCallback(ProcessMovementCallback fn);

void InputControl_NotifyMovementActivated();

using CursorVisibilityCallback = void(*)(bool visible);
void SetCursorVisibilityCallback(CursorVisibilityCallback fn);

using CinematicJoystickCVarQuery = bool(*)();
void SetCinematicJoystickCVarQuery(CinematicJoystickCVarQuery fn);

void InputControl_ResetCursorVisibilityForUiInit();

void SetSpellTargetingCursorForce(bool active);

using JoystickFreeLookModeCallback = void(*)(bool enabled);
void SetJoystickFreeLookModeCallback(JoystickFreeLookModeCallback fn);

using InputTickCountProvider = std::uint32_t(*)();
void SetInputTickCountProvider(InputTickCountProvider fn);

using InputFrameTickProvider = std::uint32_t(*)();
void SetInputFrameTickProvider(InputFrameTickProvider fn);

bool InputControl_ApplyControlFlagChange(std::uint32_t flag,
                                         bool set_flag,
                                         std::uint32_t timestamp,
                                         std::uint32_t clear_aux_arg = 0);

int InputControl_MoveForwardStart(std::uint32_t timestamp);
int InputControl_MoveForwardStop(std::uint32_t timestamp);
int InputControl_MoveBackwardStart(std::uint32_t timestamp);
int InputControl_MoveBackwardStop(std::uint32_t timestamp);
int InputControl_TurnLeftStart(std::uint32_t timestamp);
int InputControl_TurnLeftStop(std::uint32_t timestamp);
int InputControl_TurnRightStart(std::uint32_t timestamp);
int InputControl_TurnRightStop(std::uint32_t timestamp);
int InputControl_StrafeLeftStart(std::uint32_t timestamp);
int InputControl_StrafeLeftStop(std::uint32_t timestamp);
int InputControl_StrafeRightStart(std::uint32_t timestamp);
int InputControl_StrafeRightStop(std::uint32_t timestamp);
int InputControl_PitchUpStart(std::uint32_t timestamp);
int InputControl_PitchUpStop(std::uint32_t timestamp);
int InputControl_PitchDownStart(std::uint32_t timestamp);
int InputControl_PitchDownStop(std::uint32_t timestamp);

int InputControl_JumpOrAscendStart(std::uint32_t timestamp);
int InputControl_AscendStop(std::uint32_t timestamp);
int InputControl_DescendStart(std::uint32_t timestamp);
int InputControl_DescendStop(std::uint32_t timestamp);

int InputControl_TurnOrActionStart(std::uint32_t timestamp);
int InputControl_TurnOrActionStop(std::uint32_t timestamp);
int InputControl_CameraOrSelectOrMoveStart(std::uint32_t timestamp);
int InputControl_CameraOrSelectOrMoveStop(std::uint32_t timestamp, std::uint32_t lock_camera = 0);
int InputControl_MoveAndSteerStart(std::uint32_t timestamp);
int InputControl_MoveAndSteerStop(std::uint32_t timestamp);
int InputControl_MouselookStart(std::uint32_t timestamp);
int InputControl_MouselookStop(std::uint32_t timestamp);

int InputControl_ToggleAutoRun(std::uint32_t timestamp);

int GameUI_ProcessMovementState(GameUiMovementGateContext& context);

int InputControl_VehicleExit(WorldSession& session);
int InputControl_VehiclePrevSeat(WorldSession& session);
int InputControl_VehicleNextSeat(WorldSession& session);

void Camera_ApplyMouseSensitivity(float* dx, float* dy);

void Camera_ConvertMouseDeltaToRadians(float* dx, float* dy);

struct MovementRates {
  float forward{0.0f};
  float strafe{0.0f};
  float vertical{0.0f};
  float turn_rate{0.0f};
};

struct ProcessMovementDecision {
  std::uint32_t timestamp{0};
  bool movement_active{false};
  bool used_movement_rates{false};
  bool movement_rates_updated{false};
  bool can_move{false};
  bool can_turn{false};
  bool stop_auto_attack{false};
  bool vehicle_control_allows_free_movement{true};
  bool allow_keyboard_turn_in_move_and_steer{false};
  MovementRates movement_rates{};
  std::uint32_t movement_flags{0};
  std::uint16_t movement_flags2{0};

  bool force_forward_override{false};
};

bool ComputeMovementRates(std::uint32_t flags, bool movement_active,
                          MovementRates& out);

bool QueryCanMouseSteerUnitForControlFlags(const CInputControl& control);

}

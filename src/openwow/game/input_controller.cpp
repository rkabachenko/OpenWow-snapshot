#include "openwow/game/input_controller.h"

namespace openwow::game {

void InputController::SetInput(const InputState& input) {
  input_ = input;
}

const InputState& InputController::input() const {
  return input_;
}

void InputController::PressAction(InputAction action) {
  input_.actions |= static_cast<std::uint32_t>(action);
}

void InputController::ReleaseAction(InputAction action) {
  input_.actions &= ~static_cast<std::uint32_t>(action);
}

bool InputController::IsActionHeld(InputAction action) const {
  return (input_.actions & static_cast<std::uint32_t>(action)) != 0;
}

void InputController::ToggleAction(InputAction action) {
  input_.actions ^= static_cast<std::uint32_t>(action);
}

void InputController::ClearAllActions() {
  input_.actions = 0;
}

std::uint32_t InputController::GetActiveActionCount() const {

  std::uint32_t v = input_.actions;
  std::uint32_t count = 0;
  while (v) {
    count += v & 1u;
    v >>= 1;
  }
  return count;
}

bool InputController::IsIdle() const {
  static constexpr std::uint32_t kMovementMask =
      kInputForward | kInputBackward |
      kInputStrafeLeft | kInputStrafeRight |
      kInputTurnLeft | kInputTurnRight |
      kInputAscend | kInputDescend |
      kInputAutoRun;
  return (input_.actions & kMovementMask) == 0;
}

void InputController::UpdatePreviousFrame() {
  prev_actions_ = input_.actions;
}

std::uint32_t InputController::GetNewlyPressed() const {
  return input_.actions & ~prev_actions_;
}

std::uint32_t InputController::GetNewlyReleased() const {
  return prev_actions_ & ~input_.actions;
}

const char* InputController::GetActionName(InputAction action) {
  switch (action) {
    case kInputNone:          return "None";
    case kInputForward:       return "Forward";
    case kInputBackward:      return "Backward";
    case kInputTurnLeft:      return "TurnLeft";
    case kInputTurnRight:     return "TurnRight";
    case kInputStrafeLeft:    return "StrafeLeft";
    case kInputStrafeRight:   return "StrafeRight";
    case kInputJump:          return "Jump";
    case kInputToggleWalkRun: return "ToggleWalkRun";
    case kInputAutoRun:       return "AutoRun";
    case kInputPitchUp:       return "PitchUp";
    case kInputPitchDown:     return "PitchDown";
    case kInputAscend:        return "Ascend";
    case kInputDescend:       return "Descend";
    case kInputTabTarget:     return "TabTarget";
    case kInputClearTarget:   return "ClearTarget";
    default:                  return "Unknown";
  }
}

void InputController::ResetAxes() {
  input_.move_x = 0.0f;
  input_.move_y = 0.0f;
  input_.camera_yaw_delta = 0.0f;
  input_.camera_pitch_delta = 0.0f;
}

void InputController::SetMousePosition(float x, float y) {
  mouse_x_ = x;
  mouse_y_ = y;
}

void InputController::SetMouseButtonDown(bool left, bool right) {
  left_mouse_down_ = left;
  right_mouse_down_ = right;
}

void InputController::Reset() {
  input_ = {};
  prev_actions_ = 0;
  mouse_x_ = 0.0f;
  mouse_y_ = 0.0f;
  left_mouse_down_ = false;
  right_mouse_down_ = false;
}

}

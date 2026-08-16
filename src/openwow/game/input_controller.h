#pragma once

#include <cstdint>

namespace openwow::game {

enum InputAction : std::uint32_t {
  kInputNone         = 0,
  kInputForward      = 1u << 0,
  kInputBackward     = 1u << 1,
  kInputTurnLeft     = 1u << 2,
  kInputTurnRight    = 1u << 3,
  kInputStrafeLeft   = 1u << 4,
  kInputStrafeRight  = 1u << 5,
  kInputJump         = 1u << 6,
  kInputToggleWalkRun = 1u << 7,
  kInputAutoRun      = 1u << 8,
  kInputPitchUp      = 1u << 9,
  kInputPitchDown    = 1u << 10,
  kInputAscend       = 1u << 11,
  kInputDescend      = 1u << 12,
  kInputTabTarget    = 1u << 13,
  kInputClearTarget  = 1u << 14,
};

struct InputState {

  float move_x{0.0f};
  float move_y{0.0f};
  float camera_yaw_delta{0.0f};
  float camera_pitch_delta{0.0f};

  std::uint32_t actions{0};
};

class InputController {
 public:
  void SetInput(const InputState& input);
  [[nodiscard]] const InputState& input() const;

  void PressAction(InputAction action);

  void ReleaseAction(InputAction action);

  [[nodiscard]] bool IsActionHeld(InputAction action) const;

  [[nodiscard]] std::uint32_t actions() const { return input_.actions; }

  void ToggleAction(InputAction action);

  void ClearAllActions();

  [[nodiscard]] std::uint32_t GetActiveActionCount() const;

  [[nodiscard]] bool IsIdle() const;

  void UpdatePreviousFrame();

  [[nodiscard]] std::uint32_t GetNewlyPressed() const;

  [[nodiscard]] std::uint32_t GetNewlyReleased() const;

  [[nodiscard]] static const char* GetActionName(InputAction action);

  void ResetAxes();

  void SetMousePosition(float x, float y);
  [[nodiscard]] float GetMouseX() const { return mouse_x_; }
  [[nodiscard]] float GetMouseY() const { return mouse_y_; }

  void SetMouseButtonDown(bool left, bool right);
  [[nodiscard]] bool IsLeftMouseDown() const { return left_mouse_down_; }
  [[nodiscard]] bool IsRightMouseDown() const { return right_mouse_down_; }

  void Reset();

 private:
  InputState input_{};
  std::uint32_t prev_actions_{0};
  float mouse_x_{0.0f};
  float mouse_y_{0.0f};
  bool left_mouse_down_{false};
  bool right_mouse_down_{false};
};

}

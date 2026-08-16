
#include "openwow/input/input_control.h"

#include "openwow/core/window_manager.h"
#include "openwow/input/input_manager.h"

namespace openwow::input {

namespace {

void SetWorldMouseCaptureState(bool capture) {
  InputManager::Get().SetMouseCapture(capture);
  openwow::core::WindowManager::Instance().SetMouseCapture(capture);
}

}

void OnMouseButtonSet() {
  SetWorldMouseCaptureState(true);
}

void OnMouseButtonClear() {
  SetWorldMouseCaptureState(false);
}

MovementRates ComputeMovementRates(uint32_t flags, bool active) {
    MovementRates rates{};

    if (!active) return rates;

    int fwd = 0;
    if (flags & 0x1000) ++fwd;

    if (flags & 0x010)  ++fwd;

    if ((flags & 0x001) && (flags & 0x002)) ++fwd;

    if (flags & 0x020)  --fwd;

    if (fwd != 0) {
        rates.forwardRate = (fwd > 0) ? 1.0f : -1.0f;
    }

    int strafe = 0;
    if (flags & 0x040) ++strafe;

    bool mouseTurn = (flags & 0x2000001) != 0;
    if (mouseTurn && (flags & 0x100)) ++strafe;

    if (flags & 0x080) --strafe;

    if (mouseTurn && (flags & 0x200)) --strafe;

    if (strafe != 0) {
        rates.strafeRate = (strafe > 0) ? 1.0f : -1.0f;
    }

    int vert = 0;
    if (flags & 0x2000) ++vert;

    if (flags & 0x4000) --vert;

    rates.verticalRate = static_cast<float>(vert);

    int turn = 0;
    if (flags & 0x100) ++turn;

    if (flags & 0x200) --turn;

    if (mouseTurn) turn = 0;

    rates.turnRate = static_cast<float>(turn) * 0.05f;

    return rates;
}

MouseDelta ApplyMouseSensitivity(float dx, float dy, float cameraDistance) {
    float ratio = cameraDistance * 0.81851113f;

    float clamped;
    if (ratio >= 1.0f) {
        clamped = 1.0f;
    } else if (ratio > 0.05f) {
        clamped = ratio;
    } else {
        clamped = 0.05f;
    }

    float scale = 0.05f + clamped * 0.95f;

    return {dx * scale, dy * scale};
}

InWorldMouseMotionDispatch DispatchInWorldMouseMotion(int mouseX,
                                                      int mouseY,
                                                      int rawDeltaX,
                                                      int rawDeltaY,
                                                      bool captureCamera) {
    InputManager::Get().ProcessMouseMove(mouseX, mouseY, rawDeltaX, rawDeltaY);

    if (!captureCamera) {
        return {};
    }

    return {
        .has_camera_delta = true,
        .camera_dx = static_cast<float>(rawDeltaX),
        .camera_dy = static_cast<float>(rawDeltaY),
    };
}

}

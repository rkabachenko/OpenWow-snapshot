
#pragma once

#include <cstdint>

namespace openwow::input {

void OnMouseButtonSet();

void OnMouseButtonClear();

struct MovementRates {
    float forwardRate{0.0f};

    float strafeRate{0.0f};

    float verticalRate{0.0f};

    float turnRate{0.0f};

};

MovementRates ComputeMovementRates(uint32_t flags, bool active);

struct MouseDelta {
    float dx{0.0f};
    float dy{0.0f};
};

MouseDelta ApplyMouseSensitivity(float dx, float dy, float cameraDistance);

struct InWorldMouseMotionDispatch {
    bool  has_camera_delta{false};
    float camera_dx{0.0f};
    float camera_dy{0.0f};
};

InWorldMouseMotionDispatch DispatchInWorldMouseMotion(int mouseX,
                                                      int mouseY,
                                                      int rawDeltaX,
                                                      int rawDeltaY,
                                                      bool captureCamera);

}

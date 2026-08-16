#pragma once

#include <cstdint>

namespace openwow::input {

class MouseLookController {
public:
    MouseLookController() = default;

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    void BeginLook();
    void EndLook();
    [[nodiscard]] bool IsLooking() const;

    void SetMouseDelta(float dx, float dy);

    [[nodiscard]] float GetYawDelta() const;
    [[nodiscard]] float GetPitchDelta() const;

    void  SetSensitivity(float sens);
    [[nodiscard]] float GetSensitivity() const;

    void SetInvertY(bool invert);
    [[nodiscard]] bool IsInvertY() const;

    void  SetMaxPitch(float degrees);
    [[nodiscard]] float GetMaxPitch() const;
    void  SetMinPitch(float degrees);
    [[nodiscard]] float GetMinPitch() const;

    [[nodiscard]] float GetAccumulatedYaw() const;
    [[nodiscard]] float GetAccumulatedPitch() const;
    void ResetAccumulated();

    void  SetSmoothing(float factor);
    [[nodiscard]] float GetSmoothing() const;

    void Update(float dt);

    void Reset();

private:
    bool  enabled_    = true;
    bool  looking_    = false;
    float sensitivity_ = 1.0f;
    bool  invert_y_   = false;
    float max_pitch_  = 88.0f;
    float min_pitch_  = -88.0f;
    float smoothing_  = 0.0f;

    float raw_dx_ = 0.0f;
    float raw_dy_ = 0.0f;

    float yaw_delta_   = 0.0f;
    float pitch_delta_ = 0.0f;

    float accumulated_yaw_   = 0.0f;
    float accumulated_pitch_ = 0.0f;
};

}


#include "openwow/input/mouse_look.h"
#include <algorithm>
#include <cmath>

namespace openwow::input {

void MouseLookController::SetEnabled(bool enabled) { enabled_ = enabled; }
bool MouseLookController::IsEnabled() const { return enabled_; }

void MouseLookController::BeginLook() {
    if (enabled_) looking_ = true;
}

void MouseLookController::EndLook() {
    looking_ = false;
    raw_dx_ = 0.0f;
    raw_dy_ = 0.0f;
}

bool MouseLookController::IsLooking() const { return looking_; }

void MouseLookController::SetMouseDelta(float dx, float dy) {
    if (!enabled_ || !looking_) return;
    raw_dx_ += dx;
    raw_dy_ += dy;
}

float MouseLookController::GetYawDelta() const { return yaw_delta_; }
float MouseLookController::GetPitchDelta() const { return pitch_delta_; }

void  MouseLookController::SetSensitivity(float sens) { sensitivity_ = std::max(0.01f, sens); }
float MouseLookController::GetSensitivity() const { return sensitivity_; }

void MouseLookController::SetInvertY(bool invert) { invert_y_ = invert; }
bool MouseLookController::IsInvertY() const { return invert_y_; }

void  MouseLookController::SetMaxPitch(float degrees) { max_pitch_ = degrees; }
float MouseLookController::GetMaxPitch() const { return max_pitch_; }
void  MouseLookController::SetMinPitch(float degrees) { min_pitch_ = degrees; }
float MouseLookController::GetMinPitch() const { return min_pitch_; }

float MouseLookController::GetAccumulatedYaw() const { return accumulated_yaw_; }
float MouseLookController::GetAccumulatedPitch() const { return accumulated_pitch_; }
void  MouseLookController::ResetAccumulated() {
    accumulated_yaw_   = 0.0f;
    accumulated_pitch_ = 0.0f;
}

void  MouseLookController::SetSmoothing(float factor) { smoothing_ = std::clamp(factor, 0.0f, 1.0f); }
float MouseLookController::GetSmoothing() const { return smoothing_; }

void MouseLookController::Update(float ) {
    if (!enabled_ || !looking_) {
        yaw_delta_   = 0.0f;
        pitch_delta_ = 0.0f;
        raw_dx_      = 0.0f;
        raw_dy_      = 0.0f;
        return;
    }

    float target_yaw   = raw_dx_ * sensitivity_;
    float target_pitch = raw_dy_ * sensitivity_ * (invert_y_ ? -1.0f : 1.0f);

    float alpha = 1.0f - smoothing_;
    yaw_delta_   = yaw_delta_   * smoothing_ + target_yaw   * alpha;
    pitch_delta_ = pitch_delta_ * smoothing_ + target_pitch * alpha;

    accumulated_yaw_  += yaw_delta_;
    accumulated_pitch_ += pitch_delta_;
    accumulated_pitch_ = std::clamp(accumulated_pitch_, min_pitch_, max_pitch_);

    raw_dx_ = 0.0f;
    raw_dy_ = 0.0f;
}

void MouseLookController::Reset() {
    enabled_    = true;
    looking_    = false;
    sensitivity_ = 1.0f;
    invert_y_   = false;
    max_pitch_  = 88.0f;
    min_pitch_  = -88.0f;
    smoothing_  = 0.0f;
    raw_dx_     = 0.0f;
    raw_dy_     = 0.0f;
    yaw_delta_  = 0.0f;
    pitch_delta_ = 0.0f;
    accumulated_yaw_   = 0.0f;
    accumulated_pitch_ = 0.0f;
}

}

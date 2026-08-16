
#include "openwow/world/camera/world_camera.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/math/retail_camera_matrix.h"
#include "openwow/world/collision/collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

namespace openwow::world {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

constexpr float kMaxPitchRad = 1.5533430576324463f;
constexpr float kMinPitchRad = -1.5533430576324463f;
constexpr float kDegreesToRadians = kPi / 180.0f;

constexpr float kCameraTargetFadeEndDistance = 1.8315001726150513f;
constexpr float kCameraTargetHideDistance = 0.0027777778450399637f;
constexpr float kCameraTargetFadePitchThreshold = -1.186823844909668f;
constexpr float kCameraTargetFadePitchOrigin = -1.5707963705062866f;
constexpr float kCameraTargetFadePitchSpan = -0.38397252559661865f;
constexpr float kCameraTargetAlphaOpaque = 255.0f;

constexpr float kCameraCompareEpsilon = 2.384185791015625e-07f;

float CosineEase(const float a, const float b, const float t) {
  return a + (1.0f - std::cos(t * kPi)) * 0.5f * (b - a);
}

constexpr float kCameraCollisionClearance = 0.1111111119389534f;

constexpr float kCameraShakeAmplitudeScale = 0.027777778f;
constexpr float kCameraShakeNearRadius = 9.0f;
constexpr float kCameraShakeMaxRadiusSquared = 6400.0f;
constexpr float kCameraShakeAttenuationBase = 0.69999998807907104f;

float SmoothFactor(float speed, float dt) {

  return 1.0f - std::exp(-speed * dt);
}

float Lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

constexpr float kCameraInterpolationEpsilon = 0.001f;

constexpr float kCameraSmoothingDelayMilliseconds = 1000.0f;

constexpr std::uint32_t kCameraPitchSmoothingBlockedMoveFlags = 0x02200000u;

float RewindAngleNearest(float current, float target) {
  float error = target - current;
  while (error < -kPi) {
    current -= kTwoPi;
    error = target - current;
  }
  while (error > kPi) {
    current += kTwoPi;
    error = target - current;
  }
  return current;
}

constexpr float kCameraSmoothingParameterMinimum = 0.0f;
constexpr float kCameraSmoothingParameterMaximum = 100.0f;

float ClampCameraSmoothingParameter(float value) {
  if (value < kCameraSmoothingParameterMinimum) {
    value = kCameraSmoothingParameterMinimum;
  }
  if (value >= kCameraSmoothingParameterMaximum) {
    value = kCameraSmoothingParameterMaximum - 1.0f;
  }
  return value;
}

constexpr std::array<CameraViewPreset, 8> kShippedViewPresets{{
    {},
    {},
    {5.55f, 0.17453292f, 0.0f},
    {5.55f, 0.34906584f, 0.0f},
    {13.88f, 0.52359879f, 0.0f},
    {13.88f, 0.17453292f, 0.0f},
    {},
    {5.0f, 0.17453292f, 0.0f},
}};

std::int32_t QuantizeScriptedMoveMilliseconds(const float seconds_or_units) {
  return static_cast<std::int32_t>(
      static_cast<std::int64_t>(static_cast<double>(seconds_or_units) * 1000.0));
}

std::uint32_t QuantizeScriptedMoveAmountMilliUnits(const float amount) {
  return static_cast<std::uint32_t>(
      static_cast<std::int64_t>(static_cast<double>(amount) * 1000.0));
}

bool IsFiniteVec3(const std::array<float, 3>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) &&
         std::isfinite(value[2]);
}

float Vec3LengthSquared(const std::array<float, 3>& value) {
  return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
}

bool NormalizeVec3(std::array<float, 3>& value) {
  const float length_squared = Vec3LengthSquared(value);
  if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f) {
    return false;
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  value[0] *= inverse_length;
  value[1] *= inverse_length;
  value[2] *= inverse_length;
  return true;
}

}

std::uint8_t Camera_ComputeBoundUnitAlphaByte(
    const float resolved_distance, const float near_plane, const float pitch,
    const float camera_height) noexcept {

  float fade_end = kCameraTargetFadeEndDistance;
  if (resolved_distance < camera_height &&
      pitch < kCameraTargetFadePitchThreshold) {
    const float span =
        (kCameraTargetFadePitchOrigin - pitch) / kCameraTargetFadePitchSpan;
    if (std::fabs(span) >= kCameraCompareEpsilon) {
      fade_end /= span * span;
    }
  }

  const float eye_clearance = resolved_distance - near_plane;
  if (eye_clearance >= fade_end) {
    return 0xFFu;
  }
  if (eye_clearance <= kCameraTargetHideDistance) {
    return 0u;
  }
  const float t = (eye_clearance - kCameraTargetHideDistance) /
                  (fade_end - kCameraTargetHideDistance);

  const auto eased = static_cast<std::uint8_t>(
      static_cast<int>(CosineEase(0.0f, kCameraTargetAlphaOpaque, t)));
  return eased < 0xFFu ? eased : static_cast<std::uint8_t>(0xFFu);
}

namespace {

bool g_active_player_bound_alpha_visible = true;
}

bool Camera_IsActivePlayerBoundAlphaVisible() noexcept {
  return g_active_player_bound_alpha_visible;
}

bool Camera_SetActivePlayerBoundAlphaVisible(const bool visible) noexcept {
  const bool previous = g_active_player_bound_alpha_visible;
  g_active_player_bound_alpha_visible = visible;
  return previous;
}

bool Camera_ToggleActivePlayerBoundAlphaVisible() noexcept {
  g_active_player_bound_alpha_visible = !g_active_player_bound_alpha_visible;
  return g_active_player_bound_alpha_visible;
}

float ClampCameraFieldOfView(const float fov_radians) noexcept {
  if (std::isnan(fov_radians) ||
      fov_radians <= kCameraMinFieldOfViewRadians) {
    return kCameraMinFieldOfViewRadians;
  }
  return std::min(fov_radians, kCameraMaxFieldOfViewRadians);
}

float AdvanceCameraFieldOfView(const float current_fov_radians,
                               const float target_fov_radians,
                               const float delta_seconds) noexcept {
  float delta = target_fov_radians - current_fov_radians;
  const float max_step =
      ((current_fov_radians - kCameraMinFieldOfViewRadians) * 0.40000004f) /
          2.076942f +
      0.2f;
  delta = std::clamp(delta, -max_step, max_step);

  float velocity = delta;
  if (delta > 0.0f && delta < 1.0f) {
    velocity = std::sqrt(delta);
  } else if (delta < 0.0f && delta > -1.0f) {
    velocity = -std::sqrt(-delta);
  }

  const float candidate = current_fov_radians + delta_seconds * velocity;
  const float next = velocity < 0.0f ? std::max(candidate, target_fov_radians)
                                    : std::min(candidate, target_fov_radians);
  return ClampCameraFieldOfView(next);
}

WorldCamera::WorldCamera() = default;
WorldCamera::~WorldCamera() = default;

void WorldCamera::SetTarget(float x, float y, float z) {
  target_x_ = x;
  target_y_ = y;
  target_z_ = z;
  if (!initial_set_) {
    smooth_x_ = x;
    smooth_y_ = y;
    smooth_z_ = z;
    initial_set_ = true;
  }
}

void WorldCamera::SetYaw(float yaw_radians) {
  orbit_yaw_ = WrapAngle(yaw_radians - ComposedYawBias());
  CancelYawInterpolation();
}

void WorldCamera::SetTargetYaw(float yaw_radians) {
  (void)StartYawInterpolation(WrapAngle(yaw_radians - ComposedYawBias()), 0.0f,
                              1.0f, last_update_ms_);
}

void WorldCamera::SetOrbitYaw(const float orbit_yaw_radians) {

  orbit_yaw_ = WrapAngle(orbit_yaw_radians + ViewPresetYawBias());
  CancelYawInterpolation();
}

void WorldCamera::SetTargetOrbitYaw(const float orbit_yaw_radians) {

  (void)StartYawInterpolation(
      WrapAngle(orbit_yaw_radians + ViewPresetYawBias()), 0.0f, 1.0f,
      last_update_ms_);
}

void WorldCamera::FlipYaw(const float delta_radians) {

  flip_yaw_bias_ = WrapAngle(flip_yaw_bias_ + delta_radians);
}

void WorldCamera::SetReferenceFacing(const float facing_radians) {
  reference_facing_ = WrapAngle(facing_radians);
}

void WorldCamera::ShiftYawFrame(const float delta_radians) {

  target_orbit_yaw_ = WrapAngle(target_orbit_yaw_ + delta_radians);
  yaw_interpolation_source_ = RewindAngleNearest(
      WrapAngle(yaw_interpolation_source_ + delta_radians), target_orbit_yaw_);
  orbit_yaw_ =
      RewindAngleNearest(WrapAngle(orbit_yaw_ + delta_radians),
                         target_orbit_yaw_);
}

void WorldCamera::ShiftPitchFrame(const float delta_radians) {

  target_pitch_ =
      Clamp(target_pitch_ + delta_radians, kMinPitchRad, kMaxPitchRad);
  pitch_interpolation_source_ = RewindAngleNearest(
      Clamp(pitch_interpolation_source_ + delta_radians, kMinPitchRad,
            kMaxPitchRad),
      target_pitch_);
  pitch_ = RewindAngleNearest(
      Clamp(pitch_ + delta_radians, kMinPitchRad, kMaxPitchRad), target_pitch_);
}

void WorldCamera::SetPitch(float pitch_radians) {
  pitch_ = Clamp(pitch_radians, kMinPitchRad, kMaxPitchRad);
  CancelPitchInterpolation();
}

void WorldCamera::SetTargetPitch(float pitch_radians) {
  (void)StartPitchInterpolation(
      Clamp(pitch_radians, kMinPitchRad, kMaxPitchRad), 0.0f, 1.0f,
      last_update_ms_);
}

void WorldCamera::SetDistance(float distance) {
  distance_ = Clamp(distance, min_distance_, max_distance_);
  CancelDistanceInterpolation();
  resolved_distance_ = std::min(resolved_distance_, distance_);
}

void WorldCamera::SetTargetDistance(float distance) {

  (void)StartDistanceInterpolation(Clamp(distance, min_distance_, max_distance_),
                                   0.0f, 1.0f, last_update_ms_);
}

void WorldCamera::CancelDistanceInterpolation() {
  target_distance_ = distance_;
  distance_interpolation_source_ = distance_;
  distance_interpolation_duration_seconds_ = 0.0f;
  distance_interpolation_start_ms_ = 0;
  distance_interpolation_armed_ = false;
}

bool WorldCamera::StartDistanceInterpolation(
    const float target_distance, const float delay_seconds, const float factor,
    const std::uint32_t timestamp_ms) {

  if (distance_interpolation_armed_ &&
      std::fabs(target_distance_ - target_distance) <
          kCameraInterpolationEpsilon &&
      std::fabs(distance_interpolation_delay_seconds_ - delay_seconds) <
          kCameraInterpolationEpsilon &&
      std::fabs(distance_interpolation_factor_ - factor) <
          kCameraInterpolationEpsilon) {
    return true;
  }

  if (std::fabs(distance_ - target_distance) < kCameraInterpolationEpsilon) {
    return false;
  }

  distance_interpolation_delay_seconds_ = delay_seconds;
  distance_interpolation_factor_ = factor;
  distance_interpolation_duration_seconds_ =
      (std::fabs(target_distance - distance_) /
       smoothing_settings_.distance_smooth_speed) *
      factor;
  distance_interpolation_source_ = distance_;
  target_distance_ = target_distance;
  distance_interpolation_start_ms_ =
      timestamp_ms +
      static_cast<std::uint32_t>(static_cast<std::int32_t>(
          delay_seconds * kCameraSmoothingDelayMilliseconds));
  distance_interpolation_armed_ = true;
  return true;
}

void WorldCamera::HandleMouseDelta(float dx, float dy) {

  if (taxi_flight_input_suppressed_) {
    return;
  }

  ShiftYawFrame(dx);
  ShiftPitchFrame(dy);
}

void WorldCamera::AdjustPitch(float delta_radians) {
  ShiftPitchFrame(delta_radians);
}

void WorldCamera::PushYawOffset() {

  ++yaw_offset_depth_;
  if (yaw_offset_depth_ == 1) {
    ShiftYawFrame(reference_facing_);
  }
}

void WorldCamera::PopYawOffset() {
  if (yaw_offset_depth_ == 0) {
    return;
  }

  --yaw_offset_depth_;
  if (yaw_offset_depth_ == 0) {
    ShiftYawFrame(-reference_facing_);
  }
}

void WorldCamera::EnterFreelook() {
  if (freelook_) {
    return;
  }

  freelook_ = true;
  PushYawOffset();
  ++freelook_pitch_depth_;

  CancelYawInterpolation();
  CancelPitchInterpolation();
}

void WorldCamera::ExitFreelook() {
  if (!freelook_) {
    return;
  }

  freelook_ = false;
  PopYawOffset();
  if (freelook_pitch_depth_ != 0) {
    --freelook_pitch_depth_;
  }

  CancelYawInterpolation();
  CancelPitchInterpolation();
}

void WorldCamera::HandleScrollDelta(float delta) {
  SetTargetDistance(target_distance_ - delta * zoom_speed_);
}

void WorldCamera::StartScriptedMoveChannel(const ScriptedCameraMoveChannel channel,
                                           const std::uint32_t timestamp, const float rate_scale) {
  if (scripted_move_blocked_) {
    return;
  }

  auto &state = scripted_move_channels_[static_cast<std::size_t>(channel)];
  if (!state.active) {
    state.active = true;
    state.last_tick = timestamp;
  }
  state.stopping = false;
  state.stop_tick = 0;
  state.queued_stop_tick = 0;
  state.rate_scale = rate_scale;
}

void WorldCamera::StopScriptedMoveChannel(const ScriptedCameraMoveChannel channel,
                                          const std::uint32_t timestamp) {
  auto &state = scripted_move_channels_[static_cast<std::size_t>(channel)];
  if (!state.active) {
    return;
  }
  state.stop_tick = 0;
  state.stopping = false;
  state.queued_stop_tick = timestamp;
}

void WorldCamera::QueueScriptedZoomStep(const bool zoom_in, const std::uint32_t timestamp,
                                        const float amount, const float duration_seconds) {
  if (scripted_move_blocked_) {
    return;
  }

  const auto channel = zoom_in ? ScriptedCameraMoveChannel::kZoomIn
                               : ScriptedCameraMoveChannel::kZoomOut;
  const auto opposite_channel = zoom_in ? ScriptedCameraMoveChannel::kZoomOut
                                        : ScriptedCameraMoveChannel::kZoomIn;

  const float distance_speed = motion_settings_.distance_speed;
  float rate_scale = 1.0f;
  std::int32_t duration_ms = 0;
  if (duration_seconds > 0.0f) {
    duration_ms = QuantizeScriptedMoveMilliseconds(duration_seconds);
    rate_scale = static_cast<float>(
        static_cast<double>(QuantizeScriptedMoveAmountMilliUnits(amount)) /
        (1000.0 * static_cast<double>(duration_seconds * distance_speed)));
  } else {
    duration_ms = QuantizeScriptedMoveMilliseconds(amount / distance_speed);
  }

  auto &opposite_state = scripted_move_channels_[static_cast<std::size_t>(opposite_channel)];
  if (opposite_state.active) {
    opposite_state.stop_tick = timestamp;
    opposite_state.queued_stop_tick = 0;
    opposite_state.stopping = true;
  }

  auto &state = scripted_move_channels_[static_cast<std::size_t>(channel)];
  if (state.active && state.queued_stop_tick != 0u) {
    state.queued_stop_tick += static_cast<std::uint32_t>(duration_ms);
    return;
  }

  if (!state.active) {
    state.active = true;
    state.last_tick = timestamp;
  }

  state.rate_scale = rate_scale;
  if (duration_ms != 0) {
    state.queued_stop_tick = timestamp + static_cast<std::uint32_t>(duration_ms);
  } else {
    state.queued_stop_tick = 0;
  }
  state.stop_tick = 0;
  state.stopping = false;
}

bool WorldCamera::ApplyScriptedMoveInputs(const std::uint32_t timestamp) {
  bool changed = false;
  for (std::size_t index = 0; index < kScriptedMoveChannelCount; ++index) {
    auto &state = scripted_move_channels_[index];
    const auto elapsed = ConsumeScriptedMoveElapsed(state, timestamp);
    if (elapsed == 0) continue;
    const float seconds =
        static_cast<float>(elapsed) * 0.001f * state.rate_scale;
    switch (static_cast<ScriptedCameraMoveChannel>(index)) {

      case ScriptedCameraMoveChannel::kZoomIn:
        SetDistance(Clamp(distance_ - motion_settings_.distance_speed * seconds,
                          motion_settings_.minimum_distance,
                          motion_settings_.maximum_distance));
        break;
      case ScriptedCameraMoveChannel::kZoomOut:
        SetDistance(Clamp(distance_ + motion_settings_.distance_speed * seconds,
                          motion_settings_.minimum_distance,
                          motion_settings_.maximum_distance));
        break;

      case ScriptedCameraMoveChannel::kYawRight:
        SetOrbitYaw(WrapAngle(orbit_yaw_ +
                              motion_settings_.yaw_speed_degrees * seconds *
                                  kDegreesToRadians) -
                    ViewPresetYawBias());
        break;
      case ScriptedCameraMoveChannel::kYawLeft:
        SetOrbitYaw(WrapAngle(orbit_yaw_ -
                              motion_settings_.yaw_speed_degrees * seconds *
                                  kDegreesToRadians) -
                    ViewPresetYawBias());
        break;
      case ScriptedCameraMoveChannel::kPitchUp:
        SetPitch(pitch_ + motion_settings_.pitch_speed_degrees * seconds *
                              kDegreesToRadians);
        break;
      case ScriptedCameraMoveChannel::kPitchDown:
        SetPitch(pitch_ - motion_settings_.pitch_speed_degrees * seconds *
                              kDegreesToRadians);
        break;
    }
    changed = true;
  }
  return changed;
}

std::uint32_t WorldCamera::ConsumeScriptedMoveElapsed(
    ScriptedMoveChannelState& state, const std::uint32_t current_tick) {
  if (!state.active) return 0;
  if (state.queued_stop_tick != 0 &&
      static_cast<std::int32_t>(current_tick - state.queued_stop_tick) >= 0) {
    state.stop_tick = state.queued_stop_tick;
    state.stopping = true;
  }
  if (state.stopping) {
    if (static_cast<std::int32_t>(current_tick - state.stop_tick) < 0) {
      return 0;
    }
    const auto elapsed = state.stop_tick >= state.last_tick
                             ? state.stop_tick - state.last_tick
                             : 0;
    state = {};
    return elapsed;
  }
  if (static_cast<std::int32_t>(current_tick - state.last_tick) < 0) {
    return 0;
  }
  const auto elapsed = current_tick - state.last_tick;
  state.last_tick = current_tick;
  return elapsed;
}

void WorldCamera::ResetScriptedMoveInputs() {
  for (auto &state : scripted_move_channels_) {
    state = {};
  }
}

void WorldCamera::SetScriptedMoveBlocked(const bool blocked) {
  scripted_move_blocked_ = blocked;
}

void WorldCamera::SetSmoothingSettings(CameraSmoothingSettings settings) {
  smoothing_settings_ = std::move(settings);
}

void WorldCamera::SetActiveView(const int view) {
  active_view_ = std::clamp(view, 0, 7);
}

void WorldCamera::CancelYawInterpolation() {
  target_orbit_yaw_ = orbit_yaw_;
  yaw_interpolation_source_ = orbit_yaw_;
  yaw_interpolation_duration_seconds_ = 0.0f;
  yaw_interpolation_start_ms_ = 0;
  yaw_interpolation_armed_ = false;
}

void WorldCamera::CancelPitchInterpolation() {
  target_pitch_ = pitch_;
  pitch_interpolation_source_ = pitch_;
  pitch_interpolation_duration_seconds_ = 0.0f;
  pitch_interpolation_start_ms_ = 0;
  pitch_interpolation_armed_ = false;
}

bool WorldCamera::StartYawInterpolation(const float target_orbit_yaw,
                                        const float delay_seconds,
                                        const float factor,
                                        const std::uint32_t timestamp_ms) {

  orbit_yaw_ = RewindAngleNearest(orbit_yaw_, target_orbit_yaw);

  if (yaw_interpolation_armed_ &&
      std::fabs(target_orbit_yaw_ - target_orbit_yaw) <
          kCameraInterpolationEpsilon &&
      std::fabs(yaw_interpolation_delay_seconds_ - delay_seconds) <
          kCameraInterpolationEpsilon &&
      std::fabs(yaw_interpolation_factor_ - factor) <
          kCameraInterpolationEpsilon) {
    return true;
  }

  if (std::fabs(orbit_yaw_ - target_orbit_yaw) < kCameraInterpolationEpsilon) {
    return false;
  }

  yaw_interpolation_delay_seconds_ = delay_seconds;
  yaw_interpolation_factor_ = factor;

  const float error_magnitude = std::fabs(target_orbit_yaw - orbit_yaw_);
  orbit_yaw_ = RewindAngleNearest(orbit_yaw_, target_orbit_yaw);
  if (std::fabs(orbit_yaw_ - target_orbit_yaw) < kCameraInterpolationEpsilon) {
    return false;
  }

  yaw_interpolation_source_ = orbit_yaw_;
  target_orbit_yaw_ = target_orbit_yaw;
  yaw_interpolation_duration_seconds_ =
      (error_magnitude / (smoothing_settings_.yaw_smooth_speed_degrees *
                          kDegreesToRadians)) *
      factor;
  yaw_interpolation_start_ms_ =
      timestamp_ms +
      static_cast<std::uint32_t>(static_cast<std::int32_t>(
          delay_seconds * kCameraSmoothingDelayMilliseconds));
  yaw_interpolation_armed_ = true;
  return true;
}

bool WorldCamera::StartPitchInterpolation(const float target_pitch,
                                          const float delay_seconds,
                                          const float factor,
                                          const std::uint32_t timestamp_ms) {

  pitch_ = RewindAngleNearest(pitch_, target_pitch);

  if (pitch_interpolation_armed_ &&
      std::fabs(target_pitch_ - target_pitch) < kCameraInterpolationEpsilon &&
      std::fabs(pitch_interpolation_delay_seconds_ - delay_seconds) <
          kCameraInterpolationEpsilon &&
      std::fabs(pitch_interpolation_factor_ - factor) <
          kCameraInterpolationEpsilon) {
    return true;
  }

  if (std::fabs(pitch_ - target_pitch) < kCameraInterpolationEpsilon) {
    return false;
  }

  pitch_interpolation_delay_seconds_ = delay_seconds;
  pitch_interpolation_factor_ = factor;
  const float error_magnitude = std::fabs(target_pitch - pitch_);
  pitch_ = RewindAngleNearest(pitch_, target_pitch);
  if (std::fabs(pitch_ - target_pitch) < kCameraInterpolationEpsilon) {
    return false;
  }

  pitch_interpolation_source_ = pitch_;
  target_pitch_ = target_pitch;
  pitch_interpolation_duration_seconds_ =
      (error_magnitude / (smoothing_settings_.pitch_smooth_speed_degrees *
                          kDegreesToRadians)) *
      factor;
  pitch_interpolation_start_ms_ =
      timestamp_ms +
      static_cast<std::uint32_t>(static_cast<std::int32_t>(
          delay_seconds * kCameraSmoothingDelayMilliseconds));
  pitch_interpolation_armed_ = true;
  return true;
}

void WorldCamera::AdvanceSmoothingInterpolations(
    const std::uint32_t timestamp_ms) {

  if (std::fabs(target_distance_ - distance_) < kCameraCompareEpsilon) {
    target_distance_ = distance_;
    distance_interpolation_start_ms_ = 0;
    distance_interpolation_duration_seconds_ = 0.0f;
    distance_interpolation_armed_ = false;
  } else if (distance_interpolation_armed_ &&
             static_cast<std::int32_t>(timestamp_ms -
                                       distance_interpolation_start_ms_) >= 0) {
    const float progress =
        (static_cast<float>(timestamp_ms - distance_interpolation_start_ms_) /
         kCameraSmoothingDelayMilliseconds) /
        distance_interpolation_duration_seconds_;
    distance_ = progress < 1.0f
                    ? CosineEase(distance_interpolation_source_,
                                 target_distance_, progress)
                    : target_distance_;
  }

  if (std::fabs(target_pitch_ - pitch_) < kCameraCompareEpsilon) {
    target_pitch_ = pitch_;
    pitch_interpolation_start_ms_ = 0;
    pitch_interpolation_duration_seconds_ = 0.0f;
    pitch_interpolation_armed_ = false;
  } else if (pitch_interpolation_armed_ &&
             static_cast<std::int32_t>(timestamp_ms -
                                       pitch_interpolation_start_ms_) >= 0) {
    const float progress =
        (static_cast<float>(timestamp_ms - pitch_interpolation_start_ms_) /
         kCameraSmoothingDelayMilliseconds) /
        pitch_interpolation_duration_seconds_;
    pitch_ = progress < 1.0f
                 ? CosineEase(pitch_interpolation_source_, target_pitch_,
                              progress)
                 : target_pitch_;
  }

  if (!freelook_) {
    if (std::fabs(target_orbit_yaw_ - orbit_yaw_) < kCameraCompareEpsilon) {
      target_orbit_yaw_ = orbit_yaw_;
      yaw_interpolation_start_ms_ = 0;
      yaw_interpolation_duration_seconds_ = 0.0f;
      yaw_interpolation_armed_ = false;
    } else if (yaw_interpolation_armed_ &&
               static_cast<std::int32_t>(timestamp_ms -
                                         yaw_interpolation_start_ms_) >= 0) {
      const float progress =
          (static_cast<float>(timestamp_ms - yaw_interpolation_start_ms_) /
           kCameraSmoothingDelayMilliseconds) /
          yaw_interpolation_duration_seconds_;
      orbit_yaw_ = progress < 1.0f
                       ? CosineEase(yaw_interpolation_source_,
                                    target_orbit_yaw_, progress)
                       : target_orbit_yaw_;
    }
  }

  pitch_ = Clamp(pitch_, kMinPitchRad, kMaxPitchRad);
  orbit_yaw_ = WrapAngle(orbit_yaw_);
}

bool WorldCamera::ActiveViewPresetAllowsSmoothing() const {
  const auto index = static_cast<std::size_t>(std::clamp(active_view_, 0, 7));
  const CameraViewPreset& preset = view_presets_[index];
  const CameraViewPreset& shipped = kShippedViewPresets[index];
  return std::fabs(preset.distance - shipped.distance) <
             kCameraInterpolationEpsilon &&
         std::fabs(preset.pitch_radians - shipped.pitch_radians) <
             kCameraInterpolationEpsilon &&
         std::fabs(preset.relative_yaw_radians -
                   shipped.relative_yaw_radians) < kCameraInterpolationEpsilon;
}

std::uint32_t WorldCamera::BuildSmoothingEventCandidates(
    const std::uint32_t control_flags, const bool stop_event) const {

  std::uint32_t candidates = 0;

  if (tracking_camera_active_) {
    if (tracking_camera_follows_unit_ && !auto_interact_enabled_) {
      candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kMove);
    } else {
      candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kTrack);
    }
  }

  if (((control_flags & kControlMaskTurnKeys) != 0 &&
       (control_flags & kControlMaskMouseSteer) == 0) ||
      (control_flags & kControlMaskMouseSteer) != 0) {
    candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kTurn);
  }
  if ((control_flags & kControlMaskStrafeKeys) != 0 ||
      ((control_flags & kControlMaskMouseSteer) != 0 &&
       (control_flags & kControlMaskTurnKeys) != 0)) {
    candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kStrafe);
  }
  if ((control_flags & kControlMaskForwardBackwardAutoRun) != 0 ||
      ((control_flags & kControlTurnOrActionButton) != 0 &&
       (control_flags & kControlCameraOrSelectButton) != 0)) {
    candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kMove);
  }
  if (stop_event) {
    candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kStop);
  }
  if ((control_flags & kControlMaskForwardBackwardAutoRun) == 0 &&
      (control_flags & kControlMaskStrafeKeys) == 0 &&
      (control_flags & kControlMaskTurnKeys) == 0 &&
      (control_flags & kControlMaskVehicleSeats) == 0) {
    candidates |= 1u << static_cast<unsigned>(CameraSmoothingEvent::kIdle);
  }

  return candidates;
}

void WorldCamera::SetTrackingCamera(const bool click_to_move_active,
                                    const bool follows_unit,
                                    const std::uint32_t control_flags,
                                    const std::uint32_t timestamp_ms) {

  tracking_camera_active_ = click_to_move_active;
  tracking_camera_follows_unit_ = follows_unit;

  const bool hold_world_frame = tracking_camera_active_ && !follows_unit;
  if (hold_world_frame) {
    if (!tracking_camera_holds_world_frame_) {
      PushYawOffset();
      tracking_camera_holds_world_frame_ = true;
    }
  } else if (tracking_camera_holds_world_frame_) {
    PopYawOffset();
    tracking_camera_holds_world_frame_ = false;
  }

  ApplyControlFlagSmoothing(control_flags, false, timestamp_ms);
}

void WorldCamera::ApplyControlFlagSmoothing(const std::uint32_t control_flags,
                                            const bool stop_event,
                                            const std::uint32_t timestamp_ms) {
  const std::uint32_t candidates =
      BuildSmoothingEventCandidates(control_flags, stop_event);
  if (candidates == 0u) {
    return;
  }

  if (!smoothing_settings_.custom_view_smoothing &&
      !ActiveViewPresetAllowsSmoothing()) {
    return;
  }

  constexpr std::uint32_t kTrackingStyleCandidates =
      (1u << static_cast<unsigned>(CameraSmoothingEvent::kTrack)) |
      (1u << static_cast<unsigned>(CameraSmoothingEvent::kFear));
  const bool use_tracking_style =
      (candidates & kTrackingStyleCandidates) != 0u;

  std::size_t winning_event = 0;
  for (std::size_t bit = kCameraSmoothingEventCount; bit-- > 0;) {
    if ((candidates & (1u << bit)) != 0u) {
      winning_event = bit;
      break;
    }
  }

  const auto& event_row = use_tracking_style
                              ? smoothing_settings_.tracking_events[winning_event]
                              : smoothing_settings_.events[winning_event];
  const float event_delay =
      ClampCameraSmoothingParameter(event_row.delay_seconds);
  const float event_factor = ClampCameraSmoothingParameter(event_row.factor);

  const auto axis_parameters =
      [&](const std::size_t axis) -> CameraSmoothingDelayFactor {
    const auto& row = use_tracking_style
                          ? smoothing_settings_.tracking_view_data[axis]
                          : smoothing_settings_.view_data[axis];
    return {ClampCameraSmoothingParameter(row.delay_seconds + event_delay),
            ClampCameraSmoothingParameter(row.factor * event_factor)};
  };
  const CameraSmoothingDelayFactor pitch_parameters =
      axis_parameters(kCameraSmoothingAxisPitch);
  const CameraSmoothingDelayFactor yaw_parameters =
      axis_parameters(kCameraSmoothingAxisYaw);

  const CameraViewPreset& preset =
      view_presets_[static_cast<std::size_t>(std::clamp(active_view_, 0, 7))];

  float longest_duration_seconds = 0.0f;

  bool pitch_started = false;
  if (ShouldSmoothPitch()) {
    if (pitch_parameters.factor == 0.0f) {
      CancelPitchInterpolation();
    } else {
      float target = pitch_ >= smoothing_settings_.pitch_smooth_min_radians
                         ? preset.pitch_radians
                         : smoothing_settings_.pitch_smooth_min_radians;
      if (pitch_ > smoothing_settings_.pitch_smooth_max_radians) {
        target = smoothing_settings_.pitch_smooth_max_radians;
      }
      pitch_started = StartPitchInterpolation(
          target, pitch_parameters.delay_seconds, pitch_parameters.factor,
          timestamp_ms);
      if (pitch_started && pitch_interpolation_duration_seconds_ >= 0.0f) {
        longest_duration_seconds = pitch_interpolation_duration_seconds_;
      }
    }
  }

  bool yaw_started = false;
  if (ShouldSmoothYaw()) {
    if (yaw_parameters.factor == 0.0f) {
      CancelYawInterpolation();
    } else {
      float target = 0.0f;
      if (yaw_offset_depth_ == 0u) {
        target = orbit_yaw_ >= smoothing_settings_.yaw_smooth_min_radians
                     ? preset.relative_yaw_radians
                     : smoothing_settings_.yaw_smooth_min_radians;
        if (orbit_yaw_ > smoothing_settings_.yaw_smooth_max_radians) {
          target = smoothing_settings_.yaw_smooth_max_radians;
        }
      } else {

        target = WrapAngle(reference_facing_ + preset.relative_yaw_radians);
      }
      yaw_started =
          StartYawInterpolation(target, yaw_parameters.delay_seconds,
                                yaw_parameters.factor, timestamp_ms);
      if (yaw_started &&
          longest_duration_seconds <= yaw_interpolation_duration_seconds_) {
        longest_duration_seconds = yaw_interpolation_duration_seconds_;
      }
    }
  }

  float duration = smoothing_settings_.smooth_time_min_seconds;
  if (duration <= longest_duration_seconds) {
    duration = longest_duration_seconds;
  }
  if (duration > smoothing_settings_.smooth_time_max_seconds) {
    duration = smoothing_settings_.smooth_time_max_seconds;
  }
  if (yaw_started) {
    yaw_interpolation_duration_seconds_ = duration;
  }
  if (pitch_started) {
    pitch_interpolation_duration_seconds_ = duration;
  }
}

bool WorldCamera::ShouldSmoothYaw() const {

  if (bound_object_ == 0u || freelook_ ||
      !smoothing_settings_.smooth_yaw_enabled) {
    return false;
  }
  if (yaw_offset_depth_ == 0u) {
    return orbit_yaw_ < smoothing_settings_.yaw_smooth_min_radians ||
           orbit_yaw_ > smoothing_settings_.yaw_smooth_max_radians;
  }
  return std::fabs(orbit_yaw_ - reference_facing_) >= kCameraCompareEpsilon;
}

bool WorldCamera::ShouldSmoothPitch() const {

  if (bound_object_ == 0u || freelook_ ||
      !smoothing_settings_.smooth_pitch_enabled) {
    return false;
  }
  if ((bound_unit_movement_flags_ & kCameraPitchSmoothingBlockedMoveFlags) !=
      0u) {
    return false;
  }
  return pitch_ < smoothing_settings_.pitch_smooth_min_radians ||
         pitch_ > smoothing_settings_.pitch_smooth_max_radians;
}

void WorldCamera::SetMotionSettings(CameraMotionSettings settings) {
  settings.minimum_distance = std::max(0.0f, settings.minimum_distance);
  settings.maximum_distance =
      Clamp(settings.maximum_distance, settings.minimum_distance, 50.0f);
  settings.distance_speed = std::max(0.0f, settings.distance_speed);
  settings.yaw_speed_degrees = std::max(0.0f, settings.yaw_speed_degrees);
  settings.pitch_speed_degrees =
      std::max(0.0f, settings.pitch_speed_degrees);
  motion_settings_ = settings;
}

void WorldCamera::Update(float dt_seconds, const std::uint32_t timestamp_ms) {
  last_update_ms_ = timestamp_ms;
  if (dt_seconds <= 0.0f)
    return;

  if (smoothing_) {
    const float tf = SmoothFactor(kTargetSmoothSpeed, dt_seconds);
    smooth_x_ = Lerp(smooth_x_, target_x_, tf);
    smooth_y_ = Lerp(smooth_y_, target_y_, tf);
    smooth_z_ = Lerp(smooth_z_, target_z_, tf);

    AdvanceSmoothingInterpolations(timestamp_ms);
  } else {
    smooth_x_ = target_x_;
    smooth_y_ = target_y_;
    smooth_z_ = target_z_;
    distance_ = target_distance_;
    orbit_yaw_ = target_orbit_yaw_;
    pitch_ = target_pitch_;
    yaw_interpolation_armed_ = false;
    pitch_interpolation_armed_ = false;
    distance_interpolation_armed_ = false;
  }

  UpdateCollision();
  UpdateCameraTargetAlpha();

  for (auto& shake : camera_shakes_) {
    shake.elapsed += dt_seconds;
  }
  std::erase_if(camera_shakes_, [](const ActiveCameraShake& shake) {
    return shake.duration <= 0.0f || shake.elapsed + shake.phase >= shake.duration;
  });
}

void WorldCamera::TriggerSpellEffectCameraShakes(
    const std::uint32_t effect_id,
    const std::array<float, 3>& origin) {
  if (effect_id == 0 || dbc_ == nullptr || !IsFiniteVec3(origin)) {
    return;
  }

  const auto* effect =
      dbc_->spell_effect_camera_shakes().LookupEntry(effect_id);
  if (effect == nullptr) {
    return;
  }

  for (const std::uint32_t shake_id : effect->camera_shake) {
    TriggerCameraShake(shake_id, origin);
  }
}

void WorldCamera::TriggerCameraShake(
    const std::uint32_t shake_id, const std::array<float, 3>& origin) {
  if (shake_id == 0u || dbc_ == nullptr || !IsFiniteVec3(origin)) {
    return;
  }
  const auto* shake = dbc_->camera_shakes().LookupEntry(shake_id);
  if (shake == nullptr || shake->direction >= 3u || shake->duration <= 0.0f) {
    return;
  }
  camera_shakes_.push_back({
      .origin = origin,
      .shake_type = shake->shake_type,
      .direction = shake->direction,
      .amplitude = shake->amplitude * kCameraShakeAmplitudeScale,
      .frequency = shake->frequency,
      .duration = shake->duration,
      .elapsed = 0.0f,
      .phase = shake->phase,
      .coefficient = shake->coefficient,
  });
}

void WorldCamera::ResetForWorldEntry(const float x, const float y,
                                     const float z,
                                     const float facing_radians,
                                     const int requested_view) {
  const auto& preset =
      view_presets_[static_cast<std::size_t>(
          std::clamp(requested_view, 0, 7))];
  const float distance =
      Clamp(preset.distance, min_distance_, max_distance_);
  const float pitch =
      Clamp(preset.pitch_radians, kMinPitchRad, kMaxPitchRad);
  const float relative_yaw = preset.relative_yaw_radians;

  target_x_ = smooth_x_ = x;
  target_y_ = smooth_y_ = y;
  target_z_ = smooth_z_ = z;
  initial_set_ = true;
  active_view_ = std::clamp(requested_view, 0, 7);
  distance_ = resolved_distance_ = distance;
  CancelDistanceInterpolation();
  pitch_ = pitch;

  reference_facing_ = WrapAngle(facing_radians);
  bound_unit_movement_flags_ = 0;
  orbit_yaw_ = WrapAngle(relative_yaw);

  CancelYawInterpolation();
  CancelPitchInterpolation();
  fov_ = kPi * 0.5f;
  freelook_ = false;
  yaw_offset_depth_ = 0;
  freelook_pitch_depth_ = 0;
  ResetScriptedMoveInputs();
  scripted_move_blocked_ = false;
  camera_shakes_.clear();
  frame_pose_ = {};
}

void WorldCamera::SetViewPreset(const int requested_view,
                                const CameraViewPreset preset) {
  view_presets_[static_cast<std::size_t>(
      std::clamp(requested_view, 0, 7))] = preset;
}

void WorldCamera::ApplyViewPreset(const int requested_view, const bool snap) {
  const int view = std::clamp(requested_view, 0, 7);

  active_view_ = view;
  const auto& preset = view_presets_[static_cast<std::size_t>(view)];
  const float distance =
      Clamp(preset.distance, min_distance_, max_distance_);
  const float pitch =
      Clamp(preset.pitch_radians, kMinPitchRad, kMaxPitchRad);

  const float relative_yaw = preset.relative_yaw_radians;
  SetTargetDistance(distance);
  SetTargetPitch(pitch);
  SetTargetOrbitYaw(relative_yaw);
  if (snap) {
    SetDistance(distance);
    SetPitch(pitch);
    SetOrbitYaw(relative_yaw);
  }
}

CameraFramePose WorldCamera::BuildOrbitFramePose(
    const CameraFrameContext& frame) const {
  CameraFramePose pose{};
  pose.source = CameraPoseSource::kOrbit;
  const float world_yaw = yaw();
  ComputeEyePosition(pose.position[0], pose.position[1], pose.position[2]);
  pose.target = {smooth_x_, smooth_y_, smooth_z_ + height_offset_};
  pose.forward = {
      pose.target[0] - pose.position[0],
      pose.target[1] - pose.position[1],
      pose.target[2] - pose.position[2],
  };
  if (!NormalizeVec3(pose.forward)) {
    pose.forward = {std::cos(world_yaw), std::sin(world_yaw), 0.0f};
  }
  pose.up = {0.0f, 0.0f, 1.0f};
  pose.vertical_fov_radians =
      ProjectionFieldOfViewRadians(fov_);
  pose.desired_arm_distance = distance_;
  pose.resolved_arm_distance = resolved_distance_;
  pose.pitch_radians = pitch_;
  pose.yaw_radians = world_yaw;
  openwow::math::BuildRetailCameraViewMatrix(
      pose.position.data(), pose.target.data(), pose.up.data(), pose.view.data());
  openwow::math::BuildRetailCameraProjectionMatrix(
      pose.vertical_fov_radians, frame.aspect_ratio, frame.near_plane,
      frame.far_plane, pose.projection.data());
  return pose;
}

void WorldCamera::ApplyCameraShakes(CameraFramePose& pose) const {
  std::array<const ActiveCameraShake*, 3> strongest{};
  std::array<float, 3> strongest_amplitudes{};

  for (const auto& shake : camera_shakes_) {
    const float dx = shake.origin[0] - pose.position[0];
    const float dy = shake.origin[1] - pose.position[1];
    const float dz = shake.origin[2] - pose.position[2];
    const float distance_squared = dx * dx + dy * dy + dz * dz;
    if (distance_squared > kCameraShakeMaxRadiusSquared) {
      continue;
    }

    float amplitude = shake.amplitude;
    if (distance_squared >
        kCameraShakeNearRadius * kCameraShakeNearRadius) {
      const float distance = std::sqrt(distance_squared);
      amplitude *= std::pow(
          kCameraShakeAttenuationBase,
          (distance - kCameraShakeNearRadius) / kCameraShakeNearRadius);
    }

    const auto axis = static_cast<std::size_t>(shake.direction);
    if (amplitude > strongest_amplitudes[axis]) {
      strongest_amplitudes[axis] = amplitude;
      strongest[axis] = &shake;
    }
  }

  for (std::size_t axis = 0; axis < strongest.size(); ++axis) {
    const auto* shake = strongest[axis];
    if (shake == nullptr) {
      continue;
    }
    const float time = shake->elapsed + shake->phase;
    float displacement =
        std::sin(shake->frequency * time * kTwoPi) *
        strongest_amplitudes[axis];
    if (shake->shake_type == 1u) {
      displacement *= std::exp(-time * shake->coefficient);
    }

    if (axis == 2u) {
      pose.position[2] += displacement;
    } else {
      const float angle = pose.yaw_radians +
                          (axis == 1u ? kPi * 0.5f : 0.0f);
      pose.position[0] += std::cos(angle) * displacement;
      pose.position[1] += std::sin(angle) * displacement;
    }
  }
}

void WorldCamera::PublishFramePose(CameraFramePose pose,
                                   const CameraFrameContext& frame) {
  if (!NormalizeVec3(pose.forward)) {
    return;
  }
  if (!NormalizeVec3(pose.up)) {
    pose.up = {0.0f, 0.0f, 1.0f};
  }
  std::array<float, 3> target_direction{
      pose.target[0] - pose.position[0],
      pose.target[1] - pose.position[1],
      pose.target[2] - pose.position[2],
  };
  if (IsFiniteVec3(pose.target) && NormalizeVec3(target_direction)) {
    pose.forward = target_direction;
  } else {
    pose.target = {
        pose.position[0] + pose.forward[0],
        pose.position[1] + pose.forward[1],
        pose.position[2] + pose.forward[2],
    };
  }
  openwow::math::BuildRetailCameraViewMatrix(
      pose.position.data(), pose.target.data(), pose.up.data(), pose.view.data());
  openwow::math::BuildRetailCameraProjectionMatrix(
      pose.vertical_fov_radians, frame.aspect_ratio, frame.near_plane,
      frame.far_plane, pose.projection.data());
  pose.generation = ++frame_pose_generation_;
  frame_pose_ = std::move(pose);
}

const CameraFramePose& WorldCamera::ResolveFramePose(
    const CameraFrameContext& frame,
    const std::span<const CameraPoseOverride> overrides) {
  last_frame_aspect_ratio_ = frame.aspect_ratio;
  last_frame_far_plane_ = frame.far_plane;
  CameraFramePose pose = BuildOrbitFramePose(frame);

  for (const CameraPoseOverride& camera_override : overrides) {
    if (!IsFiniteVec3(camera_override.position) ||
        !IsFiniteVec3(camera_override.forward) ||
        !IsFiniteVec3(camera_override.up) ||
        Vec3LengthSquared(camera_override.forward) <= 1.0e-8f) {
      continue;
    }

    pose.source = camera_override.source;
    pose.position = camera_override.position;
    pose.forward = camera_override.forward;
    pose.up = camera_override.up;
    pose.target = camera_override.target.has_value() &&
                          IsFiniteVec3(*camera_override.target)
                      ? *camera_override.target
                      : std::array<float, 3>{
                            pose.position[0] + pose.forward[0],
                            pose.position[1] + pose.forward[1],
                            pose.position[2] + pose.forward[2],
                        };
    if (camera_override.vertical_fov_radians > 0.0f &&
        camera_override.vertical_fov_radians < kPi) {
      pose.vertical_fov_radians =
          camera_override.vertical_fov_radians;
    }
    break;
  }

  ApplyCameraShakes(pose);
  PublishFramePose(std::move(pose), frame);
  return frame_pose_;
}

void WorldCamera::SetViewportSize(const float width, const float height) {
  if (width > 0.0f && height > 0.0f) {
    viewport_height_over_width_ = height / width;
  }
}

void WorldCamera::UpdateCollision() {
  float desired_eye_x = 0.0f;
  float desired_eye_y = 0.0f;
  float desired_eye_z = 0.0f;
  ComputeEyePositionAtDistance(distance_, desired_eye_x, desired_eye_y,
                               desired_eye_z);

  const std::array<float, 3> target{
      smooth_x_, smooth_y_, smooth_z_ + height_offset_};
  const std::array<float, 3> desired_eye{
      desired_eye_x, desired_eye_y, desired_eye_z};
  std::optional<float> hit_distance;
  const float dx = desired_eye[0] - target[0];
  const float dy = desired_eye[1] - target[1];
  const float dz = desired_eye[2] - target[2];
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (distance > 1.0e-6f) {
    const float ray_x = dx / distance;
    const float ray_y = dy / distance;
    const float ray_z = dz / distance;
    if (collision_query_) {
      hit_distance = collision_query_(target[0], target[1], target[2],
                                      ray_x, ray_y, ray_z, distance);
    } else if (collision_ != nullptr) {
      const auto hit = collision_->Raycast(
          target[0], target[1], target[2], ray_x, ray_y, ray_z, distance);
      if (hit.has_value()) {
        hit_distance = hit->distance;
      }
    }
  }

  float collision_limit = distance_;
  bool arm_moved = false;
  if (hit_distance.has_value()) {
    const float swept = Clamp(*hit_distance, min_distance_, distance_);
    arm_moved = std::fabs(distance_ - swept) >= kCameraCompareEpsilon;
    collision_limit = swept;
  }

  if (near_clearance_query_ &&
      collision_limit > kWorldCameraNearClipDistance) {
    float swept_eye_x = 0.0f;
    float swept_eye_y = 0.0f;
    float swept_eye_z = 0.0f;
    ComputeEyePositionAtDistance(collision_limit, swept_eye_x, swept_eye_y,
                                 swept_eye_z);
    const float penetration = near_clearance_query_(
        target, {swept_eye_x, swept_eye_y, swept_eye_z},
        kWorldCameraNearClipDistance, collision_limit,
        ProjectionFieldOfViewRadians(fov_), last_frame_aspect_ratio_,
        last_frame_far_plane_);
    if (penetration > 0.0f) {
      const float pulled = collision_limit -
                           (collision_limit - kWorldCameraNearClipDistance) *
                               std::min(penetration, 1.0f);
      if (std::fabs(collision_limit - pulled) >= kCameraCompareEpsilon) {
        arm_moved = true;
      }
      collision_limit = std::max(pulled, min_distance_);
    }
  }
  if (arm_moved) {
    collision_limit =
        std::max(collision_limit - kCameraCollisionClearance, min_distance_);
  }

  resolved_distance_ = collision_limit;
}

void WorldCamera::UpdateCameraTargetAlpha() {

  camera_target_alpha_ = Camera_ComputeBoundUnitAlphaByte(
      resolved_distance_, kWorldCameraNearClipDistance, pitch_, height_offset_);
}

void WorldCamera::GetViewMatrix(float *out) const {
  float ex, ey, ez;
  ComputeEyePosition(ex, ey, ez);
  const float eye[3] = {ex, ey, ez};
  const float target[3] = {smooth_x_, smooth_y_, smooth_z_ + height_offset_};
  constexpr float kWorldUp[3] = {0.0f, 0.0f, 1.0f};
  openwow::math::BuildRetailCameraViewMatrix(eye, target, kWorldUp, out);
}

void WorldCamera::GetProjectionMatrix(float *out, float aspect, float near_plane,
                                      float far_plane) const {
  const float effective_fov = ProjectionFieldOfViewRadians(fov_);
  openwow::math::BuildRetailCameraProjectionMatrix(
      effective_fov, aspect, near_plane, far_plane, out);
}

float WorldCamera::GetX() const {
  float ex, ey, ez;
  ComputeEyePosition(ex, ey, ez);
  return ex;
}

float WorldCamera::GetY() const {
  float ex, ey, ez;
  ComputeEyePosition(ex, ey, ez);
  return ey;
}

float WorldCamera::GetZ() const {
  float ex, ey, ez;
  ComputeEyePosition(ex, ey, ez);
  return ez;
}

void WorldCamera::ComputeEyePosition(float &ex, float &ey, float &ez) const {
  ComputeEyePositionAtDistance(resolved_distance_, ex, ey, ez);
}

void WorldCamera::ComputeEyePositionAtDistance(const float arm_distance,
                                               float &ex, float &ey,
                                               float &ez) const {
  const float tx = smooth_x_;
  const float ty = smooth_y_;
  const float tz = smooth_z_ + height_offset_;

  const float cos_p = std::cos(pitch_);
  const float sin_p = std::sin(pitch_);
  const float world_yaw = yaw();
  const float cos_y = std::cos(world_yaw);
  const float sin_y = std::sin(world_yaw);

  ex = tx - cos_p * cos_y * arm_distance;
  ey = ty - cos_p * sin_y * arm_distance;
  ez = tz + sin_p * arm_distance;
}

float WorldCamera::Clamp(float v, float lo, float hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

float WorldCamera::WrapAngle(float a) {
  a = std::fmod(a, kTwoPi);
  if (a < 0.0f)
    a += kTwoPi;
  return a;
}

}

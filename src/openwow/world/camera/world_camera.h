#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::world {

class CollisionManager;

inline constexpr float kCameraProjectionFieldOfViewScale = 0.6f;
inline constexpr float kWorldCameraNearClipDistance = 0.2f;
inline constexpr float kCameraMinFieldOfViewRadians = 0.017453292f;
inline constexpr float kCameraMaxFieldOfViewRadians = 2.0943952f;

[[nodiscard]] inline float ProjectionFieldOfViewRadians(
    const float camera_fov_radians) noexcept {
  return camera_fov_radians * kCameraProjectionFieldOfViewScale;
}

[[nodiscard]] float ClampCameraFieldOfView(float fov_radians) noexcept;
[[nodiscard]] float AdvanceCameraFieldOfView(float current_fov_radians,
                                             float target_fov_radians,
                                             float delta_seconds) noexcept;

[[nodiscard]] std::uint8_t Camera_ComputeBoundUnitAlphaByte(
    float resolved_distance, float near_plane, float pitch,
    float camera_height) noexcept;

[[nodiscard]] bool Camera_IsActivePlayerBoundAlphaVisible() noexcept;

bool Camera_SetActivePlayerBoundAlphaVisible(bool visible) noexcept;

bool Camera_ToggleActivePlayerBoundAlphaVisible() noexcept;

enum class ScriptedCameraMoveChannel : std::uint8_t {
  kZoomIn = 0,
  kZoomOut = 1,
  kYawRight = 2,
  kYawLeft = 3,
  kPitchUp = 4,
  kPitchDown = 5,
};

struct CameraMotionSettings {
  float minimum_distance{};
  float maximum_distance{50.0f};
  float distance_speed{8.33f};
  float yaw_speed_degrees{180.0f};
  float pitch_speed_degrees{90.0f};
};

struct CameraViewPreset {
  float distance{};
  float pitch_radians{};
  float relative_yaw_radians{};
};

enum class CameraSmoothingEvent : std::uint8_t {
  kIdle = 0,
  kStop = 1,
  kTrack = 2,
  kMove = 3,
  kStrafe = 4,
  kTurn = 5,
  kFear = 6,
};

inline constexpr std::uint32_t kControlTurnOrActionButton = 0x00000001u;
inline constexpr std::uint32_t kControlCameraOrSelectButton = 0x00000002u;
inline constexpr std::uint32_t kControlMaskStrafeKeys = 0x000000C0u;
inline constexpr std::uint32_t kControlMaskTurnKeys = 0x00000300u;
inline constexpr std::uint32_t kControlMaskForwardBackwardAutoRun = 0x00001030u;
inline constexpr std::uint32_t kControlMaskVehicleSeats = 0x01E00000u;

inline constexpr std::uint32_t kControlMaskMouseSteer = 0x02000001u;

inline constexpr std::size_t kCameraSmoothingEventCount = 7;
inline constexpr std::size_t kCameraSmoothingAxisCount = 3;

inline constexpr std::size_t kCameraSmoothingAxisDistance = 0;
inline constexpr std::size_t kCameraSmoothingAxisPitch = 1;
inline constexpr std::size_t kCameraSmoothingAxisYaw = 2;

struct CameraSmoothingDelayFactor {
  float delay_seconds{};
  float factor{};
};

struct CameraSmoothingSettings {
  bool smooth_yaw_enabled{true};
  bool smooth_pitch_enabled{true};
  bool custom_view_smoothing{false};
  float yaw_smooth_min_radians{0.0f};
  float yaw_smooth_max_radians{0.0f};
  float pitch_smooth_min_radians{0.0f};
  float pitch_smooth_max_radians{0.52359879f};

  float yaw_smooth_speed_degrees{180.0f};
  float pitch_smooth_speed_degrees{45.0f};

  float distance_smooth_speed{8.33f};
  float smooth_time_min_seconds{0.1f};
  float smooth_time_max_seconds{2.0f};
  std::array<CameraSmoothingDelayFactor, kCameraSmoothingEventCount> events{{
      {0.0f, 0.0f},
      {0.0f, 0.0f},
      {0.4f, 10.0f},
      {0.0f, 1.0f},
      {0.0f, 1.0f},
      {0.0f, 1.0f},
      {0.4f, 10.0f},
  }};
  std::array<CameraSmoothingDelayFactor, kCameraSmoothingAxisCount> view_data{{
      {0.0f, 0.0f},
      {0.0f, 1.0f},
      {0.0f, 1.0f},
  }};

  std::array<CameraSmoothingDelayFactor, kCameraSmoothingEventCount>
      tracking_events{{
          {0.0f, 0.0f},
          {0.0f, 0.0f},
          {0.4f, 10.0f},
          {0.0f, 1.0f},
          {0.0f, 1.0f},
          {0.0f, 1.0f},
          {0.4f, 10.0f},
      }};
  std::array<CameraSmoothingDelayFactor, kCameraSmoothingAxisCount>
      tracking_view_data{{
          {0.0f, 0.0f},
          {0.0f, 1.0f},
          {0.0f, 1.0f},
      }};
};

enum class CameraPoseSource : std::uint8_t {
  kOrbit,
  kCinematic,
  kCommentator,
};

struct CameraPoseOverride {
  CameraPoseSource source = CameraPoseSource::kOrbit;
  std::array<float, 3> position{};

  std::optional<std::array<float, 3>> target;
  std::array<float, 3> forward{1.0f, 0.0f, 0.0f};
  std::array<float, 3> up{0.0f, 0.0f, 1.0f};

  float vertical_fov_radians = 0.0f;
};

struct CameraFrameContext {
  float aspect_ratio = 16.0f / 9.0f;
  float near_plane = 0.2f;
  float far_plane = 1000.0f;
};

struct CameraFramePose {
  CameraPoseSource source = CameraPoseSource::kOrbit;
  std::uint64_t generation = 0;
  std::array<float, 3> position{};
  std::array<float, 3> target{};
  std::array<float, 3> forward{1.0f, 0.0f, 0.0f};
  std::array<float, 3> up{0.0f, 0.0f, 1.0f};
  std::array<float, 16> view{};
  std::array<float, 16> projection{};
  float vertical_fov_radians = 0.94247782f;
  float desired_arm_distance = 0.0f;
  float resolved_arm_distance = 0.0f;
  float pitch_radians = 0.0f;
  float yaw_radians = 0.0f;
};

class WorldCamera {
public:
  using CollisionQuery = std::function<std::optional<float>(
      float, float, float, float, float, float, float)>;

  using NearClearanceQuery = std::function<float(
      const std::array<float, 3>& pivot, const std::array<float, 3>& eye,
      float near_plane, float arm_length, float vertical_fov_radians,
      float aspect_ratio, float far_plane)>;

  WorldCamera();
  ~WorldCamera();

  WorldCamera(const WorldCamera &) = delete;
  WorldCamera &operator=(const WorldCamera &) = delete;
  WorldCamera(WorldCamera &&) noexcept = delete;
  WorldCamera &operator=(WorldCamera &&) noexcept = delete;

  void SetTarget(float x, float y, float z);
  void SetBoundObject(std::uint64_t guid) {
    bound_object_ = guid;
  }
  [[nodiscard]] std::uint64_t bound_object() const {
    return bound_object_;
  }

  void SetYaw(float yaw_radians);
  void SetTargetYaw(float yaw_radians);

  void SetReferenceFacing(float facing_radians);
  [[nodiscard]] float reference_facing() const {
    return reference_facing_;
  }

  void SetBoundUnitMovementFlags(std::uint32_t movement_flags) {
    bound_unit_movement_flags_ = movement_flags;
  }

  void SetPitch(float pitch_radians);
  void SetTargetPitch(float pitch_radians);

  void SetDistance(float distance);
  void SetTargetDistance(float distance);

  void HandleMouseDelta(float dx, float dy);

  void AdjustPitch(float delta_radians);

  void FlipYaw(float delta_radians);

  void PushYawOffset();

  void PopYawOffset();

  void EnterFreelook();

  void ExitFreelook();

  [[nodiscard]] bool IsFreelooking() const {
    return freelook_;
  }
  [[nodiscard]] std::uint32_t GetYawOffsetDepth() const {
    return yaw_offset_depth_;
  }

  void HandleScrollDelta(float delta);

  void StartScriptedMoveChannel(ScriptedCameraMoveChannel channel, std::uint32_t timestamp,
                                float rate_scale);
  void StopScriptedMoveChannel(ScriptedCameraMoveChannel channel, std::uint32_t timestamp);
  void QueueScriptedZoomStep(bool zoom_in, std::uint32_t timestamp, float amount,
                             float duration_seconds = 0.0f);
  [[nodiscard]] bool ApplyScriptedMoveInputs(std::uint32_t timestamp);
  void ResetScriptedMoveInputs();
  void SetScriptedMoveBlocked(bool blocked);
  void SetMotionSettings(CameraMotionSettings settings);
  void SetSmoothingSettings(CameraSmoothingSettings settings);
  [[nodiscard]] const CameraSmoothingSettings& smoothing_settings() const {
    return smoothing_settings_;
  }

  void ApplyControlFlagSmoothing(std::uint32_t control_flags, bool stop_event,
                                 std::uint32_t timestamp_ms);

  void SetTrackingCamera(bool click_to_move_active, bool follows_unit,
                         std::uint32_t control_flags,
                         std::uint32_t timestamp_ms);

  void SetAutoInteractEnabled(bool enabled) {
    auto_interact_enabled_ = enabled;
  }

  void SetTaxiFlightInputSuppressed(bool suppressed) {
    taxi_flight_input_suppressed_ = suppressed;
  }
  [[nodiscard]] bool IsTaxiFlightInputSuppressed() const {
    return taxi_flight_input_suppressed_;
  }

  void Update(float dt_seconds, std::uint32_t timestamp_ms);

  void ResetForWorldEntry(float x, float y, float z, float facing_radians,
                          int view);
  void SetViewPreset(int view, CameraViewPreset preset);
  void ApplyViewPreset(int view, bool snap);

  void SetActiveView(int view);
  [[nodiscard]] int active_view() const {
    return active_view_;
  }
  void BindDbc(const openwow::data::dbc::DbcLoader* dbc) noexcept {
    if (dbc_ != dbc) {
      camera_shakes_.clear();
    }
    dbc_ = dbc;
  }
  void TriggerSpellEffectCameraShakes(
      std::uint32_t effect_id, const std::array<float, 3>& origin);
  void TriggerCameraShake(std::uint32_t shake_id,
                          const std::array<float, 3>& origin);

  const CameraFramePose& ResolveFramePose(
      const CameraFrameContext& frame,
      std::span<const CameraPoseOverride> overrides = {});

  [[nodiscard]] const CameraFramePose& frame_pose() const {
    return frame_pose_;
  }
  [[nodiscard]] bool has_frame_pose() const {
    return frame_pose_.generation != 0;
  }

  void SetViewportSize(float width, float height);

  void GetViewMatrix(float *out_4x4) const;

  void GetProjectionMatrix(float *out_4x4, float aspect_ratio, float near_plane,
                           float far_plane) const;

  [[nodiscard]] float GetX() const;
  [[nodiscard]] float GetY() const;
  [[nodiscard]] float GetZ() const;

  [[nodiscard]] float yaw() const {
    return WrapAngle(orbit_yaw_ + ComposedYawBias());
  }
  [[nodiscard]] float target_yaw() const {
    return WrapAngle(target_orbit_yaw_ + ComposedYawBias());
  }
  [[nodiscard]] float flip_yaw_bias() const {
    return flip_yaw_bias_;
  }

  [[nodiscard]] float orbit_yaw() const {
    return orbit_yaw_;
  }
  [[nodiscard]] float target_orbit_yaw() const {
    return target_orbit_yaw_;
  }
  void SetOrbitYaw(float orbit_yaw_radians);
  void SetTargetOrbitYaw(float orbit_yaw_radians);

  [[nodiscard]] float pitch() const {
    return pitch_;
  }
  [[nodiscard]] float target_pitch() const {
    return target_pitch_;
  }
  [[nodiscard]] float distance() const {
    return distance_;
  }
  [[nodiscard]] float target_distance() const {
    return target_distance_;
  }
  [[nodiscard]] float resolved_distance() const {
    return resolved_distance_;
  }

  [[nodiscard]] std::uint8_t camera_target_alpha() const {
    return camera_target_alpha_;
  }
  [[nodiscard]] float fov() const {
    return fov_;
  }

  void SetFov(float fov_radians) {
    fov_ = fov_radians;
  }
  void SetHeightOffset(float offset) {
    height_offset_ = offset;
  }
  void SetMaxDistance(float max) {
    max_distance_ = max;
  }
  void SetZoomSpeed(float speed) {
    zoom_speed_ = speed;
  }
  void SetSmoothingEnabled(bool enabled) {
    smoothing_ = enabled;
  }
  void BindCollision(const CollisionManager* collision) {
    collision_ = collision;
  }
  void BindCollisionQuery(CollisionQuery query) {
    collision_query_ = std::move(query);
  }
  void BindNearClearanceQuery(NearClearanceQuery query) {
    near_clearance_query_ = std::move(query);
  }

private:
  void UpdateCollision();
  void UpdateCameraTargetAlpha();
  [[nodiscard]] CameraFramePose BuildOrbitFramePose(
      const CameraFrameContext& frame) const;
  void PublishFramePose(CameraFramePose pose,
                        const CameraFrameContext& frame);
  void ApplyCameraShakes(CameraFramePose& pose) const;
  void ComputeEyePositionAtDistance(float arm_distance, float &ex, float &ey,
                                    float &ez) const;
  void ComputeEyePosition(float &ex, float &ey, float &ez) const;

  static float Clamp(float v, float lo, float hi);
  static float WrapAngle(float a);

  [[nodiscard]] float ReferenceFacingBias() const {
    return yaw_offset_depth_ == 0u ? reference_facing_ : 0.0f;
  }

  [[nodiscard]] float ComposedYawBias() const {
    return flip_yaw_bias_ + ReferenceFacingBias();
  }

  [[nodiscard]] float ViewPresetYawBias() const {
    return yaw_offset_depth_ != 0u ? reference_facing_ : 0.0f;
  }

  void ShiftYawFrame(float delta_radians);

  void ShiftPitchFrame(float delta_radians);

  bool StartYawInterpolation(float target_orbit_yaw, float delay_seconds,
                             float factor, std::uint32_t timestamp_ms);

  bool StartPitchInterpolation(float target_pitch, float delay_seconds,
                               float factor, std::uint32_t timestamp_ms);

  bool StartDistanceInterpolation(float target_distance, float delay_seconds,
                                  float factor, std::uint32_t timestamp_ms);
  void CancelDistanceInterpolation();

  void CancelYawInterpolation();
  void CancelPitchInterpolation();

  void AdvanceSmoothingInterpolations(std::uint32_t timestamp_ms);

  [[nodiscard]] bool ActiveViewPresetAllowsSmoothing() const;

  [[nodiscard]] std::uint32_t BuildSmoothingEventCandidates(
      std::uint32_t control_flags, bool stop_event) const;

  [[nodiscard]] bool ShouldSmoothYaw() const;
  [[nodiscard]] bool ShouldSmoothPitch() const;

  float target_x_ = 0.0f;
  float target_y_ = 0.0f;
  float target_z_ = 0.0f;
  float smooth_x_ = 0.0f;
  float smooth_y_ = 0.0f;
  float smooth_z_ = 0.0f;
  bool initial_set_ = false;
  std::uint64_t bound_object_ = 0;

  int active_view_ = 2;

  float orbit_yaw_ = 0.0f;

  float target_orbit_yaw_ = 0.0f;
  float yaw_interpolation_source_ = 0.0f;
  float yaw_interpolation_duration_seconds_ = 0.0f;
  float yaw_interpolation_delay_seconds_ = 0.0f;
  float yaw_interpolation_factor_ = 0.0f;
  std::uint32_t yaw_interpolation_start_ms_ = 0;
  bool yaw_interpolation_armed_ = false;

  float reference_facing_ = 0.0f;

  std::uint32_t bound_unit_movement_flags_ = 0;

  float flip_yaw_bias_ = 0.0f;
  float pitch_ = 0.3f;

  float target_pitch_ = 0.3f;
  float pitch_interpolation_source_ = 0.3f;
  float pitch_interpolation_duration_seconds_ = 0.0f;
  float pitch_interpolation_delay_seconds_ = 0.0f;
  float pitch_interpolation_factor_ = 0.0f;
  std::uint32_t pitch_interpolation_start_ms_ = 0;
  bool pitch_interpolation_armed_ = false;

  std::uint32_t last_update_ms_ = 0;

  float distance_ = 15.0f;
  float target_distance_ = 15.0f;
  float distance_interpolation_source_ = 15.0f;
  float distance_interpolation_duration_seconds_ = 0.0f;
  float distance_interpolation_delay_seconds_ = 0.0f;
  float distance_interpolation_factor_ = 0.0f;
  std::uint32_t distance_interpolation_start_ms_ = 0;
  bool distance_interpolation_armed_ = false;
  float resolved_distance_ = 15.0f;
  float viewport_height_over_width_ = 9.0f / 16.0f;

  std::uint8_t camera_target_alpha_ = 0xFFu;
  CameraFramePose frame_pose_{};
  std::uint64_t frame_pose_generation_ = 0;

  float fov_ = 1.5707964f;

  float height_offset_ = 1.5f;
  float zoom_speed_ = 4.0f;
  float max_distance_ = 50.0f;
  float min_distance_ = 0.0f;
  bool smoothing_ = true;
  bool freelook_ = false;

  bool tracking_camera_active_ = false;
  bool tracking_camera_follows_unit_ = false;
  bool tracking_camera_holds_world_frame_ = false;
  bool auto_interact_enabled_ = true;
  bool taxi_flight_input_suppressed_ = false;
  std::uint32_t yaw_offset_depth_ = 0;
  std::uint32_t freelook_pitch_depth_ = 0;

  struct ScriptedMoveChannelState {
    std::uint32_t last_tick = 0;
    std::uint32_t stop_tick = 0;
    std::uint32_t queued_stop_tick = 0;
    float rate_scale = 1.0f;
    bool active = false;
    bool stopping = false;
  };

  static std::uint32_t ConsumeScriptedMoveElapsed(
      ScriptedMoveChannelState& state, std::uint32_t current_tick);

  static constexpr std::size_t kScriptedMoveChannelCount = 6;
  ScriptedMoveChannelState scripted_move_channels_[kScriptedMoveChannelCount]{};
  CameraMotionSettings motion_settings_{};
  std::array<CameraViewPreset, 8> view_presets_{{
      {},
      {},
      {5.55f, 0.17453292f, 0.0f},
      {5.55f, 0.34906584f, 0.0f},
      {13.88f, 0.52359879f, 0.0f},
      {13.88f, 0.17453292f, 0.0f},
      {},
      {5.0f, 0.17453292f, 0.0f},
  }};
  bool scripted_move_blocked_ = false;
  CameraSmoothingSettings smoothing_settings_{};
  const CollisionManager* collision_ = nullptr;
  CollisionQuery collision_query_;
  NearClearanceQuery near_clearance_query_;

  float last_frame_aspect_ratio_ = 16.0f / 9.0f;
  float last_frame_far_plane_ = 1000.0f;
  const openwow::data::dbc::DbcLoader* dbc_ = nullptr;

  struct ActiveCameraShake {
    std::array<float, 3> origin{};
    std::uint32_t shake_type = 0;
    std::uint32_t direction = 0;
    float amplitude = 0.0f;
    float frequency = 0.0f;
    float duration = 0.0f;
    float elapsed = 0.0f;
    float phase = 0.0f;
    float coefficient = 0.0f;
  };
  std::vector<ActiveCameraShake> camera_shakes_;

  static constexpr float kTargetSmoothSpeed = 10.0f;
};

}

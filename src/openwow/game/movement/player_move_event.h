
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <utility>

#include "openwow/game/movement_info.h"
#include "openwow/game/object_types.h"

namespace openwow::game {
class CGUnit_C;
class ObjectManager;
}

namespace openwow::game::movement {

enum class MoveEventType : std::uint32_t {
  kStartForward = 0,
  kStartBackward = 1,
  kStopForwardBackward = 2,
  kStartStrafeLeft = 3,
  kStartStrafeRight = 4,
  kStopStrafe = 5,
  kStartAscend = 6,
  kStartDescend = 7,
  kStopVerticalExplicit = 8,
  kHeartbeat = 9,
  kJump = 10,
  kStartTurnLeft = 11,
  kStartTurnRight = 12,
  kStopTurn = 13,
  kStartPitchUp = 14,
  kStartPitchDown = 15,
  kStopPitch = 16,
  kStartRun = 17,
  kStartWalk = 18,
  kSetFacing = 19,
  kSetPitch = 20,
  kStartSwim = 21,
  kStopSwim = 22,

  kSetRunSpeed = 23,
  kSetRunBackSpeed = 24,
  kSetWalkSpeed = 25,
  kSetSwimSpeed = 26,
  kSetSwimBackSpeed = 27,
  kSetFlightSpeed = 28,
  kSetFlightBackSpeed = 29,
  kSetTurnRate = 30,
  kSetPitchRate = 31,
  kGravityEnable = 32,
  kGravityDisable = 33,
  kKnockBack = 34,
  kFeatherFallEnable = 35,
  kFeatherFallDisable = 36,
  kHoverEnable = 37,
  kHoverDisable = 38,
  kWaterWalkEnable = 39,
  kWaterWalkDisable = 40,
  kRoot = 41,
  kUnroot = 42,
  kRemoteMovementSnapshot = 43,
  kChangeTransportSeat = 44,
  kEnableFlyMode = 45,
  kEnableSwimMode = 46,
  kCanFlyEnable = 47,
  kCanFlyDisable = 48,
  kTimeSync = 49,
  kScheduledTurnStop = 50,
  kScheduledPitchStop = 51,
  kDismissControlledVehicle = 52,
  kBoundedTurnFacing = 53,
  kSetVehiclePitch = 54,
  kVehicleSeatSwitch = 55,
  kSwimFlyTransitionEnable = 56,
  kSwimFlyTransitionDisable = 57,
  kSetCollisionHeight = 58,
  kFallLand = 59,
};

enum class SplineMovementMode : std::uint8_t {
  kUnroot,
  kFeatherFall,
  kNormalFall,
  kHover,
  kNoHover,
  kWaterWalk,
  kLandWalk,
  kStartSwim,
  kStopSwim,
  kRun,
  kWalk,
  kRoot,
  kSetFlying,
  kUnsetFlying,
  kGravityDisable,
  kGravityEnable,
};

struct CPlayerMoveEvent {
  std::uint32_t timestamp{0};
  std::uint32_t event_type{0};
  float cos_angle{0.0f};
  float sin_angle{0.0f};
  float jump_z_speed{0.0f};
  float auxiliary_f32{0.0f};
  std::array<float, 3> runtime_position{};
  float runtime_orientation{0.0f};
  float runtime_pitch{0.0f};
  std::uint32_t runtime_flags{0};
  std::uint16_t runtime_flags2{0};
  std::uint64_t runtime_transport_guid{0};
  std::uint32_t runtime_fall_time{0};
  float runtime_fall_start_z{0.0f};
  std::uint8_t runtime_transport_seat{0};
  bool has_runtime_snapshot{false};
  std::uint32_t auxiliary_u32{0};
  std::uint32_t auxiliary_u32_secondary{0};
  std::uint8_t auxiliary_u8{0};
  bool needs_ack{true};
  std::shared_ptr<const MovementInfo> deferred_authoritative_movement;
  std::uint32_t deferred_authoritative_opcode{0};
};

struct QueuedMovementPreview {
  std::int32_t lead_time_ms{0};
  std::array<float, 3> delta_position{};
  float delta_orientation{0.0f};
  float delta_pitch{0.0f};
  bool has_linear_motion_delta{false};
  bool has_orientation_delta{false};
  bool has_pitch_delta{false};
};

class CPlayerMoveEventQueue {
 public:
  CPlayerMoveEventQueue() = default;
  ~CPlayerMoveEventQueue() = default;

  void QueueEvent(CPlayerMoveEvent event);

  void RemoveByType(std::uint32_t event_type);

  [[nodiscard]] bool RescheduleFirstByType(std::uint32_t event_type,
                                           std::uint32_t timestamp);

  void ClearAll();

  [[nodiscard]] bool HasEvents() const { return !events_.empty(); }

  [[nodiscard]] CPlayerMoveEvent PopFront();

  [[nodiscard]] const CPlayerMoveEvent& PeekFront() const;

  [[nodiscard]] std::size_t Size() const { return events_.size(); }

  [[nodiscard]] bool HasEventInTypeRange(std::uint32_t min_type,
                                         std::uint32_t max_type) const;

  template <typename Visitor>
  void ForEachMutable(Visitor&& visitor) {
    for (auto& event : events_) {
      visitor(event);
    }
  }

 private:
  std::list<CPlayerMoveEvent> events_;
};

class CMovementData {
  friend class openwow::game::CGUnit_C;
 public:
  using RuntimeNotificationCallback = std::function<void(CMovementData&)>;
  using TransportSeatChangeCallback =
      std::function<void(std::uint64_t old_transport_guid,
                         std::uint64_t new_transport_guid,
                         std::uint8_t transport_seat)>;
  using TransportParentCommitCallback =
      std::function<void(std::uint64_t old_transport_guid,
                         std::uint64_t new_transport_guid)>;
  using TransportParentRebaseCallback =
      std::function<void(bool leaving_parent, std::uint64_t transport_guid,
                         float body_facing_delta)>;

  CMovementData() = default;

  void Init();

  void ClearQueuedEvents();

  [[nodiscard]] std::uint64_t Cleanup();

  void InitCollisionBounds(float width, float height, float effective_scale,
                           float raw_scale, bool forced,
                           bool is_navigable_as_player = false);

  [[nodiscard]] float GetCollisionHalfWidth() const { return collision_half_width_; }
  [[nodiscard]] float GetCollisionHeightProduct() const { return collision_height_product_; }
  [[nodiscard]] float GetCollisionScaleRatio() const { return collision_scale_ratio_; }

  void SetCollisionHeightProduct(float height) {
    collision_height_product_ = height;
  }

  float* BuildAABBFromPosition(const float* position, float* out_aabb) const;

  struct TerrainIntersectUnitState {

    bool is_navigable_as_player = false;

    bool can_control_character = false;

    bool is_ghost_player = false;
  };

  [[nodiscard]] std::uint32_t BuildTerrainIntersectFlags(
      const TerrainIntersectUnitState& unit_state) const;

  void SetGroundSlopeZ(float z) { ground_slope_z_ = z; }

  [[nodiscard]] float GetGroundSlopeZ() const { return ground_slope_z_; }

  struct PassengerCollisionUnitState {
    bool is_navigable_as_player = false;

    bool is_ghost_player = false;

    std::optional<bool> has_character_control;
  };

  struct PassengerCollisionHit {
    float distance = 0.0f;
    std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
  };

  using TracePassengerCollisionCallback =
      std::function<std::optional<PassengerCollisionHit>(
          const float* aabb_source, const float* position_source,
          const float* direction_source, float sweep_distance,
          std::uint32_t flags)>;

  using GetPassengerCollisionUnitStateCallback =
      std::function<PassengerCollisionUnitState(const CMovementData& md)>;

  using MapPassengerCollisionTimestampCallback =
      std::function<std::uint32_t(std::uint32_t raw_timestamp)>;

  using ForceSetTransportAndNotifyCallback =
      std::function<void(CMovementData& md)>;

  struct PassengerCollisionCallbacks {
    TracePassengerCollisionCallback trace_world;
    GetPassengerCollisionUnitStateCallback get_unit_state;
    MapPassengerCollisionTimestampCallback map_transport_timestamp;
    ForceSetTransportAndNotifyCallback force_set_transport_notify;
  };

  bool TestUnitBone(std::uint32_t timestamp_offset,
                    std::uint32_t object_timestamp_offset,
                    std::uint32_t time_delta,
                    float sweep_distance,
                    const float* sweep_direction,
                    const float* transport_transform,
                    const PassengerCollisionCallbacks& callbacks);

  [[nodiscard]] float GetCumulativeCollisionZ() const {
    return cumulative_collision_z_;
  }
  void SetCumulativeCollisionZ(float z) { cumulative_collision_z_ = z; }

  void QueueForwardMove(std::uint32_t timestamp, bool forward);

  void QueueStrafeMove(std::uint32_t timestamp, bool left);

  void QueueJump(std::uint32_t timestamp);

  void QueueFallLand(std::uint32_t timestamp);

  void QueueHeartbeat(std::uint32_t timestamp);
  void QueueTimeSync(std::uint32_t timestamp, std::uint32_t counter);

  void QueueCollisionHeartbeat(std::uint32_t timestamp);

  void QueueStopForward(std::uint32_t timestamp);

  void QueueStopStrafe(std::uint32_t timestamp);

  void QueueTurnMovement(std::uint32_t timestamp, bool turning_right);

  void QueuePitchMovement(std::uint32_t timestamp, bool pitching_down);

  void QueueStopTurn(std::uint32_t timestamp);

  void QueueToggleRun(std::uint32_t timestamp, bool running);

  void QueueStopPitch(std::uint32_t timestamp);

  void QueueSpeedChangeEvent(std::uint32_t timestamp,
                             MoveEventType event_type,
                             float speed);

  void QueueForceSpeedChangeEvent(std::uint32_t timestamp,
                                  MoveEventType event_type,
                                  std::uint32_t counter,
                                  float speed);

  enum class UpdateResult : std::uint8_t {
    kCompleted,
    kEventsPending,

    kAborted,
  };

  struct RuntimeTimelineState {
    std::uint32_t current_tick{0};
    std::uint32_t active_mover_deadline{0};
  };

  UpdateResult Update(const openwow::game::ObjectManager& objects,
                      std::uint32_t end_time, std::uint32_t current_time,
                      RuntimeTimelineState* runtime_timeline = nullptr);

  void DelayActiveMoverDeadline(std::uint32_t timestamp) noexcept {
    fallback_runtime_timeline_.active_mover_deadline = timestamp + 500u;
  }

  [[nodiscard]] std::uint32_t GetActiveMoverDeadline() const noexcept {
    return fallback_runtime_timeline_.active_mover_deadline;
  }

  using SkipExcessTimeCallback =
      std::function<void(CMovementData& md, std::uint32_t skip_ms)>;
  using ProcessMovementLoopCallback =
      std::function<bool(CMovementData& md, std::uint32_t time_ms,
                         std::uint32_t step_ms)>;
  using DispatchMovementEventsCallback =
      std::function<int(CMovementData& md, std::uint32_t time_ms)>;
  using FinalizeStoppedCallback =
      std::function<void(CMovementData& md, bool full_stop)>;
  using VehiclePostUpdateCallback =
      std::function<void(CMovementData& md, std::uint32_t end_time)>;
  using SendHeartbeatCallback =
      std::function<void(CMovementData& md, std::uint32_t time_ms)>;
  using IsActiveMoverCallback =
      std::function<bool(const CMovementData& md)>;
  using IsActivePlayerCallback =
      std::function<bool(const CMovementData& md)>;
  using ClearInputControlOnStopCallback =
      std::function<void(CMovementData& md)>;

  struct UpdateCallbacks {
    SkipExcessTimeCallback          skip_excess_time;
    ProcessMovementLoopCallback     process_movement_loop;
    DispatchMovementEventsCallback  dispatch_events;
    FinalizeStoppedCallback         finalize_stopped;
    VehiclePostUpdateCallback       vehicle_post_update;
    SendHeartbeatCallback           send_heartbeat;
    IsActiveMoverCallback           is_active_mover;
    IsActivePlayerCallback          is_active_player;

    ClearInputControlOnStopCallback clear_input_control_on_stop;
  };

  void SetUpdateCallbacks(UpdateCallbacks callbacks) {
    update_callbacks_ = std::move(callbacks);
  }

  using EventProcessingGateCallback =
      std::function<bool(const CMovementData&, const CPlayerMoveEvent&)>;

  using DispatchMovementOpcodeCallback =
      std::function<void(CMovementData&, std::uint16_t opcode,
                         std::uint32_t timestamp,
                         const CPlayerMoveEvent&)>;
  using DispatchDeferredAuthoritativeMovementCallback =
      std::function<void(CMovementData&, const CPlayerMoveEvent&)>;

  void SetEventProcessingGateCallback(EventProcessingGateCallback callback) {
    event_processing_gate_callback_ = std::move(callback);
  }
  void SetDispatchMovementOpcodeCallback(
      DispatchMovementOpcodeCallback callback) {
    dispatch_movement_opcode_callback_ = std::move(callback);
  }
  void SetDispatchDeferredAuthoritativeMovementCallback(
      DispatchDeferredAuthoritativeMovementCallback callback) {
    dispatch_deferred_authoritative_movement_callback_ =
        std::move(callback);
  }

  int DispatchDueEvents(const openwow::game::ObjectManager& objects,
                        std::uint32_t timestamp);

  int FlushQueuedEvents(const openwow::game::ObjectManager& objects);

  bool HandleRemoteForwardStart(bool forward);

  bool HandleRemoteJump();

  bool HandleRemoteStrafeStart(bool strafe_left);

  bool SetStrafeDirection(bool strafe_left);

  void HandleRemoteTurnStart(bool turn_left);

  void SetTurnDirection(bool turn_left);

  void HandleRemotePitchStart(bool pitch_up);

  void HandleRemotePoseSnapshot();

  bool HandleRemoteAscendDescend(bool ascending);
  bool HandleRemoteStopAscendDescend();

  void HandleRemoteStartSwim();

  void HandleRemoteHoverSnapshot();
  bool HandleRemoteWaterWalkSnapshot();
  bool HandleRemoteCanFlySnapshot();
  bool HandleRemoteGravitySnapshot();

  bool HandleRemoteCanTransitionSwimFly(bool enabled);

  bool HandleRemoteChangeTransportSeat();

  bool HandleRemoteSpeedAck(bool spline_enabled, float new_speed,
                            openwow::game::SpeedType speed_type);

  void HandleRemoteTeleportReset();

  void HandleRemoteStopSwimReset();

  void HandleRemoteRootAck();

  void HandleRemoteUnrootAck();

  void ResetMovementBaseState();

  void ResetClientControlTransition();

  std::uint32_t RecalculateStateFlags();

  bool TryInitRemoteMovement();

  [[nodiscard]] bool IsMovementInitSuppressed() const;

  [[nodiscard]] bool CanSerializeActiveMoverMovementState(
      std::uint16_t opcode, bool is_active_mover) const;

  void SetParentMovementFlags(std::uint32_t flags, bool has_parent = true) {
    parent_movement_flags_ = flags;
    has_parent_movement_ = has_parent;
  }
  [[nodiscard]] std::uint32_t GetParentMovementFlags() const {
    return parent_movement_flags_;
  }
  [[nodiscard]] bool HasParentMovement() const { return has_parent_movement_; }

  [[nodiscard]] bool HasNonExemptSplineFlag(std::uint32_t flag) const {
    return has_parent_movement_ &&
           (parent_movement_flags_ & kParentAllowStopFlag) == 0u &&
           (parent_movement_flags_ & flag) != 0u;
  }

  static constexpr std::uint32_t kParentAllowStopFlag = 0x400u;
  static constexpr std::uint32_t kParentFallingSplineFlag = 0x200u;
  static constexpr std::uint32_t kParentParabolicSplineFlag = 0x800u;
  static constexpr std::uint32_t kParentFlyingSplineFlag = 0x2000u;

  void QueueDeferredMoveEvent(std::uint32_t timestamp,
                              std::uint32_t event_type,
                              bool needs_ack,
                              std::uint32_t auxiliary_u32,
                              float auxiliary_f32,
                              bool capture_runtime_snapshot,
                              std::uint32_t current_time_ms);

  void QueueVehicleSeatSwitch(std::uint32_t timestamp,
                              std::uint32_t target_guid_lo,
                              std::uint32_t target_guid_hi,
                              std::uint8_t seat);

  void QueueSetFacing(std::uint32_t timestamp, float facing);

  void QueueSetPitch(std::uint32_t timestamp, float pitch);

  void QueueSetVehiclePitch(std::uint32_t timestamp, float pitch);

  void QueueBoundedTurnFacing(std::uint32_t timestamp, float facing);

  bool ScheduleTurnToFacing(std::uint32_t timestamp, float angle_delta);

  bool SchedulePitchToTarget(std::uint32_t timestamp, float pitch_delta);

  void QueueSwimToFly(std::uint32_t timestamp, bool enable_fly_mode);

  void QueueVerticalMove(std::uint32_t timestamp, bool ascending);

  void QueueStopVertical(std::uint32_t timestamp);

  void QueueStartSwim(std::uint32_t timestamp);

  void QueueStopSwim(std::uint32_t timestamp);

  void StopSwimmingForTransportAttach();

  bool TryStartJump(bool allow_hover = false);

  void QueueGravityToggle(std::uint32_t timestamp, std::uint32_t counter,
                          bool disable_gravity);

  void QueueKnockBack(std::uint32_t timestamp, std::uint32_t counter,
                      float direction_x, float direction_y,
                      float horizontal_speed, float vertical_speed);

  void RefreshQueuedMovementPreview(std::uint32_t current_time_ms);

  bool ApplyQueuedMovementPreview(std::uint32_t current_time_ms,
                                  std::uint32_t frame_delta_ms, float &x,
                                  float &y, float &z, float &orientation,
                                  float &pitch);

  bool ForceSetTransport(const openwow::game::ObjectManager& objects,
                         std::uint64_t transport_guid, std::uint8_t seat,
                         bool force = false,
                         std::optional<float>* old_parent_body_facing_delta_out = nullptr,
                         std::optional<float>* new_parent_body_facing_delta_out = nullptr,
                         std::function<void()> before_parent_commit = {},
                         std::function<void(std::uint64_t old_transport_guid,
                                            std::uint64_t new_transport_guid,
                                            std::uint8_t transport_seat)>
                             before_parent_rebase = {},
                         std::function<void(bool leaving_parent,
                                            std::uint64_t transport_guid,
                                            float body_facing_delta)>
                             after_parent_rebase = {});

  void SeedAuthoritativeTransportState(const MovementInfo &movement_info);

  void ApplyTeleportArrivalPose(const MovementInfo &movement_info);

  [[nodiscard]] static std::uint32_t ResolveTeleportArrivalFlags(std::uint32_t flags);

  static constexpr std::uint16_t kTeleportPacketOwnedFlags2Mask = 0x2040u;

  static constexpr std::uint16_t kHasParentMovementUpdateBitMask = 0x0080u;

  static constexpr std::uint16_t kInterpolatedFlags2Mask =
      openwow::game::kMoveFlag2InterpolatedMovement |
      openwow::game::kMoveFlag2InterpolatedTurning |
      openwow::game::kMoveFlag2InterpolatedPitching;

  static constexpr std::uint32_t kTeleportPreservedMoveFlagMask = 0xF9003F00u;

  void ImportMovementOpcodeSnapshot(const MovementInfo &movement_info);

  void SyncAuthoritativeMovementInfo(
      const MovementInfo &movement_info,
      bool replace_spline_ownership = false);

  void AdvanceKinematics(std::uint32_t step_ms);

  struct ServerMovementTimingDecision {
    std::uint32_t presentation_tick{0};
    std::int32_t presentation_delta_ms{0};
    bool apply_now{true};
  };

  ServerMovementTimingDecision ResolveServerMovementTiming(
      std::uint32_t event_tick, std::uint32_t server_movement_tick,
      std::uint32_t runtime_current_tick, bool parent_timeline);

  void ResetServerMovementTimeline(std::uint32_t event_tick,
                                   std::uint32_t server_movement_tick) noexcept;

  void QueueDeferredAuthoritativeMovement(
      std::uint32_t presentation_tick, std::uint32_t event_type,
      std::uint32_t opcode, const MovementInfo& movement_info,
      float auxiliary_f32 = 0.0f);

  void SyncPresentedMovementInfo(const MovementInfo &movement_info);

  void ApplyServerMovementTimeSkipped(std::uint32_t skipped_time_ms) noexcept {
    server_movement_timestamp_baseline_ += skipped_time_ms;
  }
  [[nodiscard]] std::uint32_t GetServerMovementTimestampBaseline() const noexcept {
    return server_movement_timestamp_baseline_;
  }

  void SetTransformPosition(float x, float y, float z);
  void SetPresentedTransform(float x, float y, float z, float facing) noexcept;
  [[nodiscard]] const std::array<float, 3>& GetTransformPosition() const {
    return transform_position_;
  }

  float* GetPassengerWorldPosition(const openwow::game::ObjectManager& objects,
                                   float* out_world_pos) const;

  [[nodiscard]] std::array<float, 4> GetPassengerWorldOrientation(
      const openwow::game::ObjectManager& objects) const;

  [[nodiscard]] float GetPassengerWorldFacing(
      const openwow::game::ObjectManager& objects) const;

  void SetScalarFacing(float facing);
  [[nodiscard]] float GetScalarFacing() const { return scalar_facing_; }

  void SetPackedOrientation(std::int64_t packed_orientation);
  [[nodiscard]] std::int64_t GetPackedOrientation() const {
    return packed_orientation_;
  }
  [[nodiscard]] bool UsesPackedOrientation() const {
    return uses_packed_orientation_;
  }

  void InterpolateOrientation(const openwow::game::ObjectManager& objects,
                              float desired_facing);

  void SnapshotStateForDirectionRecompute();

  using SetFacingVisualUpdateCallback =
      std::function<void(CMovementData& md, float facing)>;

  void SetSetFacingVisualUpdateCallback(SetFacingVisualUpdateCallback callback) {
    set_facing_visual_update_callback_ = std::move(callback);
  }

  void SetFacingWithVisualUpdate(const openwow::game::ObjectManager& objects,
                                 float facing, bool update_visuals);

  static constexpr std::uint32_t kFacingVisualUpdateSuppressionMask = 0x00C010FFu;

  void ComputeFacingVectors();

  struct TrigMemo {
    bool valid{false};
    std::uint32_t angle_bits{0u};
    float cos{1.0f};
    float sin{0.0f};
  };

  static void EvaluateMemoisedTrig(TrigMemo& memo, float angle,
                                   float& out_cos, float& out_sin);

  [[nodiscard]] float CalculateCurrentSpeed(bool use_walk_cap = false) const;

  void ComputeDirectionVectors(bool force);

  bool SetGravityEnabledState(bool gravity_enabled);

  void SetRootState(bool rooted);

  void SetHoverState(bool enable);

  void ForceApplyRoot();

  void ForceRemoveRoot(bool try_reinit);

  void UpdateDirectionOnPositionChange();

  void UpdateDirectionConditional();

  [[nodiscard]] bool ProcessPendingTurnStop();

  [[nodiscard]] bool ProcessPendingPitchStop();

  void SetPitchDirection(bool pitchUp);

  [[nodiscard]] bool SetPitchAndTestSteepFall(float new_pitch);

  void ApplySetFacingEvent(float new_facing);

  [[nodiscard]] bool EnableFlyMode();

  [[nodiscard]] bool SetAscendDescend(bool ascending);

  [[nodiscard]] bool ClearAscendDescend();

  [[nodiscard]] bool TryStopForwardBackwardIfPreviouslyActive(
      std::uint32_t old_flags);

  [[nodiscard]] bool TryClearStrafeIfPreviouslyActive(std::uint32_t old_flags);

  void ClearStrafeAndRecalculate();

  int ExecStopForwardBackward();

  void StopForwardBackwardAndRecalculate(bool check_parent_init);

  void ProcessPendingMovementStops();

  using DispatchStopOpcodeCallback =
      std::function<void(CMovementData& md, std::uint32_t opcode)>;

  void SetDispatchStopOpcodeCallback(DispatchStopOpcodeCallback callback) {
    dispatch_stop_opcode_callback_ = std::move(callback);
  }

  void ClearParentMovement();

  void FinishTeleportArrival();

  [[nodiscard]] const std::array<float, 3>& GetPrevPosition() const {
    return prev_position_;
  }
  [[nodiscard]] float GetPrevOrientation() const { return prev_orientation_; }
  [[nodiscard]] float GetPrevPitch() const { return prev_pitch_; }
  [[nodiscard]] float GetInterpolationProgress() const {
    return interpolation_progress_;
  }

  [[nodiscard]] const std::array<float, 3>& GetForwardDirection() const {
    return dir_forward_;
  }
  [[nodiscard]] float GetFacingCos() const { return facing_cos_; }
  [[nodiscard]] float GetFacingSin() const { return facing_sin_; }
  [[nodiscard]] float GetPitchCos() const { return pitch_cos_; }
  [[nodiscard]] float GetPitchSin() const { return pitch_sin_; }
  [[nodiscard]] float GetCurrentSpeed() const { return current_speed_; }

  void ApplyAirborneCollisionKinematics(float current_speed,
                                        float horizontal_x,
                                        float horizontal_y);
  void SetCurrentSpeedFromAirborneCollision(float current_speed);

  bool SetSpeed(openwow::game::SpeedType type, float value);
  [[nodiscard]] float GetSpeed(openwow::game::SpeedType type) const;

  void ApplySplineMovementMode(SplineMovementMode mode);

  void SetRuntimeFlags(std::uint32_t flags) { runtime_flags_ = flags; }
  [[nodiscard]] std::uint32_t GetRuntimeFlags() const { return runtime_flags_; }
  [[nodiscard]] bool HasActiveMoverMotion() const {
    return (runtime_flags_ & kActiveMoverMotionMask) != 0u;
  }
  void SetRuntimeFlags2(std::uint16_t flags2) { runtime_flags2_ = flags2; }
  [[nodiscard]] std::uint16_t GetRuntimeFlags2() const { return runtime_flags2_; }

  [[nodiscard]] float GetRuntimePitch() const { return runtime_pitch_; }
  void SetRuntimePitch(float pitch) { runtime_pitch_ = pitch; }
  [[nodiscard]] std::uint32_t GetRuntimeFallTime() const {
    return runtime_fall_time_;
  }

  void SetRuntimeFallTime(std::uint32_t fall_time) {
    runtime_fall_time_ = fall_time;
  }

  [[nodiscard]] float GetRuntimeFallStartZ() const {
    return runtime_fall_start_z_;
  }
  void SetRuntimeFallStartZ(float fall_start_z) {
    runtime_fall_start_z_ = fall_start_z;
  }
  [[nodiscard]] float GetRuntimeJumpZSpeed() const {
    return runtime_jump_z_speed_;
  }
  [[nodiscard]] float GetRuntimeJumpSinAngle() const {
    return runtime_jump_sin_angle_;
  }
  [[nodiscard]] float GetRuntimeJumpCosAngle() const {
    return runtime_jump_cos_angle_;
  }
  [[nodiscard]] float GetRuntimeJumpXYSpeed() const {
    return runtime_jump_xy_speed_;
  }

  void SetVehicleSeatTransferPacketBit(bool enabled);
  [[nodiscard]] bool HasVehicleSeatTransferPacketBit() const {
    return (vehicle_seat_transfer_runtime_flags_ &
            kVehicleSeatTransferPacketBitMask) != 0u;
  }

  void SetStandAnimRefreshFlag(bool enabled);
  [[nodiscard]] bool HasStandAnimRefreshFlag() const {
    return (vehicle_seat_transfer_runtime_flags_ &
            kStandAnimRefreshBitMask) != 0u;
  }

  [[nodiscard]] bool IsOnTransport() const { return transport_guid_ != 0; }

  [[nodiscard]] bool HasTransferredMovementControl() const {
    return (runtime_flags_ & kVehicleControlTransferFlag) != 0u;
  }
  [[nodiscard]] std::uint64_t GetTransportGuid() const { return transport_guid_; }
  [[nodiscard]] std::uint8_t GetTransportSeat() const { return transport_seat_; }
  [[nodiscard]] CPlayerMoveEventQueue& GetEventQueue() { return event_queue_; }
  [[nodiscard]] const CPlayerMoveEventQueue& GetEventQueue() const { return event_queue_; }
  [[nodiscard]] const QueuedMovementPreview& GetQueuedMovementPreview() const {
    return queued_preview_;
  }
  [[nodiscard]] bool HasPendingRuntimeNotification() const {
    return has_pending_runtime_notification_;
  }
  void ClearPendingRuntimeNotification() {
    has_pending_runtime_notification_ = false;
  }
  void SetRuntimeNotificationCallback(RuntimeNotificationCallback callback) {
    runtime_notification_callback_ = std::move(callback);
  }
  void SetTransportSeatChangeCallback(TransportSeatChangeCallback callback) {
    transport_seat_change_callback_ = std::move(callback);
  }
  void SetTransportParentCommitCallback(
      TransportParentCommitCallback callback) {
    transport_parent_commit_callback_ = std::move(callback);
  }
  void SetTransportParentRebaseCallback(
      TransportParentRebaseCallback callback) {
    transport_parent_rebase_callback_ = std::move(callback);
  }

 private:
  enum class SpeedRefreshTiming : std::uint8_t {
    kNone,
    kBeforeDirection,
    kAfterDirection,
  };

  void ApplyDirectionStateTransition(SpeedRefreshTiming speed_refresh);
  bool SetForwardDirection(bool forward);
  void ApplyStopSwimState();
  void DisableFlyMode();
  void SetFallingSlowState(bool enable);
  void SetHoverStateAndRefresh(bool enable, bool allow_fall_transition);
  bool SetGravityEnabledAndRefresh(bool gravity_enabled);
  void RefreshGravityState();
  void ApplyKnockBackState(const openwow::game::ObjectManager &objects,
                           float direction_x, float direction_y,
                           float horizontal_speed, float vertical_speed);
  void RestoreRuntimeSnapshot(const CPlayerMoveEvent &event);

  CPlayerMoveEventQueue event_queue_;
  std::uint64_t transport_guid_{0};
  std::uint8_t transport_seat_{0};
  std::array<float, 3> transform_position_{};
  float scalar_facing_{0.0f};
  float runtime_pitch_{0.0f};
  std::uint32_t runtime_fall_time_{0};
  float runtime_fall_start_z_{0.0f};
  float runtime_jump_z_speed_{0.0f};
  float runtime_jump_sin_angle_{0.0f};
  float runtime_jump_cos_angle_{0.0f};
  float runtime_jump_xy_speed_{0.0f};
  std::uint32_t runtime_flags_{0};
  std::uint16_t runtime_flags2_{0};
  std::uint32_t parent_movement_flags_{0};
  bool has_parent_movement_{false};
  bool remote_gravity_changed_{false};
  std::uint32_t server_movement_timestamp_baseline_{0};
  std::uint32_t server_event_presentation_anchor_{0};
  std::array<std::int16_t, 32> server_timing_bias_history_{};
  std::uint8_t server_timing_bias_head_{0};
  std::int32_t server_timing_bias_ms_{0};
  std::int64_t packed_orientation_{0};
  bool uses_packed_orientation_{false};
  QueuedMovementPreview queued_preview_{};
  std::uint32_t queued_preview_total_lead_time_ms_{0};
  std::uint32_t queued_preview_source_timestamp_{0};
  std::uint32_t queued_preview_source_event_type_{0};

  std::array<float, 3> prev_position_{};
  float prev_orientation_{0.0f};
  float prev_pitch_{0.0f};
  float interpolation_progress_{0.0f};

  float camera_pitch_min_{0.33333334f};
  float camera_pitch_max_{2.0277777f};
  float camera_zoom_{1.0f};
  bool has_pending_runtime_notification_{false};
  RuntimeNotificationCallback runtime_notification_callback_{};
  TransportSeatChangeCallback transport_seat_change_callback_{};
  TransportParentCommitCallback transport_parent_commit_callback_{};
  TransportParentRebaseCallback transport_parent_rebase_callback_{};
  DispatchStopOpcodeCallback dispatch_stop_opcode_callback_{};
  SetFacingVisualUpdateCallback set_facing_visual_update_callback_{};
  std::uint16_t vehicle_seat_transfer_runtime_flags_{0};

  std::array<float, 3> dir_forward_{};
  float facing_cos_{1.0f};
  float facing_sin_{0.0f};
  float pitch_cos_{1.0f};
  float pitch_sin_{0.0f};
  float current_speed_{0.0f};

  TrigMemo facing_trig_memo_{};
  TrigMemo pitch_trig_memo_{};

  std::array<float, openwow::game::kMaxSpeeds> speed_table_{
      2.5f,
      7.0f,
      4.5f,
      4.7222f,
      2.5f,
      7.0f,
      4.5f,
      3.14159f,
      0.0f,
  };

  float collision_half_width_{0.0f};
  float collision_height_product_{0.0f};
  float collision_scale_ratio_{1.0f};

  float ground_slope_z_{0.0f};

  float cumulative_collision_z_{0.0f};

  static constexpr std::uint32_t kTerrainBasePlayer = 0x100111u;
  static constexpr std::uint32_t kTerrainBaseNPC    = 0x102111u;

  static constexpr std::uint32_t kBoneTerrainBasePlayer = 0x000111u;
  static constexpr std::uint32_t kBoneTerrainBaseNPC = 0x002111u;

  static constexpr std::uint32_t kTerrainCanControl    = 0x80000000u;
  static constexpr std::uint32_t kTerrainWaterWalk     = 0x10000u;
  static constexpr std::uint32_t kTerrainFlying        = 0x200u;
  static constexpr std::uint32_t kTerrainNonHoverFly   = 0x20000u;

  static constexpr std::uint32_t kTerrainPlayerGhost   = 0x8000u;

  static constexpr std::uint32_t kFlagWaterWalking = 0x10000000u;
  static constexpr std::uint32_t kFlagSwimming     = 0x00200000u;
  static constexpr std::uint32_t kFlagFlying       = 0x02000000u;

  static constexpr std::uint16_t kFlags2WaterWalkSlopeOverride = 0x0200u;
  static constexpr std::uint16_t kFlags2HoverFlight            = 0x4000u;

  static constexpr float kWaterWalkSlopeThreshold = -0.6457718f;

  static constexpr std::uint16_t kVehicleSeatTransferPacketBitMask = 0x0040u;
  static constexpr std::uint16_t kStandAnimRefreshBitMask = 0x0100u;

  static constexpr std::uint32_t kInitSuppressActiveMotion =
      0x2201000u;
  static constexpr std::uint32_t kInitBlockedByRootOrTransport =
      0xA00u;
  static constexpr std::uint32_t kInitResetClearMask =
      0x06E01000u;
  static constexpr std::uint16_t kFlags2SuppressBit = 0x0004u;
  static constexpr std::uint16_t kFlags2AllowPitchBit = 0x0020u;

  static constexpr std::uint32_t kCleanupFlagRetainMask = 0x77FFFFFFu;

  static constexpr std::uint32_t kMaxUpdateStepMs = 250u;

  static constexpr std::uint32_t kActiveMoverMotionMask = 0xC0100Fu;

  static constexpr std::uint32_t kActiveMotionFlagMask = 0x40C010FFu;

  static constexpr std::uint32_t kVehicleControlTransferFlag = 0x00000200u;

  static constexpr std::uint32_t kRuntimeOwnedFlagMask = 0x88000200u;

  static constexpr std::uint32_t kPacketOwnedFlagMask = ~kRuntimeOwnedFlagMask;

  static constexpr std::uint32_t kSyncPreservedRuntimeFlagMask =
      kVehicleControlTransferFlag | 0x80000000u;

  UpdateCallbacks update_callbacks_{};
  RuntimeTimelineState fallback_runtime_timeline_{};
  EventProcessingGateCallback event_processing_gate_callback_{};
  DispatchMovementOpcodeCallback dispatch_movement_opcode_callback_{};
  DispatchDeferredAuthoritativeMovementCallback
      dispatch_deferred_authoritative_movement_callback_{};

  CPlayerMoveEvent MakeEvent(std::uint32_t timestamp,
                             std::uint32_t event_type);

  CPlayerMoveEvent MakeAngleEvent(std::uint32_t timestamp,
                                  std::uint32_t event_type,
                                  float primary_angle,
                                  float secondary_angle);

  void CaptureRuntimeSnapshot(CPlayerMoveEvent& event) const;

  void RebaseQueuedTransportState(float facing_delta);

  void ClearQueuedMovementPreview();

  void QueueAndNotify(CPlayerMoveEvent event);

  void RebaseTransportInterpolationState(const float* transform_matrix,
                                         float facing_delta,
                                         bool clear_spline);

  bool TryResetMovementState();
};

}

#pragma once

#include "openwow/game/monster_move.h"
#include "openwow/game/movement/player_move_event.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/vec3.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class CGUnit_C;
class CInputControl;
class MovementCollisionSolver;
class ObjectManager;
class UnitMissileTrajectory_C;
class WorldSession;
struct TransportMotionStep;
struct C3Vector;
struct MovementCollisionResult;
struct MovementInfo;
struct MovementParentTransform;
struct MovementOnlyUpdate;
struct MovementUpdate;
struct ProcessMovementDecision;

class UnitMovementRuntime final {
public:
  explicit UnitMovementRuntime(CGUnit_C &owner) noexcept : owner_(owner) {
    data_.Init();
  }

  UnitMovementRuntime(const UnitMovementRuntime &) = delete;
  UnitMovementRuntime &operator=(const UnitMovementRuntime &) = delete;

  [[nodiscard]] movement::CMovementData &Data() noexcept { return data_; }
  [[nodiscard]] const movement::CMovementData &Data() const noexcept { return data_; }

  void ApplyCreateUpdate(std::uint32_t client_receive_tick_ms);
  void RefreshCreateRootedGroundContact();

  void RunMovementPostUpdate(WorldSession &session, std::uint32_t timestamp,
                             std::uint32_t frame_delta_ms);
  void UpdateWaterRipples(std::uint32_t timestamp,
                          std::uint32_t explicit_event = 0u);
  bool ApplyMovementUpdate(const MovementOnlyUpdate &update);
  void ApplyPredictedMovement(const MovementInfo &movement_info);
  void ApplyActiveMoverMovementUpdate(WorldSession &session,
                                      const MovementOnlyUpdate &update);

  void ApplyActiveMoverTeleportAck(WorldSession &session,
                                   MovementOnlyUpdate update);
  void SynchronizeAuthoritativeState(WorldSession &session,
                                     const MovementUpdate &update,
                                     std::uint32_t client_receive_tick_ms);
  void ApplyCreateMovementMetadata(WorldSession &session,
                                   const MovementUpdate &update);
  void ApplySplineSpeedChange(WorldSession &session, SpeedType type,
                              float speed, std::uint32_t movement_opcode);
  void ApplySplineMovementPose(const Vec3 &position, float facing,
                               bool spline_active,
                               bool moving_backward = false,
                               bool has_coordinate_parent_binding = false,
                               ObjectGuid coordinate_parent = {},
                               std::int8_t coordinate_parent_seat = -1,
                               std::uint32_t spline_flags = 0u,
                               float spline_arc_length = 0.0f,
                               std::uint32_t spline_duration_ms = 0u,
                               bool parent_movement_active = false);
  void ClearSplineMovementPoseOwnership();
  [[nodiscard]] bool TrySettleSplineMovementPoseOwnership();
  void StopLocomotionForDeath(WorldSession &session);
  [[nodiscard]] bool HasSplineMovementPoseOwnership() const noexcept {
    return spline_movement_pose_owned_;
  }
  [[nodiscard]] bool HasActiveSplineLocomotion() const noexcept {
    return spline_locomotion_active_;
  }

  [[nodiscard]] float ComputeCurrentSpeed() const;

  [[nodiscard]] bool HasNonExemptFlyingSpline() const noexcept;

  [[nodiscard]] bool HasNonExemptSplineFlag(
      std::uint32_t flag) const noexcept {
    return spline_locomotion_active_ &&
           (spline_locomotion_flags_ & SplineFlag::kStateQueryExempt) == 0u &&
           (spline_locomotion_flags_ & flag) != 0u;
  }
  [[nodiscard]] bool IsSplineLocomotionBackward() const noexcept {
    return spline_locomotion_backward_;
  }
  void ApplyServerMovementTimeSkipped(std::uint32_t skipped_time_ms);
  void ApplyQueuedPreview(std::uint32_t current_tick_ms,
                          std::uint32_t frame_delta_ms);
  void ApplyInputControlMovement(WorldSession &session,
                                 CInputControl &control,
                                 const ProcessMovementDecision &decision);

  void FaceTowardObject(std::uint64_t target_guid, bool update_visuals);
  void TurnTowardTarget(WorldSession &session);
  void SeedBodyFacing(float orientation);
  void UpdateBodyFacing(float *vehicle_facing);

  void UpdateSmoothBodyFacing(float dt_seconds);

  void SettleBodyTwistBoneOverrides();

  void OffsetBodyFacingAngles(float delta);
  [[nodiscard]] float BodyFacing() const noexcept { return body_facing_; }
  [[nodiscard]] float SmoothBodyFacing() const noexcept {
    return smooth_body_facing_;
  }

  [[nodiscard]] float WorldSmoothBodyFacing() const;
  [[nodiscard]] std::uint32_t UpdateFlags() const noexcept {
    return update_flags_;
  }
  [[nodiscard]] std::uint8_t GroundOrientationMode() const noexcept {
    return ground_orientation_mode_;
  }
  void SetGroundOrientationMode(std::uint8_t mode) noexcept {
    ground_orientation_mode_ = mode;
  }
  void InterpolateShadowBlobPosition(float dt);
  void BlendMountTransitionPosition(float dt, std::uint32_t current_tick_ms);
  void BuildStaticBodyMatrix(float *out_matrix) const;
  void ComputeBodyLeanMatrix(float *out_matrix, float dt, bool apply_lean);
  [[nodiscard]] bool HasBodyLean() const noexcept {
    return (body_lean_flags_ & 7u) != 0u;
  }
  void MarkBodyLeanMoving() noexcept { body_lean_flags_ |= 1u; }
  void ResetFlightTransitionBodyLeanState();

  [[nodiscard]] bool CanControlCharacter() const;
  [[nodiscard]] bool IsNavigableAsPlayer() const;

  [[nodiscard]] bool IsActivePlayerMover() const;

  [[nodiscard]] bool IsGhostPlayerDescriptorPair() const;
  [[nodiscard]] std::uint32_t BuildTerrainIntersectFlags() const;
  [[nodiscard]] bool IsLocallyControlled() const;
  [[nodiscard]] bool CanChangeDirection() const;
  [[nodiscard]] bool CanTurn() const;
  [[nodiscard]] bool IsFacingTarget(const CGUnit_C &other) const;
  [[nodiscard]] bool IsFacingTarget(ObjectGuid target_guid) const;
  [[nodiscard]] bool IsMoving() const;
  [[nodiscard]] bool IsFlying() const;
  [[nodiscard]] bool IsSwimming() const;

  [[nodiscard]] bool IsInWater() const;
  [[nodiscard]] bool IsRooted() const;
  [[nodiscard]] bool HasFallingLaunchVelocity() const;
  [[nodiscard]] bool IsMovingAtRunPace() const;
  [[nodiscard]] bool IsMovingAtWalkPace() const;
  [[nodiscard]] bool HasCanFlyFlag() const;

  void Update(WorldSession &session, std::uint32_t current_tick_ms);
  void HandleArrival();
  void IncrementMoveSequence(std::int32_t delta) noexcept;
  [[nodiscard]] std::int32_t MoveSequence() const noexcept {
    return move_sequence_;
  }
  void SetSpeedBounds(float walk_speed, float run_speed) noexcept;
  [[nodiscard]] const float *SpeedBounds() const noexcept {
    return speed_bounds_.data();
  }

  void StartSwim(std::uint32_t timestamp, bool force_remote);
  void StopSwim(std::uint32_t timestamp, bool force_remote);
  void SwimToFly(std::uint32_t timestamp, bool enable_fly_mode);
  void ReconcileCanFlyGroundContact(
      const WorldSession &session, std::uint32_t timestamp,
      std::optional<float> ground_surface_height,
      std::optional<float> vertical_clearance);
  bool InterpolateSwimHeight(WorldSession &session, float water_level,
                             float current_z, float target_pitch,
                             float delta_step, std::int32_t timestamp,
                             bool *out_steep);

  void SetClientControlState(openwow::world::WorldCamera *camera, bool enabled);
  void SynchronizeBoundWorldCamera(openwow::world::WorldCamera *camera) const;
  void SendSetFacing(WorldSession &session, std::uint32_t timestamp,
                     float facing);
  void SendBoundedTurnFacing(WorldSession &session, std::uint32_t timestamp,
                             float facing);
  void SendSetPitch(WorldSession &session, std::uint32_t timestamp, float pitch);
  void SendSetVehiclePitch(WorldSession &session, std::uint32_t timestamp,
                           float pitch);
  void SendTurnMovement(std::uint32_t timestamp, bool turning_right);
  void SendPitchMovement(std::uint32_t timestamp, bool pitching_down);
  [[nodiscard]] bool ClampAngleToVehicleYawWindow(float *angle) const;
  void StopForward(std::uint32_t timestamp);
  void StopStrafe(std::uint32_t timestamp);
  void StopVertical(std::uint32_t timestamp);
  void StopTurn(std::uint32_t timestamp);
  void ToggleRun(std::uint32_t timestamp);
  void QueueHeartbeat(std::uint32_t timestamp);
  void InputControlStopForward(std::uint32_t timestamp);

  void InputControlClearClickToMoveFacingFlags(std::uint32_t timestamp);
  static void SetInputControlPitchFlag(bool enable, std::uint32_t timestamp);
  void SendForward(WorldSession &session, std::uint32_t timestamp, bool forward);
  void SendStrafe(WorldSession &session, int direction, std::uint32_t timestamp);
  void SendVertical(WorldSession &session, int direction,
                    std::uint32_t timestamp);
  void SendTurn(WorldSession &session, int direction, std::uint32_t timestamp);
  void SendPitch(WorldSession &session, int direction, std::uint32_t timestamp);
  void SendStopPitch(WorldSession &session, std::uint32_t timestamp);
  bool SendSimpleMovePacket(const WorldSession &session, std::uint16_t opcode,
                            std::uint32_t timestamp);

  bool SendImmediateMovementPacket(const WorldSession &session,
                                   std::uint16_t opcode,
                                   std::uint32_t timestamp);
  void SendJump(WorldSession &session, std::uint32_t timestamp);
  void GameUIAutoWalk(WorldSession &session, const float *direction);

  int DispatchClientMovementOpcode(WorldSession &session, std::uint32_t opcode,
                                   const MovementInfo &movement_info,
                                   std::uint32_t client_receive_tick);
  int ApplySplineMovementStateOpcode(WorldSession &session,
                                     std::uint32_t opcode);
  int DispatchSpeedAckOpcode(WorldSession &session, std::uint32_t opcode,
                             const MovementInfo &movement_info,
                             std::uint32_t client_receive_tick, float value);
  bool HandleRemoteMoveChangeTransportSeat(WorldSession &session,
                                           const MovementInfo &movement_info,
                                           std::uint32_t client_receive_tick);

  void ArmDeferredAutoRelease();

  bool ForceSetTransport(WorldSession &session, std::uint64_t transport_guid,
                         std::uint8_t seat, bool force = false);
  bool ApplyTransportMotionCollision(WorldSession &session,
                                     const TransportMotionStep &step,
                                     std::uint32_t client_time_ms);
  void RebaseAutoAttackForTransportChange(const float *matrix4x4,
                                          float facing_delta);

  void RebaseTransportSplineState(const float *matrix4x4,
                                  float facing_delta,
                                  bool leaving_parent,
                                  std::uint64_t parent_guid,
                                  std::uint8_t parent_seat);

  void ApplyTransportParentRebaseSideEffects(ObjectManager &objects,
                                             bool leaving_parent,
                                             std::uint64_t parent_guid,
                                             float body_facing_delta);
  void Cleanup();
  void ResetState() noexcept;

  [[nodiscard]] static float ComputePitchAngle(const float *a, const float *b);
  static void ResetCanFlyGroundContactRuntimeForTesting();

 private:

  void NoteSuccessfulMovementPacket(std::uint32_t timestamp);
  friend struct PlayerControlRuntime;

  enum class MovementUpdateOrigin : std::uint8_t {
    kAuthoritative,
    kPredicted,
  };

  enum class TransportChangeResult : std::uint8_t {
    kUnchanged,
    kApplied,
    kClearedStaleParent,
  };

  void ApproachSmoothBodyFacing(float target, float dt_seconds);

  void CommitMovementUpdate(const MovementOnlyUpdate &update,
                            MovementUpdateOrigin origin);
  void AdvanceMovementStep(WorldSession &session,
                           std::uint32_t timestamp,
                           std::uint32_t step_ms,
                           bool commit_owner = true);

  void LogTransportSolveBail(WorldSession &session, const char *reason);

  void AdoptTransportParentFromGroundContact(
      WorldSession &session, const MovementCollisionResult &collision);

  [[nodiscard]] bool ResolveMovementParentTransform(
      const ObjectManager &objects, MovementParentTransform &out);

  [[nodiscard]] bool TryDetachTransportParentForMovementStep(
      WorldSession &session);

  void ForgetMovementParentTransform() noexcept;

  void PublishSolvedTransformPosition(const ObjectManager &objects,
                                      const C3Vector &transform_position,
                                      bool has_parent,
                                      MovementInfo &movement_info) const;

  TransportChangeResult ApplyTransportChange(
      WorldSession &session, std::uint64_t transport_guid,
      std::uint8_t seat,
      bool seed_from_owner = true, bool force = false);
  void RebaseAutoAttackForTransportGuidChange(
      ObjectManager &objects, std::uint64_t old_transport_guid,
      std::uint64_t new_transport_guid);
  void FinalizeActiveMoverRelease(WorldSession &session,
                                  std::uint32_t timestamp);
  void PrepareActiveMoverControl(WorldSession &session,
                                 std::uint32_t timestamp);
  void FlushMovementStateForControlTransition(WorldSession &session,
                                              std::uint32_t timestamp,
                                              bool finalize_stops);

  void InstallEventProcessingGate();

  void InstallUpdateScopedCallbacks();
  void CommitMovementRuntimeState(
      WorldSession &session, std::uint32_t timestamp,
      std::optional<std::uint16_t> opcode = std::nullopt,
      const movement::CPlayerMoveEvent *event = nullptr);
  int ApplyClientMovementOpcodeState(WorldSession &session,
                                     std::uint32_t opcode,
                                     const MovementInfo &movement_info,
                                     std::uint32_t presentation_tick,
                                     std::uint32_t application_tick);
  void ImportRemoteMovementOpcodeState(WorldSession &session,
                                       const MovementInfo &movement_info,
                                       std::uint32_t presentation_tick,
                                       std::uint32_t application_tick);
  void CommitRemoteMovementOpcodeState(WorldSession &session,
                                       const MovementInfo &movement_info,
                                       std::uint32_t presentation_tick,
                                       std::uint32_t application_tick);
  int ApplySpeedAckOpcodeState(WorldSession &session, std::uint32_t opcode,
                               const MovementInfo &movement_info,
                               std::uint32_t presentation_tick,
                               std::uint32_t application_tick, float value);

  CGUnit_C &owner_;
  movement::CMovementData data_{};

  bool deferred_auto_release_pending_{false};

  bool body_twist_override_active_{false};

  bool pending_landing_animation_suppressed_{false};
  std::weak_ptr<MovementCollisionSolver> collision_solver_source_{};
  std::shared_ptr<MovementCollisionSolver> collision_solver_{};

  bool collision_stepping_{false};
  bool spline_movement_pose_owned_{false};
  bool spline_locomotion_active_{false};
  bool spline_locomotion_backward_{false};

  std::uint32_t spline_locomotion_flags_{0};
  float spline_locomotion_arc_length_{0.0f};
  std::uint32_t spline_locomotion_duration_ms_{0};
  ObjectGuid spline_coordinate_parent_{};
  std::int8_t spline_coordinate_parent_seat_{-1};
  Vec3 spline_local_position_{};
  float spline_local_facing_{0.0f};
  bool spline_pose_waiting_for_parent_{false};

  std::array<float, 16> movement_parent_matrix_{};
  std::uint64_t movement_parent_guid_{0};
  std::uint64_t movement_parent_revision_{0};
  bool has_movement_parent_matrix_{false};

  std::uint32_t last_transport_solve_bail_log_ms_{0};
  std::uint32_t update_flags_{0};
  std::int32_t move_sequence_{0};
  std::array<float, 6> speed_bounds_{};
  std::array<float, 3> ground_contact_normal_{{0.0f, 0.0f, 1.0f}};
  std::uint8_t ground_orientation_mode_{0};

  struct GroundAlignedMatrixMemo {
    bool valid{false};
    std::array<float, 3> position{};
    float body_facing{0.0f};
    float scale{0.0f};
    std::array<float, 3> normal{};
    std::uint8_t orientation_mode{0};
    std::array<float, 16> matrix{};
  };
  GroundAlignedMatrixMemo ground_aligned_matrix_memo_{};
  float body_lean_last_facing_{0.0f};
  float body_lean_yaw_{0.0f};
  float body_lean_pitch_{0.0f};
  float body_lean_heading_{0.0f};
  std::uint32_t body_lean_flags_{0};

  float smooth_body_facing_{0.0f};
  float smooth_body_facing_velocity_{0.0f};
  float smooth_body_facing_blend_{1.0f};

  std::uint32_t turn_input_idle_tick_ms_{0};
  float body_facing_{0.0f};
  float body_facing_speed_{0.0f};
  std::array<float, 4> body_facing_samples_{};
  float locked_facing_{0.0f};
  std::uint32_t locked_facing_time_{0};
  movement::CMovementData::RuntimeTimelineState movement_timeline_{};

  WorldSession *update_session_{nullptr};

  std::uint32_t update_dispatch_tick_ms_{0};

  bool persistent_callbacks_installed_{false};

  bool update_callbacks_live_{false};
  std::uint32_t last_movement_update_tick_{0};
  bool has_movement_update_tick_{false};
  float previous_liquid_depth_{0.0f};
  bool in_water_{false};
  std::uint32_t next_water_ripple_timestamp_{0u};

  UnitSoundGroundState post_update_ground_state_{};
  bool post_update_ground_state_result_{false};
  std::uint32_t post_update_ground_state_tick_{0u};
  std::array<float, 3> post_update_ground_state_position_{};
  bool post_update_ground_state_stamped_{false};

  [[nodiscard]] bool TryComposeSplineParentPose();
};

}

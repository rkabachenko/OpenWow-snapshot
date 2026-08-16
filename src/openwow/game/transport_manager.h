
#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct TransportPathNode {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::uint32_t mapId = 0;
  std::uint32_t delay = 0;
  std::uint32_t actionFlag = 0;
  std::uint32_t arrivalTime = 0;

};

struct TransportRotationKeyframe {
  std::uint32_t timeIndex = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

struct TransportBasePose {
  std::array<float, 3> position{};
  std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct TransportPathInfo {
  std::uint32_t pathId = 0;
  std::vector<TransportPathNode> nodes;
  std::vector<TransportRotationKeyframe> rotation_keyframes;
  std::uint32_t totalCycleTime = 0;
  TransportBasePose base_pose{};
  bool uses_local_animation = false;

  bool live_pose = false;
};

enum class TransportMoveState : std::uint8_t {
  Moving = 0,
  Stopped = 1,
};

struct TransportVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct TransportWorldPos {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float o = 0.0f;
  std::uint32_t mapId = 0;
};

struct TransportPassengerAttachment {
  float offset_x = 0.0f;
  float offset_y = 0.0f;
  float offset_z = 0.0f;
  float offset_o = 0.0f;
};

struct TransportFrameAdvance {
  TransportVec3 collision_start{};
  TransportVec3 collision_end{};
  std::uint32_t collision_elapsed_ms = 0;

  std::optional<std::uint32_t> sequence_change;
};

struct TransportMotionStep {
  ObjectGuid guid{};
  std::uint32_t elapsed_ms = 0;

  TransportVec3 direction{};

  float distance = 0.0f;
  std::array<float, 16> world_transform{};
};

struct TransportSequenceChange {
  ObjectGuid guid{};
  std::uint32_t sequenceId = 0;
};

struct TransportManagerUpdate {
  std::vector<TransportMotionStep> motion_steps;
  std::vector<TransportSequenceChange> sequence_changes;
};

class Transport {
public:
  Transport() = default;
  Transport(ObjectGuid guid, std::uint32_t entry, std::uint32_t displayId,
            const TransportPathInfo &path);

  [[nodiscard]] ObjectGuid GetGuid() const {
    return guid_;
  }
  [[nodiscard]] std::uint32_t GetEntry() const {
    return entry_;
  }
  [[nodiscard]] std::uint32_t GetDisplayId() const {
    return displayId_;
  }

  [[nodiscard]] const TransportPathInfo &GetPath() const {
    return path_;
  }
  [[nodiscard]] bool HasPath() const {
    return !path_.nodes.empty();
  }
  [[nodiscard]] std::uint32_t GetPathNodeCount() const;

  void SetPathTimer(std::uint32_t timerMs);
  [[nodiscard]] std::uint32_t GetPathTimer() const {
    return pathTimerMs_;
  }

  [[nodiscard]] TransportVec3 InterpolatePosition() const;

  [[nodiscard]] TransportVec3 InterpolatePosition(std::uint32_t timerMs) const;

  [[nodiscard]] std::uint32_t GetCurrentMapId() const;

  [[nodiscard]] float GetFacing() const;

  [[nodiscard]] std::array<float, 4> GetWorldRotationQuaternion() const;

  void BuildWorldTransform(float *out_matrix_4x4) const;

  [[nodiscard]] TransportMoveState GetMoveState() const;

  [[nodiscard]] std::size_t GetCurrentNodeIndex() const;

  [[nodiscard]] std::uint32_t GetMapIdAtTimer(std::uint32_t timerMs) const;

  void AddPassenger(ObjectGuid playerGuid, float offsetX, float offsetY, float offsetZ,
                    float offsetO);
  void RemovePassenger(ObjectGuid playerGuid);
  [[nodiscard]] bool HasPassenger(ObjectGuid playerGuid) const;
  [[nodiscard]] bool MatchesPassengerAttachment(ObjectGuid playerGuid, float offsetX, float offsetY,
                                                float offsetZ) const;
  [[nodiscard]] std::size_t GetPassengerCount() const;
  [[nodiscard]] const std::unordered_set<std::uint64_t> &GetPassengers() const;

  [[nodiscard]] TransportWorldPos GetWorldPosition(float offsetX, float offsetY, float offsetZ,
                                                   float offsetO) const;

  TransportFrameAdvance AdvanceFrame(float dt);

  void SyncFromServer(float x, float y, float z, float facing, std::uint32_t mapId,
                      std::uint32_t pathTimerMs);

  void SetLivePoseMotion(const TransportVec3 &previous_position,
                        const TransportVec3 &current_position,
                        std::uint32_t elapsed_ms);

  void SyncGameObjectAnimationState(std::uint32_t absolute_tick_ms, std::uint8_t previous_state,
                                    std::uint8_t current_state, std::uint16_t packed_progress);

  void SyncMOTransportAnimationState(std::uint32_t absolute_tick_ms, std::uint8_t current_state,
                                     std::uint16_t packed_progress, std::uint16_t dynamic_flags);

  void ReplayMOTransportAnimationStateChange(std::uint32_t absolute_tick_ms,
                                             std::uint8_t previous_state,
                                             std::uint8_t current_state);

  void Rebind(std::uint32_t entry, std::uint32_t displayId, const TransportPathInfo &path);

  void RefreshLivePosePath(const TransportPathInfo &path);

  void ResetHandlerCommittedState();

private:
  struct GameObjectAnimationStateClock {
    std::uint32_t stopped_duration_ms = 0;
    std::uint32_t cycle_duration_ms = 0;
    std::uint32_t anchor_tick_ms = 0;
    std::uint32_t transition_offset_ms = 0;
    std::uint32_t absolute_tick_ms = 0;
    std::int32_t committed_state = -1;
    std::uint8_t active_state = 0;
    bool initialized = false;

    [[nodiscard]] std::uint32_t StateDuration(std::uint8_t state) const;
    void Seed(std::uint32_t absolute_tick_ms, std::uint8_t current_state,
              std::uint16_t packed_progress);
    void RequestStateChange(std::uint32_t absolute_tick_ms, std::uint8_t previous_state,
                            std::uint8_t current_state);
    void Advance(std::uint32_t absolute_tick_ms);
    [[nodiscard]] std::uint32_t Resolve(std::uint32_t absolute_tick_ms,
                                        std::uint8_t current_state) const;
    [[nodiscard]] float ComputeProgress(std::uint32_t absolute_tick_ms,
                                        std::uint8_t current_state) const;
  };

  struct MOTransportAnimationStateClock {
    std::uint32_t cycle_duration_ms = 0;
    std::uint32_t phase_offset_ms = 0;
    std::uint32_t latched_segment_end_ms = 0;
    std::uint32_t absolute_tick_ms = 0;
    bool ready_state = false;
    bool latched_to_segment_end = false;
    bool initialized = false;
  };

  struct LivePoseMotion {
    TransportVec3 previous_position{};
    TransportVec3 current_position{};
    std::uint32_t elapsed_ms = 0;
  };

  ObjectGuid guid_;
  std::uint32_t entry_ = 0;
  std::uint32_t displayId_ = 0;
  TransportPathInfo path_;
  std::uint32_t pathTimerMs_ = 0;
  GameObjectAnimationStateClock game_object_animation_clock_{};
  MOTransportAnimationStateClock mo_transport_animation_clock_{};

  std::optional<LivePoseMotion> pending_live_pose_motion_{};

  mutable TransportVec3 cachedPos_;
  mutable std::uint32_t cachedTimer_ = UINT32_MAX;

  std::optional<std::uint32_t> lastEmittedSequenceId_;

  std::unordered_set<std::uint64_t> passengers_;
  std::unordered_map<std::uint64_t, TransportPassengerAttachment> passenger_attachments_;

  [[nodiscard]] bool SupportsGameObjectAnimationClock() const;
  [[nodiscard]] std::uint32_t WrapTimer(std::uint32_t timerMs) const;
  [[nodiscard]] std::uint32_t ResolveCurrentSegmentEnd(std::uint32_t timerMs) const;
  void SeedMOTransportAnimationClock(std::uint32_t absolute_tick_ms, std::uint16_t packed_progress);
  void AlignMOTransportClockToPathTime(std::uint32_t absolute_tick_ms, std::uint32_t path_time_ms);
  void ApplyMOTransportReadyState(std::uint32_t absolute_tick_ms, bool ready_state);
  void LatchMOTransportReadySegment(std::uint32_t absolute_tick_ms);
  void AdvanceMOTransportAnimationClock(std::uint32_t absolute_tick_ms,
                                        std::uint32_t frame_delta_ms);
  [[nodiscard]] std::uint32_t ResolveMOTransportPathTime(std::uint32_t absolute_tick_ms) const;
  [[nodiscard]] static bool DidMOTransportClockCrossBoundary(std::uint32_t boundary_start,
                                                             std::uint32_t boundary_end,
                                                             std::uint32_t current_value);
  void AdvanceTimer(std::uint32_t elapsed_ms);
  [[nodiscard]] std::pair<std::size_t, float> FindSegment(std::uint32_t timerMs) const;
  void InvalidateCache() const;
};

class TransportManager {
public:

  void OnTransportCreate(ObjectGuid guid, std::uint32_t entry, std::uint32_t displayId,
                         const TransportPathInfo &path);

  void OnTransportDestroy(ObjectGuid guid);

  void OnTransportUpdate(ObjectGuid guid, float x, float y, float z, float facing,
                         std::uint32_t mapId, std::uint32_t pathTimerMs);

  [[nodiscard]] const Transport *GetTransport(ObjectGuid guid) const;
  [[nodiscard]] Transport *GetTransportMutable(ObjectGuid guid);
  [[nodiscard]] std::vector<const Transport *> GetAllTransports() const;
  [[nodiscard]] std::vector<const Transport *> GetTransportsOnMap(std::uint32_t mapId) const;
  [[nodiscard]] std::size_t GetTransportCount() const;

  [[nodiscard]] const Transport *FindTransportForPassenger(ObjectGuid playerGuid) const;

  TransportManagerUpdate Update(float dt);

  void Clear();

private:
  std::unordered_map<std::uint64_t, Transport> transports_;
};

}

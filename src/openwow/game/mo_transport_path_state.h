
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "openwow/render/models/animation/spline.h"

namespace openwow::game {

enum class MOTransportMovePhase : std::uint32_t {
  Stopped       = 0,
  Accelerating  = 162,
  ConstantSpeed = 163,
  Decelerating  = 164,
};

struct MOTransportStopEntry {
  std::uint32_t timestampMs = 0;
  float accumulatedDistance = 0.0f;
  std::uint32_t durationMs = 0;
};

struct MOTransportTimeEvent {
  std::uint32_t timestampMs = 0;
  std::uint32_t eventId = 0;
};

struct TaxiPathNodeRaw {
  std::uint32_t id = 0;
  std::uint32_t pathId = 0;
  std::uint32_t sequenceIndex = 0;
  std::uint32_t mapId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  std::uint32_t flags = 0;
  std::uint32_t delaySeconds = 0;
  std::uint32_t arrivalEventId = 0;
  std::uint32_t departureEventId = 0;
};

struct MOTransportPathSegment {
  std::uint32_t mapId = 0;
  render::CSpline spline{render::CSpline::CurveType::kCatmullRom};
  std::uint32_t startTimeMs = 0;
  std::uint32_t endTimeMs = 0;
  std::vector<MOTransportStopEntry> stops;
};

struct WaveOscillationParams {

  float waveAmplitude = 0.0f;
  float waveTimeScale = 0.0f;
  float rollAmplitude = 0.0f;
  float rollTimeScale = 0.0f;
  float pitchAmplitude = 0.0f;
  float pitchTimeScale = 0.0f;
  float maxBank = 0.0f;
  float maxBankTurnSpeed = 0.0f;
  float speedDampThreshold = 0.0f;
  float speedDamp = 0.0f;
};

struct MOTransportEvalResult {
  std::uint32_t mapId = 0;
  MOTransportMovePhase movePhase = MOTransportMovePhase::Stopped;
  std::array<float, 3> position{};

  std::array<float, 3> pathPosition{};
  float facing = 0.0f;
  std::uint32_t segmentIndex = 0;
  float pitch = 0.0f;
  float roll = 0.0f;
};

struct FindSegmentResult {
  std::uint32_t segmentIndex = 0;
  std::uint32_t stopIndex = 0;
};

class MOTransportTimedPathState {
 public:
  MOTransportTimedPathState() = default;

  void SetMaxVelocity(float v) { maxVelocity_ = v; }
  void SetAcceleration(float a) { acceleration_ = a; }

  void SetCycleDuration(std::uint32_t ms) {
    cycleDurationMs_ = ms;
    if (!segments_.empty()) {
      segments_.back().endTimeMs = ms;
    }
  }
  void SetTimeOffset(std::uint32_t ms) { timeOffsetMs_ = ms; }
  void SetReadyState(bool ready) { readyState_ = ready; }
  void SetLatchedToEnd(bool latched) { latchedToEnd_ = latched; }
  void SetLatchedTime(std::uint32_t ms) { latchedTimeMs_ = ms; }

  [[nodiscard]] float GetMaxVelocity() const { return maxVelocity_; }
  [[nodiscard]] float GetAcceleration() const { return acceleration_; }
  [[nodiscard]] std::uint32_t GetCycleDuration() const { return cycleDurationMs_; }

  void SetSegments(std::vector<MOTransportPathSegment> segs) {
    segments_ = std::move(segs);
  }
  [[nodiscard]] const std::vector<MOTransportPathSegment>& GetSegments() const {
    return segments_;
  }

  void SetWaveParams(const WaveOscillationParams& p) {
    waveParams_ = p;
    hasWaveParams_ = true;
  }
  [[nodiscard]] bool HasWaveParams() const { return hasWaveParams_; }

  [[nodiscard]] MOTransportEvalResult EvaluatePathAtTime(
      std::uint32_t absoluteTimeMs,
      std::uint32_t timeDeltaMs,
      bool directTimeMode = false,
      bool computePitchRoll = true);

  [[nodiscard]] std::uint32_t GetCurrentPathTime(
      std::uint32_t absoluteTimeMs) const;

  [[nodiscard]] std::uint32_t ResolvePathTime(
      std::uint32_t absoluteTimeMs,
      std::uint32_t timeDeltaMs,
      bool directTimeMode);

  bool LatchReadySegment(std::uint32_t absoluteTimeMs);

  void ApplyReadyState(std::uint32_t absoluteTimeMs, bool ready);

  void SeedFromPackedProgress(std::uint32_t absoluteTimeMs, float progress);

  void BuildFromTaxiPathNodes(
      const std::vector<TaxiPathNodeRaw>& nodes,
      float maxVelocity,
      float acceleration,
      const WaveOscillationParams* waveParams = nullptr);

  [[nodiscard]] const std::vector<MOTransportTimeEvent>& GetTimeEvents() const {
    return timeEvents_;
  }

 private:

  [[nodiscard]] float CalcDistanceCruiseToStop(
      float elapsedSec, float segDurSec,
      MOTransportMovePhase& outPhase, float& outCurrentVel) const;

  [[nodiscard]] float CalcDistanceAccelerateCruiseDecelerate(
      float elapsedSec, float segDurSec,
      MOTransportMovePhase& outPhase, float& outCurrentVel) const;

  void ComputeProceduralPitchRoll(
      float currentVelocity, float facing, float distanceAlongSpline,
      std::uint32_t segmentIndex, std::uint32_t pathTimeMs,
      std::array<float, 3>& inOutPosition,
      float& outPitch, float& outRoll) const;

  void ApplyWaveOscillation(
      std::uint32_t pathTimeMs, float currentVelocity, float steering,
      std::array<float, 3>& position,
      float& outPitch, float& outRoll) const;

  [[nodiscard]] static bool CrossesBoundaryWindow(
      std::uint32_t windowStart, std::uint32_t windowEnd,
      std::uint32_t value);

  [[nodiscard]] FindSegmentResult FindSegment(std::uint32_t pathTime) const;

  void AlignOffsetToPathTime(std::uint32_t absoluteTimeMs,
                             std::uint32_t targetPathTime);

  [[nodiscard]] std::uint32_t CalcTravelTimeSymmetric(float distance) const;

  [[nodiscard]] std::uint32_t CalcTravelTimeAcceleratingFromRest(
      float distance) const;

  struct LegBuildInput {
    std::vector<render::C3Vector> controlPoints;
    struct StopInput {
      std::uint32_t controlPointIndex = 0;
      std::uint32_t delayMs = 0;
    };
    std::vector<StopInput> stops;
    struct AnimEventInput {
      std::uint32_t controlPointIndex = 0;
      std::uint32_t arrivalEventId = 0;
      std::uint32_t departureEventId = 0;
    };
    std::vector<AnimEventInput> animEvents;
  };
  void BuildLeg(std::uint32_t legIndex, const LegBuildInput& input);

  std::vector<MOTransportPathSegment> segments_;
  float maxVelocity_ = 0.0f;
  float acceleration_ = 0.0f;
  std::uint32_t cycleDurationMs_ = 0;
  std::uint32_t timeOffsetMs_ = 0;
  std::uint32_t latchedTimeMs_ = 0;
  bool readyState_ = false;
  bool latchedToEnd_ = false;
  bool updatePending_ = false;

  WaveOscillationParams waveParams_{};
  bool hasWaveParams_ = false;

  std::vector<MOTransportTimeEvent> timeEvents_;
};

class CObjectEffect;

void MOTransport_SetEffectForMovePhase(CObjectEffect* effect,
                                       MOTransportMovePhase phase);

void Transport_UpdateSequenceEffectState(CObjectEffect* effect,
                                         std::uint32_t sequence_id,
                                         std::uint32_t& cached_state);

}

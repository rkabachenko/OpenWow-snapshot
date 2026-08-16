
#include "openwow/game/mo_transport_path_state.h"

#include "openwow/game/object_effect_system.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openwow::game {

namespace {

constexpr float kMillisecondsToSeconds = 0.001f;
constexpr float kNormalizationEpsilon = 0.00000023841858f;

constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831855f;
constexpr float kTwoPiTimes1000 = 6283.1855f;

[[nodiscard]] std::uint32_t RoundSecondsToMilliseconds(const float seconds) {

  return static_cast<std::uint32_t>(std::nearbyint(seconds * 1000.0f));
}

float WrapAngle(float a) {
  if (a > kPi) a -= kTwoPi;
  else if (a < -kPi) a += kTwoPi;
  return a;
}

}

std::uint32_t MOTransportTimedPathState::GetCurrentPathTime(
    const std::uint32_t absoluteTimeMs) const {
  if (cycleDurationMs_ == 0) {
    return 0;
  }
  const std::uint32_t pathTime =
      (absoluteTimeMs + timeOffsetMs_) % cycleDurationMs_;
  if (latchedToEnd_) {
    return latchedTimeMs_;
  }
  return pathTime;
}

std::uint32_t MOTransportTimedPathState::ResolvePathTime(
    const std::uint32_t absoluteTimeMs,
    const std::uint32_t timeDeltaMs,
    const bool directTimeMode) {
  if (cycleDurationMs_ == 0) {
    return 0;
  }
  if (directTimeMode) {
    return absoluteTimeMs % cycleDurationMs_;
  }

  std::uint32_t pathTime = (absoluteTimeMs + timeOffsetMs_) % cycleDurationMs_;

  if (readyState_) {
    if (latchedToEnd_) {
      return latchedTimeMs_;
    }
    const std::uint32_t windowEnd =
        (latchedTimeMs_ + timeDeltaMs) % cycleDurationMs_;
    if (CrossesBoundaryWindow(latchedTimeMs_, windowEnd, pathTime)) {

      latchedToEnd_ = true;
      return latchedTimeMs_;
    }
  }
  return pathTime;
}

bool MOTransportTimedPathState::CrossesBoundaryWindow(
    const std::uint32_t windowStart,
    const std::uint32_t windowEnd,
    const std::uint32_t value) {
  if (windowStart == windowEnd) {
    return value == windowStart;
  }
  if (windowStart < windowEnd) {
    return windowStart < value && value <= windowEnd;
  }

  return value <= windowEnd || windowStart < value;
}

FindSegmentResult MOTransportTimedPathState::FindSegment(
    const std::uint32_t pathTime) const {
  FindSegmentResult out{};
  const auto segCount = static_cast<std::uint32_t>(segments_.size());

  for (std::uint32_t segIdx = 0; segIdx < segCount; ++segIdx) {
    const auto& seg = segments_[segIdx];
    const auto stopCount = static_cast<std::uint32_t>(seg.stops.size());

    if (pathTime < seg.endTimeMs && stopCount != 1) {

      for (std::uint32_t stopIdx = 0; stopIdx < stopCount - 1; ++stopIdx) {
        const auto& stop = seg.stops[stopIdx];
        if (pathTime < stop.timestampMs + stop.durationMs) {
          out.segmentIndex = segIdx;
          out.stopIndex = stopIdx;
          return out;
        }
      }

    }
  }

  return out;
}

void MOTransportTimedPathState::AlignOffsetToPathTime(
    const std::uint32_t absoluteTimeMs,
    const std::uint32_t targetPathTime) {
  const std::uint32_t currentPhase = absoluteTimeMs % cycleDurationMs_;

  if (targetPathTime <= currentPhase) {
    timeOffsetMs_ = targetPathTime + cycleDurationMs_ - currentPhase;
  } else {
    timeOffsetMs_ = targetPathTime - currentPhase;
  }
}

bool MOTransportTimedPathState::LatchReadySegment(
    const std::uint32_t absoluteTimeMs) {
  if (cycleDurationMs_ == 0) {
    return false;
  }
  if (latchedToEnd_) {
    return false;
  }

  const std::uint32_t pathTime =
      (cycleDurationMs_ + timeOffsetMs_ + absoluteTimeMs - 1) % cycleDurationMs_;

  const auto [segIdx, stopIdx] = FindSegment(pathTime);

  const auto& stop = segments_[segIdx].stops[stopIdx];
  const std::uint32_t stopEndTime =
      (stop.timestampMs + stop.durationMs) % cycleDurationMs_;

  readyState_ = true;
  latchedToEnd_ = true;
  updatePending_ = false;
  latchedTimeMs_ = stopEndTime;

  return true;
}

void MOTransportTimedPathState::ApplyReadyState(
    const std::uint32_t absoluteTimeMs,
    const bool ready) {
  if (cycleDurationMs_ == 0) {
    return;
  }
  if (ready == readyState_) {
    return;
  }

  const bool wasLatched = latchedToEnd_;

  std::uint32_t lookupTime;
  if (wasLatched) {

    lookupTime = (latchedTimeMs_ + cycleDurationMs_ - 1) % cycleDurationMs_;
  } else {

    lookupTime = (absoluteTimeMs + timeOffsetMs_) % cycleDurationMs_;
  }

  const auto [segIdx, stopIdx] = FindSegment(lookupTime);
  const auto& stop = segments_[segIdx].stops[stopIdx];
  const std::uint32_t stopEndRaw = stop.timestampMs + stop.durationMs;

  if (ready) {
    latchedTimeMs_ = stopEndRaw % cycleDurationMs_;
  } else if (wasLatched) {
    AlignOffsetToPathTime(absoluteTimeMs, stopEndRaw);
  }

  readyState_ = ready;
  latchedToEnd_ = false;
  updatePending_ = false;
}

void MOTransportTimedPathState::SeedFromPackedProgress(
    const std::uint32_t absoluteTimeMs,
    const float progress) {
  if (cycleDurationMs_ == 0) {
    return;
  }

  const auto targetPathTime = static_cast<std::uint32_t>(std::nearbyint(
      static_cast<float>(cycleDurationMs_) * progress));
  const std::uint32_t currentPhase = absoluteTimeMs % cycleDurationMs_;

  if (targetPathTime <= currentPhase) {
    timeOffsetMs_ = targetPathTime + cycleDurationMs_ - currentPhase;
  } else {
    timeOffsetMs_ = targetPathTime - currentPhase;
  }
}

float MOTransportTimedPathState::CalcDistanceCruiseToStop(
    const float elapsedSec, const float segDurSec,
    MOTransportMovePhase& outPhase, float& outCurrentVel) const {

  const float timeToMaxVel = maxVelocity_ / acceleration_;
  const float decelTime = std::min(segDurSec, timeToMaxVel);
  const float constTime = segDurSec - decelTime;

  if (elapsedSec <= constTime) {

    outPhase = MOTransportMovePhase::ConstantSpeed;
    outCurrentVel = maxVelocity_;
    return elapsedSec * maxVelocity_;
  }

  const float dt = elapsedSec - constTime;
  const float initialDecelVel = decelTime * acceleration_;
  outCurrentVel = initialDecelVel - acceleration_ * dt;
  const float avgVel = (initialDecelVel + initialDecelVel - acceleration_ * dt) * 0.5f;
  const float constDist = constTime * maxVelocity_;
  outPhase = MOTransportMovePhase::Decelerating;
  return dt * avgVel + constDist;
}

float MOTransportTimedPathState::CalcDistanceAccelerateCruiseDecelerate(
    const float elapsedSec, const float segDurSec,
    MOTransportMovePhase& outPhase, float& outCurrentVel) const {

  const float timeToMaxVel = maxVelocity_ / acceleration_;
  float accelTime = segDurSec * 0.5f;
  if (accelTime >= timeToMaxVel) {
    accelTime = timeToMaxVel;
  }

  const float decelStart = segDurSec - accelTime;

  if (elapsedSec <= decelStart) {

    if (accelTime >= elapsedSec) {

      const float vel = acceleration_ * elapsedSec;
      outCurrentVel = vel;
      outPhase = MOTransportMovePhase::Accelerating;
      return 0.5f * vel * elapsedSec;
    }

    const float constDist = (elapsedSec - accelTime) * maxVelocity_;
    outPhase = MOTransportMovePhase::ConstantSpeed;
    outCurrentVel = maxVelocity_;
    return accelTime * (0.5f * maxVelocity_) + constDist;
  }

  const float peakVel = accelTime * acceleration_;
  const float dt = elapsedSec - decelStart;
  outCurrentVel = peakVel - acceleration_ * dt;
  const float avgDecelVel = (peakVel * 2.0f - acceleration_ * dt) * 0.5f;
  const float constDist = (decelStart - accelTime) * maxVelocity_;
  const float accelDist = accelTime * (0.5f * peakVel);
  outPhase = MOTransportMovePhase::Decelerating;
  return accelDist + dt * avgDecelVel + constDist;
}

void MOTransportTimedPathState::ComputeProceduralPitchRoll(
    const float currentVelocity,
    const float facing,
    const float distanceAlongSpline,
    const std::uint32_t segmentIndex,
    const std::uint32_t pathTimeMs,
    std::array<float, 3>& inOutPosition,
    float& outPitch,
    float& outRoll) const {

  if (currentVelocity <= 0.0f) {
    ApplyWaveOscillation(pathTimeMs, currentVelocity, 0.0f,
                         inOutPosition, outPitch, outRoll);
    return;
  }

  if (segmentIndex >= segments_.size()) {
    return;
  }
  const auto& seg = segments_[segmentIndex];
  const float splineLen = seg.spline.GetTotalLength();
  if (splineLen <= 0.0f) {
    return;
  }

  float damping;
  {
    const float threshold15 = waveParams_.speedDampThreshold * 0.15f;
    const float threshold30 = waveParams_.speedDampThreshold * 0.30f;
    float effectiveVel = currentVelocity;
    if (effectiveVel < threshold15) {
      effectiveVel = 0.0f;
    } else if (effectiveVel < threshold30) {
      const float ratio = (effectiveVel - threshold15) / (threshold30 - threshold15);
      effectiveVel *= ratio * ratio;
    }
    damping = effectiveVel;
  }

  float steering = 0.0f;
  float prevFacing = facing;
  for (int i = 1; i <= 4; ++i) {
    const float lookAheadDist = distanceAlongSpline - damping * static_cast<float>(i) * 0.45f;
    const float clampedDist = std::max(lookAheadDist, 0.0f);
    float t = clampedDist / splineLen;
    t = std::clamp(t, 0.0f, 1.0f);

    const auto frame = seg.spline.EvaluateFrame(t, render::CSpline::kArcLengthParameterMode);
    if (!frame.has_value()) {
      continue;
    }

    const float fx = -frame->forward.x;
    const float fy = -frame->forward.y;
    const float lenSq = fx * fx + fy * fy;
    float sampleFacing;
    if (lenSq > kNormalizationEpsilon) {
      const float invLen = 1.0f / std::sqrt(lenSq);
      sampleFacing = std::atan2(fy * invLen, fx * invLen);
    } else {
      sampleFacing = std::atan2(fy, fx);
    }

    const float delta = WrapAngle(prevFacing - sampleFacing);
    steering += delta * 2.2222223f;
    prevFacing = sampleFacing;
  }

  steering *= 0.2f;

  ApplyWaveOscillation(pathTimeMs, damping, steering,
                       inOutPosition, outPitch, outRoll);
}

void MOTransportTimedPathState::ApplyWaveOscillation(
    const std::uint32_t pathTimeMs,
    const float currentVelocity,
    const float steering,
    std::array<float, 3>& position,
    float& outPitch,
    float& outRoll) const {

  if (!hasWaveParams_) {
    outPitch = 0.0f;
    outRoll = 0.0f;
    return;
  }

  const auto& w = waveParams_;

  float blend;
  if (currentVelocity >= w.speedDampThreshold) {
    blend = 1.0f;
  } else {
    const float ratio = std::sqrt(currentVelocity / w.speedDampThreshold);
    blend = ratio * ((1.0f - w.speedDamp) * ratio) + w.speedDamp;
  }

  if (std::fabs(w.waveTimeScale) >= kNormalizationEpsilon) {
    const auto period = static_cast<std::uint32_t>(kTwoPiTimes1000 / w.waveTimeScale);
    if (period > 0) {
      const float phase =
          static_cast<float>(pathTimeMs % period) * kTwoPi / static_cast<float>(period);
      position[2] += std::sin(phase) * w.waveAmplitude * blend;
    }
  }

  float tilt = steering;
  if (tilt > w.maxBankTurnSpeed) tilt = w.maxBankTurnSpeed;
  else if (tilt < -w.maxBankTurnSpeed) tilt = -w.maxBankTurnSpeed;
  const float baseBank = tilt * (w.maxBank / w.maxBankTurnSpeed);

  if (std::fabs(w.rollTimeScale) < kNormalizationEpsilon) {
    outRoll = 0.0f;
  } else {
    const auto period = static_cast<std::uint32_t>(kTwoPiTimes1000 / w.rollTimeScale);
    if (period > 0) {
      const float phase =
          static_cast<float>(pathTimeMs % period) * kTwoPi / static_cast<float>(period) + 0.5f;
      outRoll = (baseBank + std::sin(phase) * w.rollAmplitude) * blend;
    } else {
      outRoll = 0.0f;
    }
  }

  if (std::fabs(w.pitchTimeScale) < kNormalizationEpsilon) {
    outPitch = 0.0f;
  } else {
    const auto period = static_cast<std::uint32_t>(kTwoPiTimes1000 / w.pitchTimeScale);
    if (period > 0) {
      const float phase =
          static_cast<float>(pathTimeMs % period) * kTwoPi / static_cast<float>(period) + 0.2f;
      outPitch = std::sin(phase) * w.pitchAmplitude * blend;
    } else {
      outPitch = 0.0f;
    }
  }
}

MOTransportEvalResult MOTransportTimedPathState::EvaluatePathAtTime(
    const std::uint32_t absoluteTimeMs,
    const std::uint32_t timeDeltaMs,
    const bool directTimeMode,
    const bool computePitchRoll) {

  MOTransportEvalResult result{};
  result.movePhase = MOTransportMovePhase::Stopped;

  if (segments_.empty()) {
    return result;
  }

  const std::uint32_t pathTime =
      ResolvePathTime(absoluteTimeMs, timeDeltaMs, directTimeMode);

  std::uint32_t segIdx = 0;
  while (segIdx < segments_.size() && pathTime >= segments_[segIdx].endTimeMs) {
    ++segIdx;
  }
  if (segIdx >= segments_.size()) {
    return result;
  }
  result.segmentIndex = segIdx;

  const auto& seg = segments_[segIdx];
  const float splineLength = seg.spline.GetTotalLength();

  float baseDistance = 0.0f;
  std::uint32_t runStartMs = seg.startTimeMs;
  std::uint32_t stopIndex = 0;
  const std::uint32_t stopCount = static_cast<std::uint32_t>(seg.stops.size());
  float savedVelocity = 0.0f;
  bool atStop = false;

  if (stopCount > 1) {
    for (std::uint32_t i = 0; i < stopCount - 1; ++i) {
      const auto& stop = seg.stops[i];

      if (pathTime < stop.timestampMs) {
        break;
      }

      if (pathTime - stop.timestampMs < stop.durationMs) {

        result.movePhase = MOTransportMovePhase::Stopped;
        baseDistance = stop.accumulatedDistance;
        atStop = true;
        break;
      }

      baseDistance = stop.accumulatedDistance;
      runStartMs = stop.timestampMs + stop.durationMs;
      ++stopIndex;
    }
  }

  if (!atStop) {
    const float elapsedSec =
        static_cast<float>(pathTime - runStartMs) * kMillisecondsToSeconds;

    const std::uint32_t nextStopMs =
        (stopIndex < stopCount) ? seg.stops[stopIndex].timestampMs : seg.endTimeMs;
    const float segDurSec =
        static_cast<float>(nextStopMs - runStartMs) * kMillisecondsToSeconds;

    MOTransportMovePhase phase = MOTransportMovePhase::ConstantSpeed;
    float currentVel = maxVelocity_;
    float distance = 0.0f;

    const bool isFirstStop = (stopIndex == 0);
    const bool isLastStop = (stopIndex == stopCount - 1);

    if (isFirstStop) {
      if (!isLastStop) {

        distance = CalcDistanceCruiseToStop(elapsedSec, segDurSec, phase, currentVel);
      } else {

        phase = MOTransportMovePhase::ConstantSpeed;
        currentVel = maxVelocity_;
        distance = elapsedSec * maxVelocity_;
      }
    } else {
      if (!isLastStop) {

        distance = CalcDistanceAccelerateCruiseDecelerate(
            elapsedSec, segDurSec, phase, currentVel);
      } else {

        const float timeToMaxVel = maxVelocity_ / acceleration_;
        const float accelTime = std::min(segDurSec, timeToMaxVel);
        if (accelTime >= elapsedSec) {

          const float vel = acceleration_ * elapsedSec;
          phase = MOTransportMovePhase::Accelerating;
          currentVel = vel;
          distance = elapsedSec * (vel * 0.5f);
        } else {

          const float accelDist = accelTime * (maxVelocity_ * 0.5f);
          const float constDist = (elapsedSec - accelTime) * maxVelocity_;
          phase = MOTransportMovePhase::ConstantSpeed;
          currentVel = maxVelocity_;
          distance = accelDist + constDist;
        }
      }
    }

    result.movePhase = phase;
    baseDistance += distance;
    savedVelocity = currentVel;
  }

  if (splineLength > 0.0f) {
    float t = baseDistance / splineLength;
    t = std::clamp(t, 0.0f, 1.0f);

    const auto frame =
        seg.spline.EvaluateFrame(t, render::CSpline::kArcLengthParameterMode);
    if (frame.has_value()) {

      const float tx = -frame->forward.x;
      const float ty = -frame->forward.y;
      const float lenSq = tx * tx + ty * ty;
      if (lenSq > kNormalizationEpsilon) {
        const float invLen = 1.0f / std::sqrt(lenSq);
        result.facing = std::atan2(ty * invLen, tx * invLen);
      } else {
        result.facing = std::atan2(ty, tx);
      }

      result.position[0] = frame->position.x;
      result.position[1] = frame->position.y;
      result.position[2] = frame->position.z;

      result.pathPosition = result.position;
    }

    if (computePitchRoll && hasWaveParams_) {
      ComputeProceduralPitchRoll(
          savedVelocity, result.facing, baseDistance,
          segIdx, pathTime, result.position,
          result.pitch, result.roll);
    }
  }

  result.mapId = seg.mapId;
  return result;
}

namespace {

constexpr std::uint32_t kEffectStateStopped       = 2;
constexpr std::uint32_t kEffectStateAccelerating   = 3;
constexpr std::uint32_t kEffectStateConstantSpeed  = 4;
constexpr std::uint32_t kEffectStateDecelerating   = 5;

}

void MOTransport_SetEffectForMovePhase(CObjectEffect* effect,
                                       const MOTransportMovePhase phase) {
  if (effect == nullptr) {
    return;
  }

  std::uint32_t state_to_apply = 0;
  std::array<std::uint32_t, 3> states_to_clear{};

  switch (phase) {
    case MOTransportMovePhase::Stopped:
      state_to_apply = kEffectStateStopped;
      states_to_clear = {kEffectStateAccelerating, kEffectStateConstantSpeed,
                         kEffectStateDecelerating};
      break;
    case MOTransportMovePhase::Accelerating:
      state_to_apply = kEffectStateAccelerating;
      states_to_clear = {kEffectStateStopped, kEffectStateConstantSpeed,
                         kEffectStateDecelerating};
      break;
    case MOTransportMovePhase::ConstantSpeed:
      state_to_apply = kEffectStateConstantSpeed;
      states_to_clear = {kEffectStateAccelerating, kEffectStateStopped,
                         kEffectStateDecelerating};
      break;
    case MOTransportMovePhase::Decelerating:
      state_to_apply = kEffectStateDecelerating;
      states_to_clear = {kEffectStateAccelerating, kEffectStateConstantSpeed,
                         kEffectStateStopped};
      break;
    default:
      return;
  }

  (void)effect->ApplyState(state_to_apply, true);
  for (const std::uint32_t state_id : states_to_clear) {
    (void)effect->ClearState(state_id, true);
  }
}

void Transport_UpdateSequenceEffectState(CObjectEffect* effect,
                                         const std::uint32_t sequence_id,
                                         std::uint32_t& cached_state) {
  if (effect == nullptr) {
    return;
  }

  const std::uint32_t new_state =
      ObjectEffectDataStore::Instance().GetEventSoundState(sequence_id,
                                                           false);
  if (new_state == 0) {
    return;
  }

  if (new_state == cached_state) {
    return;
  }

  (void)effect->ClearState(cached_state, true);
  (void)effect->ApplyState(new_state, true);

  if (new_state == 15 || new_state == 81) {
    static constexpr std::uint32_t kResetStates[] = {
        37, 38,
        6,  7,
        47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
        58, 59, 60, 61, 62, 63, 64, 65, 66,
    };
    for (const std::uint32_t state_id : kResetStates) {
      (void)effect->ClearState(state_id, true);
    }
  }

  cached_state = new_state;
}

std::uint32_t MOTransportTimedPathState::CalcTravelTimeSymmetric(
    const float distance) const {
  if (acceleration_ <= 0.0f || maxVelocity_ <= 0.0f || distance <= 0.0f) {
    return 0;
  }

  const float inv_accel = 1.0f / acceleration_;
  const float ramp_time = maxVelocity_ * inv_accel;
  const float ramp_dist = maxVelocity_ * 0.5f * ramp_time;
  float time_sec;
  if (0.5f * distance <= ramp_dist) {
    time_sec = 2.0f * std::sqrt(distance * inv_accel);
  } else {
    time_sec = 2.0f * ramp_time +
               (distance - 2.0f * ramp_dist) / maxVelocity_;
  }
  return RoundSecondsToMilliseconds(time_sec);
}

std::uint32_t MOTransportTimedPathState::CalcTravelTimeAcceleratingFromRest(
    const float distance) const {
  if (acceleration_ <= 0.0f || maxVelocity_ <= 0.0f || distance <= 0.0f) {
    return 0;
  }

  const float time_to_max_velocity = maxVelocity_ / acceleration_;
  const float acceleration_distance =
      maxVelocity_ * 0.5f * time_to_max_velocity;
  const float time_sec = distance <= acceleration_distance
      ? std::sqrt((distance + distance) / acceleration_)
      : time_to_max_velocity +
            (distance - acceleration_distance) / maxVelocity_;
  return RoundSecondsToMilliseconds(time_sec);
}

void MOTransportTimedPathState::BuildLeg(
    const std::uint32_t legIndex,
    const LegBuildInput& input) {
  if (legIndex >= segments_.size()) {
    return;
  }

  auto& seg = segments_[legIndex];

  seg.spline.SetControlPoints(input.controlPoints);

  const auto num_stops = static_cast<std::uint32_t>(input.stops.size());
  const auto num_points = static_cast<std::uint32_t>(input.controlPoints.size());

  seg.stops.resize(num_stops + 1);

  seg.startTimeMs = cycleDurationMs_;

  float prev_distance = 0.0f;
  std::uint32_t accumulated_dwell_ms = 0;
  std::uint32_t stop_idx = 0;
  std::uint32_t anim_idx = 0;

  for (std::uint32_t i = 0; i < num_stops; ++i) {
    const auto& stop = input.stops[i];
    if (stop.controlPointIndex >= num_points - 1) break;

    while (anim_idx < input.animEvents.size()) {
      const auto& anim = input.animEvents[anim_idx];
      if (anim.controlPointIndex > stop.controlPointIndex) break;

      const float anim_dist =
          seg.spline.GetAccumulatedLengthToPoint(anim.controlPointIndex) -
          prev_distance;

      std::uint32_t anim_time_ms;
      if (stop_idx == 0) {

        anim_time_ms = CalcTravelTimeAcceleratingFromRest(anim_dist);
      } else {
        anim_time_ms = CalcTravelTimeSymmetric(anim_dist);
      }

      const std::uint32_t abs_time =
          accumulated_dwell_ms + anim_time_ms + seg.startTimeMs;

      if (anim.arrivalEventId != 0) {
        timeEvents_.push_back({abs_time + cycleDurationMs_ - seg.startTimeMs,
                               anim.arrivalEventId});
      }
      std::uint32_t delay_at_anim = 0;
      if (anim.controlPointIndex == stop.controlPointIndex) {
        delay_at_anim = stop.delayMs;
      }
      if (anim.departureEventId != 0) {
        timeEvents_.push_back(
            {delay_at_anim + abs_time + cycleDurationMs_ - seg.startTimeMs,
             anim.departureEventId});
      }
      ++anim_idx;
    }

    const float stop_dist =
        seg.spline.GetAccumulatedLengthToPoint(stop.controlPointIndex);
    const float delta = stop_dist - prev_distance;
    std::uint32_t travel_ms;

    if (stop_idx == 0) {
      travel_ms = CalcTravelTimeAcceleratingFromRest(delta);
    } else {
      travel_ms = CalcTravelTimeSymmetric(delta);
    }

    cycleDurationMs_ += travel_ms;
    prev_distance = stop_dist;

    seg.stops[stop_idx].timestampMs =
        accumulated_dwell_ms + cycleDurationMs_ - seg.startTimeMs;
    seg.stops[stop_idx].accumulatedDistance = stop_dist;
    seg.stops[stop_idx].durationMs = stop.delayMs;
    accumulated_dwell_ms += stop.delayMs;

    ++stop_idx;
  }

  while (anim_idx < input.animEvents.size()) {
    const auto& anim = input.animEvents[anim_idx];
    const float anim_dist =
        seg.spline.GetAccumulatedLengthToPoint(anim.controlPointIndex) -
        prev_distance;
    std::uint32_t anim_time_ms;
    if (stop_idx == 0) {

      if (maxVelocity_ > 0.0f) {
        anim_time_ms = RoundSecondsToMilliseconds(anim_dist / maxVelocity_);
      } else {
        anim_time_ms = 0;
      }
    } else {

      anim_time_ms = CalcTravelTimeAcceleratingFromRest(anim_dist);
    }
    const std::uint32_t abs_time =
        accumulated_dwell_ms + anim_time_ms + cycleDurationMs_;
    if (anim.arrivalEventId != 0) {
      timeEvents_.push_back({abs_time, anim.arrivalEventId});
    }
    if (anim.departureEventId != 0) {
      timeEvents_.push_back({abs_time, anim.departureEventId});
    }
    ++anim_idx;
  }

  const float end_dist = seg.spline.GetTotalLength();
  const float final_delta = end_dist - prev_distance;
  std::uint32_t final_travel_ms;
  if (stop_idx > 0) {

    final_travel_ms = CalcTravelTimeAcceleratingFromRest(final_delta);
  } else {

    if (maxVelocity_ > 0.0f) {
      final_travel_ms = RoundSecondsToMilliseconds(final_delta / maxVelocity_);
    } else {
      final_travel_ms = 0;
    }
  }
  cycleDurationMs_ += final_travel_ms;

  seg.stops[stop_idx].timestampMs =
      accumulated_dwell_ms + cycleDurationMs_ - seg.startTimeMs;
  seg.stops[stop_idx].accumulatedDistance = end_dist;
  seg.stops[stop_idx].durationMs = 0;

  const std::uint32_t legEndTimeMs =
      cycleDurationMs_ + accumulated_dwell_ms;
  seg.endTimeMs = legEndTimeMs;
  cycleDurationMs_ = legEndTimeMs;

  for (auto& s : seg.stops) {
    s.timestampMs += seg.startTimeMs;
  }
}

void MOTransportTimedPathState::BuildFromTaxiPathNodes(
    const std::vector<TaxiPathNodeRaw>& nodes,
    const float maxVelocity,
    const float acceleration,
    const WaveOscillationParams* waveParams) {

  maxVelocity_ = maxVelocity;
  acceleration_ = acceleration;
  cycleDurationMs_ = 0;
  timeEvents_.clear();

  if (waveParams) {
    waveParams_ = *waveParams;
    hasWaveParams_ = true;
  } else {

    waveParams_ = {};
    hasWaveParams_ = false;
  }

  if (nodes.empty()) {
    segments_.clear();
    return;
  }

  struct LegAccum {
    LegBuildInput input;
    std::uint32_t mapId = 0;
  };
  std::vector<LegAccum> legs;
  LegAccum current_leg;
  current_leg.mapId = nodes[0].mapId;

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];

    if (i > 0) {

      const bool previous_node_ends_leg = (nodes[i - 1].flags & 1u) != 0u;
      if (node.mapId != current_leg.mapId || previous_node_ends_leg) {
        legs.push_back(std::move(current_leg));
        current_leg = {};
        current_leg.mapId = node.mapId;
      }
    }

    const auto pt_index = static_cast<std::uint32_t>(
        current_leg.input.controlPoints.size());
    current_leg.input.controlPoints.push_back(
        render::C3Vector{node.x, node.y, node.z});

    if ((node.flags & 2u) != 0u && pt_index != 0u) {
      current_leg.input.stops.push_back(
          {pt_index, node.delaySeconds * 1000u});
    }

    if (node.arrivalEventId != 0 || node.departureEventId != 0) {
      current_leg.input.animEvents.push_back(
          {pt_index, node.arrivalEventId, node.departureEventId});
    }
  }

  legs.push_back(std::move(current_leg));

  segments_.resize(legs.size());
  for (std::size_t i = 0; i < legs.size(); ++i) {
    segments_[i].mapId = legs[i].mapId;
    BuildLeg(static_cast<std::uint32_t>(i), legs[i].input);
  }
}

}

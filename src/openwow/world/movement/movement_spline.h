#pragma once

#include "openwow/game/vec3.h"
#include "openwow/game/monster_move.h"
#include "openwow/game/movement_info.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::world {

namespace MoveConst {

  inline constexpr float kGravity          = 19.291103f;

  inline constexpr float kTerminalVelocity = 60.148003f;

  inline constexpr float kSafeFallTerminal = 7.0f;

  inline constexpr float kInvDoubleGravity = 1.0f / (2.0f * kGravity);
}

enum class SplineFacingMode : std::uint8_t {
  kNone   = 0,
  kSpot   = 1,
  kTarget = 2,
  kAngle  = 3,
};

struct UnitSpeeds {
  float walk      = 2.5f;
  float run       = 7.0f;
  float runBack   = 4.5f;
  float swim      = 4.7222f;
  float swimBack  = 2.5f;
  float flight    = 7.0f;
  float flightBack= 4.5f;
  float turn      = 3.14159f;
  float pitch     = 3.14159f;
};

namespace CatmullRom {

inline float Eval(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  return 0.5f * ((2.0f * p1)
               + (-p0 + p2) * t
               + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
               + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

inline float EvalDerivative(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t;
  return 0.5f * ((-p0 + p2)
               + (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) * t
               + (-3.0f * p0 + 9.0f * p1 - 9.0f * p2 + 3.0f * p3) * t2);
}

game::Vec3 EvalVec3(const game::Vec3& p0, const game::Vec3& p1,
                    const game::Vec3& p2, const game::Vec3& p3, float t);

game::Vec3 EvalTangent(const game::Vec3& p0, const game::Vec3& p1,
                       const game::Vec3& p2, const game::Vec3& p3, float t);

float SegmentLength(const game::Vec3& p0, const game::Vec3& p1,
                    const game::Vec3& p2, const game::Vec3& p3,
                    int steps = 20);

}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline game::Vec3 LerpVec3(const game::Vec3& a, const game::Vec3& b, float t) {
  return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}

class MoveSpline {
 public:
  MoveSpline() = default;

  void Initialize(const game::MonsterMoveInfo& info, float start_facing = 0.0f,
                  std::optional<game::Vec3> current_position = std::nullopt,
                  std::optional<float> target_facing = std::nullopt,
                  float run_speed = UnitSpeeds{}.run);

  void Initialize(const game::MovementUpdate& update,
                  std::optional<float> target_facing = std::nullopt);

  void Initialize(const std::vector<game::Vec3>& points,
                  std::uint32_t duration_ms,
                  std::uint32_t spline_flags,
                  bool catmull_rom,
                  float start_facing = 0.0f);

  void Stop();

  bool Update(std::uint32_t dt_ms);

  [[nodiscard]] game::Vec3 GetCurrentPosition() const;

  [[nodiscard]] game::Vec3 GetCurrentVelocity() const;

  [[nodiscard]] game::Vec3 GetFinalDestination() const;

  [[nodiscard]] float GetCurrentFacing() const;

  [[nodiscard]] bool IsActive() const { return active_; }
  [[nodiscard]] bool IsFinished() const { return finished_; }
  [[nodiscard]] bool IsCyclic() const;
  [[nodiscard]] bool IsParabolic() const;
  [[nodiscard]] bool IsCatmullRom() const { return catmull_rom_; }
  [[nodiscard]] bool IsFalling() const;

  [[nodiscard]] std::uint32_t GetSplineId() const { return spline_id_; }
  [[nodiscard]] std::uint32_t GetSplineFlags() const { return spline_flags_; }
  [[nodiscard]] std::uint32_t GetDuration() const { return duration_ms_; }

  [[nodiscard]] float GetTotalArcLength() const { return total_arc_length_; }
  [[nodiscard]] std::uint32_t GetTimePassed() const { return time_passed_ms_; }
  [[nodiscard]] bool HasCoordinateParentBinding() const {
    return has_coordinate_parent_binding_;
  }
  [[nodiscard]] game::ObjectGuid GetCoordinateParent() const {
    return coordinate_parent_;
  }
  [[nodiscard]] std::int8_t GetCoordinateParentSeat() const {
    return coordinate_parent_seat_;
  }

  [[nodiscard]] const std::vector<game::Vec3>& GetPoints() const { return points_; }
  [[nodiscard]] std::size_t GetPointCount() const { return points_.size(); }

  [[nodiscard]] SplineFacingMode GetFacingMode() const { return facing_mode_; }
  [[nodiscard]] game::Vec3 GetFacingSpot() const { return facing_spot_; }
  [[nodiscard]] std::uint64_t GetFacingTarget() const { return facing_target_; }
  [[nodiscard]] float GetFacingAngle() const { return facing_angle_; }

  void ResolveArrivalTargetFacing(std::optional<game::Vec3> target_position);

  [[nodiscard]] float GetVerticalAcceleration() const { return vertical_accel_; }
  [[nodiscard]] std::uint32_t GetParabolicStartTime() const { return parabolic_start_ms_; }

  [[nodiscard]] float ComputeParabolicZ(std::uint32_t time_ms) const;
  [[nodiscard]] float ComputeFallingZ(std::uint32_t time_ms) const;

  [[nodiscard]] std::uint8_t GetAnimationId() const { return animation_id_; }

  [[nodiscard]] std::uint32_t GetAnimStartTime() const { return anim_start_time_ms_; }
  [[nodiscard]] bool HasTriggeredAnimationTier() const {
    return (spline_flags_ & game::SplineFlag::kAnimation) != 0u &&
           time_passed_ms_ >= anim_start_time_ms_;
  }

  [[nodiscard]] float GetBaseRate() const { return base_rate_; }
  void SetBaseRate(float rate) { base_rate_ = rate; }

  [[nodiscard]] float GetPlaybackSpeed() const { return playback_speed_; }

  void SyncAnimationSpeed(float target_progress);

  [[nodiscard]] float GetStartFacing() const { return start_facing_; }

 private:
  struct ArcLengthSegment {
    int index = 0;
    float local_t = 0.0f;
    float length = 0.0f;
  };

  void RebuildArcLengthCache();
  void RebuildControlPoints();

  void ApplyMonsterMoveSpeedCap(float run_speed);
  void EnterCyclicLoop();
  [[nodiscard]] std::uint32_t EffectiveDurationMs() const;

  [[nodiscard]] ArcLengthSegment SelectArcLengthSegment(float t) const;

  [[nodiscard]] game::Vec3 EvaluatePosition(float t) const;

  [[nodiscard]] game::Vec3 EvaluateLinear(const ArcLengthSegment& segment) const;

  [[nodiscard]] game::Vec3 EvaluateCatmullRom(
      const ArcLengthSegment& segment) const;

  [[nodiscard]] game::Vec3 EvaluateLinearTangent(
      const ArcLengthSegment& segment) const;
  [[nodiscard]] game::Vec3 EvaluateCatmullRomTangent(
      const ArcLengthSegment& segment) const;

  std::vector<game::Vec3> points_;

  std::vector<game::Vec3> control_points_;
  std::vector<float> segment_lengths_;
  std::uint32_t spline_id_     = 0;
  std::uint32_t spline_flags_  = 0;
  std::uint32_t duration_ms_   = 0;
  std::uint32_t time_passed_ms_= 0;
  float total_arc_length_ = 0.0f;
  bool catmull_rom_ = false;
  bool entered_cycle_ = false;
  bool active_      = false;
  bool finished_    = false;
  bool has_coordinate_parent_binding_ = false;
  game::ObjectGuid coordinate_parent_{};
  std::int8_t coordinate_parent_seat_ = -1;

  SplineFacingMode facing_mode_ = SplineFacingMode::kNone;
  game::Vec3 facing_spot_{};
  std::uint64_t facing_target_ = 0;
  float facing_angle_          = 0.0f;

  float vertical_accel_         = 0.0f;
  std::uint32_t parabolic_start_ms_ = 0;

  std::uint8_t animation_id_       = 0;
  std::uint32_t anim_start_time_ms_= 0;

  float base_rate_       = 1.0f;
  float playback_speed_  = 1.0f;

  game::Vec3 start_pos_{};
  game::Vec3 final_destination_{};
  float start_facing_              = 0.0f;
};

class MovementSplineManager {
 public:

  [[nodiscard]] bool ShouldStartMonsterSpline(
      const game::MonsterMoveInfo& info) const;

  void ApplyMonsterMove(const game::MonsterMoveInfo& info,
                        float start_facing = 0.0f,
                        std::optional<game::Vec3> current_position = std::nullopt,
                        std::optional<float> target_facing = std::nullopt);

  void SyncAuthoritativeSpline(std::uint64_t guid,
                               const game::MovementUpdate& update,
                               std::optional<float> target_facing = std::nullopt);

  [[nodiscard]] const MoveSpline* GetSpline(std::uint64_t guid) const;
  [[nodiscard]] MoveSpline* GetSpline(std::uint64_t guid);

  void SyncFlightSplineAnimation(std::uint64_t guid, float target_progress);

  [[nodiscard]] bool HasActiveSpline(std::uint64_t guid) const;

  void CancelSpline(std::uint64_t guid);

  void SetSpeeds(std::uint64_t guid, const UnitSpeeds& speeds);

  [[nodiscard]] UnitSpeeds GetSpeeds(std::uint64_t guid) const;

  void Update(std::uint32_t client_time_ms);

  [[nodiscard]] std::optional<game::Vec3> GetWorldPosition(
      std::uint64_t guid) const;

  void RemoveEntity(std::uint64_t guid);

  void Clear();

  [[nodiscard]] std::size_t ActiveSplineCount() const;

 private:
  std::unordered_map<std::uint64_t, MoveSpline> splines_;
  std::unordered_map<std::uint64_t, UnitSpeeds> speeds_;
  std::optional<std::uint32_t> last_update_tick_ms_;
};

}

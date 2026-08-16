#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"
#include "openwow/render/models/animation/spline.h"

#include <array>
#include <cstdint>
#include <vector>

namespace openwow::game {

struct TransportInfo {
  ObjectGuid guid;
  float offset_x{0.0f};
  float offset_y{0.0f};
  float offset_z{0.0f};
  float offset_o{0.0f};
  std::uint32_t time{0};
  std::int8_t seat{-1};
  std::uint32_t time2{0};
};

struct JumpInfo {
  float z_speed{0.0f};
  float sin_angle{0.0f};
  float cos_angle{0.0f};
  float xy_speed{0.0f};
};

enum class SplineFacing : std::uint8_t {
  kNone   = 0,
  kTarget = 1,
  kAngle  = 2,
  kPoint  = 3,
};

struct SplineInfo {
  bool active{false};
  std::uint32_t flags{0};

  SplineFacing facing_type{SplineFacing::kNone};
  ObjectGuid facing_target;
  float facing_angle{0.0f};
  float facing_x{0.0f}, facing_y{0.0f}, facing_z{0.0f};

  std::uint32_t time_passed{0};
  std::uint32_t duration{0};
  std::uint32_t spline_id{0};

  float duration_mod{1.0f};
  float duration_mod_next{1.0f};
  float vertical_acceleration{0.0f};
  std::int32_t effect_start_time{0};

  std::vector<float> waypoints;
  [[nodiscard]] std::uint32_t point_count() const {
    return static_cast<std::uint32_t>(waypoints.size() / 3);
  }
  render::CSpline curve{render::CSpline::CurveType::kCatmullRom};

  std::uint8_t mode{0};
  float dest_x{0.0f}, dest_y{0.0f}, dest_z{0.0f};
};

struct MovementInfo {
  std::uint32_t flags{0};
  std::uint16_t flags2{0};
  std::uint32_t time{0};

  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float orientation{0.0f};

  TransportInfo transport;
  float pitch{0.0f};
  std::uint32_t fall_time{0};
  JumpInfo jump;
  float spline_elevation{0.0f};

  [[nodiscard]] bool HasFlag(MovementFlag f) const {
    return (flags & static_cast<std::uint32_t>(f)) != 0;
  }
  [[nodiscard]] bool HasFlag2(MovementFlag2 f) const {
    return (flags2 & static_cast<std::uint16_t>(f)) != 0;
  }
  [[nodiscard]] bool HasMovementFlag(std::uint32_t f) const {
    return (flags & f) != 0;
  }

  [[nodiscard]] bool IsOnTransport() const {
    return HasFlag(kMoveFlagOnTransport);
  }
  [[nodiscard]] bool IsSwimmingOrFlying() const {
    return HasFlag(kMoveFlagSwimming) || HasFlag(kMoveFlagFlying);
  }
  [[nodiscard]] bool IsFalling() const {
    return HasFlag(kMoveFlagFalling);
  }
  [[nodiscard]] bool HasFallingLaunchVelocity() const {
    return HasFlag(kMoveFlagFalling) && jump.z_speed != 0.0f;
  }
  [[nodiscard]] bool HasSplineElevation() const {
    return HasFlag(kMoveFlagSplineElevation);
  }
  [[nodiscard]] bool HasSplineEnabled() const {
    return HasFlag(kMoveFlagSplineEnabled);
  }
};

inline void ResetMovementInfoScratch(MovementInfo& movement) {
  movement = MovementInfo{};
}

struct MovementUpdate {
  std::uint16_t update_flags{0};

  MovementInfo movement;
  std::array<float, kMaxSpeeds> speeds{};

  SplineInfo spline;

  float stationary_x{0.0f};
  float stationary_y{0.0f};
  float stationary_z{0.0f};
  float stationary_o{0.0f};

  ObjectGuid transport_guid;
  float position_x{0.0f};
  float position_y{0.0f};
  float position_z{0.0f};
  float transport_offset_x{0.0f};
  float transport_offset_y{0.0f};
  float transport_offset_z{0.0f};
  float position_o{0.0f};
  float corpse_o{0.0f};

  ObjectGuid target_guid;

  std::uint32_t transport_path_timer{0};

  std::uint32_t vehicle_id{0};
  float vehicle_orientation{0.0f};

  std::int64_t go_rotation{0};

  std::uint32_t low_guid_value{0};

  std::uint32_t unknown_value{0};

  [[nodiscard]] bool HasUpdateFlag(UpdateFlag f) const {
    return (update_flags & static_cast<std::uint16_t>(f)) != 0;
  }
  [[nodiscard]] bool IsLiving() const {
    return HasUpdateFlag(kUpdateFlagLiving);
  }
  [[nodiscard]] bool HasStationaryPosition() const {
    return HasUpdateFlag(kUpdateFlagStationaryPosition);
  }
  [[nodiscard]] bool IsSelf() const {
    return HasUpdateFlag(kUpdateFlagSelf);
  }

  [[nodiscard]] float GetX() const {
    return IsLiving() ? movement.x : (HasUpdateFlag(kUpdateFlagPosition) ? position_x : stationary_x);
  }
  [[nodiscard]] float GetY() const {
    return IsLiving() ? movement.y : (HasUpdateFlag(kUpdateFlagPosition) ? position_y : stationary_y);
  }
  [[nodiscard]] float GetZ() const {
    return IsLiving() ? movement.z : (HasUpdateFlag(kUpdateFlagPosition) ? position_z : stationary_z);
  }
  [[nodiscard]] float GetO() const {
    return IsLiving() ? movement.orientation : (HasUpdateFlag(kUpdateFlagPosition) ? position_o : stationary_o);
  }
};

}

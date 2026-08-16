
#pragma once

#include "openwow/foundation/math/quaternion_xyzw.h"

#include <array>
#include <cstdint>

namespace openwow::game {

struct PassengerPose {
  std::array<float, 3> position{};
  float scalar_facing = 0.0f;
  std::int64_t packed_orientation = 0;
  bool uses_packed_orientation = false;
};

using PassengerQuaternion = openwow::math::quaternion_xyzw::Quaternion;

float* Passenger_TransformPointByMatrix(float* out_position,
                                        const float* position,
                                        const float* matrix_4x4) noexcept;

float* Passenger_TransformLocalPointToWorld(float* out_world_position,
                                            const float* local_position,
                                            const float* parent_world_4x4) noexcept;

float* Passenger_TransformWorldPointToLocal(float* out_local_position,
                                            const float* world_position,
                                            const float* parent_world_4x4) noexcept;

[[nodiscard]] PassengerQuaternion
Passenger_QuaternionFromTransform(const float* matrix_4x4) noexcept;

[[nodiscard]] PassengerQuaternion
Passenger_DecodePackedQuaternion(std::int64_t packed_orientation) noexcept;

void Passenger_ApplyParentTransform(
    PassengerPose& pose,
    const float* parent_world_4x4,
    float parent_facing,
    const PassengerQuaternion& parent_world_rotation) noexcept;

void Passenger_ApplyInverseParentTransform(
    PassengerPose& pose,
    const float* parent_world_4x4,
    float parent_facing,
    float* out_parent_inverse_4x4 = nullptr) noexcept;

[[nodiscard]] PassengerQuaternion Passenger_GetWorldOrientation(
    std::int64_t packed_local_orientation,
    const PassengerQuaternion* parent_world_rotation) noexcept;

[[nodiscard]] float Passenger_GetFacingFromQuaternion(
    const PassengerQuaternion& orientation) noexcept;

}

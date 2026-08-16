
#include "openwow/game/passenger_movement.h"

#include "openwow/foundation/math/angle_normalize.h"
#include "openwow/foundation/math/packed_quaternion64.h"
#include "openwow/foundation/math/row_major_mat4x4.h"

#include <cmath>

namespace openwow::game {

namespace {

constexpr float kQuaternionFacingEpsilon = 2.3841858e-7f;
constexpr float kHalfPi = 1.5707964f;
constexpr float kPi = 3.1415927f;
constexpr float kThreeHalfPi = 4.712389f;

[[nodiscard]] std::int64_t EncodePackedQuaternion(
    const PassengerQuaternion& quaternion) noexcept {
  return openwow::math::packed_quaternion64::Compress(
      quaternion.x, quaternion.y, quaternion.z, quaternion.w);
}

}

float* Passenger_TransformPointByMatrix(
    float* const out_position,
    const float* const position,
    const float* const matrix_4x4) noexcept {
  if (out_position == nullptr || position == nullptr || matrix_4x4 == nullptr) {
    return out_position;
  }

  return openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      out_position, position, matrix_4x4);
}

float* Passenger_TransformLocalPointToWorld(
    float* const out_world_position,
    const float* const local_position,
    const float* const parent_world_4x4) noexcept {
  return Passenger_TransformPointByMatrix(
      out_world_position, local_position, parent_world_4x4);
}

float* Passenger_TransformWorldPointToLocal(
    float* const out_local_position,
    const float* const world_position,
    const float* const parent_world_4x4) noexcept {
  if (out_local_position == nullptr || world_position == nullptr ||
      parent_world_4x4 == nullptr) {
    return out_local_position;
  }

  float inverse[16];
  openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
      inverse, parent_world_4x4);
  return Passenger_TransformPointByMatrix(
      out_local_position, world_position, inverse);
}

PassengerQuaternion Passenger_QuaternionFromTransform(
    const float* const matrix_4x4) noexcept {
  if (matrix_4x4 == nullptr) {
    return {};
  }

  const float r00 = matrix_4x4[0];
  const float r01 = matrix_4x4[4];
  const float r02 = matrix_4x4[8];
  const float r10 = matrix_4x4[1];
  const float r11 = matrix_4x4[5];
  const float r12 = matrix_4x4[9];
  const float r20 = matrix_4x4[2];
  const float r21 = matrix_4x4[6];
  const float r22 = matrix_4x4[10];

  const float trace = r00 + r11 + r22;
  if (trace > 0.0f) {
    const float scale = std::sqrt(trace + 1.0f) * 2.0f;
    return {
        (r21 - r12) / scale,
        (r02 - r20) / scale,
        (r10 - r01) / scale,
        0.25f * scale,
    };
  }

  if (r00 > r11 && r00 > r22) {
    const float scale = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
    return {
        0.25f * scale,
        (r01 + r10) / scale,
        (r02 + r20) / scale,
        (r21 - r12) / scale,
    };
  }

  if (r11 > r22) {
    const float scale = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
    return {
        (r01 + r10) / scale,
        0.25f * scale,
        (r12 + r21) / scale,
        (r02 - r20) / scale,
    };
  }

  const float scale = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
  return {
      (r02 + r20) / scale,
      (r12 + r21) / scale,
      0.25f * scale,
      (r10 - r01) / scale,
  };
}

PassengerQuaternion Passenger_DecodePackedQuaternion(
    const std::int64_t packed_orientation) noexcept {
  PassengerQuaternion result{};
  openwow::math::packed_quaternion64::Decompress(
      packed_orientation, result.x, result.y, result.z, result.w);
  return result;
}

void Passenger_ApplyParentTransform(
    PassengerPose& pose,
    const float* const parent_world_4x4,
    const float parent_facing,
    const PassengerQuaternion& parent_world_rotation) noexcept {
  if (parent_world_4x4 == nullptr) {
    return;
  }

  Passenger_TransformPointByMatrix(
      pose.position.data(), pose.position.data(), parent_world_4x4);

  if (pose.uses_packed_orientation) {
    const auto local_rotation =
        Passenger_DecodePackedQuaternion(pose.packed_orientation);
    const auto world_rotation = openwow::math::quaternion_xyzw::Multiply(
        local_rotation, parent_world_rotation);
    pose.packed_orientation = EncodePackedQuaternion(world_rotation);
    return;
  }

  pose.scalar_facing = openwow::math::NormalizePositiveAngle(
      pose.scalar_facing + parent_facing);
}

void Passenger_ApplyInverseParentTransform(
    PassengerPose& pose,
    const float* const parent_world_4x4,
    const float parent_facing,
    float* const out_parent_inverse_4x4) noexcept {
  if (parent_world_4x4 == nullptr) {
    return;
  }

  float inverse[16];
  openwow::math::row_major_mat4x4::BuildInverseRigidTransform4x4(
      inverse, parent_world_4x4);
  Passenger_TransformPointByMatrix(
      pose.position.data(), pose.position.data(), inverse);

  if (out_parent_inverse_4x4 != nullptr) {
    openwow::math::row_major_mat4x4::Copy4x4(
        out_parent_inverse_4x4, inverse);
  }

  if (pose.uses_packed_orientation) {
    const auto world_rotation =
        Passenger_DecodePackedQuaternion(pose.packed_orientation);
    const auto inverse_parent_rotation =
        Passenger_QuaternionFromTransform(inverse);
    const auto local_rotation = openwow::math::quaternion_xyzw::Multiply(
        world_rotation, inverse_parent_rotation);
    pose.packed_orientation = EncodePackedQuaternion(local_rotation);
    return;
  }

  pose.scalar_facing = openwow::math::NormalizePositiveAngle(
      pose.scalar_facing - parent_facing);
}

PassengerQuaternion Passenger_GetWorldOrientation(
    const std::int64_t packed_local_orientation,
    const PassengerQuaternion* const parent_world_rotation) noexcept {
  const auto local_rotation =
      Passenger_DecodePackedQuaternion(packed_local_orientation);
  if (parent_world_rotation == nullptr) {
    return local_rotation;
  }

  return openwow::math::quaternion_xyzw::Multiply(
      local_rotation, *parent_world_rotation);
}

float Passenger_GetFacingFromQuaternion(
    const PassengerQuaternion& orientation) noexcept {
  const float sine =
      2.0f * (orientation.x * orientation.y +
              orientation.z * orientation.w);
  const float cosine =
      1.0f - 2.0f * (orientation.y * orientation.y +
                     orientation.z * orientation.z);

  if (std::fabs(cosine) < kQuaternionFacingEpsilon) {
    return sine >= 0.0f ? kHalfPi : kThreeHalfPi;
  }
  if (std::fabs(sine) < kQuaternionFacingEpsilon) {
    return cosine <= 0.0f ? kPi : 0.0f;
  }
  return std::atan2(sine, cosine);
}

}

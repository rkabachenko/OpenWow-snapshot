
#include "openwow/game/movement_callbacks.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/passenger_movement.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/world_session.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/world/camera/world_camera.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace openwow::game {

namespace {

constexpr float kTwoPi = 6.2831855f;
int kMovementVf244Resolved = 1;
int kMovementVf236Resolved = 1;

std::string FormatMovementGuidLog(const char* context, const std::uint64_t guid,
                                  const int line = 0) {
  std::ostringstream out;
  out << "Failed to dereference transport! Provided GUID 0x"
      << std::uppercase << std::hex << std::setw(16) << std::setfill('0')
      << guid;
  if (context != nullptr && context[0] != '\0') {
    out << " from " << context;
    if (line != 0) {
      out << '(' << std::dec << line << ')';
    }
  }
  return out.str();
}

void WriteIdentityMatrix(float* matrix) {
  if (matrix == nullptr) {
    return;
  }

  for (int i = 0; i < 16; ++i) {
    matrix[i] = 0.0f;
  }
  matrix[0] = 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
  matrix[15] = 1.0f;
}

void CopyVec3(float* dst, const float* src) {
  if (dst == nullptr || src == nullptr) {
    return;
  }

  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
}

void WriteIdentityQuaternion(float* quaternion_xyzw) {
  if (quaternion_xyzw == nullptr) {
    return;
  }

  quaternion_xyzw[0] = 0.0f;
  quaternion_xyzw[1] = 0.0f;
  quaternion_xyzw[2] = 0.0f;
  quaternion_xyzw[3] = 1.0f;
}

void BuildYawTranslationMatrix(float* matrix,
                               const float x,
                               const float y,
                               const float z,
                               const float facing) {
  WriteIdentityMatrix(matrix);

  const float cosine = std::cos(facing);
  const float sine = std::sin(facing);
  matrix[0] = cosine;
  matrix[1] = sine;
  matrix[4] = -sine;
  matrix[5] = cosine;
  matrix[12] = x;
  matrix[13] = y;
  matrix[14] = z;
}

bool TryResolveMovementObjectFacing(const ObjectManager& objects,
                                    const ObjectGuid guid,
                                    float* const out_facing) {
  if (out_facing == nullptr) {
    return false;
  }

  *out_facing = 0.0f;
  if (guid.GetRawValue() == 0) {
    return false;
  }

  if (const auto* transport = objects.transport_manager().GetTransport(guid)) {
    *out_facing = transport->GetFacing();
    return true;
  }

  const auto* object = objects.GetObjectByGUID(guid);
  if (object == nullptr) {
    return false;
  }

  *out_facing = object->GetFacing();
  return true;
}

float TransformFacingRelativeToParent(const ObjectManager& objects,
                                      const std::uint64_t parent_guid,
                                      const float facing,
                                      const float parent_scale) {
  if (parent_guid == 0) {
    return facing;
  }

  const float parent_facing =
      Movement_GetObjectOrientation(objects, parent_guid);
  return Movement_NormalizeFacing0ToTau(
      facing + parent_facing * parent_scale);
}

}

bool Movement_IsVehicleOrPlayerGuid(const std::uint64_t guid) {
  const auto guid_hi = static_cast<std::uint32_t>(guid >> 32);
  const auto guid_lo = static_cast<std::uint32_t>(guid & 0xFFFFFFFFu);
  return (guid_hi & 0xF0F00000u) == 0xF0500000u ||
         ((guid & 0xF000000000000000ull) == 0 &&
          (((guid_hi & 0xF07FFFFFu) | guid_lo) != 0));
}

void* Movement_ResolveAndCallVf244(const ObjectManager& objects,
                                   int , const std::uint64_t guid,
                                   int ) {
  if (guid == 0) {
    return nullptr;
  }

  const ObjectGuid object_guid(guid);
  if (objects.transport_manager().GetTransport(object_guid) != nullptr) {
    return &kMovementVf244Resolved;
  }

  if (const auto* const unit = objects.GetUnit(object_guid);
      unit != nullptr && unit->Vehicle().GetVehicleData() != nullptr) {
    return &kMovementVf244Resolved;
  }

  const auto* const object = objects.GetObjectByGUID(object_guid);
  if (object != nullptr && object->IsGameObject() &&
      object->CanBeTransportParent()) {
    return &kMovementVf244Resolved;
  }

  return nullptr;
}

void* Movement_ResolveAndCallVf236(const ObjectManager& objects,
                                   const std::uint64_t guid_raw) {
  if (guid_raw == 0) {
    return nullptr;
  }

  const ObjectGuid guid(guid_raw);
  if (objects.transport_manager().GetTransport(guid) != nullptr) {
    return &kMovementVf236Resolved;
  }

  const auto* const object = objects.GetObjectByGUID(guid);
  if (object != nullptr && object->CanBeTransportParent()) {
    return &kMovementVf236Resolved;
  }

  return nullptr;
}

int Movement_GetObjectTransform(const ObjectManager& objects,
                                const std::uint64_t guid_raw,
                                float* out_matrix_4x4) {
  if (out_matrix_4x4 == nullptr) {
    return 0;
  }

  WriteIdentityMatrix(out_matrix_4x4);

  if (guid_raw == 0) {
    return 0;
  }

  const ObjectGuid guid(guid_raw);
  if (const auto* transport = objects.transport_manager().GetTransport(guid)) {
    transport->BuildWorldTransform(out_matrix_4x4);
    return 1;
  }

  const auto* object = objects.GetObjectByGUID(guid);
  if (object == nullptr) {
    return 0;
  }

  if (const auto* unit = objects.GetUnit(guid)) {

    unit->GetWorldMatrix(out_matrix_4x4);
    return 1;
  }

  const ObjectGuid transport_guid = object->GetTransportGUID();
  if (!transport_guid.IsEmpty()) {
    if (const auto* transport = objects.transport_manager().GetTransport(transport_guid)) {
      const auto& movement = object->GetMovementInfo();
      openwow::render::RenderMatrix4x4 local_matrix{};
      BuildYawTranslationMatrix(local_matrix.data(),
                                movement.transport.offset_x,
                                movement.transport.offset_y,
                                movement.transport.offset_z,
                                movement.transport.offset_o);
      openwow::render::RenderMatrix4x4 transport_matrix{};
      transport->BuildWorldTransform(transport_matrix.data());
      const auto world_matrix =
          openwow::render::MultiplyMatrix4x4(local_matrix, transport_matrix);
      std::copy(world_matrix.begin(), world_matrix.end(), out_matrix_4x4);
      return 1;
    }
  }

  object->GetWorldMatrix(out_matrix_4x4);
  return 1;
}

void Movement_GetObjectWorldRotation(const ObjectManager& objects,
                                     const std::uint64_t guid_raw,
                                     float* out_quaternion_xyzw) {
  if (out_quaternion_xyzw == nullptr) {
    return;
  }

  WriteIdentityQuaternion(out_quaternion_xyzw);

  if (guid_raw == 0) {
    return;
  }

  const ObjectGuid guid(guid_raw);
  if (const auto* transport = objects.transport_manager().GetTransport(guid)) {
    const auto rotation = transport->GetWorldRotationQuaternion();
    out_quaternion_xyzw[0] = rotation[0];
    out_quaternion_xyzw[1] = rotation[1];
    out_quaternion_xyzw[2] = rotation[2];
    out_quaternion_xyzw[3] = rotation[3];
    return;
  }

  const auto* object = objects.GetObjectByGUID(guid);
  if (object == nullptr) {
    diagnostics::Log(diagnostics::LogLevel::kDebug,
              FormatMovementGuidLog("Movement_GetObjectWorldRotation", guid_raw, __LINE__));
    return;
  }

  const auto [x, y, z, w] = object->GetWorldRotation();
  out_quaternion_xyzw[0] = x;
  out_quaternion_xyzw[1] = y;
  out_quaternion_xyzw[2] = z;
  out_quaternion_xyzw[3] = w;
}

float Movement_GetObjectOrientation(const ObjectManager& objects,
                                    const std::uint64_t guid) {
  float facing = 0.0f;
  if (TryResolveMovementObjectFacing(objects, ObjectGuid(guid), &facing)) {
    return facing;
  }

  if (guid != 0) {
    diagnostics::Log(diagnostics::LogLevel::kDebug,
              FormatMovementGuidLog(nullptr, guid));
  }
  return 0.0f;
}

float Movement_QueryScriptPlayerFacing(const WorldSession& session) {
  const auto* const active_camera = session.world_camera();
  if (active_camera == nullptr) {
    return 0.0f;
  }

  float facing = 0.0f;
  if (TryResolveMovementObjectFacing(session.objects(),
                                     ObjectGuid{active_camera->bound_object()},
                                     &facing)) {
    return facing;
  }

  return active_camera->yaw();
}

float Movement_NormalizeFacing0ToTau(float angle) {
  angle = std::fmod(angle, kTwoPi);
  if (angle < 0.0f) {
    angle += kTwoPi;
  }
  return angle;
}

float Movement_TransformLocalFacingToWorld(const ObjectManager& objects,
                                           const std::uint64_t parent_guid,
                                           const float local_facing) {
  return TransformFacingRelativeToParent(objects, parent_guid, local_facing, 1.0f);
}

float Movement_TransformWorldFacingToLocal(const ObjectManager& objects,
                                           const std::uint64_t parent_guid,
                                           const float world_facing) {
  return TransformFacingRelativeToParent(objects, parent_guid, world_facing, -1.0f);
}

float* Passenger_TransformLocalToWorldPosition(
    const ObjectManager& objects, const std::uint64_t parent_guid,
                                               float* out_world_pos,
                                               const float* local_pos) {
  if (out_world_pos == nullptr || local_pos == nullptr) {
    return out_world_pos;
  }

  if (parent_guid == 0) {
    CopyVec3(out_world_pos, local_pos);
    return out_world_pos;
  }

  float transform[16];
  if (Movement_GetObjectTransform(objects, parent_guid, transform) == 0) {
    diagnostics::Log(diagnostics::LogLevel::kDebug,
              FormatMovementGuidLog("Passenger.cpp", parent_guid, 0x42));
  }
  return Passenger_TransformLocalPointToWorld(
      out_world_pos, local_pos, transform);
}

float* Passenger_TransformWorldToLocalPosition(
    const ObjectManager& objects, const std::uint64_t parent_guid,
                                               float* out_local_pos,
                                               const float* world_pos) {
  if (out_local_pos == nullptr || world_pos == nullptr) {
    return out_local_pos;
  }

  if (parent_guid == 0) {
    CopyVec3(out_local_pos, world_pos);
    return out_local_pos;
  }

  float transform[16];
  if (Movement_GetObjectTransform(objects, parent_guid, transform) == 0) {
    diagnostics::Log(diagnostics::LogLevel::kDebug,
              FormatMovementGuidLog("Passenger.cpp", parent_guid, 0x4B));
  }
  return Passenger_TransformWorldPointToLocal(
      out_local_pos, world_pos, transform);
}

void Passenger_GetPackedWorldOrientation(
    const ObjectManager& objects,
    const std::uint64_t parent_guid,
    const std::int64_t packed_local_orientation,
    float* const out_quaternion_xyzw) {
  if (out_quaternion_xyzw == nullptr) {
    return;
  }

  PassengerQuaternion parent_rotation{};
  const PassengerQuaternion* parent_rotation_ptr = nullptr;
  if (parent_guid != 0) {
    Movement_GetObjectWorldRotation(objects, parent_guid, &parent_rotation.x);
    parent_rotation_ptr = &parent_rotation;
  }

  const auto world_rotation = Passenger_GetWorldOrientation(
      packed_local_orientation, parent_rotation_ptr);
  out_quaternion_xyzw[0] = world_rotation.x;
  out_quaternion_xyzw[1] = world_rotation.y;
  out_quaternion_xyzw[2] = world_rotation.z;
  out_quaternion_xyzw[3] = world_rotation.w;
}

float Passenger_GetPackedWorldFacing(
    const ObjectManager& objects,
    const std::uint64_t parent_guid,
    const std::int64_t packed_local_orientation) {
  PassengerQuaternion world_rotation{};
  Passenger_GetPackedWorldOrientation(objects, parent_guid,
                                      packed_local_orientation,
                                      &world_rotation.x);
  return Passenger_GetFacingFromQuaternion(world_rotation);
}

void MovementShared_GetWorldPosition(const ObjectManager& objects,
                                     const std::uint64_t transport_guid,
                                     const float* local_position,
                                     float* out_world_position) {
  if (out_world_position == nullptr || local_position == nullptr) {
    return;
  }

  if (transport_guid == 0) {
    CopyVec3(out_world_position, local_position);
    return;
  }

  float transform[16];
  Movement_GetObjectTransform(objects, transport_guid, transform);
  openwow::math::row_major_mat4x4::TransformVec3ByUpper3x3(
      out_world_position, local_position, transform);
}

void MovementShared_GetWorldFacingDirection(const ObjectManager& objects,
                                            const std::uint64_t transport_guid,
                                            const float* local_direction,
                                            float* out_world_direction) {
  if (out_world_direction == nullptr || local_direction == nullptr) {
    return;
  }

  if (transport_guid == 0) {
    CopyVec3(out_world_direction, local_direction);
    return;
  }

  float transform[16];
  Movement_GetObjectTransform(objects, transport_guid, transform);
  openwow::math::row_major_mat4x4::TransformVec3ByUpper3x3(
      out_world_direction, local_direction, transform);
}

void Movement_NotifyVehicle(const ObjectManager& objects, int , int ,
                            const std::uint64_t guid) {
  if (!Movement_IsVehicleOrPlayerGuid(guid)) {
    return;
  }

  const auto* const unit = objects.GetUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return;
  }

  auto* const vehicle_data = unit->Vehicle().GetVehicleData();
  if (vehicle_data == nullptr) {
    return;
  }

  vehicle::Vehicle_C_ComputeAvailableSeatMask(vehicle_data);
}

}

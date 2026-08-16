
#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;
class ObjectManager;

void* Movement_ResolveAndCallVf244(const ObjectManager& objects, int param1,
                                   std::uint64_t guid, int param3);

void* Movement_ResolveAndCallVf236(const ObjectManager& objects,
                                   std::uint64_t guid);

[[nodiscard]] bool Movement_IsVehicleOrPlayerGuid(std::uint64_t guid);

int Movement_GetObjectTransform(const ObjectManager& objects,
                                std::uint64_t guid, float* out_matrix_4x4);

void Movement_GetObjectWorldRotation(const ObjectManager& objects,
                                     std::uint64_t guid,
                                     float* out_quaternion_xyzw);

float Movement_GetObjectOrientation(const ObjectManager& objects,
                                    std::uint64_t guid);

float Movement_QueryScriptPlayerFacing(const WorldSession& session);

float Movement_NormalizeFacing0ToTau(float angle);

float Movement_TransformLocalFacingToWorld(const ObjectManager& objects,
                                           std::uint64_t parent_guid,
                                           float local_facing);

float Movement_TransformWorldFacingToLocal(const ObjectManager& objects,
                                           std::uint64_t parent_guid,
                                           float world_facing);

float* Passenger_TransformLocalToWorldPosition(const ObjectManager& objects,
                                               std::uint64_t parent_guid,
                                               float* out_world_pos,
                                               const float* local_pos);

float* Passenger_TransformWorldToLocalPosition(const ObjectManager& objects,
                                               std::uint64_t parent_guid,
                                               float* out_local_pos,
                                               const float* world_pos);

void Passenger_GetPackedWorldOrientation(
    const ObjectManager& objects,
    std::uint64_t parent_guid,
    std::int64_t packed_local_orientation,
    float* out_quaternion_xyzw);

[[nodiscard]] float Passenger_GetPackedWorldFacing(
    const ObjectManager& objects,
    std::uint64_t parent_guid,
    std::int64_t packed_local_orientation);

void MovementShared_GetWorldPosition(const ObjectManager& objects,
                                     std::uint64_t transport_guid,
                                     const float* local_position,
                                     float* out_world_position);

void MovementShared_GetWorldFacingDirection(const ObjectManager& objects,
                                            std::uint64_t transport_guid,
                                            const float* local_direction,
                                            float* out_world_direction);

void Movement_NotifyVehicle(const ObjectManager& objects, int param1, int param2,
                            std::uint64_t guid);

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace openwow::game {
class CGObject_C;
class CGUnit_C;
class ObjectManager;
class WorldSession;
}

namespace openwow::game::vehicle {

static constexpr std::uint32_t kMaxSeatIndex = 26;

static constexpr int kSeatSlotCount = 16;

static constexpr int kSeatSlotStride = 4;

static constexpr int kDbcSeatCount = 8;

inline constexpr std::size_t kVehicleRuntimeStorageBytes = 416u;

enum class PendingSeatTransitionPolicy : std::uint32_t {

  kCompletePassenger = 1u,

  kKeepAnimationActive = 2u,
};

int Vehicle_C_AddPendingSeatTransition(void* vehicle,
                                       int guidLow, int guidHigh,
                                       std::uint32_t attachmentId,
                                       PendingSeatTransitionPolicy policy);

void Vehicle_C_RemovePendingSeatTransition(void* vehicle, int guidLow, int guidHigh);

enum class PendingSeatAnimationRoute : std::uint8_t {
  kEnterGesture,
  kExitTransition,
};

std::size_t Vehicle_C_ConsumePendingSeatAnimation(
    WorldSession& session,
    void* vehicle,
    PendingSeatAnimationRoute route,
    std::uint32_t animation_slot,
    std::uint32_t fourcc);

void* VehiclePassenger_SetSeatBit(void* passenger, int bitIndex);

void* VehiclePassenger_ClearSeatBit(void* vehicle_data, int bitIndex);

void* Vehicle_C_Init();

void Vehicle_C_Cleanup();

void* Vehicle_C_CreateRuntimeData(const openwow::game::CGUnit_C& owner,
                                  std::uint32_t vehicleRecordId);

void Vehicle_C_UpdateVehicleEntry(void* vehicle, std::uint32_t vehicleRecordId);

[[nodiscard]] bool Vehicle_C_StartSeatAnimation(void* vehicle,
                                                std::uint32_t animationGroup,
                                                std::uint32_t animationId);

void Vehicle_C_SetPendingTransfer(void* vehicle, std::uint64_t passengerGuid,
                                  std::uint32_t animationGroup);

void Vehicle_C_ResetSeatAnimations(WorldSession& session, void* vehicle);

void* Vehicle_C_GetRootModel(void* vehicleData);

void SendEjectPassengerPacket(int guidLow, int guidHigh);

void Vehicle_C_UpdatePassengers(WorldSession& session, void* vehicle, int animId,
                                unsigned int seatIndex, int animParam,
                                int skipFlag, int extraParam);

int Vehicle_ProcessDirtySeatAnimation(WorldSession& session,
                                      openwow::game::CGUnit_C& ownerUnit,
                                      void* vehicleData,
                                      std::int32_t m2InstanceId,
                                      std::uint32_t seatIndex);

[[nodiscard]] bool Vehicle_C_HasSeatAnimOverride(const void* vehicleData,
                                                 std::uint32_t animGroupId);

void Vehicle_C_ForEachPassengerUnit(
    const void* vehicleData,
    const std::function<void(openwow::game::CGUnit_C&)>& fn);

void* Vehicle_C_DetachAllPassengers(WorldSession& session, void* vehicle);

int VehiclePassenger_HandleTransition(WorldSession& session, void* vehicle,
                                     unsigned int seatIndex, int param1,
                                     int param2);

int Vehicle_C_UnlinkAndReinsertNode(void* vehicle, void* node);

void Vehicle_C_UpdateTransformHierarchy(void* vehicle, float* parentTransform);

void* Vehicle_C_UpdateTransformFromParent(const ObjectManager& objects,
                                          void* vehicle);

bool Vehicle_C_TryCopyTransformMatrix(const void* vehicle, float* out_matrix);

[[nodiscard]] bool Vehicle_C_HasDbcEntry(const void* vehicle);

void Vehicle_C_UpdateBoundingRadius(void* vehicle);

void Vehicle_C_SyncPassengerAnimations(void* vehicleData);

void Vehicle_C_ComputeAvailableSeatMask(void* vehicle);

int Vehicle_C_AccumulateObjectFacing(float* accumulator,
                                     const openwow::game::CGObject_C* object);

void Vehicle_C_DecrementTypeHandle(void* typeInfo);

bool Vehicle_C_ShouldRedirectMouseToAim(const void* vehicleData,
                                        std::uint32_t ownerMovementFlags);

}

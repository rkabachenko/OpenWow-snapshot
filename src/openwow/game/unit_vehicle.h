
#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::data::dbc { struct VehicleEntry; }
namespace openwow::net { struct CDataStore; }

namespace openwow::game {

class CGUnit_C;
class WorldSession;

inline constexpr std::uint32_t CMSG_REQUEST_VEHICLE_EXIT        = 1142;
inline constexpr std::uint32_t CMSG_REQUEST_VEHICLE_PREV_SEAT   = 1143;
inline constexpr std::uint32_t CMSG_REQUEST_VEHICLE_NEXT_SEAT   = 1144;
inline constexpr std::uint32_t CMSG_REQUEST_VEHICLE_SWITCH_SEAT = 1145;

void Unit_SetVehicleSeatTransferPacketBit(void* unit, int enabled);

bool UnitVehicle_HasNonStaticSeat(const WorldSession& session,
                                   std::uint64_t vehicle_guid_lo,
                                   std::uint64_t vehicle_guid_hi,
                                   std::uint8_t seat_index);

bool UnitVehicle_IsActivePlayerInVehicle(const void* unit);

bool UnitVehicle_CanTransferToSeat(const WorldSession& session,
                                    const void* unit,
                                    std::uint32_t target_guid_lo,
                                    std::uint32_t target_guid_hi,
                                    std::uint8_t seat_index);

bool UnitVehicle_TransferToSeat(WorldSession& session,
                                 const void* unit,
                                 double timestamp,
                                 std::uint64_t target_guid,
                                 std::uint8_t seat_index);

void UnitVehicle_ConditionalExitOnInteraction(WorldSession& session,
                                               CGUnit_C& unit);

void UnitVehicle_RequestExit(WorldSession& session, const void* unit);

void UnitVehicle_RequestPrevSeat(WorldSession& session,
                                 const void* unit);

void UnitVehicle_RequestNextSeat(WorldSession& session,
                                 const void* unit);

bool UnitVehicle_RequestSwitchToSeat(WorldSession& session,
                                     const void* unit,
                                     const void* root_vehicle,
                                     int expanded_seat);

void UnitVehicle_EnsureVehicleData(WorldSession& session, CGUnit_C& unit,
                                   std::uint32_t vehicle_record_id);

void UnitVehicle_ClearVehicleData(WorldSession& session, CGUnit_C& unit,
                                  bool detach_passengers);

void UnitVehicle_UpdateSeatUI(const CGUnit_C* unit,
                              const data::dbc::VehicleEntry* vehicle_entry);

void* UnitVehicle_FindRootVehicle(void* unit, void* stop_at);

void* UnitVehicle_WalkPassengerChain(void* unit, void* stop_at);

[[nodiscard]] bool UnitVehicle_ShouldSetupVehicleCameraForActiveCamera(
    const CGUnit_C& unit,
    const CGUnit_C* active_player,
    ObjectGuid active_camera_bound_guid,
    const CGUnit_C* active_camera_vehicle_owner);

void UnitVehicle_DestroyCamera(WorldSession& session, void* unit);

void UnitVehicle_ProcessSeatChange(WorldSession& session, void* unit,
                                    double timestamp,
                                    std::uint64_t target_guid,
                                    std::uint8_t seat_index,
                                    bool from_update);

bool UnitVehicle_TryAttachPassengerFromUpdate(WorldSession& session,
                                              CGUnit_C& unit,
                                              double timestamp,
                                              std::uint64_t target_guid,
                                              std::uint8_t seat_index);

void UnitVehicle_RebuildCreatePassengerAttachment(WorldSession& session,
                                                   CGUnit_C& unit,
                                                   double timestamp);

void UnitVehicle_ReleasePassengerForUnit(CGUnit_C& unit);

bool TryVehicleSeatTransfer(WorldSession& session, CGUnit_C& unit,
                            double timestamp,
                            net::CDataStore* packet_remainder,
                            std::uint64_t target_guid,
                            std::uint8_t seat_index);

}

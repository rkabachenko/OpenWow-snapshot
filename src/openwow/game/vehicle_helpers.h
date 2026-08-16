
#pragma once

#include <cstdint>
#include "openwow/game/object_guid.h"

namespace openwow::data::dbc {
struct VehicleSeatEntry;
}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class CGUnit_C;
class VehiclePassengerC;
class WorldSession;
class ObjectManager;

namespace VehicleControlSeatFlag {
inline constexpr std::uint32_t kCanExit = 0x02000000u;
inline constexpr std::uint32_t kCanSwitch = 0x04000000u;
inline constexpr std::uint32_t kHasControls = 0x08000000u;
}

[[nodiscard]] std::uint32_t ResolveUnitVehicleSeatRecordId(
    const WorldSession& session, const CGUnit_C& unit);
[[nodiscard]] std::uint32_t LookupVehicleSeatRecordIdForVehicleSeat(
    const WorldSession& session, const CGUnit_C& vehicle_unit,
    std::uint8_t seat_index);
[[nodiscard]] const openwow::data::dbc::VehicleSeatEntry* LookupVehicleSeatEntryById(
    const WorldSession& session, std::uint32_t seat_record_id);

[[nodiscard]] bool HasVehicleTransitionTargetGuid(std::uint64_t guid);
[[nodiscard]] const openwow::data::dbc::VehicleSeatEntry* LookupVehicleSeatEntryForVehicleSeat(
    const WorldSession& session, const CGUnit_C& vehicle_unit,
    std::uint8_t seat_index);
[[nodiscard]] const openwow::data::dbc::VehicleSeatEntry* ResolveUnitVehicleSeatEntry(
    const WorldSession& session, const CGUnit_C& unit);

[[nodiscard]] bool VehicleTransitionProfileEnabled(
    const CGUnit_C& unit,
    const openwow::data::dbc::VehicleSeatEntry* seat_entry,
    bool allow_seat_profile);
[[nodiscard]] const openwow::data::dbc::VehicleSeatEntry* LookupCachedUnitVehicleSeatEntry(
    const WorldSession& session, ObjectGuid guid);
[[nodiscard]] const CGUnit_C* ResolveVehicleControlBoundUnit(
    const WorldSession& session);
[[nodiscard]] bool CanUseVehicleControlAction(const WorldSession& session,
                                              std::uint32_t required_seat_flag);

[[nodiscard]] const CGUnit_C* ResolveRootVehicleUnit(const CGUnit_C& unit);
[[nodiscard]] int CountExpandedVehicleSeats(const CGUnit_C& vehicle_unit);
[[nodiscard]] bool FindExpandedVehicleSeat(const CGUnit_C& vehicle_unit,
                                           int seat_ordinal,
                                           const CGUnit_C*& out_vehicle_unit,
                                           std::uint8_t& out_seat_index);
[[nodiscard]] const CGUnit_C* FindDirectVehiclePassengerBySeatIndex(
    const CGUnit_C& vehicle_unit, std::uint8_t seat_index);

[[nodiscard]] std::int32_t ResolveVehicleSeatTransitionAnimationId(
    const VehiclePassengerC& passenger,
    const openwow::data::dbc::VehicleSeatEntry& seat_entry);

void Vehicle_DispatchSeatAnimation(VehiclePassengerC& passenger, std::int32_t behavior_id);

int Vehicle_ProcessPendingSeatTimer(WorldSession& session,
                                    VehiclePassengerC* passenger);

void Vehicle_PlayTransitionEmote(
    CGUnit_C& unit,
    const openwow::data::dbc::VehicleSeatEntry* seat_dbc,
    int animation_id);

void* Vehicle_ForwardDismount(void* unit, int param);

void* Vehicle_ForwardSeatSwitch(void* unit, int time, int guid_lo,
                                 int guid_hi, std::uint8_t seat);

int Vehicle_ValidateCameraChain(void* unit,
                                const openwow::world::WorldCamera& camera);

void Vehicle_TransformCameraTargetFacingToLocal(
    const ObjectManager& objects, const CGUnit_C& unit, float* facing);

void Vehicle_RecordCameraFacingMouseYawOverride(
    const ObjectManager& objects, CGUnit_C& unit,
    const openwow::world::WorldCamera& camera, float world_facing);

}


#include "openwow/game/vehicle_aim_controller.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openwow::game {

void VehicleAimController::EnterVehicle(uint64_t vehicleGuid,
                                        uint8_t seatIndex,
                                        VehicleSeatType seatType,
                                        bool hasAim) {
    inVehicle_   = true;
    vehicleGuid_ = vehicleGuid;
    seatIndex_   = seatIndex;
    seatType_    = seatType;
    hasAim_      = hasAim;
    aimState_    = {};
    maxYaw_      = static_cast<float>(2.0 * M_PI);
    seats_.clear();
}

void VehicleAimController::ExitVehicle() {
    inVehicle_   = false;
    vehicleGuid_ = 0;
    seatIndex_   = 0;
    seatType_    = VehicleSeatType::Passenger;
    hasAim_      = false;
    aimState_    = {};
    maxYaw_      = static_cast<float>(2.0 * M_PI);
    seats_.clear();
}

bool VehicleAimController::IsInVehicle() const {
    return inVehicle_;
}

uint8_t VehicleAimController::GetCurrentSeat() const {
    return seatIndex_;
}

VehicleSeatType VehicleAimController::GetSeatType() const {
    return seatType_;
}

void VehicleAimController::AdjustPitch(float delta) {
    if (!hasAim_) return;
    aimState_.pitchAngle = std::clamp(aimState_.pitchAngle + delta,
                                      aimState_.minPitch,
                                      aimState_.maxPitch);
}

void VehicleAimController::AdjustYaw(float delta) {
    if (!hasAim_) return;
    float halfYaw = maxYaw_ / 2.0f;
    aimState_.yawAngle = std::clamp(aimState_.yawAngle + delta,
                                    -halfYaw, halfYaw);
}

VehicleAimState VehicleAimController::GetAimState() const {
    return aimState_;
}

void VehicleAimController::SetAimLimits(float minPitch, float maxPitch,
                                        float maxYaw) {
    aimState_.minPitch = minPitch;
    aimState_.maxPitch = maxPitch;
    maxYaw_            = maxYaw;

    aimState_.pitchAngle = std::clamp(aimState_.pitchAngle,
                                      aimState_.minPitch,
                                      aimState_.maxPitch);
    float halfYaw = maxYaw_ / 2.0f;
    aimState_.yawAngle = std::clamp(aimState_.yawAngle, -halfYaw, halfYaw);
}

void VehicleAimController::ResetAim() {
    aimState_.pitchAngle = 0.0f;
    aimState_.yawAngle   = 0.0f;
}

bool VehicleAimController::HasAimControl() const {
    return inVehicle_ && hasAim_;
}

void VehicleAimController::AddSeat(const VehicleSeatUIInfo& info) {
    if (seats_.size() < kMaxSeats) {
        seats_.push_back(info);
    }
}

std::vector<VehicleSeatUIInfo> VehicleAimController::GetSeats() const {
    return seats_;
}

uint8_t VehicleAimController::GetOccupiedSeatCount() const {
    uint8_t count = 0;
    for (const auto& s : seats_) {
        if (s.isOccupied) ++count;
    }
    return count;
}

bool VehicleAimController::CanSwitchSeat(uint8_t fromIndex,
                                         uint8_t toIndex) const {
    if (!inVehicle_) return false;
    if (fromIndex == toIndex) return false;

    bool fromValid = false;
    bool toValid   = false;

    for (const auto& s : seats_) {
        if (s.seatIndex == fromIndex && s.isOccupied) {
            fromValid = true;
        }
        if (s.seatIndex == toIndex && !s.isOccupied && s.canSwitch) {
            toValid = true;
        }
    }

    return fromValid && toValid;
}

}

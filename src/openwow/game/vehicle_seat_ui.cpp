
#include "openwow/game/vehicle_seat_ui.h"

namespace openwow::game {

void VehicleSeatUI::SetVehicleData(const VehicleUIData& data) {
    data_ = data;

    hasMySeat_ = false;
    for (const auto& seat : data.seats) {
        if (HasFlag(static_cast<VehicleSeatFlag>(seat.flags),
                    VehicleSeatFlag::IsDriver)) {
            mySeatIndex_ = seat.seatIndex;
            hasMySeat_ = true;
            break;
        }
    }
    if (!hasMySeat_) {
        for (const auto& seat : data.seats) {
            if (!seat.isEmpty) {
                mySeatIndex_ = seat.seatIndex;
                hasMySeat_ = true;
                break;
            }
        }
    }
}

void VehicleSeatUI::ClearVehicleData() {
    data_.reset();
    hasMySeat_ = false;
    mySeatIndex_ = 0;
}

std::optional<VehicleUIData> VehicleSeatUI::GetVehicleData() const {
    return data_;
}

bool VehicleSeatUI::IsInVehicle() const {
    return data_.has_value();
}

std::optional<VehicleSeatUIEntry> VehicleSeatUI::GetMySeat() const {
    if (!data_ || !hasMySeat_) return std::nullopt;
    for (const auto& seat : data_->seats) {
        if (seat.seatIndex == mySeatIndex_) return seat;
    }
    return std::nullopt;
}

uint8_t VehicleSeatUI::GetSeatCount() const {
    if (!data_) return 0;
    return data_->seatCount;
}

uint8_t VehicleSeatUI::GetPassengerCount() const {
    if (!data_) return 0;
    uint8_t count = 0;
    for (const auto& seat : data_->seats) {
        if (!seat.isEmpty) ++count;
    }
    return count;
}

std::vector<uint8_t> VehicleSeatUI::GetEmptySeats() const {
    std::vector<uint8_t> result;
    if (!data_) return result;
    for (const auto& seat : data_->seats) {
        if (seat.isEmpty) {
            result.push_back(seat.seatIndex);
        }
    }
    return result;
}

bool VehicleSeatUI::CanExit() const {
    auto seat = GetMySeat();
    if (!seat) return false;
    return HasFlag(static_cast<VehicleSeatFlag>(seat->flags),
                   VehicleSeatFlag::CanExit);
}

bool VehicleSeatUI::HasAimControl() const {
    auto seat = GetMySeat();
    if (!seat) return false;
    return HasFlag(static_cast<VehicleSeatFlag>(seat->flags),
                   VehicleSeatFlag::HasAimControl);
}

bool VehicleSeatUI::HasVehicleActionBar() const {
    if (!data_) return false;
    return data_->hasActionBar;
}

void VehicleSeatUI::Reset() {
    ClearVehicleData();
}

}

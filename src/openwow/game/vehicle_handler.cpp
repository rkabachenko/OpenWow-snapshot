
#include "openwow/game/vehicle_handler.h"

#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

bool VehicleHandler::HandlePlayerVehicleData(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  PlayerVehicleData vd{};
  if (!r.ReadPackedGuid(vd.player_guid)) return false;
  if (!r.ReadU32(vd.vehicle_id)) return false;
  last_vehicle_data_ = vd;
  return true;
}

bool VehicleHandler::HandleForceSetVehicleRecId(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  ForceSetVehicleRecId fv{};
  if (!r.ReadPackedGuid(fv.unit_guid)) return false;
  if (!r.ReadU32(fv.sequence)) return false;
  std::uint32_t rec_u32 = 0;
  if (!r.ReadU32(rec_u32)) return false;
  fv.vehicle_rec_id = static_cast<std::int32_t>(rec_u32);
  last_force_vehicle_ = fv;
  return true;
}

bool VehicleHandler::HandleCancelExpectedRideVehicleAura(
    const std::uint8_t* , std::size_t ) {

  ride_vehicle_cancelled_ = true;
  return true;
}

void VehicleHandler::SetSeats(std::vector<VehicleSeatInfo> seats) {
  seats_ = std::move(seats);
}

std::size_t VehicleHandler::GetSeatCount() const {
  return seats_.size();
}

const VehicleSeatInfo* VehicleHandler::GetSeat(std::uint32_t index) const {
  if (static_cast<std::size_t>(index) >= seats_.size()) return nullptr;
  return &seats_[index];
}

VehicleSeatInfo* VehicleHandler::GetSeatMutable(std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= seats_.size()) return nullptr;
  return &seats_[index];
}

std::size_t VehicleHandler::GetOccupiedSeatCount() const {
  std::size_t count = 0;
  for (const auto& s : seats_) {
    if (s.IsOccupied()) ++count;
  }
  return count;
}

std::size_t VehicleHandler::GetEmptySeatCount() const {
  std::size_t count = 0;
  for (const auto& s : seats_) {
    if (!s.IsOccupied()) ++count;
  }
  return count;
}

int VehicleHandler::GetFirstEmptySeat() const {
  for (std::size_t i = 0; i < seats_.size(); ++i) {
    if (!seats_[i].IsOccupied()) return static_cast<int>(i);
  }
  return -1;
}

int VehicleHandler::GetDriverSeatIndex() const {
  for (std::size_t i = 0; i < seats_.size(); ++i) {
    if (seats_[i].IsDriver()) return static_cast<int>(i);
  }
  return -1;
}

bool VehicleHandler::SetPassenger(std::uint32_t seat_index, ObjectGuid passenger) {
  if (static_cast<std::size_t>(seat_index) >= seats_.size()) return false;
  if (seats_[seat_index].IsOccupied()) return false;
  seats_[seat_index].passenger = passenger;
  return true;
}

bool VehicleHandler::RemovePassenger(std::uint32_t seat_index) {
  if (static_cast<std::size_t>(seat_index) >= seats_.size()) return false;
  seats_[seat_index].passenger = ObjectGuid{};
  return true;
}

bool VehicleHandler::RemovePassengerByGuid(ObjectGuid passenger) {
  for (auto& s : seats_) {
    if (s.passenger.GetRawValue() == passenger.GetRawValue()) {
      s.passenger = ObjectGuid{};
      return true;
    }
  }
  return false;
}

int VehicleHandler::FindPassengerSeat(ObjectGuid passenger) const {
  for (std::size_t i = 0; i < seats_.size(); ++i) {
    if (seats_[i].passenger.GetRawValue() == passenger.GetRawValue()) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void VehicleHandler::EnterVehicle(ObjectGuid vehicle_guid,
                                   std::uint32_t seat_index) {
  vehicle_guid_ = vehicle_guid;
  current_seat_ = seat_index;
  in_vehicle_ = true;
  ride_vehicle_cancelled_ = false;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "VehicleHandler: entered vehicle seat " +
      std::to_string(seat_index));
}

void VehicleHandler::ExitVehicle() {
  if (in_vehicle_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "VehicleHandler: exited vehicle");
  }

  in_vehicle_ = false;
  vehicle_guid_ = ObjectGuid{};
  current_seat_ = 0;
}

bool VehicleHandler::SwitchSeat(std::uint32_t new_seat) {
  if (!in_vehicle_) return false;
  if (static_cast<std::size_t>(new_seat) >= seats_.size()) return false;
  if (seats_[new_seat].IsOccupied()) return false;

  if (static_cast<std::size_t>(current_seat_) < seats_.size()) {
    seats_[current_seat_].passenger = ObjectGuid{};
  }
  current_seat_ = new_seat;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "VehicleHandler: switched to seat " + std::to_string(new_seat));
  return true;
}

void VehicleHandler::Clear() {
  last_vehicle_data_ = {};
  last_force_vehicle_ = {};
  ride_vehicle_cancelled_ = false;
  seats_.clear();
  in_vehicle_ = false;
  vehicle_guid_ = ObjectGuid{};
  current_seat_ = 0;
}

}

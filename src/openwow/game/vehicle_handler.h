
#pragma once

#include <cstdint>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

namespace VehicleHandlerSeatFlag {
  inline constexpr std::uint32_t kHasLowerAnimForEnter = 0x01;
  inline constexpr std::uint32_t kHasLowerAnimForRide  = 0x02;
  inline constexpr std::uint32_t kCanControl           = 0x08;
  inline constexpr std::uint32_t kCanCastOnVehicle     = 0x20;
  inline constexpr std::uint32_t kUncontrolled         = 0x40;
  inline constexpr std::uint32_t kCanAttack            = 0x80;
  inline constexpr std::uint32_t kExposeVehicle        = 0x200;
  inline constexpr std::uint32_t kIsDriver             = 0x2000;
}

struct VehicleSeatInfo {
  std::uint32_t seat_index = 0;
  std::uint32_t flags = 0;
  ObjectGuid    passenger;
  float         attachment_offset_x = 0.0f;
  float         attachment_offset_y = 0.0f;
  float         attachment_offset_z = 0.0f;

  [[nodiscard]] bool IsOccupied() const { return static_cast<bool>(passenger); }
  [[nodiscard]] bool IsDriver() const {
    return (flags & VehicleHandlerSeatFlag::kIsDriver) != 0;
  }
  [[nodiscard]] bool CanControl() const {
    return (flags & VehicleHandlerSeatFlag::kCanControl) != 0;
  }
  [[nodiscard]] bool CanAttack() const {
    return (flags & VehicleHandlerSeatFlag::kCanAttack) != 0;
  }
};

struct PlayerVehicleData {
  ObjectGuid player_guid;
  std::uint32_t vehicle_id = 0;
};

struct ForceSetVehicleRecId {
  ObjectGuid unit_guid;
  std::uint32_t sequence = 0;
  std::int32_t vehicle_rec_id = 0;
};

class VehicleHandler {
 public:
  bool HandlePlayerVehicleData(const std::uint8_t* data, std::size_t len);
  bool HandleForceSetVehicleRecId(const std::uint8_t* data, std::size_t len);

  bool HandleCancelExpectedRideVehicleAura(const std::uint8_t* data,
                                            std::size_t len);

  const PlayerVehicleData& last_vehicle_data() const { return last_vehicle_data_; }
  const ForceSetVehicleRecId& last_force_vehicle() const { return last_force_vehicle_; }
  bool ride_vehicle_cancelled() const { return ride_vehicle_cancelled_; }

  void SetSeats(std::vector<VehicleSeatInfo> seats);

  [[nodiscard]] std::size_t GetSeatCount() const;

  [[nodiscard]] const VehicleSeatInfo* GetSeat(std::uint32_t index) const;

  VehicleSeatInfo* GetSeatMutable(std::uint32_t index);

  [[nodiscard]] std::size_t GetOccupiedSeatCount() const;

  [[nodiscard]] std::size_t GetEmptySeatCount() const;

  [[nodiscard]] int GetFirstEmptySeat() const;

  [[nodiscard]] int GetDriverSeatIndex() const;

  bool SetPassenger(std::uint32_t seat_index, ObjectGuid passenger);

  bool RemovePassenger(std::uint32_t seat_index);

  bool RemovePassengerByGuid(ObjectGuid passenger);

  [[nodiscard]] int FindPassengerSeat(ObjectGuid passenger) const;

  void EnterVehicle(ObjectGuid vehicle_guid, std::uint32_t seat_index);

  void ExitVehicle();

  [[nodiscard]] bool IsInVehicle() const { return in_vehicle_; }

  [[nodiscard]] ObjectGuid GetVehicleGuid() const { return vehicle_guid_; }

  [[nodiscard]] std::uint32_t GetCurrentSeat() const { return current_seat_; }

  bool SwitchSeat(std::uint32_t new_seat);

  void Clear();

 private:
  PlayerVehicleData last_vehicle_data_{};
  ForceSetVehicleRecId last_force_vehicle_{};
  bool ride_vehicle_cancelled_ = false;

  std::vector<VehicleSeatInfo> seats_;
  bool in_vehicle_ = false;
  ObjectGuid vehicle_guid_;
  std::uint32_t current_seat_ = 0;
};

}

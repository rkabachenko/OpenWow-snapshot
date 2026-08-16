#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class VehicleSeatFlag : uint32_t {
    HasPassenger  = 1,
    IsDriver      = 2,
    CanAttack     = 4,
    CanCast       = 8,
    HasAimControl = 16,
    CanExit       = 32,
};

inline constexpr VehicleSeatFlag operator|(VehicleSeatFlag a,
                                           VehicleSeatFlag b) {
    return static_cast<VehicleSeatFlag>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr VehicleSeatFlag operator&(VehicleSeatFlag a,
                                           VehicleSeatFlag b) {
    return static_cast<VehicleSeatFlag>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr bool HasFlag(VehicleSeatFlag val, VehicleSeatFlag flag) {
    return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

struct VehicleSeatUIEntry {
    uint8_t seatIndex = 0;
    ObjectGuid passenger{};
    uint32_t flags = 0;
    std::string passengerName;
    bool isEmpty = true;
};

struct VehicleUIData {
    uint32_t vehicleId = 0;
    ObjectGuid vehicleGuid{};
    uint8_t seatCount = 0;
    std::vector<VehicleSeatUIEntry> seats;
    uint32_t vehicleSkinId = 0;
    bool hasActionBar = false;
};

class VehicleSeatUI {
 public:
    VehicleSeatUI() = default;

    void SetVehicleData(const VehicleUIData& data);
    void ClearVehicleData();
    [[nodiscard]] std::optional<VehicleUIData> GetVehicleData() const;

    [[nodiscard]] bool IsInVehicle() const;
    [[nodiscard]] std::optional<VehicleSeatUIEntry> GetMySeat() const;
    [[nodiscard]] uint8_t GetSeatCount() const;
    [[nodiscard]] uint8_t GetPassengerCount() const;
    [[nodiscard]] std::vector<uint8_t> GetEmptySeats() const;
    [[nodiscard]] bool CanExit() const;
    [[nodiscard]] bool HasAimControl() const;
    [[nodiscard]] bool HasVehicleActionBar() const;

    void Reset();

 private:
    std::optional<VehicleUIData> data_;
    uint8_t mySeatIndex_ = 0;
    bool hasMySeat_ = false;
};

}

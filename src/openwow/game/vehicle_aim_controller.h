
#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openwow::game {

enum class VehicleSeatType : uint8_t {
    Driver    = 0,
    Passenger = 1,
    Gunner    = 2,
    Control   = 3,
};

struct VehicleSeatUIInfo {
    uint8_t         seatIndex    = 0;
    VehicleSeatType seatType     = VehicleSeatType::Passenger;
    bool            isOccupied   = false;
    uint64_t        occupantGuid = 0;
    std::string     occupantName;
    bool            canSwitch    = false;
    bool            hasAim       = false;
};

struct VehicleAimState {
    float pitchAngle = 0.0f;
    float yawAngle   = 0.0f;
    float aimSpeed   = 2.0f;
    float maxPitch   = static_cast<float>(M_PI / 4.0);
    float minPitch   = static_cast<float>(-M_PI / 4.0);
};

class VehicleAimController {
 public:
    static constexpr uint8_t kMaxSeats = 8;

    void EnterVehicle(uint64_t vehicleGuid, uint8_t seatIndex,
                      VehicleSeatType seatType, bool hasAim);
    void ExitVehicle();
    [[nodiscard]] bool IsInVehicle() const;

    [[nodiscard]] uint8_t          GetCurrentSeat() const;
    [[nodiscard]] VehicleSeatType  GetSeatType() const;

    void AdjustPitch(float delta);
    void AdjustYaw(float delta);
    [[nodiscard]] VehicleAimState GetAimState() const;
    void SetAimLimits(float minPitch, float maxPitch, float maxYaw);
    void ResetAim();
    [[nodiscard]] bool HasAimControl() const;

    void AddSeat(const VehicleSeatUIInfo& info);
    [[nodiscard]] std::vector<VehicleSeatUIInfo> GetSeats() const;
    [[nodiscard]] uint8_t GetOccupiedSeatCount() const;
    [[nodiscard]] bool CanSwitchSeat(uint8_t fromIndex,
                                     uint8_t toIndex) const;

 private:
    bool             inVehicle_   = false;
    uint64_t         vehicleGuid_ = 0;
    uint8_t          seatIndex_   = 0;
    VehicleSeatType  seatType_    = VehicleSeatType::Passenger;
    bool             hasAim_      = false;
    VehicleAimState  aimState_;
    float            maxYaw_      = static_cast<float>(2.0 * M_PI);

    std::vector<VehicleSeatUIInfo> seats_;
};

}

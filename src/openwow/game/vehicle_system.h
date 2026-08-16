
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

enum class VehicleSeatFlags : uint32_t {
    None                     = 0x00000000,
    HasLowerAnimation        = 0x00000001,
    HasLowerAnim             = 0x00000002,
    DisableGravity           = 0x00000004,
    ShouldUseVehicleSeatExit = 0x00000008,
    IdleEmoteLoop            = 0x00000010,
    CanCast                  = 0x00000020,
    CanAttack                = 0x00000040,
    ShouldUseVehSeatExitAnim = 0x00000080,
    Uncontrolled             = 0x00000100,
    CanAim                   = 0x00000200,
    StaticPassenger          = 0x00000400,
    AllowsInteraction        = 0x00000800,
    IsDriver                 = 0x00002000,
    AllowsTurning            = 0x00004000,
    CanControl               = 0x00008000,
    Unk2                     = 0x00010000,
    RecHasVehicleEnterAnim   = 0x00020000,
    RecHasRideAnimLoop       = 0x00040000,
    Unk3                     = 0x00080000,
    RecHasVehicleExitAnim    = 0x00100000,
    CanSwitch                = 0x04000000,
    HasVehicleUI             = 0x20000000,
    CanInteractFromSeat      = 0x80000000,
};

inline constexpr VehicleSeatFlags operator|(VehicleSeatFlags a,
                                            VehicleSeatFlags b) {
    return static_cast<VehicleSeatFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr VehicleSeatFlags operator&(VehicleSeatFlags a,
                                            VehicleSeatFlags b) {
    return static_cast<VehicleSeatFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr bool HasFlag(VehicleSeatFlags val, VehicleSeatFlags flag) {
    return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

enum class VehicleFlags : uint32_t {
    None                     = 0x00000000,
    NoStrafe                 = 0x00000001,
    NoJumping                = 0x00000002,
    FullSpeedTurning         = 0x00000004,
    AllowPitching            = 0x00000010,
    FullSpeedPitching        = 0x00000020,
    CustomPitch              = 0x00000040,
    AimAngleAdjustable       = 0x00000100,
    AimNoPitch               = 0x00000400,
    AllowGroundPitch         = 0x00040000,
    FixedPosition            = 0x00200000,
    OverrideAimMouseRedirect = 0x40000000,
};

inline constexpr VehicleFlags operator|(VehicleFlags a, VehicleFlags b) {
    return static_cast<VehicleFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr bool HasFlag(VehicleFlags val, VehicleFlags flag) {
    return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

struct VehicleSeat {
    uint8_t seatIndex = 0;
    VehicleSeatFlags flags = VehicleSeatFlags::None;
    uint32_t attachmentId = 0;
    ObjectGuid passengerGuid{};

    float cameraOffsetX = 0.0f;
    float cameraOffsetY = 0.0f;
    float cameraOffsetZ = 0.0f;
    float cameraFacingAdjust = 0.0f;
    float cameraPitchOffset = 0.0f;

    uint32_t enterAnimKitId = 0;
    uint32_t rideAnimKitId = 0;
    uint32_t exitAnimKitId = 0;

    [[nodiscard]] bool isEmpty() const { return passengerGuid.IsEmpty(); }
    [[nodiscard]] bool isDriver() const { return HasFlag(flags, VehicleSeatFlags::IsDriver); }
    [[nodiscard]] bool canCast() const { return HasFlag(flags, VehicleSeatFlags::CanCast); }
    [[nodiscard]] bool canAttack() const { return HasFlag(flags, VehicleSeatFlags::CanAttack); }
    [[nodiscard]] bool canAim() const { return HasFlag(flags, VehicleSeatFlags::CanAim); }
    [[nodiscard]] bool canSwitch() const { return HasFlag(flags, VehicleSeatFlags::CanSwitch); }
    [[nodiscard]] bool hasUI() const { return HasFlag(flags, VehicleSeatFlags::HasVehicleUI); }
    [[nodiscard]] bool canControl() const { return HasFlag(flags, VehicleSeatFlags::CanControl); }
    [[nodiscard]] bool canInteractFromSeat() const { return HasFlag(flags, VehicleSeatFlags::CanInteractFromSeat); }

    [[nodiscard]] uint8_t seat_index() const { return seatIndex; }
    [[nodiscard]] uint64_t passenger_guid() const { return passengerGuid.GetRawValue(); }
    [[nodiscard]] bool occupied() const { return !isEmpty(); }
    [[nodiscard]] bool is_usable() const { return true; }
};

enum class VehiclePowerType : uint8_t {
    Mana       = 0,
    Pyrite     = 41,
    Steam      = 61,
    Heat       = 101,
    Ooze       = 121,
    Blood      = 141,
    Wrath      = 142,
    ArcaneEnergy = 143,
    LifeEnergy = 144,
    SunWell    = 145,
    None       = 255,
};

struct VehicleInfo {
    uint32_t vehicleId = 0;
    ObjectGuid guid{};
    uint8_t seatCount = 0;
    std::vector<VehicleSeat> seats;
    uint32_t vehicleFlags = 0;
    VehiclePowerType powerType = VehiclePowerType::None;
    float maxSpeed = 0.0f;
    float turnSpeed = 0.0f;
    float pitchSpeed = 0.0f;

    bool canStrafe = true;
    bool canJump = true;
    bool allowPitching = false;

    uint32_t skinType = 0;

    uint32_t power_type = 0;
    uint32_t current_power = 0;
    uint32_t max_power = 0;

    [[nodiscard]] uint64_t vehicle_guid() const { return guid.GetRawValue(); }
    [[nodiscard]] uint32_t vehicle_id() const { return vehicleId; }
};

struct VehicleActionBarSpell {
    uint32_t spellId = 0;
    uint8_t actionType = 0;
    bool enabled = true;
};

struct SetVehicleRecPacket {
    ObjectGuid unitGuid;
    uint32_t vehicleRecId = 0;
};

class VehicleSystem {
 public:
    static VehicleSystem& Get();

    static constexpr uint8_t kMaxSeats = 8;
    static constexpr uint8_t kMaxActionBarSlots = 6;

    void SetVehicle(const VehicleInfo& info);
    void ExitVehicle();
    [[nodiscard]] bool IsInVehicle() const;
    [[nodiscard]] ObjectGuid GetVehicleGuid() const;
    [[nodiscard]] uint32_t GetVehicleId() const;

    [[nodiscard]] uint8_t GetSeatIndex() const;
    [[nodiscard]] uint8_t GetSeatCount() const;
    [[nodiscard]] std::optional<VehicleSeat> GetSeat(uint8_t index) const;
    [[nodiscard]] std::vector<VehicleSeat> GetOccupiedSeats() const;
    [[nodiscard]] std::vector<uint8_t> GetEmptySeats() const;
    [[nodiscard]] bool IsSeatOccupied(uint8_t index) const;

    [[nodiscard]] bool CanSwitchSeat(uint8_t fromSeat, uint8_t toSeat) const;
    void SwitchSeat(uint8_t newSeat);

    void SetSeatPassenger(uint8_t index, ObjectGuid guid);
    void RemoveSeatPassenger(uint8_t index);

    [[nodiscard]] bool IsDriverSeat() const;
    [[nodiscard]] bool IsDriverSeat(uint8_t seatIndex) const;
    [[nodiscard]] std::optional<uint8_t> FindDriverSeat() const;

    [[nodiscard]] bool CanCastInVehicle() const;
    [[nodiscard]] bool CanAttackInVehicle() const;
    [[nodiscard]] bool CanAim() const;
    [[nodiscard]] bool CanStrafe() const;
    [[nodiscard]] bool CanJump() const;
    [[nodiscard]] bool HasVehicleUI() const;

    [[nodiscard]] float GetVehicleSpeed() const;
    [[nodiscard]] float GetVehicleTurnSpeed() const;
    void SetVehicleSpeed(float speed);

    void SetAimAngle(float radians);
    [[nodiscard]] float GetAimAngle() const;
    void SetAimPitchLimits(float min, float max);
    [[nodiscard]] float GetAimPitchMin() const;
    [[nodiscard]] float GetAimPitchMax() const;

    void UpdatePower(uint32_t current, uint32_t max);
    [[nodiscard]] uint32_t GetCurrentPower() const;
    [[nodiscard]] uint32_t GetMaxPower() const;
    [[nodiscard]] VehiclePowerType GetPowerType() const;

    void SetVehicleActionBar(const std::vector<uint32_t>& spellIds);
    void SetVehicleActionBarDetailed(const std::vector<VehicleActionBarSpell>& spells);
    [[nodiscard]] const std::vector<uint32_t>& GetVehicleActionBar() const;
    [[nodiscard]] const std::vector<VehicleActionBarSpell>& GetDetailedActionBar() const;
    [[nodiscard]] bool HasVehicleActionBar() const;
    [[nodiscard]] uint32_t GetActionBarSpell(uint8_t slot) const;

    struct CameraOffset {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float facingAdjust = 0.0f;
        float pitchOffset = 0.0f;
    };
    [[nodiscard]] CameraOffset GetCameraOffset() const;

    static bool ParseSetVehicleRec(const uint8_t* data, size_t len,
                                   SetVehicleRecPacket& out);

    using VehicleEventCallback = std::function<void()>;
    void SetOnVehicleEntered(VehicleEventCallback cb) { on_entered_ = std::move(cb); }
    void SetOnVehicleExited(VehicleEventCallback cb) { on_exited_ = std::move(cb); }

    void EnterVehicle(const VehicleInfo& info);
    [[nodiscard]] const VehicleInfo* GetCurrentVehicle() const;
    void UpdateSeat(uint8_t seatIndex, uint64_t passenger, bool occupied);
    [[nodiscard]] uint8_t GetPlayerSeat() const;
    void SetPlayerSeat(uint8_t seat);

    void Reset();

    void ClearPassengerAndUpdateCamera();

 private:
    VehicleSystem() = default;

    [[nodiscard]] const VehicleSeat* FindCurrentSeatUnlocked() const;

    bool in_vehicle_ = false;
    VehicleInfo vehicle_;
    uint8_t player_seat_ = 0;
    float aim_angle_ = 0.0f;
    float aim_pitch_min_ = -1.5707964f;
    float aim_pitch_max_ =  1.5707964f;
    std::vector<uint32_t> action_bar_;
    std::vector<VehicleActionBarSpell> detailed_action_bar_;
    VehicleEventCallback on_entered_;
    VehicleEventCallback on_exited_;
    mutable std::mutex mutex_;
};

}

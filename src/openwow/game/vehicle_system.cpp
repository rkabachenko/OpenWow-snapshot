
#include "openwow/game/vehicle_system.h"

#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

namespace {

constexpr float kDefaultAimPitchMin = -1.5707964f;
constexpr float kDefaultAimPitchMax = 1.5707964f;

void FireVehicleAngleUpdate(float raw_pitch, float min_pitch, float max_pitch) {
    ui::game::ScriptEventDispatch::Get().FireVehicleAngleUpdate(raw_pitch, min_pitch, max_pitch);
}

}

VehicleSystem& VehicleSystem::Get() {
    static VehicleSystem instance;
    return instance;
}

const VehicleSeat* VehicleSystem::FindCurrentSeatUnlocked() const {
    for (const auto& seat : vehicle_.seats) {
        if (seat.seatIndex == player_seat_) return &seat;
    }
    return nullptr;
}

void VehicleSystem::SetVehicle(const VehicleInfo& info) {
    VehicleEventCallback cb;
    float aim_angle = 0.0f;
    float min_pitch = kDefaultAimPitchMin;
    float max_pitch = kDefaultAimPitchMax;
    {
        std::lock_guard lock(mutex_);
        vehicle_ = info;
        in_vehicle_ = true;
        player_seat_ = 0;
        aim_angle_ = 0.0f;
        aim_pitch_min_ = kDefaultAimPitchMin;
        aim_pitch_max_ = kDefaultAimPitchMax;
        action_bar_.clear();
        detailed_action_bar_.clear();

        vehicle_.canStrafe = !HasFlag(static_cast<VehicleFlags>(info.vehicleFlags),
                                       VehicleFlags::NoStrafe);
        vehicle_.canJump = !HasFlag(static_cast<VehicleFlags>(info.vehicleFlags),
                                     VehicleFlags::NoJumping);
        vehicle_.allowPitching = HasFlag(static_cast<VehicleFlags>(info.vehicleFlags),
                                          VehicleFlags::AllowPitching);

        aim_angle = aim_angle_;
        min_pitch = aim_pitch_min_;
        max_pitch = aim_pitch_max_;
        cb = on_entered_;
    }
    FireVehicleAngleUpdate(aim_angle, min_pitch, max_pitch);
    if (cb) cb();
}

void VehicleSystem::ExitVehicle() {
    VehicleEventCallback cb;
    float aim_angle = 0.0f;
    float min_pitch = kDefaultAimPitchMin;
    float max_pitch = kDefaultAimPitchMax;
    {
        std::lock_guard lock(mutex_);
        in_vehicle_ = false;
        vehicle_ = {};
        player_seat_ = 0;
        aim_angle_ = 0.0f;
        aim_pitch_min_ = kDefaultAimPitchMin;
        aim_pitch_max_ = kDefaultAimPitchMax;
        action_bar_.clear();
        detailed_action_bar_.clear();
        aim_angle = aim_angle_;
        min_pitch = aim_pitch_min_;
        max_pitch = aim_pitch_max_;
        cb = on_exited_;
    }
    FireVehicleAngleUpdate(aim_angle, min_pitch, max_pitch);
    if (cb) cb();
}

bool VehicleSystem::IsInVehicle() const {
    std::lock_guard lock(mutex_);
    return in_vehicle_;
}

ObjectGuid VehicleSystem::GetVehicleGuid() const {
    std::lock_guard lock(mutex_);
    return vehicle_.guid;
}

uint32_t VehicleSystem::GetVehicleId() const {
    std::lock_guard lock(mutex_);
    return vehicle_.vehicleId;
}

uint8_t VehicleSystem::GetSeatIndex() const {
    std::lock_guard lock(mutex_);
    return player_seat_;
}

uint8_t VehicleSystem::GetSeatCount() const {
    std::lock_guard lock(mutex_);
    return vehicle_.seatCount;
}

std::optional<VehicleSeat> VehicleSystem::GetSeat(uint8_t index) const {
    std::lock_guard lock(mutex_);
    for (const auto& seat : vehicle_.seats) {
        if (seat.seatIndex == index) return seat;
    }
    return std::nullopt;
}

std::vector<VehicleSeat> VehicleSystem::GetOccupiedSeats() const {
    std::lock_guard lock(mutex_);
    std::vector<VehicleSeat> result;
    for (const auto& seat : vehicle_.seats) {
        if (!seat.isEmpty()) result.push_back(seat);
    }
    return result;
}

std::vector<uint8_t> VehicleSystem::GetEmptySeats() const {
    std::lock_guard lock(mutex_);
    std::vector<uint8_t> result;
    for (const auto& seat : vehicle_.seats) {
        if (seat.isEmpty()) result.push_back(seat.seatIndex);
    }
    return result;
}

bool VehicleSystem::IsSeatOccupied(uint8_t index) const {
    std::lock_guard lock(mutex_);
    for (const auto& seat : vehicle_.seats) {
        if (seat.seatIndex == index) return !seat.isEmpty();
    }
    return false;
}

bool VehicleSystem::CanSwitchSeat(uint8_t fromSeat, uint8_t toSeat) const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return false;
    if (fromSeat == toSeat) return false;

    bool from_exists = false;
    bool to_exists = false;
    bool to_empty = false;

    for (const auto& seat : vehicle_.seats) {
        if (seat.seatIndex == fromSeat) from_exists = true;
        if (seat.seatIndex == toSeat) {
            to_exists = true;
            to_empty = seat.isEmpty();
        }
    }

    return from_exists && to_exists && to_empty;
}

void VehicleSystem::SwitchSeat(uint8_t newSeat) {
    std::lock_guard lock(mutex_);
    player_seat_ = newSeat;
}

void VehicleSystem::SetSeatPassenger(uint8_t index, ObjectGuid guid) {
    std::lock_guard lock(mutex_);
    for (auto& seat : vehicle_.seats) {
        if (seat.seatIndex == index) {
            seat.passengerGuid = guid;
            return;
        }
    }

    VehicleSeat s;
    s.seatIndex = index;
    s.passengerGuid = guid;
    vehicle_.seats.push_back(s);
}

void VehicleSystem::RemoveSeatPassenger(uint8_t index) {
    std::lock_guard lock(mutex_);
    for (auto& seat : vehicle_.seats) {
        if (seat.seatIndex == index) {
            seat.passengerGuid = ObjectGuid{};
            return;
        }
    }
}

bool VehicleSystem::IsDriverSeat() const {
    std::lock_guard lock(mutex_);
    const auto* seat = FindCurrentSeatUnlocked();
    return seat != nullptr && seat->isDriver();
}

bool VehicleSystem::IsDriverSeat(uint8_t seatIndex) const {
    std::lock_guard lock(mutex_);
    for (const auto& seat : vehicle_.seats) {
        if (seat.seatIndex == seatIndex) return seat.isDriver();
    }
    return false;
}

std::optional<uint8_t> VehicleSystem::FindDriverSeat() const {
    std::lock_guard lock(mutex_);
    for (const auto& seat : vehicle_.seats) {
        if (seat.isDriver()) return seat.seatIndex;
    }
    return std::nullopt;
}

bool VehicleSystem::CanCastInVehicle() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return false;
    const auto* seat = FindCurrentSeatUnlocked();
    return seat != nullptr && seat->canCast();
}

bool VehicleSystem::CanAttackInVehicle() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return false;
    const auto* seat = FindCurrentSeatUnlocked();
    return seat != nullptr && seat->canAttack();
}

bool VehicleSystem::CanAim() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return false;
    const auto* seat = FindCurrentSeatUnlocked();
    return seat != nullptr && seat->canAim();
}

bool VehicleSystem::CanStrafe() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return true;
    return vehicle_.canStrafe;
}

bool VehicleSystem::CanJump() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return true;
    return vehicle_.canJump;
}

bool VehicleSystem::HasVehicleUI() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return false;

    const auto* seat = FindCurrentSeatUnlocked();
    if (seat && seat->hasUI()) return true;
    return (vehicle_.vehicleFlags & 0x1) != 0;
}

float VehicleSystem::GetVehicleSpeed() const {
    std::lock_guard lock(mutex_);
    return vehicle_.maxSpeed;
}

float VehicleSystem::GetVehicleTurnSpeed() const {
    std::lock_guard lock(mutex_);
    return vehicle_.turnSpeed;
}

void VehicleSystem::SetVehicleSpeed(float speed) {
    std::lock_guard lock(mutex_);
    vehicle_.maxSpeed = speed;
}

void VehicleSystem::SetAimAngle(float radians) {
    float aim_angle = 0.0f;
    float min_pitch = kDefaultAimPitchMin;
    float max_pitch = kDefaultAimPitchMax;
    {
        std::lock_guard lock(mutex_);
        aim_angle_ = std::clamp(radians, aim_pitch_min_, aim_pitch_max_);
        aim_angle = aim_angle_;
        min_pitch = aim_pitch_min_;
        max_pitch = aim_pitch_max_;
    }
    FireVehicleAngleUpdate(aim_angle, min_pitch, max_pitch);
}

float VehicleSystem::GetAimAngle() const {
    std::lock_guard lock(mutex_);
    return aim_angle_;
}

void VehicleSystem::SetAimPitchLimits(float min, float max) {
    std::lock_guard lock(mutex_);
    aim_pitch_min_ = min;
    aim_pitch_max_ = max;
    aim_angle_ = std::clamp(aim_angle_, aim_pitch_min_, aim_pitch_max_);
}

float VehicleSystem::GetAimPitchMin() const {
    std::lock_guard lock(mutex_);
    return aim_pitch_min_;
}

float VehicleSystem::GetAimPitchMax() const {
    std::lock_guard lock(mutex_);
    return aim_pitch_max_;
}

void VehicleSystem::UpdatePower(uint32_t current, uint32_t max) {
    std::lock_guard lock(mutex_);
    vehicle_.current_power = current;
    vehicle_.max_power = max;
}

uint32_t VehicleSystem::GetCurrentPower() const {
    std::lock_guard lock(mutex_);
    return vehicle_.current_power;
}

uint32_t VehicleSystem::GetMaxPower() const {
    std::lock_guard lock(mutex_);
    return vehicle_.max_power;
}

VehiclePowerType VehicleSystem::GetPowerType() const {
    std::lock_guard lock(mutex_);
    return vehicle_.powerType;
}

void VehicleSystem::SetVehicleActionBar(const std::vector<uint32_t>& spellIds) {
    std::lock_guard lock(mutex_);
    action_bar_ = spellIds;
    detailed_action_bar_.clear();
    for (const auto& id : spellIds) {
        VehicleActionBarSpell s;
        s.spellId = id;
        s.enabled = (id != 0);
        detailed_action_bar_.push_back(s);
    }
}

void VehicleSystem::SetVehicleActionBarDetailed(
    const std::vector<VehicleActionBarSpell>& spells) {
    std::lock_guard lock(mutex_);
    detailed_action_bar_ = spells;
    action_bar_.clear();
    for (const auto& s : spells) {
        action_bar_.push_back(s.spellId);
    }
}

const std::vector<uint32_t>& VehicleSystem::GetVehicleActionBar() const {
    std::lock_guard lock(mutex_);
    return action_bar_;
}

const std::vector<VehicleActionBarSpell>& VehicleSystem::GetDetailedActionBar() const {
    std::lock_guard lock(mutex_);
    return detailed_action_bar_;
}

bool VehicleSystem::HasVehicleActionBar() const {
    std::lock_guard lock(mutex_);
    return !action_bar_.empty();
}

uint32_t VehicleSystem::GetActionBarSpell(uint8_t slot) const {
    std::lock_guard lock(mutex_);
    if (slot >= action_bar_.size()) return 0;
    return action_bar_[slot];
}

VehicleSystem::CameraOffset VehicleSystem::GetCameraOffset() const {
    std::lock_guard lock(mutex_);
    const auto* seat = FindCurrentSeatUnlocked();
    if (!seat) return {};
    return CameraOffset{
        seat->cameraOffsetX,
        seat->cameraOffsetY,
        seat->cameraOffsetZ,
        seat->cameraFacingAdjust,
        seat->cameraPitchOffset
    };
}

bool VehicleSystem::ParseSetVehicleRec(const uint8_t* data, size_t len,
                                        SetVehicleRecPacket& out) {
    PacketReader reader(data, len);
    if (!reader.ReadPackedGuid(out.unitGuid)) return false;
    if (!reader.ReadU32(out.vehicleRecId)) return false;
    return true;
}

void VehicleSystem::EnterVehicle(const VehicleInfo& info) {
    SetVehicle(info);
}

const VehicleInfo* VehicleSystem::GetCurrentVehicle() const {
    std::lock_guard lock(mutex_);
    if (!in_vehicle_) return nullptr;
    return &vehicle_;
}

void VehicleSystem::UpdateSeat(uint8_t seatIndex, uint64_t passenger,
                               bool occupied) {
    if (occupied) {
        SetSeatPassenger(seatIndex, ObjectGuid(passenger));
    } else {
        RemoveSeatPassenger(seatIndex);
    }
}

uint8_t VehicleSystem::GetPlayerSeat() const {
    return GetSeatIndex();
}

void VehicleSystem::SetPlayerSeat(uint8_t seat) {
    SwitchSeat(seat);
}

void VehicleSystem::Reset() {
    std::lock_guard lock(mutex_);
    in_vehicle_ = false;
    vehicle_ = {};
    player_seat_ = 0;
    aim_angle_ = 0.0f;
    aim_pitch_min_ = kDefaultAimPitchMin;
    aim_pitch_max_ = kDefaultAimPitchMax;
    action_bar_.clear();
    detailed_action_bar_.clear();
    on_entered_ = nullptr;
    on_exited_ = nullptr;
}

void VehicleSystem::ClearPassengerAndUpdateCamera() {

  if (on_exited_) {
    on_exited_();
  }
}

}

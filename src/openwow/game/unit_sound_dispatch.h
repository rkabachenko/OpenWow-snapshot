
#pragma once
namespace openwow::audio { class SoundRuntime; }

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class CGUnit_C;
class WorldSession;

using VehicleDescriptorRenderReadyCallback =
    bool (*)(const CGUnit_C& unit, void* context);

struct UnitSoundGroundState {
  std::uint32_t terrain_type_id{0};
  std::uint32_t liquid_type_id{0};
  float liquid_surface_z{0.0f};
  bool has_liquid_surface{false};
  float ground_surface_z{0.0f};
  bool has_ground_surface{false};
  float vertical_clearance{0.0f};
  bool has_vertical_clearance{false};
};

using UnitSoundGroundStateCallback = UnitSoundGroundState (*)(
    const CGUnit_C& unit, const float* event_position, void* context);

[[nodiscard]] bool UnitSound_IsHandledAnimFourCC(std::uint32_t fourcc);
int UnitSound_DispatchAnimFourCC(void* unit, std::uint32_t fourcc,
                                  int param, const float* position = nullptr);

void UnitSound_PlayAtPosition(void* unit, const float* position);

void UnitSound_PlayDeathThud(CGUnit_C& unit);

void SetUnitSoundGroundStateCallback(UnitSoundGroundStateCallback callback,
                                     void* context);
void ClearUnitSoundGroundStateCallback();

[[nodiscard]] bool UnitSound_QueryGroundState(
    const CGUnit_C& unit, const float* event_position,
    UnitSoundGroundState& out_state);

int PlayerSound_PlayKit(void* unit, int sound_type, bool force);

int UnitSound_PlayPetActionSound(const CGUnit_C& unit,
                                  std::uint32_t action_type);

void UnitSound_InitializeFootsteps(
    openwow::audio::SoundRuntime& sound_runtime,
    const openwow::data::dbc::DbcLoader& dbc);

void UnitSound_FreeDeathThudTables();

[[nodiscard]] std::uint32_t UnitSound_GetDeathThudSoundKit(
    std::uint32_t size_class, std::uint32_t terrain_type_sound_id, bool use_water_variant);
[[nodiscard]] std::uint32_t UnitSound_GetSpiritWolfPushToTalkSoundKitId();

void VehiclePassenger_SetUpdateFlag();
[[nodiscard]] bool VehiclePassenger_IsUpdatePending();
void VehiclePassenger_ClearUpdateFlag();

void InputControl_SetMouseYaw(void* passenger_component, float yaw_angle);

int Vehicle_RelinkOpaqueNode(void* unit, void* node, int arg3);

void Vehicle_ProcessDescriptorDirty(WorldSession& session, void* unit);

void SetVehicleDescriptorRenderReadyCallback(
    VehicleDescriptorRenderReadyCallback callback, void* context);
void ClearVehicleDescriptorRenderReadyCallback();

void Unit_ResetVehicleCameraAttachmentCache();

void Unit_ClearPassengerComponent(void* unit);

int InputControl_CheckTurnFlag(const void* unit);

}

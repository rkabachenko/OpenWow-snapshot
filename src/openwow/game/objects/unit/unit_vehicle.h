#pragma once
#include "openwow/game/object_guid.h"

#include <cstdint>
#include <memory>

namespace openwow::data::dbc {
struct VehicleEntry;
struct VehicleSeatEntry;
}

namespace openwow::game {

class CGObject_C;
class CGUnit_C;
class VehiclePassengerC;
class WorldSession;

class UnitVehicleComponent final {
public:
  struct LocalTransformFrame {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float facing = 0.0f;
    ObjectGuid parent_guid{};
  };

  UnitVehicleComponent();
  UnitVehicleComponent(const UnitVehicleComponent &) = delete;
  UnitVehicleComponent &operator=(const UnitVehicleComponent &) = delete;
  UnitVehicleComponent(UnitVehicleComponent &&) = delete;
  UnitVehicleComponent &operator=(UnitVehicleComponent &&) = delete;
  ~UnitVehicleComponent();

  [[nodiscard]] void *GetVehicleData() const noexcept;
  void SetVehicleData(CGUnit_C &owner, WorldSession &session, void *vehicle_data);
  void SetOwnedVehicleData(CGUnit_C &owner, WorldSession &session,
                           std::unique_ptr<std::uint8_t[]> vehicle_data);
  void ResetOwnedVehicleData(CGUnit_C &owner, WorldSession &session);
  [[nodiscard]] bool OwnsVehicleData(const void *vehicle_data) const noexcept;
  [[nodiscard]] static CGUnit_C *ResolveVehicleDataOwner(const void *vehicle_data);

  [[nodiscard]] CGUnit_C *GetVehicleUnit() const;
  [[nodiscard]] bool HasValidVehicleUnitGuid() const;
  [[nodiscard]] CGUnit_C *GetVehicleObject(const CGUnit_C &owner) const;
  [[nodiscard]] const data::dbc::VehicleEntry *GetVehicleEntry() const;
  [[nodiscard]] bool HasAttachedVehiclePassenger() const;
  [[nodiscard]] bool IsUsingVehicle() const;
  [[nodiscard]] VehiclePassengerC *GetVehiclePassengerComponent() const noexcept;
  void SetVehiclePassengerComponent(CGUnit_C &owner,
                                    VehiclePassengerC *passenger);
  [[nodiscard]] VehiclePassengerC *CreateVehiclePassenger(CGUnit_C &owner);
  void ReleaseVehiclePassenger(CGUnit_C &owner);

  [[nodiscard]] ObjectGuid ResolveCameraTargetGuid(const CGUnit_C &owner) const;
  [[nodiscard]] const data::dbc::VehicleSeatEntry *
  GetVehiclePassengerSeatEntry() const;
  [[nodiscard]] bool HasVehicleSeatMovementBlock(const CGUnit_C &owner) const;
  [[nodiscard]] bool
  VehicleSuppressesTransitionAnimation(const CGUnit_C &owner) const;
  [[nodiscard]] const data::dbc::VehicleSeatEntry *
  ResolveAttachedVehicleSeatEntry(const CGUnit_C &owner,
                                  const WorldSession &session) const;
  [[nodiscard]] bool VehicleSeatOwnsEmoteState(const CGUnit_C &owner,
                                               const WorldSession &session,
                                               std::uint32_t emote_state) const;
  [[nodiscard]] float GetVehicleChainWorldFacing(const CGUnit_C &owner) const;
  [[nodiscard]] LocalTransformFrame
  GetLocalTransformFrame(const CGUnit_C &owner) const;
  void BuildWorldMatrixWithVehicle(const CGUnit_C &owner,
                                   float *out_matrix) const;
  void BuildPositionWithVehicle(const CGUnit_C &owner,
                                float *out_position) const;

  [[nodiscard]] bool IsPartyRaidPlayerVehicle(
      const CGUnit_C &owner, const CGUnit_C &interactor) const;

  void Cleanup(CGUnit_C &owner) noexcept;

private:
  void *data_{nullptr};
  std::unique_ptr<std::uint8_t[]> owned_data_;
  VehiclePassengerC *passenger_{nullptr};
  std::unique_ptr<VehiclePassengerC> owned_passenger_;
};

}

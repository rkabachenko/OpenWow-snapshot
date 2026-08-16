
#include "openwow/game/vehicle.h"

#include "openwow/core/cobject_heap.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/unit/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_runtime_layout.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_system.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace openwow::game::vehicle {

namespace {

constexpr std::size_t kVehicleRelinkListRootOffset = 92u * sizeof(std::uint32_t);
constexpr std::size_t kVehicleRelinkFlagOffset = 44u;
constexpr std::size_t kVehicleTransformMatrixSize = sizeof(float) * 16u;
constexpr std::size_t kVehicleAvailableSeatMaskOffset = 365u;
constexpr std::size_t kVehicleOrientationOffset = 80u;
constexpr std::size_t kVehicleBoundingRadiusOffset = 84u;
constexpr std::size_t kVehicleRuntimeFlagsOffset = 8u;
constexpr std::size_t kVehicleTransferStateOffset = 88u;
constexpr std::size_t kVehicleSeatSlotsOffset = 96u;
constexpr std::size_t kVehicleSeatSlotsBytes = 16u * 16u;
constexpr std::size_t kVehicleSeatDescriptorStateOffset = 352u;
constexpr std::size_t kVehicleSeatDescriptorState2Offset = 356u;
constexpr std::size_t kVehicleTransferSeatOffset = 360u;
constexpr std::size_t kVehicleSeatBitsOffset = 364u;

struct VehicleRelinkListRoot {
    std::uintptr_t link_offset = 0;
    std::uintptr_t tail_link = 0;
    std::uintptr_t first_node = 0;
};

static_assert(offsetof(VehicleRelinkListRoot, first_node) ==
              sizeof(std::uintptr_t) * 2);

using NativeStormLinkWords = openwow::core::StormIntrusiveLinkWords<std::uintptr_t>;
using NativeStormListRootWords =
    openwow::core::StormIntrusiveListRootWords<std::uintptr_t>;

[[nodiscard]] VehicleRelinkListRoot* ResolveVehicleRelinkListRoot(void* vehicle) {
    return reinterpret_cast<VehicleRelinkListRoot*>(
        static_cast<std::byte*>(vehicle) + kVehicleRelinkListRootOffset);
}

[[nodiscard]] std::uint8_t& ResolveVehicleRelinkFlag(void* node) {
    return *(static_cast<std::uint8_t*>(node) + kVehicleRelinkFlagOffset);
}

template <typename T>
[[nodiscard]] T LoadOpaqueField(const void* base, const std::size_t offset) {
    T value{};
    if (base == nullptr) {
        return value;
    }

    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void StoreOpaqueField(void* base, const std::size_t offset, const T& value) {
    if (base == nullptr) {
        return;
    }

    std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof(T));
}

class ScopedTrueFlag final {
public:
    explicit ScopedTrueFlag(bool& flag) noexcept : flag_(flag), previous_(flag) {
        flag_ = true;
    }
    ~ScopedTrueFlag() { flag_ = previous_; }

    ScopedTrueFlag(const ScopedTrueFlag&) = delete;
    ScopedTrueFlag& operator=(const ScopedTrueFlag&) = delete;

private:
    bool& flag_;
    bool previous_;
};

void StoreVehicleEntryField(
    void* const vehicle,
    const openwow::data::dbc::VehicleEntry* const vehicle_entry) {
    openwow::game::vehicle_runtime_layout::StoreVehicleEntryPointerField(
        vehicle, vehicle_entry);
}

[[nodiscard]] const openwow::data::dbc::VehicleEntry* LoadVehicleEntryField(
    const void* const vehicle) {
    return openwow::game::vehicle_runtime_layout::
        ResolveVehicleEntryPointerField(vehicle);
}

template <typename Fn>
void ForEachVehiclePassengerUnit(const void* vehicle, Fn&& fn) {
    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    auto* const objects = owner != nullptr ? owner->object_manager() : nullptr;
    if (vehicle == nullptr || objects == nullptr) {
        return;
    }

    const auto node_link_offset = LoadOpaqueField<std::int32_t>(
        vehicle,
        vehicle_runtime_layout::kVehiclePassengerListOffset +
            offsetof(NativeStormListRootWords, node_link_offset));
    if (node_link_offset < 0) {
        return;
    }

    auto node = LoadOpaqueField<std::uintptr_t>(
        vehicle,
        vehicle_runtime_layout::kVehiclePassengerListOffset +
            offsetof(NativeStormListRootWords, head_node));
    while (node != 0 &&
           (node & openwow::core::kStormIntrusiveSentinelBit<std::uintptr_t>) == 0) {
        const auto guid_holder = LoadOpaqueField<std::uintptr_t>(
            reinterpret_cast<const void*>(node),
            vehicle_runtime_layout::kVehiclePassengerGuidHolderOffset);
        if (guid_holder != 0) {
            const auto guid_raw =
                LoadOpaqueField<std::uint64_t>(reinterpret_cast<const void*>(guid_holder), 0);
            if (guid_raw != 0) {
                if (auto* unit = objects->GetMutableUnit(ObjectGuid(guid_raw));
                    unit != nullptr) {
                    fn(*unit);
                }
            }
        }

        node = LoadOpaqueField<std::uintptr_t>(
            reinterpret_cast<const void*>(node),
            static_cast<std::size_t>(node_link_offset) +
                offsetof(NativeStormLinkWords, next_node));
    }
}

}

void Vehicle_C_ForEachPassengerUnit(
    const void* vehicle,
    const std::function<void(CGUnit_C&)>& fn) {
    if (!fn) {
        return;
    }

    ForEachVehiclePassengerUnit(vehicle, [&](CGUnit_C& unit) {
        fn(unit);
    });
}

void Vehicle_RefreshOwnerStandAnimation(WorldSession& session,
                                        CGUnit_C& owner_unit) {
    owner_unit.Animation().RefreshSelectedStandAnimation(session, 0u, ~0u);
}

namespace {

bool TryGetPassengerSeatBit(const CGUnit_C& unit, std::uint8_t& out_seat_bit) {
    const auto* passenger = unit.Vehicle().GetVehiclePassengerComponent();
    if (passenger == nullptr) {
        return false;
    }

    const auto seat_index =
        passenger->GetAltVehicleGuid() != 0 ? passenger->GetAltSeatIndex()
                                            : passenger->GetPrimarySeatIndex();
    if (seat_index >= kDbcSeatCount) {
        return false;
    }

    out_seat_bit = static_cast<std::uint8_t>(1u << seat_index);
    return true;
}

[[nodiscard]] float* ResolveVehicleTransformMatrix(void* vehicle) {
    return reinterpret_cast<float*>(static_cast<std::byte*>(vehicle) +
                                    vehicle_runtime_layout::kVehicleTransformMatrixOffset);
}

[[nodiscard]] const openwow::data::dbc::VehicleEntry* ResolveVehicleEntry(
    const CGUnit_C& owner, const std::uint32_t vehicle_record_id) {
    if (owner.object_manager() == nullptr) {
        return nullptr;
    }

    return owner.object_manager()->dbc_loader().vehicle().LookupEntry(
        vehicle_record_id);
}

void InitializeVehicleRuntimeBlock(void* vehicle) {
    if (vehicle == nullptr) {
        return;
    }

    auto* const matrix = ResolveVehicleTransformMatrix(vehicle);
    openwow::math::row_major_mat4x4::SetIdentity(matrix);

    auto* const passenger_list =
        reinterpret_cast<NativeStormListRootWords*>(
            static_cast<std::byte*>(vehicle) +
            vehicle_runtime_layout::kVehiclePassengerListOffset);
    openwow::core::InitializeStormIntrusiveListRoot(*passenger_list, 0);

    StoreOpaqueField(vehicle, kVehicleBoundingRadiusOffset, 0.0f);
    StoreOpaqueField(vehicle, kVehicleTransferStateOffset, std::uint32_t{0});
    StoreOpaqueField(vehicle, kVehicleTransferStateOffset + sizeof(std::uint32_t),
                     std::uint32_t{0});
    std::memset(static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset, 0,
                kVehicleSeatSlotsBytes);
    StoreOpaqueField(vehicle, kVehicleSeatDescriptorStateOffset, std::uint32_t{0});
    StoreOpaqueField(vehicle, kVehicleSeatDescriptorState2Offset, std::uint32_t{0});
    StoreOpaqueField(vehicle, kVehicleTransferSeatOffset, std::int32_t{-1});
    StoreOpaqueField(vehicle, kVehicleSeatBitsOffset, std::uint8_t{0});
}

void SeedVehicleRuntimeTransform(void* vehicle, const CGUnit_C& owner) {
    if (vehicle == nullptr) {
        return;
    }

    auto* const matrix = ResolveVehicleTransformMatrix(vehicle);
    const auto& movement = owner.GetMovementUpdate();
    auto transform = openwow::render::BuildRotationMatrix4x4Z(movement.GetO());
    transform[12] = movement.GetX();
    transform[13] = movement.GetY();
    transform[14] = movement.GetZ();
    std::copy(transform.begin(), transform.end(), matrix);

    const float vehicle_orientation =
        movement.HasUpdateFlag(kUpdateFlagVehicle) ? movement.vehicle_orientation : 0.0f;
    StoreOpaqueField(vehicle, kVehicleOrientationOffset, vehicle_orientation);
}

}

std::uint32_t* s_vehicle_type_handle = nullptr;

static std::uint32_t ClampSeatIndex(std::uint32_t seat) {
    return (seat >= 0x23) ? kMaxSeatIndex : seat;
}

int Vehicle_C_AddPendingSeatTransition(void* vehicle,
                                       int guidLow, int guidHigh,
                                       std::uint32_t attachmentId,
                                       PendingSeatTransitionPolicy policy) {

    if (vehicle == nullptr) {
        return 0;
    }

    const auto clamped = ClampSeatIndex(attachmentId);

    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset);

    for (int i = 0; i < kSeatSlotCount; ++i) {
        const int base = i * kSeatSlotStride;
        const std::uint64_t guid =
            static_cast<std::uint64_t>(slots[base]) |
            (static_cast<std::uint64_t>(slots[base + 1]) << 32);
        if (guid != 0) {
            continue;
        }

        slots[base + 0] = static_cast<std::uint32_t>(guidLow);
        slots[base + 1] = static_cast<std::uint32_t>(guidHigh);
        slots[base + 2] = clamped;
        slots[base + 3] = static_cast<std::uint32_t>(policy);
        return 1;
    }

    return 0;
}

void Vehicle_C_RemovePendingSeatTransition(void* vehicle,
                                           int guidLow, int guidHigh) {

    if (vehicle == nullptr) {
        return;
    }

    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset);

    for (int i = 0; i < kSeatSlotCount; ++i) {
        const int base = i * kSeatSlotStride;
        if (static_cast<int>(slots[base]) == guidLow &&
            static_cast<int>(slots[base + 1]) == guidHigh) {
            slots[base + 0] = 0;
            slots[base + 1] = 0;
        }
    }
}

std::size_t Vehicle_C_ConsumePendingSeatAnimation(
    WorldSession& session,
    void* const vehicle,
    const PendingSeatAnimationRoute route,
    const std::uint32_t animation_slot,
    const std::uint32_t fourcc) {
    if (vehicle == nullptr) {
        return 0u;
    }

    const auto clamped_slot = ClampSeatIndex(animation_slot);
    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset);

    std::size_t completed = 0u;
    for (int slot_index = 0; slot_index < kSeatSlotCount; ++slot_index) {
        const int base = slot_index * kSeatSlotStride;
        const std::uint64_t passenger_guid =
            static_cast<std::uint64_t>(slots[base]) |
            (static_cast<std::uint64_t>(slots[base + 1]) << 32u);
        if (passenger_guid == 0u || slots[base + 2] != clamped_slot) {
            continue;
        }

        auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
        auto* const objects = owner != nullptr ? owner->object_manager() : nullptr;
        auto* const passenger_unit =
            objects != nullptr ? objects->GetMutableUnit(ObjectGuid(passenger_guid))
                               : nullptr;
        auto* const passenger = passenger_unit != nullptr
                                    ? passenger_unit->Vehicle().GetVehiclePassengerComponent()
                                    : nullptr;
        if (passenger == nullptr) {
            continue;
        }

        const bool accepts =
            route == PendingSeatAnimationRoute::kEnterGesture
                ? passenger->AcceptsPendingEnterAnimation(fourcc)
                : passenger->AcceptsPendingExitAnimation(fourcc);
        if (!accepts) {
            continue;
        }

        passenger->ProcessPendingSeatChange(session);
        slots[base] = 0u;
        slots[base + 1] = 0u;
        ++completed;
    }
    return completed;
}

void* VehiclePassenger_SetSeatBit(void* passenger, int bitIndex) {

    auto self = static_cast<std::uint8_t*>(passenger);
    self[364] |= static_cast<std::uint8_t>(1 << bitIndex);
    return passenger;
}

void* VehiclePassenger_ClearSeatBit(void* vehicle_data, int bitIndex) {

    auto self = static_cast<std::uint8_t*>(vehicle_data);
    self[364] &= static_cast<std::uint8_t>(~(1 << bitIndex));
    return vehicle_data;
}

void* Vehicle_C_Init() {

    delete s_vehicle_type_handle;

    auto* handle = new (std::nothrow) std::uint32_t{0};
    if (handle) {

        *handle = openwow::core::CObjectHeapList::Instance().RegisterType(
            380, 16, "Vehicle", true);
        s_vehicle_type_handle = handle;
    } else {
        s_vehicle_type_handle = nullptr;
    }
    return handle;
}

void Vehicle_C_Cleanup() {

    delete s_vehicle_type_handle;
    s_vehicle_type_handle = nullptr;
}

void* Vehicle_C_CreateRuntimeData(const openwow::game::CGUnit_C& owner,
                                  const std::uint32_t vehicleRecordId) {
    auto storage = std::make_unique<std::uint8_t[]>(kVehicleRuntimeStorageBytes);
    std::memset(storage.get(), 0, kVehicleRuntimeStorageBytes);

    auto* const vehicle = storage.get();
    InitializeVehicleRuntimeBlock(vehicle);
    StoreVehicleEntryField(vehicle, ResolveVehicleEntry(owner, vehicleRecordId));
    SeedVehicleRuntimeTransform(vehicle, owner);
    Vehicle_C_ComputeAvailableSeatMask(vehicle);
    return storage.release();
}

void Vehicle_C_UpdateVehicleEntry(void* vehicle, const std::uint32_t vehicleRecordId) {
    if (vehicle == nullptr) {
        return;
    }

    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    StoreVehicleEntryField(
        vehicle, owner != nullptr ? ResolveVehicleEntry(*owner, vehicleRecordId)
                                  : nullptr);
    Vehicle_C_ComputeAvailableSeatMask(vehicle);
}

bool Vehicle_C_StartSeatAnimation(void* const vehicle,
                                  const std::uint32_t animationGroup,
                                  const std::uint32_t animationId) {
    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    if (owner == nullptr || animationId >= 506u) {
        return false;
    }

    const auto instance_id = owner->GetPrimaryM2InstanceId();
    if (instance_id == 0u) {
        return false;
    }

    const auto clamped_group = ClampSeatIndex(animationGroup);
    const auto render_group = clamped_group == kMaxSeatIndex
                                  ? -1
                                  : static_cast<std::int32_t>(clamped_group);
    (void)owner->Animation().SetAnimationRecursive(
        instance_id, render_group, animationId, -1, 0, 1.0f, 1, 1, false);

    auto runtime_flags =
        LoadOpaqueField<std::uint32_t>(vehicle, kVehicleRuntimeFlagsOffset);
    StoreOpaqueField(vehicle, kVehicleRuntimeFlagsOffset, runtime_flags | 1u);

    const auto word_offset =
        kVehicleTransferStateOffset + (clamped_group >> 5u) * sizeof(std::uint32_t);
    auto override_word = LoadOpaqueField<std::uint32_t>(vehicle, word_offset);
    override_word |= 1u << (clamped_group & 0x1Fu);
    StoreOpaqueField(vehicle, word_offset, override_word);
    return true;
}

void Vehicle_C_SetPendingTransfer(void* const vehicle,
                                  const std::uint64_t passengerGuid,
                                  const std::uint32_t animationGroup) {
    if (vehicle == nullptr) {
        return;
    }
    StoreOpaqueField(vehicle, kVehicleSeatDescriptorStateOffset,
                     static_cast<std::uint32_t>(passengerGuid));
    StoreOpaqueField(vehicle, kVehicleSeatDescriptorState2Offset,
                     static_cast<std::uint32_t>(passengerGuid >> 32u));
    StoreOpaqueField(vehicle, kVehicleTransferSeatOffset,
                     static_cast<std::int32_t>(ClampSeatIndex(animationGroup)));
}

void Vehicle_C_ResetSeatAnimations(WorldSession& session, void* const vehicle) {
    if (vehicle == nullptr ||
        (LoadOpaqueField<std::uint32_t>(vehicle, kVehicleRuntimeFlagsOffset) & 1u) == 0u) {
        return;
    }

    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    if (owner == nullptr) {
        return;
    }
    const auto instance_id = owner->GetPrimaryM2InstanceId();
    auto* const m2_system = owner->m2_system();
    if (instance_id == 0u || m2_system == nullptr ||
        m2_system->QueryInstanceAnimationInfo(instance_id).status !=
            openwow::render::m2::M2ResultStatus::kReady) {
        return;
    }

    for (std::uint32_t group = 0u; group < 35u; ++group) {
        const auto word_offset =
            kVehicleTransferStateOffset + (group >> 5u) * sizeof(std::uint32_t);
        if ((LoadOpaqueField<std::uint32_t>(vehicle, word_offset) &
             (1u << (group & 0x1Fu))) == 0u) {
            continue;
        }

        bool cleared = false;
        if (group != kMaxSeatIndex) {
            const auto slot = m2_system->QueryAnimationSlotState(instance_id, group);
            if (slot.status == openwow::render::m2::M2ResultStatus::kReady &&
                slot.has_slot) {
                cleared = m2_system->ClearAnimationSlot(instance_id, group) ==
                          openwow::render::m2::M2ResultStatus::kReady;
            }
        }
        if (!cleared) {
            Vehicle_C_UpdatePassengers(session, vehicle, static_cast<int>(instance_id),
                                       group, -1, 1, 0);
            Vehicle_RefreshOwnerStandAnimation(session, *owner);
        }
    }

    StoreOpaqueField(vehicle, kVehicleTransferStateOffset, std::uint32_t{0});
    StoreOpaqueField(vehicle, kVehicleTransferStateOffset + sizeof(std::uint32_t),
                     std::uint32_t{0});
    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset);
    for (int index = 0; index < kSeatSlotCount; ++index) {
        slots[index * kSeatSlotStride] = 0u;
        slots[index * kSeatSlotStride + 1] = 0u;
    }
    auto runtime_flags =
        LoadOpaqueField<std::uint32_t>(vehicle, kVehicleRuntimeFlagsOffset);
    StoreOpaqueField(vehicle, kVehicleRuntimeFlagsOffset, runtime_flags & ~1u);
    StoreOpaqueField(vehicle, kVehicleSeatDescriptorStateOffset, std::uint32_t{0});
    StoreOpaqueField(vehicle, kVehicleSeatDescriptorState2Offset, std::uint32_t{0});
}

void* Vehicle_C_GetRootModel(void* vehicleData) {
    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicleData);
    if (owner == nullptr) {
        return vehicleData;
    }

    const auto* const root = ResolveRootVehicleUnit(*owner);
    return root != nullptr && root->Vehicle().GetVehicleData() != nullptr
               ? root->Vehicle().GetVehicleData()
               : vehicleData;
}

void SendEjectPassengerPacket(int guidLow, int guidHigh) {

    const auto guid = static_cast<std::uint64_t>(static_cast<std::uint32_t>(guidHigh)) << 32
                    | static_cast<std::uint64_t>(static_cast<std::uint32_t>(guidLow));
    auto pkt = net::wotlk::PacketSender::BuildControllerEjectPassenger(guid);
    (void)net::ClientServices__SendPacket(pkt);
}

void Vehicle_C_UpdatePassengers(WorldSession& session, void* vehicle, int animId,
                                unsigned int seatIndex, int animParam,
                                int skipFlag, int extraParam) {
    static thread_local bool dispatching_vehicle_animation = false;
    if (vehicle == nullptr || (skipFlag != 0 && dispatching_vehicle_animation)) {
        return;
    }

    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    auto* const objects = owner != nullptr ? owner->object_manager() : nullptr;
    if (objects == nullptr) {
        return;
    }

    const auto group = ClampSeatIndex(seatIndex);
    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset);
    std::vector<VehiclePassengerC*> completed_passengers;
    bool keep_animation_active = false;

    for (int index = 0; index < kSeatSlotCount; ++index) {
        const int base = index * kSeatSlotStride;
        const std::uint64_t guid =
            static_cast<std::uint64_t>(slots[base]) |
            (static_cast<std::uint64_t>(slots[base + 1]) << 32u);
        if (guid == 0u || slots[base + 2] != group) {
            continue;
        }

        auto* const unit = objects->GetMutableUnit(ObjectGuid(guid));
        const auto policy =
            static_cast<PendingSeatTransitionPolicy>(slots[base + 3]);
        if (unit != nullptr &&
            policy == PendingSeatTransitionPolicy::kKeepAnimationActive) {
            keep_animation_active = true;
            continue;
        }

        if (unit != nullptr &&
            policy == PendingSeatTransitionPolicy::kCompletePassenger) {
            auto* const passenger = unit->Vehicle().GetVehiclePassengerComponent();
            if (passenger != nullptr &&
                !passenger->HasFlag(VehiclePassengerFlag::kTransitionInProgress)) {
                completed_passengers.push_back(passenger);
            }
        }
        slots[base] = 0u;
        slots[base + 1] = 0u;
    }

    if (skipFlag != 0 || !keep_animation_active) {
        const auto word_offset =
            kVehicleTransferStateOffset + (group >> 5u) * sizeof(std::uint32_t);
        auto override_word = LoadOpaqueField<std::uint32_t>(vehicle, word_offset);
        override_word &= ~(1u << (group & 0x1Fu));
        StoreOpaqueField(vehicle, word_offset, override_word);
    }

    if (group == LoadOpaqueField<std::uint32_t>(vehicle, kVehicleTransferSeatOffset)) {
        StoreOpaqueField(vehicle, kVehicleSeatDescriptorStateOffset, std::uint32_t{0});
        StoreOpaqueField(vehicle, kVehicleSeatDescriptorState2Offset, std::uint32_t{0});
        StoreOpaqueField(vehicle, kVehicleTransferSeatOffset, std::int32_t{-1});
    }

    if (skipFlag == 0 && owner != nullptr) {
        if (keep_animation_active) {
            const auto render_group = group == kMaxSeatIndex
                                          ? -1
                                          : static_cast<std::int32_t>(group);
            const ScopedTrueFlag dispatch_guard(dispatching_vehicle_animation);
            (void)owner->Animation().SetAnimationRecursive(
                static_cast<std::uint32_t>(animId), render_group,
                static_cast<std::uint32_t>(animParam), -1, extraParam,
                1.0f, 1, 1, false);
        } else {
            bool cleared = false;
            if (group != kMaxSeatIndex && animId > 0) {
                auto* const m2_system = owner->m2_system();
                if (m2_system != nullptr) {
                    const auto slot = m2_system->QueryAnimationSlotState(
                        static_cast<std::uint32_t>(animId), group);
                    if (slot.status == openwow::render::m2::M2ResultStatus::kReady &&
                        slot.has_slot) {
                        cleared = m2_system->ClearAnimationSlot(
                                      static_cast<std::uint32_t>(animId), group) ==
                                  openwow::render::m2::M2ResultStatus::kReady;
                    }
                }
            }
            if (!cleared) {
                Vehicle_C_UpdatePassengers(session, vehicle, animId, group, -1, 1, 0);
                Vehicle_RefreshOwnerStandAnimation(session, *owner);
            }
        }
    }

    for (auto* const passenger : completed_passengers) {
        passenger->ProcessPendingSeatChange(session);
    }
}

int Vehicle_ProcessDirtySeatAnimation(WorldSession& session, CGUnit_C& ownerUnit,
                                      void* vehicleData,
                                      std::int32_t m2InstanceId,
                                      std::uint32_t seatIndex) {

    const std::uint32_t normalizedSeat =
        (seatIndex == kMaxSeatIndex)
            ? 0xFFFFFFFFu
            : seatIndex;

    const int result = ownerUnit.Animation().StopAnimAndPropagateToPassengers(
        true, false);

    if (result != 0) {
        return result;
    }

    Vehicle_C_UpdatePassengers(session, vehicleData, m2InstanceId, normalizedSeat,
                               -1, 1, 0);

    ownerUnit.Animation().RefreshSelectedStandAnimation(
        session, 0u, static_cast<std::uint32_t>(-1));
    return 0;
}

bool Vehicle_C_HasSeatAnimOverride(const void* vehicleData,
                                   const std::uint32_t animGroupId) {
    if (vehicleData == nullptr) {
        return false;
    }

    constexpr std::uint32_t kReady1HGroup = 26u;
    if (animGroupId == kReady1HGroup) {
        return false;
    }

    constexpr std::uint32_t kMaxValidAnimGroup = 35u;
    const auto normalized_group =
        animGroupId < kMaxValidAnimGroup ? animGroupId : kReady1HGroup;

    const std::size_t word_byte_offset =
        kVehicleTransferStateOffset + (normalized_group >> 5) * sizeof(std::uint32_t);

    std::uint32_t bitfield_word = 0;
    std::memcpy(&bitfield_word,
                static_cast<const std::byte*>(vehicleData) + word_byte_offset,
                sizeof(bitfield_word));

    return (bitfield_word & (1u << (normalized_group & 0x1Fu))) != 0;
}

void* Vehicle_C_DetachAllPassengers(WorldSession& session, void* vehicle) {

    std::vector<CGUnit_C*> passengers;
    ForEachVehiclePassengerUnit(vehicle, [&](CGUnit_C& unit) {
        passengers.push_back(&unit);
    });

    const auto now = openwow::core::GameClock::GetTickCount32();
    for (auto* const unit : passengers) {
        if (session.world_camera() != nullptr &&
            session.world_camera()->bound_object() ==
                unit->GetGuid().GetRawValue()) {

            session.world_camera()->SetBoundObject(0u);
        }

        auto* const passenger = unit->Vehicle().GetVehiclePassengerComponent();
        if (passenger == nullptr) {
            continue;
        }

        passenger->HandleTransition(
            session, 0.0, VehiclePassengerTransitionType::kExit, 0u, 0xFFu,
            now);

        if (unit->IsActivePlayer()) {
            unit->Movement().Data().QueueHeartbeat(now);
        } else {
            (void)unit->Movement().Data().TryInitRemoteMovement();
        }

        unit->State().SetForcedVehicleTransition(true);
        (void)unit->Movement().ForceSetTransport(
            session, 0u, 0xffu, true);
        unit->State().SetForcedVehicleTransition(false);
        passenger->DetachFromSeat();
    }
    Vehicle_C_ComputeAvailableSeatMask(vehicle);
    return vehicle;
}

int VehiclePassenger_HandleTransition(WorldSession& session, void* vehicle,
                                     unsigned int seatIndex, int param1,
                                     int param2) {
    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    if (owner == nullptr) {
        return 0;
    }
    const auto instance_id = owner->GetPrimaryM2InstanceId();
    if (instance_id == 0u) {
        return 0;
    }

    const auto group = ClampSeatIndex(seatIndex);
    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::byte*>(vehicle) + kVehicleSeatSlotsOffset);
    for (int index = 0; index < kSeatSlotCount; ++index) {
        const int base = index * kSeatSlotStride;
        if (static_cast<int>(slots[base]) == param1 &&
            static_cast<int>(slots[base + 1]) == param2) {
            slots[base] = 0u;
            slots[base + 1] = 0u;
        }
    }

    for (int index = 0; index < kSeatSlotCount; ++index) {
        const int base = index * kSeatSlotStride;
        if ((slots[base] != 0u || slots[base + 1] != 0u) &&
            slots[base + 2] == group) {
            return 0;
        }
    }

    const auto word_offset =
        kVehicleTransferStateOffset + (group >> 5u) * sizeof(std::uint32_t);
    if ((LoadOpaqueField<std::uint32_t>(vehicle, word_offset) &
         (1u << (group & 0x1Fu))) == 0u) {
        return 0;
    }

    bool cleared = false;
    if (group != kMaxSeatIndex) {
        auto* const m2_system = owner->m2_system();
        if (m2_system != nullptr) {
            const auto slot = m2_system->QueryAnimationSlotState(instance_id, group);
            if (slot.status == openwow::render::m2::M2ResultStatus::kReady &&
                slot.has_slot) {
                cleared = m2_system->ClearAnimationSlot(instance_id, group) ==
                          openwow::render::m2::M2ResultStatus::kReady;
            }
        }
    }
    if (!cleared) {
        Vehicle_C_UpdatePassengers(session, vehicle, static_cast<int>(instance_id),
                                   group, -1, 1, 0);
        Vehicle_RefreshOwnerStandAnimation(session, *owner);
    }
    return 0;
}

int Vehicle_C_UnlinkAndReinsertNode(void* vehicle, void* node) {
    if ((ResolveVehicleRelinkFlag(node) & 1u) == 0) {
        return 0;
    }

    auto* const list_root = ResolveVehicleRelinkListRoot(vehicle);
    const auto raw_link_pointer =
        reinterpret_cast<std::uintptr_t>(node) + list_root->link_offset;
    auto* const link_words =
        openwow::core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
            raw_link_pointer);
    auto* const previous_tail_words =
        reinterpret_cast<std::uintptr_t*>(list_root->tail_link);

    link_words->previous_link = list_root->tail_link;
    link_words->next_node = previous_tail_words[1];
    previous_tail_words[1] = reinterpret_cast<std::uintptr_t>(node);
    list_root->tail_link = raw_link_pointer;
    return 1;
}

void Vehicle_C_UpdateTransformHierarchy(void* vehicle, float* parentTransform) {
    if (vehicle == nullptr) {
        return;
    }

    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    if (owner == nullptr) {
        return;
    }

    auto* const matrix = ResolveVehicleTransformMatrix(vehicle);
    const auto frame = owner->Vehicle().GetLocalTransformFrame(*owner);
    auto transform = openwow::render::BuildRotationMatrix4x4Z(frame.facing);
    transform[12] = frame.x;
    transform[13] = frame.y;
    transform[14] = frame.z;

    if (parentTransform != nullptr) {
        transform = openwow::render::MultiplyMatrix4x4(
            transform,
            openwow::render::RenderMatrix4x4View{parentTransform, 16u});
    }
    std::copy(transform.begin(), transform.end(), matrix);

    ForEachVehiclePassengerUnit(vehicle, [&](const CGUnit_C& unit) {
        auto* const child_vehicle = unit.Vehicle().GetVehicleData();
        if (child_vehicle == nullptr || unit.Vehicle().GetVehicleEntry() == nullptr) {
            return;
        }

        Vehicle_C_UpdateTransformHierarchy(child_vehicle, matrix);
    });
}

bool Vehicle_C_HasDbcEntry(const void* vehicle) {
    if (vehicle == nullptr) return false;
    return LoadVehicleEntryField(vehicle) != nullptr;
}

void Vehicle_C_UpdateBoundingRadius(void* vehicle) {

    if (vehicle == nullptr || LoadVehicleEntryField(vehicle) == nullptr) {
        return;
    }

    float radius = 0.0f;
    ForEachVehiclePassengerUnit(vehicle, [&](const CGUnit_C& passenger) {
        float candidate = passenger.Presentation().ModelBoundingRadius();
        const auto* const child_vehicle = passenger.Vehicle().GetVehicleData();
        if (child_vehicle != nullptr && LoadVehicleEntryField(child_vehicle) != nullptr) {
            candidate += LoadOpaqueField<float>(child_vehicle,
                                                kVehicleBoundingRadiusOffset) * 2.0f;
        }
        radius = std::max(radius, candidate);
    });

    const float previous = LoadOpaqueField<float>(vehicle, kVehicleBoundingRadiusOffset);
    StoreOpaqueField(vehicle, kVehicleBoundingRadiusOffset, radius);
    if (previous == radius) {
        return;
    }

    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    auto* const parent = owner != nullptr ? owner->Vehicle().GetVehicleUnit() : nullptr;
    if (parent != nullptr && parent->Vehicle().GetVehicleData() != nullptr &&
        parent->Vehicle().GetVehicleData() != vehicle) {
        Vehicle_C_UpdateBoundingRadius(parent->Vehicle().GetVehicleData());
    }
}

void Vehicle_C_SyncPassengerAnimations(void* vehicleData) {

    ForEachVehiclePassengerUnit(vehicleData, [](CGUnit_C& unit) {
        auto* const passenger = unit.Vehicle().GetVehiclePassengerComponent();
        if (passenger != nullptr && passenger->IsAttachedToVehicle()) {
            passenger->AttachToSeat();
        }
    });
}

void Vehicle_C_ComputeAvailableSeatMask(void* vehicle) {

    if (vehicle == nullptr) {
        return;
    }

    std::uint8_t available_mask = 0;
    StoreOpaqueField(vehicle, kVehicleAvailableSeatMaskOffset, available_mask);

    const auto* const vehicle_entry = LoadVehicleEntryField(vehicle);
    if (vehicle_entry == nullptr) {
        return;
    }

    std::uint8_t occupied_mask = 0;
    ForEachVehiclePassengerUnit(vehicle, [&](const CGUnit_C& unit) {
        std::uint8_t seat_bit = 0;
        if (TryGetPassengerSeatBit(unit, seat_bit)) {
            occupied_mask |= seat_bit;
        }
    });

    for (std::size_t seat_index = 0; seat_index < kDbcSeatCount; ++seat_index) {
        const auto seat_bit = static_cast<std::uint8_t>(1u << seat_index);
        if (vehicle_entry->seat_id[seat_index] != 0 &&
            (occupied_mask & seat_bit) == 0) {
            available_mask |= seat_bit;
        }
    }

    StoreOpaqueField(vehicle, kVehicleAvailableSeatMaskOffset, available_mask);
}

void* Vehicle_C_UpdateTransformFromParent(const ObjectManager& objects,
                                          void* vehicle) {
    if (vehicle == nullptr) {
        return nullptr;
    }

    auto* const owner = UnitVehicleComponent::ResolveVehicleDataOwner(vehicle);
    if (owner == nullptr) {
        return nullptr;
    }

    const auto frame = owner->Vehicle().GetLocalTransformFrame(*owner);
    if (frame.parent_guid.IsEmpty()) {
        Vehicle_C_UpdateTransformHierarchy(vehicle, nullptr);
        return vehicle;
    }

    float parent_world[16];
    if (Movement_GetObjectTransform(objects, frame.parent_guid.GetRawValue(),
                                    parent_world) == 0) {
        return nullptr;
    }

    Vehicle_C_UpdateTransformHierarchy(vehicle, parent_world);
    return vehicle;
}

bool Vehicle_C_TryCopyTransformMatrix(const void* vehicle, float* out_matrix) {
    if (vehicle == nullptr || out_matrix == nullptr) {
        return false;
    }

    const auto* const vehicle_entry = LoadVehicleEntryField(vehicle);
    if (vehicle_entry == nullptr) {
        return false;
    }

    std::memcpy(out_matrix,
                static_cast<const std::byte*>(vehicle) +
                    vehicle_runtime_layout::kVehicleTransformMatrixOffset,
                kVehicleTransformMatrixSize);
    return true;
}

int Vehicle_C_AccumulateObjectFacing(float* accumulator, const CGObject_C* object) {

    if (object == nullptr) {
        return 0;
    }

    *accumulator += object->GetFacing();
    return 1;
}

void Vehicle_C_DecrementTypeHandle(void* typeInfo) {

    (void)typeInfo;
}

bool Vehicle_C_ShouldRedirectMouseToAim(const void* vehicleData,
                                        const std::uint32_t ownerMovementFlags) {
    if (vehicleData == nullptr) {
        return false;
    }

    constexpr std::uint32_t kSwimmingOrFlying = 0x2200000u;
    if ((ownerMovementFlags & kSwimmingOrFlying) != 0) {
        return false;
    }

    const auto* const vehicle_entry = LoadVehicleEntryField(vehicleData);
    if (vehicle_entry == nullptr) {
        return false;
    }

    constexpr std::uint32_t kAimFlagMask = 0x40040000u;
    constexpr std::uint32_t kAimFlagExpected = 0x00040000u;
    return (vehicle_entry->flags & kAimFlagMask) == kAimFlagExpected;
}

}

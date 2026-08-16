
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/monster_move.h"
#include "openwow/game/vec3.h"
#include "openwow/render/m2/m2_public_types.h"

namespace openwow::net {
struct CDataStore;
}

namespace openwow::game {

class CGUnit_C;
class WorldSession;
class ObjectManager;

}

namespace openwow::data::dbc {
struct VehicleSeatEntry;
}

namespace openwow::game {

[[nodiscard]] std::int32_t
ResolveVehicleSeatM2AttachmentLookup(std::int32_t seat_attachment_id) noexcept;

struct VehicleSeatEjectionTarget {
  ObjectGuid passenger_guid{};
  bool can_eject = false;
};

[[nodiscard]] std::optional<VehicleSeatEjectionTarget>
ResolveVehicleSeatEjectionTarget(const WorldSession& session, int one_based_seat_index);

[[nodiscard]] bool CanEjectPassengerFromSeat(
    const WorldSession& session, int one_based_seat_index);

bool EjectPassengerFromSeat(const WorldSession& session, int one_based_seat_index);

enum class VehiclePassengerTransitionType : std::uint32_t {
  kExit = 0,
  kEnterWithPos = 1,
  kTransferWithPos = 2,
  kAttached = 3,
  kEnterCaptured = 4,
  kEject = 5,
  kSeatChange = kAttached,
};

namespace VehiclePassengerFlag {
inline constexpr std::uint32_t kActive = 0x001;
inline constexpr std::uint32_t kPositionDirty = 0x002;
inline constexpr std::uint32_t kAnimSynced = 0x004;
inline constexpr std::uint32_t kHasInputControl = 0x008;
inline constexpr std::uint32_t kModelHidden = 0x010;
inline constexpr std::uint32_t kBlendAnims = 0x020;
inline constexpr std::uint32_t kHasBoneOffset = 0x040;
inline constexpr std::uint32_t kTransitionInProgress = 0x080;
inline constexpr std::uint32_t kEnterDirection = 0x100;
inline constexpr std::uint32_t kPendingSeatChange = 0x200;
inline constexpr std::uint32_t kSameVehicleTransfer = 0x400;
inline constexpr std::uint32_t kSeatAttached = 0x800;
inline constexpr std::uint32_t kMouseYawOverride = 0x1000;
}

struct VehiclePassengerRescueEntry {
  std::uint64_t guid = 0;
  std::uint64_t target = 0;
  std::uint8_t seat = 0;
  std::uint8_t flags = 0;
  std::uint8_t padding[6] = {};
};
static_assert(sizeof(VehiclePassengerRescueEntry) == 24);

struct VehiclePassengerQueueEntry {
  std::uint32_t guid_lo = 0;
  std::uint32_t guid_hi = 0;
  std::uint32_t target_lo = 0;
  std::uint32_t target_hi = 0;
  std::uint8_t seat = 0;
  std::uint8_t use_transition = 0;
  std::uint8_t padding[6] = {};
};
static_assert(sizeof(VehiclePassengerQueueEntry) == 24);

class VehiclePassengerC {
public:
  VehiclePassengerC();
  ~VehiclePassengerC();

  VehiclePassengerC(const VehiclePassengerC&) = delete;
  VehiclePassengerC& operator=(const VehiclePassengerC&) = delete;
  VehiclePassengerC(VehiclePassengerC&&) = delete;
  VehiclePassengerC& operator=(VehiclePassengerC&&) = delete;

  [[nodiscard]] bool IsEntering() const;

  [[nodiscard]] bool IsExiting() const;

  [[nodiscard]] VehiclePassengerTransitionType GetTransitionState() const {
    return transition_state_;
  }

  [[nodiscard]] bool IsAttachedToVehicle() const {
    return transition_state_ == VehiclePassengerTransitionType::kAttached;
  }

  [[nodiscard]] bool IsInVehicle() const {
    return transition_state_ != VehiclePassengerTransitionType::kExit;
  }

  void BindOwner(CGUnit_C *owner) {
    owner_ = owner;
  }
  [[nodiscard]] CGUnit_C *GetOwner() const {
    return owner_;
  }

  void SetPrimaryVehicleGuid(std::uint64_t guid) {
    primary_vehicle_guid_ = guid;
  }
  void SetAltVehicleGuid(std::uint64_t guid) {
    alt_vehicle_guid_ = guid;
  }
  void SetPrimarySeatIndex(std::uint8_t idx) {
    primary_seat_index_ = idx;
  }
  void SetAltSeatIndex(std::uint8_t idx) {
    alt_seat_index_ = idx;
  }

  [[nodiscard]] std::uint64_t GetPrimaryVehicleGuid() const {
    return primary_vehicle_guid_;
  }
  [[nodiscard]] std::uint64_t GetAltVehicleGuid() const {
    return alt_vehicle_guid_;
  }
  [[nodiscard]] std::uint8_t GetPrimarySeatIndex() const {
    return primary_seat_index_;
  }
  [[nodiscard]] std::uint8_t GetAltSeatIndex() const {
    return alt_seat_index_;
  }

  [[nodiscard]] std::uint64_t GetVehicleUnitGuid() const;

  [[nodiscard]] CGUnit_C *GetVehicleUnit() const;

  [[nodiscard]] CGUnit_C *GetVehicleObject() const;

  [[nodiscard]] const openwow::data::dbc::VehicleSeatEntry *GetSeatEntry() const;

  Vec3 GetPassengerPosition(const Vec3 &offset, const Vec3 &base_position) const;

  static Vec3 GetScaledSeatOffset(const float *attachment_data);

  Vec3 GetSeatPosition() const;

  void ClearTransitionData();

  void CreateTransitionData(openwow::net::CDataStore& source);

  void SetPendingMonsterMove(MonsterMoveInfo move) {
    pending_monster_move_ = std::move(move);
  }

  void HandleTransition(WorldSession& session, double timestamp,
                        VehiclePassengerTransitionType type,
                        std::uint64_t vehicle_guid, std::uint8_t seat_index, std::uint32_t timing);

  void UpdateSeatState(WorldSession& session, double timestamp,
                       std::uint64_t target_guid, std::uint8_t target_seat,
                       bool allow_transition_profile);

  void HandleVehicleAssignmentChange(WorldSession& session,
                                     double timestamp,
                                     std::uint64_t target_guid,
                                     std::uint8_t target_seat, CGUnit_C* target_unit,
                                     bool from_update);

  void ResetPositionVectors();

  void AttachToSeat();
  [[nodiscard]] openwow::render::m2::M2ResultStatus GetLastAttachmentAnimationStatus() const {
    return last_attachment_animation_status_;
  }

  void DetachFromSeat();

  void OnVehicleGuidUpdate(WorldSession& session, double timestamp,
                           std::uint64_t new_guid);

  void ProcessPendingSeatChange(WorldSession& session);

  void BeginPendingSeatChange(std::uint64_t root_vehicle_guid,
                              std::uint64_t target_guid,
                              std::uint8_t target_seat,
                              const openwow::data::dbc::VehicleSeatEntry& seat_entry,
                              bool entering,
                              std::uint32_t current_tick_ms);

  [[nodiscard]] bool AcceptsPendingEnterAnimation(std::uint32_t fourcc) const;
  [[nodiscard]] bool AcceptsPendingExitAnimation(std::uint32_t fourcc) const;

  void HandleActivePlayerSeatChange(const WorldSession& session);

  [[nodiscard]] std::uint32_t GetFlags() const {
    return flags_;
  }
  void SetFlag(std::uint32_t flag) {
    flags_ |= flag;
  }
  void ClearFlag(std::uint32_t flag) {
    flags_ &= ~flag;
  }
  [[nodiscard]] bool HasFlag(std::uint32_t flag) const {
    return (flags_ & flag) != 0;
  }
  void SetMouseYawOverride(float yaw) {
    flags_ |= VehiclePassengerFlag::kMouseYawOverride;
    mouse_yaw_override_ = yaw;
  }
  void ClearMouseYawOverride() {
    flags_ &= ~VehiclePassengerFlag::kMouseYawOverride;
  }
  [[nodiscard]] bool HasMouseYawOverride() const {
    return HasFlag(VehiclePassengerFlag::kMouseYawOverride);
  }
  [[nodiscard]] float GetMouseYawOverride() const {
    return mouse_yaw_override_;
  }

  [[nodiscard]] double GetVehiclePitch() const;

  [[nodiscard]] std::uint64_t GetPreviousVehicleGuid() const {
    return prev_vehicle_guid_;
  }
  [[nodiscard]] std::uint8_t GetPreviousSeatIndex() const {
    return prev_seat_index_;
  }

  [[nodiscard]] std::uint32_t GetTransitionStartedAtMs() const {
    return timing_param_;
  }
  [[nodiscard]] std::uint32_t GetTransitionDeadlineMs() const {
    return transition_deadline_ms_;
  }
  [[nodiscard]] std::uint32_t GetPendingSeatChangeDeadlineMs() const {
    return pending_seat_change_deadline_ms_;
  }
  void SetPendingSeatChangeDeadlineMs(const std::uint32_t deadline_ms) {
    pending_seat_change_deadline_ms_ = deadline_ms;
  }
  [[nodiscard]] std::uint32_t GetInputControlGraceStartedAtMs() const {
    return input_control_grace_started_at_ms_;
  }
  void SetInputControlGraceStartedAtMs(const std::uint32_t started_at_ms) {
    input_control_grace_started_at_ms_ = started_at_ms;
  }
  [[nodiscard]] float GetTransitionBlendFactor() const {
    return transition_blend_factor_;
  }
  void SetTransitionBlendFactor(const float factor) {
    transition_blend_factor_ = factor;
  }

  [[nodiscard]] std::uint64_t GetRescueVehicleGuid() const {
    return rescue_vehicle_guid_;
  }
  void SetRescueVehicleGuid(std::uint64_t guid) {
    rescue_vehicle_guid_ = guid;
  }

  static void Initialize();

  static void Shutdown();

  [[nodiscard]] static std::uint32_t GetRegisteredTypeId() {
    return s_registered_type_id_;
  }

  static void ProcessQueuedTransitions(WorldSession& session,
                                       double timestamp);

  static void QueueTransition(const VehiclePassengerQueueEntry &entry);

  static void UpdateAll(WorldSession& session, std::uint32_t current_tick_ms);

  void RegisterActive();
  void UnregisterActive();

  bool PerFrameUpdate(WorldSession& session, CGUnit_C *vehicle_unit,
                      const openwow::data::dbc::VehicleSeatEntry *seat_entry,
                      int current_time);

  bool CheckTransitionTimers(WorldSession& session, double timestamp,
                             CGUnit_C *vehicle_unit,
                             const openwow::data::dbc::VehicleSeatEntry *seat_entry,
                             int current_time);

  void Update(WorldSession& session, double timestamp, int current_time);

  void CascadeVehicleGuid(std::uint64_t guid);

  void RenderAttachment();

  void UpdatePosition(CGUnit_C *vehicle_unit,
                      const openwow::data::dbc::VehicleSeatEntry *seat_entry,
                      int current_time,
                      Vec3 &effective_position);

  Vec3 position_offset{};
  Vec3 saved_position{};
  Vec3 saved_facing_offset{};

private:

  CGUnit_C *owner_ = nullptr;

  std::uint32_t flags_ = 0;
  VehiclePassengerTransitionType transition_state_ = VehiclePassengerTransitionType::kExit;
  VehiclePassengerTransitionType previous_transition_state_ =
      VehiclePassengerTransitionType::kExit;

  std::uint64_t primary_vehicle_guid_ = 0;
  std::uint64_t alt_vehicle_guid_ = 0;

  std::uint8_t primary_seat_index_ = 0xFF;
  std::uint8_t alt_seat_index_ = 0xFF;

  std::uint64_t prev_vehicle_guid_ = 0;
  std::uint8_t prev_seat_index_ = 0xFF;

  std::uint32_t timing_param_ = 0;
  std::uint32_t transition_deadline_ms_ = 0;
  std::uint32_t input_control_grace_started_at_ms_ = 0;
  float transition_blend_factor_ = 0.0f;
  float transition_facing_from_ = 0.0f;
  float transition_facing_blended_ = 0.0f;
  float transition_facing_current_ = 0.0f;

  std::uint32_t pending_seat_change_deadline_ms_ = 0;
  std::uint64_t pending_root_vehicle_guid_ = 0;
  std::uint64_t pending_target_guid_ = 0;
  std::uint8_t pending_target_seat_index_ = 0xFF;

  std::uint64_t rescue_vehicle_guid_ = 0;
  openwow::net::CDataStore *transition_data_ = nullptr;
  std::optional<MonsterMoveInfo> pending_monster_move_;
  float mouse_yaw_override_ = 0.0f;
  const openwow::data::dbc::VehicleSeatEntry *seat_entry_ = nullptr;
  openwow::render::m2::M2ResultStatus last_attachment_animation_status_ =
      openwow::render::m2::M2ResultStatus::kNotReady;

  Vec3 seat_attachment_offset_{};
  Vec3 exit_saved_position_{};
  Vec3 exit_facing_offset_{};

  float facing_for_render_ = 0.0f;
  float prev_facing_ = 0.0f;
  float acceleration_gravity_ = 0.0f;
  float arc_height_ratio_ = 0.0f;
  Vec3 cached_bone_offset_{};

  void UpdateTransitionBlendFactor(
      int current_time, const openwow::data::dbc::VehicleSeatEntry *seat_entry);
  void RefreshTransitionDeadline(
      CGUnit_C *vehicle_unit, const openwow::data::dbc::VehicleSeatEntry *seat_entry);
  void UpdateInterpolatedFacing();

  void UpdateBoneAttachmentOffset(
      const openwow::data::dbc::VehicleSeatEntry *seat_entry);

  void ComputeSeatWorldPosition(
      CGUnit_C *vehicle_unit,
      const openwow::data::dbc::VehicleSeatEntry *seat_entry,
      Vec3 &out_position) const;

  static bool s_initialized_;
  static std::vector<VehiclePassengerQueueEntry> s_queued_transitions_;
  static std::uint32_t s_registered_type_id_;

  static std::vector<VehiclePassengerC *> s_active_passengers_;
};

class VehiclePassengerRescueTransition {
public:

  void Resize(std::uint32_t new_capacity);

  [[nodiscard]] std::uint32_t GetCapacity() const {
    return capacity_;
  }
  [[nodiscard]] std::uint32_t GetCount() const {
    return count_;
  }

  VehiclePassengerRescueEntry *GetEntry(std::uint32_t index);
  const VehiclePassengerRescueEntry *GetEntry(std::uint32_t index) const;

  void Clear() {
    count_ = 0;
  }

private:
  std::uint32_t capacity_ = 0;
  std::uint32_t count_ = 0;
  std::vector<VehiclePassengerRescueEntry> entries_;
};

}

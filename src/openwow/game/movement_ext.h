
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

struct MovementInfoWire {
  std::uint32_t movement_flags = 0;
  std::uint16_t extra_flags = 0;
  std::uint32_t timestamp = 0;
  float pos_x = 0, pos_y = 0, pos_z = 0, orientation = 0;

  bool has_transport = false;
  ObjectGuid transport_guid{ObjectGuid(0)};
  float trans_x = 0, trans_y = 0, trans_z = 0, trans_o = 0;
  std::uint32_t trans_time = 0;
  std::uint8_t trans_seat = 0;
  bool has_trans_time2 = false;
  std::uint32_t trans_time2 = 0;

  bool has_pitch = false;
  float pitch = 0;

  std::uint32_t fall_time = 0;
  bool has_jump = false;
  float jump_z_speed = 0, jump_sin_angle = 0, jump_cos_angle = 0, jump_xy_speed = 0;

  bool has_spline_elevation = false;
  float spline_elevation = 0;
};

struct MoveToggleInfo {
  ObjectGuid mover{ObjectGuid(0)};
  std::uint32_t counter = 0;
};

struct SplineSpeedInfo {
  ObjectGuid mover{ObjectGuid(0)};
  float speed = 0;
};

struct SplineGuidOnly {
  ObjectGuid mover{ObjectGuid(0)};
};

struct ForceSpeedChange {
  ObjectGuid mover{ObjectGuid(0)};
  std::uint32_t counter = 0;
  float speed = 0;
};

struct FlightSplineSyncInfo {
  float distance = 0;
  ObjectGuid mover{ObjectGuid(0)};
};

struct TimeSkippedInfo {
  ObjectGuid mover{ObjectGuid(0)};
  std::uint32_t time_skipped = 0;
};

struct SpeedBroadcastInfo {
  ObjectGuid mover{ObjectGuid(0)};
  MovementInfoWire move_info{};
  float speed = 0;
};

struct TeleportBroadcastInfo {
  ObjectGuid mover{ObjectGuid(0)};
  MovementInfoWire move_info{};
};

struct MoveSpeedUpdate {
  std::uint64_t mover_guid = 0;
  MovementInfoWire move_info{};
  float speed = 0;
};

struct KnockbackBroadcastInfo {
  ObjectGuid mover{ObjectGuid(0)};
  MovementInfoWire move_info{};
  float cos_angle = 0.0f;
  float sin_angle = 0.0f;
  float speed_xy = 0.0f;
  float speed_z = 0.0f;
};

struct TeleportAckData {
  std::uint64_t mover_guid = 0;
  std::uint32_t counter = 0;
  MovementInfoWire move_info{};
};

struct GravityToggle {
  std::uint64_t mover_guid = 0;
  std::uint32_t counter = 0;
};

struct CollisionHeight {
  std::uint64_t mover_guid = 0;
  std::uint32_t counter = 0;
  float height = 0;
};

struct MoveCollisionHgtAck {
  std::uint64_t mover_guid = 0;
  MovementInfoWire move_info{};
  float height = 0;
};

class MovementExtHandler {
 public:

  bool HandleTeleport(const std::uint8_t* data, std::size_t len);

  bool HandleWaterWalk(const std::uint8_t* data, std::size_t len);
  bool HandleLandWalk(const std::uint8_t* data, std::size_t len);

  bool HandleFeatherFall(const std::uint8_t* data, std::size_t len);
  bool HandleNormalFall(const std::uint8_t* data, std::size_t len);

  bool HandleLogoutCancelAck(const std::uint8_t* data, std::size_t len);

  bool HandleSetHover(const std::uint8_t* data, std::size_t len);
  bool HandleUnsetHover(const std::uint8_t* data, std::size_t len);

  bool HandleSetCanSwimFlyTransition(const std::uint8_t* data, std::size_t len);
  bool HandleUnsetCanSwimFlyTransition(const std::uint8_t* data, std::size_t len);

  bool HandleSplineSetRunSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveRoot(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveUnroot(const std::uint8_t* data, std::size_t len);

  bool HandleSetRunSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSetRunBackSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSetWalkSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSetSwimSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSetFlightSpeed(const std::uint8_t* data, std::size_t len);

  bool HandleForceSwimBackSpeedChange(const std::uint8_t* data, std::size_t len);
  bool HandleForceFlightBackSpeedChange(const std::uint8_t* data, std::size_t len);
  bool HandleForcePitchRateChange(const std::uint8_t* data, std::size_t len);

  bool HandleSplineSetWalkSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetSwimSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetFlightSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetRunBackSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetSwimBackSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetTurnRate(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetFlightBackSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleSplineSetPitchRate(const std::uint8_t* data, std::size_t len);

  bool HandleFlightSplineSync(const std::uint8_t* data, std::size_t len);

  bool HandleMoveTimeSkipped(const std::uint8_t* data, std::size_t len);

  bool HandleMoveSetPitch(const std::uint8_t* data, std::size_t len);
  bool HandleMoveStartPitchUp(const std::uint8_t* data, std::size_t len);
  bool HandleMoveStartPitchDown(const std::uint8_t* data, std::size_t len);
  bool HandleMoveStopPitch(const std::uint8_t* data, std::size_t len);

  bool HandleMsgMoveRoot(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveUnroot(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveKnockBack(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveTeleportAck(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveWorldportAck(const std::uint8_t* data, std::size_t len);

  bool HandleMsgMoveFeatherFall(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveHover(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveWaterWalk(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveUpdateCanFly(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveSetRunMode(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveSetWalkMode(const std::uint8_t* data, std::size_t len);

  bool HandleMsgMoveSetSwimBackSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveSetTurnRate(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveSetFlightBackSpeed(const std::uint8_t* data, std::size_t len);
  bool HandleMsgMoveSetPitchRate(const std::uint8_t* data, std::size_t len);

  bool HandleSplineMoveFeatherFall(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveNormalFall(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveSetHover(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveUnsetHover(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveWaterWalk(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveLandWalk(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveStartSwim(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveStopSwim(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveSetRunMode(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveSetWalkMode(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveSetFlying(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveUnsetFlying(const std::uint8_t* data, std::size_t len);

  bool HandleMoveGravityDisable(const std::uint8_t* data, std::size_t len);
  bool HandleMoveGravityEnable(const std::uint8_t* data, std::size_t len);
  bool HandleMoveSetCollisionHgt(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveGravityDisable(const std::uint8_t* data, std::size_t len);
  bool HandleSplineMoveGravityEnable(const std::uint8_t* data, std::size_t len);
  bool HandleMoveGravityChng(const std::uint8_t* data, std::size_t len);
  bool HandleMoveSetCollisionHgtAck(const std::uint8_t* data, std::size_t len);
  bool HandleMoveUpdateCanTransitionSwimFly(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] const TeleportBroadcastInfo& last_teleport() const { return teleport_; }
  [[nodiscard]] bool water_walking() const { return water_walk_; }
  [[nodiscard]] bool feather_falling() const { return feather_fall_; }
  [[nodiscard]] bool logout_cancelled() const { return logout_cancel_; }
  [[nodiscard]] const SpeedBroadcastInfo& last_speed() const { return speed_; }
  [[nodiscard]] const MoveToggleInfo& last_toggle() const { return last_toggle_; }
  [[nodiscard]] bool hovering() const { return hover_; }
  [[nodiscard]] bool can_swim_fly_transition() const { return swim_fly_transition_; }
  [[nodiscard]] const SplineSpeedInfo& last_spline_speed() const { return spline_speed_; }
  [[nodiscard]] const SplineGuidOnly& last_spline_root() const { return spline_root_; }
  [[nodiscard]] bool spline_rooted() const { return spline_rooted_; }
  [[nodiscard]] const ForceSpeedChange& last_force_speed() const { return force_speed_; }
  [[nodiscard]] const FlightSplineSyncInfo& last_flight_sync() const { return flight_sync_; }
  [[nodiscard]] const TimeSkippedInfo& last_time_skipped() const { return time_skipped_; }
  [[nodiscard]] const TeleportBroadcastInfo& last_move_broadcast() const { return move_broadcast_; }

  [[nodiscard]] std::uint64_t last_root_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] std::uint64_t last_unroot_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] std::uint64_t last_knockback_guid() const {
    return knockback_.mover.GetRawValue();
  }
  [[nodiscard]] const KnockbackBroadcastInfo& last_knockback() const {
    return knockback_;
  }
  [[nodiscard]] const TeleportAckData& last_teleport_ack() const { return teleport_ack_; }
  [[nodiscard]] bool worldport_ack_received() const { return worldport_ack_received_; }
  [[nodiscard]] std::uint64_t last_msg_feather_fall_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] std::uint64_t last_msg_hover_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] std::uint64_t last_msg_water_walk_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] std::uint64_t last_msg_can_fly_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] bool last_msg_can_fly_enabled() const {
    return (move_broadcast_.move_info.movement_flags & 0x01000000u) != 0u;
  }
  [[nodiscard]] const MovementInfoWire& last_msg_can_fly_move_info() const {
    return move_broadcast_.move_info;
  }
  [[nodiscard]] std::uint64_t last_msg_run_mode_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] std::uint64_t last_msg_walk_mode_guid() const {
    return move_broadcast_.mover.GetRawValue();
  }
  [[nodiscard]] const MoveSpeedUpdate& last_swim_back_speed() const { return swim_back_speed_; }
  [[nodiscard]] const MoveSpeedUpdate& last_turn_rate() const { return turn_rate_; }
  [[nodiscard]] const MoveSpeedUpdate& last_flight_back_speed() const { return flight_back_speed_; }
  [[nodiscard]] const MoveSpeedUpdate& last_pitch_rate() const { return pitch_rate_; }
  [[nodiscard]] std::uint64_t spline_feather_fall_guid() const { return spline_feather_fall_guid_; }
  [[nodiscard]] std::uint64_t spline_normal_fall_guid() const { return spline_normal_fall_guid_; }
  [[nodiscard]] std::uint64_t spline_set_hover_guid() const { return spline_set_hover_guid_; }
  [[nodiscard]] std::uint64_t spline_unset_hover_guid() const { return spline_unset_hover_guid_; }
  [[nodiscard]] std::uint64_t spline_water_walk_guid() const { return spline_water_walk_guid_; }
  [[nodiscard]] std::uint64_t spline_land_walk_guid() const { return spline_land_walk_guid_; }
  [[nodiscard]] std::uint64_t spline_start_swim_guid() const { return spline_start_swim_guid_; }
  [[nodiscard]] std::uint64_t spline_stop_swim_guid() const { return spline_stop_swim_guid_; }
  [[nodiscard]] std::uint64_t spline_run_mode_guid() const { return spline_run_mode_guid_; }
  [[nodiscard]] std::uint64_t spline_walk_mode_guid() const { return spline_walk_mode_guid_; }
  [[nodiscard]] std::uint64_t spline_set_flying_guid() const { return spline_set_flying_guid_; }
  [[nodiscard]] std::uint64_t spline_unset_flying_guid() const { return spline_unset_flying_guid_; }

  [[nodiscard]] const std::optional<GravityToggle>& last_gravity_disable() const { return gravity_disable_; }
  [[nodiscard]] const std::optional<GravityToggle>& last_gravity_enable() const { return gravity_enable_; }
  [[nodiscard]] const std::optional<CollisionHeight>& last_collision_height() const { return collision_height_; }
  [[nodiscard]] std::uint64_t spline_gravity_disable_guid() const { return spline_gravity_disable_guid_; }
  [[nodiscard]] std::uint64_t spline_gravity_enable_guid() const { return spline_gravity_enable_guid_; }
  [[nodiscard]] std::uint64_t last_gravity_chng_guid() const { return gravity_chng_guid_; }
  [[nodiscard]] const std::optional<MoveCollisionHgtAck>& last_collision_hgt_ack() const { return collision_hgt_ack_; }
  [[nodiscard]] std::uint64_t last_swim_fly_transition_guid() const { return swim_fly_transition_guid_; }
  [[nodiscard]] const MovementInfoWire& last_swim_fly_transition_move_info() const {
    return swim_fly_transition_move_info_;
  }

  void Clear();

 private:
  bool ParseMoveToggle(const std::uint8_t* data, std::size_t len, MoveToggleInfo& out);
  bool ParseMoveBroadcast(const std::uint8_t* data, std::size_t len);
  bool ParseMovementInfo(PacketReader& r, MovementInfoWire& out);
  bool ParseSpeedBroadcast(const std::uint8_t* data, std::size_t len, SpeedBroadcastInfo& out);
  bool ParseSplineSpeed(const std::uint8_t* data, std::size_t len, SplineSpeedInfo& out);
  bool ParseSplineGuidOnly(const std::uint8_t* data, std::size_t len, SplineGuidOnly& out);
  bool ParseForceSpeedChange(const std::uint8_t* data, std::size_t len, ForceSpeedChange& out);

  TeleportBroadcastInfo teleport_{};
  bool water_walk_ = false;
  bool feather_fall_ = false;
  bool hover_ = false;
  bool swim_fly_transition_ = false;
  bool logout_cancel_ = false;
  SpeedBroadcastInfo speed_{};
  MoveToggleInfo last_toggle_{};
  SplineSpeedInfo spline_speed_{};
  SplineGuidOnly spline_root_{};
  bool spline_rooted_ = false;
  ForceSpeedChange force_speed_{};
  FlightSplineSyncInfo flight_sync_{};
  TimeSkippedInfo time_skipped_{};
  TeleportBroadcastInfo move_broadcast_{};

  KnockbackBroadcastInfo knockback_{};
  TeleportAckData teleport_ack_{};
  bool worldport_ack_received_ = false;
  MoveSpeedUpdate swim_back_speed_{};
  MoveSpeedUpdate turn_rate_{};
  MoveSpeedUpdate flight_back_speed_{};
  MoveSpeedUpdate pitch_rate_{};
  std::uint64_t spline_feather_fall_guid_ = 0;
  std::uint64_t spline_normal_fall_guid_ = 0;
  std::uint64_t spline_set_hover_guid_ = 0;
  std::uint64_t spline_unset_hover_guid_ = 0;
  std::uint64_t spline_water_walk_guid_ = 0;
  std::uint64_t spline_land_walk_guid_ = 0;
  std::uint64_t spline_start_swim_guid_ = 0;
  std::uint64_t spline_stop_swim_guid_ = 0;
  std::uint64_t spline_run_mode_guid_ = 0;
  std::uint64_t spline_walk_mode_guid_ = 0;
  std::uint64_t spline_set_flying_guid_ = 0;
  std::uint64_t spline_unset_flying_guid_ = 0;

  std::optional<GravityToggle> gravity_disable_;
  std::optional<GravityToggle> gravity_enable_;
  std::optional<CollisionHeight> collision_height_;
  std::uint64_t spline_gravity_disable_guid_ = 0;
  std::uint64_t spline_gravity_enable_guid_ = 0;
  std::uint64_t gravity_chng_guid_ = 0;
  std::optional<MoveCollisionHgtAck> collision_hgt_ack_;
  std::uint64_t swim_fly_transition_guid_ = 0;
  MovementInfoWire swim_fly_transition_move_info_{};
};

}

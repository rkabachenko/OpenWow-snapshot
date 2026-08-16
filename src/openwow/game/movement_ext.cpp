
#include "openwow/game/movement_ext.h"
#include "openwow/game/object_types.h"

namespace openwow::game {

namespace {

enum class MovementInfoReadMode {
  kRequireComplete,
  kAllowRetailTruncation,
};

bool ParseMovementInfoFields(PacketReader& reader, MovementInfoWire& out,
                             const MovementInfoReadMode mode) {
  out = {};
  out.trans_seat = 0xFFu;

  bool overrun = false;
  const auto read_field = [&](auto&& operation) {
    if (!overrun && operation()) {
      return true;
    }
    overrun = true;
    return mode == MovementInfoReadMode::kAllowRetailTruncation;
  };

  if (!read_field([&] { return reader.ReadU32(out.movement_flags); })) return false;
  if (!read_field([&] { return reader.ReadU16(out.extra_flags); })) return false;
  if (!read_field([&] { return reader.ReadU32(out.timestamp); })) return false;
  if (!read_field([&] { return reader.ReadFloat(out.pos_x); })) return false;
  if (!read_field([&] { return reader.ReadFloat(out.pos_y); })) return false;
  if (!read_field([&] { return reader.ReadFloat(out.pos_z); })) return false;
  if (!read_field([&] { return reader.ReadFloat(out.orientation); })) return false;

  if ((out.movement_flags & kMoveFlagOnTransport) != 0u) {
    out.has_transport = true;
    if (!read_field([&] { return reader.ReadPackedGuid(out.transport_guid); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.trans_x); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.trans_y); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.trans_z); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.trans_o); })) return false;
    if (!read_field([&] { return reader.ReadU32(out.trans_time); })) return false;
    if (!read_field([&] { return reader.ReadU8(out.trans_seat); })) return false;
    if ((out.extra_flags & kMoveFlag2InterpolatedMovement) != 0u) {
      out.has_trans_time2 = true;
      if (!read_field([&] { return reader.ReadU32(out.trans_time2); })) return false;
    }
  }

  constexpr std::uint32_t kPitchMovementFlags =
      kMoveFlagSwimming | kMoveFlagFlying;
  if ((out.movement_flags & kPitchMovementFlags) != 0u ||
      (out.extra_flags & kMoveFlag2AlwaysAllowPitching) != 0u) {
    out.has_pitch = true;
    if (!read_field([&] { return reader.ReadFloat(out.pitch); })) return false;
  }

  if (!read_field([&] { return reader.ReadU32(out.fall_time); })) return false;

  if ((out.movement_flags & kMoveFlagFalling) != 0u) {
    out.has_jump = true;
    if (!read_field([&] { return reader.ReadFloat(out.jump_z_speed); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.jump_sin_angle); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.jump_cos_angle); })) return false;
    if (!read_field([&] { return reader.ReadFloat(out.jump_xy_speed); })) return false;
  }

  if ((out.movement_flags & kMoveFlagSplineElevation) != 0u) {
    out.has_spline_elevation = true;
    if (!read_field([&] { return reader.ReadFloat(out.spline_elevation); })) return false;
  }

  return true;
}

bool ParseRetailGravityToggle(const std::uint8_t* data, const std::size_t len,
                              GravityToggle& out) {
  PacketReader reader(data, len);
  GravityToggle parsed{};
  ObjectGuid mover{ObjectGuid(0)};
  if (!reader.ReadPackedGuid(mover)) {
    return false;
  }
  parsed.mover_guid = mover.GetRawValue();

  (void)reader.ReadU32(parsed.counter);
  out = parsed;
  return true;
}

}

bool MovementExtHandler::ParseMoveToggle(const std::uint8_t* data,
                                         std::size_t len,
                                         MoveToggleInfo& out) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(out.mover)) return false;
  if (!r.ReadU32(out.counter)) return false;
  return true;
}

bool MovementExtHandler::ParseMovementInfo(PacketReader& r,
                                           MovementInfoWire& out) {
  return ParseMovementInfoFields(r, out,
                                 MovementInfoReadMode::kRequireComplete);
}

bool MovementExtHandler::ParseSpeedBroadcast(const std::uint8_t* data,
                                             std::size_t len,
                                             SpeedBroadcastInfo& out) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(out.mover)) return false;
  if (!ParseMovementInfo(r, out.move_info)) return false;
  if (!r.ReadFloat(out.speed)) return false;
  return true;
}

bool MovementExtHandler::HandleTeleport(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(teleport_.mover)) return false;
  return ParseMovementInfo(r, teleport_.move_info);
}

bool MovementExtHandler::HandleWaterWalk(const std::uint8_t* data,
                                         std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  water_walk_ = true;
  return true;
}

bool MovementExtHandler::HandleLandWalk(const std::uint8_t* data,
                                        std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  water_walk_ = false;
  return true;
}

bool MovementExtHandler::HandleFeatherFall(const std::uint8_t* data,
                                           std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  feather_fall_ = true;
  return true;
}

bool MovementExtHandler::HandleNormalFall(const std::uint8_t* data,
                                          std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  feather_fall_ = false;
  return true;
}

bool MovementExtHandler::HandleLogoutCancelAck(const std::uint8_t* ,
                                               std::size_t ) {
  logout_cancel_ = true;
  return true;
}

bool MovementExtHandler::HandleSetRunSpeed(const std::uint8_t* data,
                                           std::size_t len) {
  return ParseSpeedBroadcast(data, len, speed_);
}

bool MovementExtHandler::HandleSetRunBackSpeed(const std::uint8_t* data,
                                               std::size_t len) {
  return ParseSpeedBroadcast(data, len, speed_);
}

bool MovementExtHandler::HandleSetWalkSpeed(const std::uint8_t* data,
                                            std::size_t len) {
  return ParseSpeedBroadcast(data, len, speed_);
}

bool MovementExtHandler::HandleSetSwimSpeed(const std::uint8_t* data,
                                            std::size_t len) {
  return ParseSpeedBroadcast(data, len, speed_);
}

bool MovementExtHandler::HandleSetFlightSpeed(const std::uint8_t* data,
                                              std::size_t len) {
  return ParseSpeedBroadcast(data, len, speed_);
}

bool MovementExtHandler::ParseSplineSpeed(const std::uint8_t* data,
                                          std::size_t len,
                                          SplineSpeedInfo& out) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(out.mover)) return false;
  if (!r.ReadFloat(out.speed)) return false;
  return true;
}

bool MovementExtHandler::ParseSplineGuidOnly(const std::uint8_t* data,
                                             std::size_t len,
                                             SplineGuidOnly& out) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(out.mover)) return false;
  return true;
}

bool MovementExtHandler::HandleSetHover(const std::uint8_t* data,
                                        std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  hover_ = true;
  return true;
}

bool MovementExtHandler::HandleUnsetHover(const std::uint8_t* data,
                                          std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  hover_ = false;
  return true;
}

bool MovementExtHandler::HandleSetCanSwimFlyTransition(
    const std::uint8_t* data, std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  swim_fly_transition_ = true;
  return true;
}

bool MovementExtHandler::HandleUnsetCanSwimFlyTransition(
    const std::uint8_t* data, std::size_t len) {
  if (!ParseMoveToggle(data, len, last_toggle_)) return false;
  swim_fly_transition_ = false;
  return true;
}

bool MovementExtHandler::HandleSplineSetRunSpeed(const std::uint8_t* data,
                                                 std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineMoveRoot(const std::uint8_t* data,
                                              std::size_t len) {
  if (!ParseSplineGuidOnly(data, len, spline_root_)) return false;
  spline_rooted_ = true;
  return true;
}

bool MovementExtHandler::HandleSplineMoveUnroot(const std::uint8_t* data,
                                                std::size_t len) {
  if (!ParseSplineGuidOnly(data, len, spline_root_)) return false;
  spline_rooted_ = false;
  return true;
}

bool MovementExtHandler::ParseForceSpeedChange(const std::uint8_t* data,
                                                std::size_t len,
                                                ForceSpeedChange& out) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(out.mover)) return false;
  if (!r.ReadU32(out.counter)) return false;
  if (!r.ReadFloat(out.speed)) return false;
  return true;
}

bool MovementExtHandler::HandleForceSwimBackSpeedChange(
    const std::uint8_t* data, std::size_t len) {
  return ParseForceSpeedChange(data, len, force_speed_);
}

bool MovementExtHandler::HandleForceFlightBackSpeedChange(
    const std::uint8_t* data, std::size_t len) {
  return ParseForceSpeedChange(data, len, force_speed_);
}

bool MovementExtHandler::HandleForcePitchRateChange(const std::uint8_t* data,
                                                     std::size_t len) {
  return ParseForceSpeedChange(data, len, force_speed_);
}

bool MovementExtHandler::HandleSplineSetWalkSpeed(const std::uint8_t* data,
                                                  std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetSwimSpeed(const std::uint8_t* data,
                                                  std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetFlightSpeed(const std::uint8_t* data,
                                                    std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetRunBackSpeed(const std::uint8_t* data,
                                                     std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetSwimBackSpeed(const std::uint8_t* data,
                                                      std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetTurnRate(const std::uint8_t* data,
                                                 std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetFlightBackSpeed(const std::uint8_t* data,
                                                        std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleSplineSetPitchRate(const std::uint8_t* data,
                                                  std::size_t len) {
  return ParseSplineSpeed(data, len, spline_speed_);
}

bool MovementExtHandler::HandleFlightSplineSync(const std::uint8_t* data,
                                                std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadFloat(flight_sync_.distance)) return false;
  if (!r.ReadPackedGuid(flight_sync_.mover)) return false;
  return true;
}

bool MovementExtHandler::HandleMoveTimeSkipped(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(time_skipped_.mover)) return false;
  if (!r.ReadU32(time_skipped_.time_skipped)) return false;
  return true;
}

bool MovementExtHandler::HandleMoveSetPitch(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(move_broadcast_.mover)) return false;
  return ParseMovementInfo(r, move_broadcast_.move_info);
}

bool MovementExtHandler::HandleMoveStartPitchUp(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(move_broadcast_.mover)) return false;
  return ParseMovementInfo(r, move_broadcast_.move_info);
}

bool MovementExtHandler::HandleMoveStartPitchDown(const std::uint8_t* data,
                                                   std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(move_broadcast_.mover)) return false;
  return ParseMovementInfo(r, move_broadcast_.move_info);
}

bool MovementExtHandler::HandleMoveStopPitch(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(move_broadcast_.mover)) return false;
  return ParseMovementInfo(r, move_broadcast_.move_info);
}

bool MovementExtHandler::ParseMoveBroadcast(const std::uint8_t* data,
                                            const std::size_t len) {
  PacketReader r(data, len);
  TeleportBroadcastInfo parsed{};
  if (!r.ReadPackedGuid(parsed.mover)) return false;
  if (!ParseMovementInfo(r, parsed.move_info)) return false;
  move_broadcast_ = parsed;
  return true;
}

bool MovementExtHandler::HandleMsgMoveRoot(const std::uint8_t* data,
                                           std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveUnroot(const std::uint8_t* data,
                                             std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveKnockBack(const std::uint8_t* data,
                                                std::size_t len) {
  PacketReader r(data, len);
  KnockbackBroadcastInfo parsed{};
  if (!r.ReadPackedGuid(parsed.mover)) return false;
  if (!ParseMovementInfo(r, parsed.move_info)) return false;
  if (!r.ReadFloat(parsed.cos_angle)) return false;
  if (!r.ReadFloat(parsed.sin_angle)) return false;
  if (!r.ReadFloat(parsed.speed_xy)) return false;
  if (!r.ReadFloat(parsed.speed_z)) return false;
  knockback_ = parsed;
  return true;
}

bool MovementExtHandler::HandleMsgMoveTeleportAck(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid(0);
  if (!r.ReadPackedGuid(guid)) return false;
  std::uint32_t counter = 0;
  if (!r.ReadU32(counter)) return false;
  TeleportAckData parsed{};
  parsed.mover_guid = guid.GetRawValue();
  parsed.counter = counter;
  if (!ParseMovementInfo(r, parsed.move_info)) return false;
  teleport_ack_ = parsed;
  return true;
}

bool MovementExtHandler::HandleMsgMoveWorldportAck(const std::uint8_t* ,
                                                   std::size_t ) {
  worldport_ack_received_ = true;
  return true;
}

bool MovementExtHandler::HandleMsgMoveFeatherFall(const std::uint8_t* data,
                                                  std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveHover(const std::uint8_t* data,
                                            std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveWaterWalk(const std::uint8_t* data,
                                                std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveUpdateCanFly(const std::uint8_t* data,
                                                   std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveSetRunMode(const std::uint8_t* data,
                                                 std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveSetWalkMode(const std::uint8_t* data,
                                                  std::size_t len) {
  return ParseMoveBroadcast(data, len);
}

bool MovementExtHandler::HandleMsgMoveSetSwimBackSpeed(
    const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid(0);
  if (!r.ReadPackedGuid(guid)) return false;
  MovementInfoWire movement_info{};
  if (!ParseMovementInfo(r, movement_info)) return false;
  float speed = 0;
  if (!r.ReadFloat(speed)) return false;
  swim_back_speed_ = MoveSpeedUpdate{guid.GetRawValue(), movement_info, speed};
  return true;
}

bool MovementExtHandler::HandleMsgMoveSetTurnRate(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid(0);
  if (!r.ReadPackedGuid(guid)) return false;
  MovementInfoWire movement_info{};
  if (!ParseMovementInfo(r, movement_info)) return false;
  float speed = 0;
  if (!r.ReadFloat(speed)) return false;
  turn_rate_ = MoveSpeedUpdate{guid.GetRawValue(), movement_info, speed};
  return true;
}

bool MovementExtHandler::HandleMsgMoveSetFlightBackSpeed(
    const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid(0);
  if (!r.ReadPackedGuid(guid)) return false;
  MovementInfoWire movement_info{};
  if (!ParseMovementInfo(r, movement_info)) return false;
  float speed = 0;
  if (!r.ReadFloat(speed)) return false;
  flight_back_speed_ = MoveSpeedUpdate{guid.GetRawValue(), movement_info, speed};
  return true;
}

bool MovementExtHandler::HandleMsgMoveSetPitchRate(const std::uint8_t* data,
                                                   std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid(0);
  if (!r.ReadPackedGuid(guid)) return false;
  MovementInfoWire movement_info{};
  if (!ParseMovementInfo(r, movement_info)) return false;
  float speed = 0;
  if (!r.ReadFloat(speed)) return false;
  pitch_rate_ = MoveSpeedUpdate{guid.GetRawValue(), movement_info, speed};
  return true;
}

bool MovementExtHandler::HandleSplineMoveFeatherFall(const std::uint8_t* data,
                                                     std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_feather_fall_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveNormalFall(const std::uint8_t* data,
                                                    std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_normal_fall_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveSetHover(const std::uint8_t* data,
                                                  std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_set_hover_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveUnsetHover(const std::uint8_t* data,
                                                    std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_unset_hover_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveWaterWalk(const std::uint8_t* data,
                                                   std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_water_walk_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveLandWalk(const std::uint8_t* data,
                                                  std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_land_walk_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveStartSwim(const std::uint8_t* data,
                                                   std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_start_swim_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveStopSwim(const std::uint8_t* data,
                                                  std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_stop_swim_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveSetRunMode(const std::uint8_t* data,
                                                    std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_run_mode_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveSetWalkMode(const std::uint8_t* data,
                                                     std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_walk_mode_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveSetFlying(const std::uint8_t* data,
                                                   std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_set_flying_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveUnsetFlying(const std::uint8_t* data,
                                                     std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_unset_flying_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleMoveGravityDisable(const std::uint8_t* data,
                                                   std::size_t len) {
  GravityToggle g{};
  if (!ParseRetailGravityToggle(data, len, g)) return false;
  gravity_disable_ = g;
  return true;
}

bool MovementExtHandler::HandleMoveGravityEnable(const std::uint8_t* data,
                                                  std::size_t len) {
  GravityToggle g{};
  if (!ParseRetailGravityToggle(data, len, g)) return false;
  gravity_enable_ = g;
  return true;
}

bool MovementExtHandler::HandleMoveSetCollisionHgt(const std::uint8_t* data,
                                                    std::size_t len) {
  PacketReader r(data, len);
  CollisionHeight ch{};
  ObjectGuid mover{ObjectGuid(0)};
  if (!r.ReadPackedGuid(mover)) return false;
  ch.mover_guid = mover.GetRawValue();
  if (!r.ReadU32(ch.counter)) return false;
  if (!r.ReadFloat(ch.height)) return false;
  collision_height_ = ch;
  return true;
}

bool MovementExtHandler::HandleSplineMoveGravityDisable(const std::uint8_t* data,
                                                         std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_gravity_disable_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleSplineMoveGravityEnable(const std::uint8_t* data,
                                                        std::size_t len) {
  SplineGuidOnly tmp{};
  if (!ParseSplineGuidOnly(data, len, tmp)) return false;
  spline_gravity_enable_guid_ = tmp.mover.GetRawValue();
  return true;
}

bool MovementExtHandler::HandleMoveGravityChng(const std::uint8_t* data,
                                                std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadPackedGuid(move_broadcast_.mover)) return false;
  gravity_chng_guid_ = move_broadcast_.mover.GetRawValue();

  return ParseMovementInfoFields(
      r, move_broadcast_.move_info,
      MovementInfoReadMode::kAllowRetailTruncation);
}

bool MovementExtHandler::HandleMoveSetCollisionHgtAck(const std::uint8_t* data,
                                                       std::size_t len) {
  PacketReader r(data, len);
  MoveCollisionHgtAck ack{};
  ObjectGuid mover{ObjectGuid(0)};
  if (!r.ReadPackedGuid(mover)) return false;
  ack.mover_guid = mover.GetRawValue();
  if (!ParseMovementInfo(r, ack.move_info)) return false;
  if (!r.ReadFloat(ack.height)) return false;
  if (r.Remaining() != 0u) return false;
  collision_hgt_ack_ = ack;
  return true;
}

bool MovementExtHandler::HandleMoveUpdateCanTransitionSwimFly(
    const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid mover{ObjectGuid(0)};
  if (!r.ReadPackedGuid(mover)) return false;
  swim_fly_transition_guid_ = mover.GetRawValue();
  return ParseMovementInfo(r, swim_fly_transition_move_info_);
}

void MovementExtHandler::Clear() {
  teleport_ = {};
  water_walk_ = false;
  feather_fall_ = false;
  hover_ = false;
  swim_fly_transition_ = false;
  logout_cancel_ = false;
  speed_ = {};
  last_toggle_ = {};
  spline_speed_ = {};
  spline_root_ = {};
  spline_rooted_ = false;
  force_speed_ = {};
  flight_sync_ = {};
  time_skipped_ = {};
  move_broadcast_ = {};

  knockback_ = {};
  teleport_ack_ = {};
  worldport_ack_received_ = false;
  swim_back_speed_ = {};
  turn_rate_ = {};
  flight_back_speed_ = {};
  pitch_rate_ = {};
  spline_feather_fall_guid_ = 0;
  spline_normal_fall_guid_ = 0;
  spline_set_hover_guid_ = 0;
  spline_unset_hover_guid_ = 0;
  spline_water_walk_guid_ = 0;
  spline_land_walk_guid_ = 0;
  spline_start_swim_guid_ = 0;
  spline_stop_swim_guid_ = 0;
  spline_run_mode_guid_ = 0;
  spline_walk_mode_guid_ = 0;
  spline_set_flying_guid_ = 0;
  spline_unset_flying_guid_ = 0;

  gravity_disable_.reset();
  gravity_enable_.reset();
  collision_height_.reset();
  spline_gravity_disable_guid_ = 0;
  spline_gravity_enable_guid_ = 0;
  gravity_chng_guid_ = 0;
  collision_hgt_ack_.reset();
  swim_fly_transition_guid_ = 0;
  swim_fly_transition_move_info_ = {};
}

}

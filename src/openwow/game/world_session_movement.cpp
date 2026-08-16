
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"

#include "openwow/game/movement_callbacks.h"
#include "openwow/game/ground_walk.h"
#include "openwow/game/movement/move_packet_batch.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/net/client_services.h"
#include "openwow/net/serialization/cdatastore_vtable.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cmath>
#include <array>
#include <cstring>
#include <optional>
#include <utility>

namespace openwow::game {

void WorldSession::SetMovementCollisionSolver(
    std::shared_ptr<MovementCollisionSolver> solver) {
  movement_collision_solver_ = std::move(solver);
  if (movement_collision_solver_ != nullptr) {
    movement_collision_solver_->InvalidateFacets();
  }
}

void WorldSession::ResetMovementCollisionSolver() {
  if (movement_collision_solver_ != nullptr) {
    movement_collision_solver_->Reset();
  }
}

std::shared_ptr<MovementCollisionSolver>
WorldSession::GetMovementCollisionSolver() const {
  return movement_collision_solver_;
}

namespace {

constexpr float kDefaultPitchEventMin = -1.5707964f;
constexpr float kDefaultPitchEventMax = 1.5707964f;

enum class MovementWarning : std::size_t {
  kMonsterMove,
  kMonsterMoveTransport,
  kCompressedMoves,
  kMultipleMoves,
  kCount,
};

void LogRateLimitedMovementWarning(const MovementWarning warning,
                                   const char *message) {
  static std::array<std::uint32_t,
                    static_cast<std::size_t>(MovementWarning::kCount)>
      counts{};
  auto &count = counts[static_cast<std::size_t>(warning)];
  ++count;
  if (count <= 4u || (count & (count - 1u)) == 0u) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              std::string(message) + " count=" +
                                  std::to_string(count));
  }
}

MovementInfo MovementInfoFromWire(const MovementInfoWire &wire) {
  MovementInfo movement;
  movement.flags = wire.movement_flags;
  movement.flags2 = wire.extra_flags;
  movement.time = wire.timestamp;
  movement.x = wire.pos_x;
  movement.y = wire.pos_y;
  movement.z = wire.pos_z;
  movement.orientation = wire.orientation;
  movement.pitch = wire.has_pitch ? wire.pitch : 0.0f;
  movement.fall_time = wire.fall_time;
  movement.spline_elevation = wire.has_spline_elevation ? wire.spline_elevation : 0.0f;

  if (wire.has_transport) {
    movement.transport.guid = wire.transport_guid;
    movement.transport.offset_x = wire.trans_x;
    movement.transport.offset_y = wire.trans_y;
    movement.transport.offset_z = wire.trans_z;
    movement.transport.offset_o = wire.trans_o;
    movement.transport.time = wire.trans_time;
    movement.transport.seat = wire.trans_seat;
    movement.transport.time2 = wire.has_trans_time2 ? wire.trans_time2 : 0;
  }

  if (wire.has_jump) {
    movement.jump.z_speed = wire.jump_z_speed;
    movement.jump.sin_angle = wire.jump_sin_angle;
    movement.jump.cos_angle = wire.jump_cos_angle;
    movement.jump.xy_speed = wire.jump_xy_speed;
  }

  return movement;
}

MovementOnlyUpdate BuildTeleportMovementUpdate(const WorldObject *existing_object,
                                               ObjectGuid mover_guid, const MovementInfoWire &wire,
                                               const std::uint32_t client_receive_tick_ms) {
  MovementOnlyUpdate update;
  update.guid = mover_guid;
  update.movement.update_flags = static_cast<std::uint16_t>(kUpdateFlagLiving);
  update.movement.movement = MovementInfoFromWire(wire);
  update.client_receive_tick_ms = client_receive_tick_ms;

  if (existing_object != nullptr) {
    for (std::size_t i = 0; i < kMaxSpeeds; ++i) {
      update.movement.speeds[i] = existing_object->GetSpeed(static_cast<SpeedType>(i));
    }
  }

  return update;
}

void AppendU32(std::vector<std::uint8_t> &payload, std::uint32_t value) {
  payload.push_back(static_cast<std::uint8_t>(value & 0xFF));
  payload.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  payload.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  payload.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

net::CDataStore MakePacketRemainderStore(
    const net::wotlk::WorldPacket &packet, const std::size_t read_position) {
  const auto size = static_cast<std::uint32_t>(packet.payload.size());
  return net::CDataStore{
      .vtable = net::CDataStore_BaseVTable(),
      .data = const_cast<std::uint8_t *>(packet.payload.data()),
      .window_base = 0u,
      .window_size = size,
      .write_pos = size,
      .read_pos = static_cast<std::uint32_t>(read_position),
  };
}

bool IsPitchEventMover(const MovementExtHandler &movement_ext,
                       const ObjectGuid &local_player_guid) {
  const auto &broadcast = movement_ext.last_move_broadcast();
  if (!broadcast.move_info.has_pitch) {
    return false;
  }

  if (broadcast.mover == local_player_guid) {
    return true;
  }

  const auto vehicle_guid = VehicleSystem::Get().GetVehicleGuid();
  return !vehicle_guid.IsEmpty() && broadcast.mover == vehicle_guid;
}

void FirePitchEventFromBroadcast(const MovementExtHandler &movement_ext) {
  const auto &broadcast = movement_ext.last_move_broadcast();

  float min_pitch = kDefaultPitchEventMin;
  float max_pitch = kDefaultPitchEventMax;
  if (VehicleSystem::Get().CanAim()) {
    min_pitch = VehicleSystem::Get().GetAimPitchMin();
    max_pitch = VehicleSystem::Get().GetAimPitchMax();
  }

  ui::game::ScriptEventDispatch::Get().FireVehicleAngleUpdate(broadcast.move_info.pitch, min_pitch,
                                                              max_pitch);
}

bool HandleSplineSpeedChange(WorldSession &session, const net::wotlk::WorldPacket &pkt,
                             bool (MovementExtHandler::*handler)(const std::uint8_t *, std::size_t),
                             const SpeedType type, const std::uint32_t movement_opcode) {
  if (!(session.movement_ext().*handler)(pkt.payload.data(), pkt.payload.size())) {
    return false;
  }

  const auto &spline_speed = session.movement_ext().last_spline_speed();
  if (auto *unit = session.objects().GetMutableUnit(spline_speed.mover); unit != nullptr) {
    unit->Movement().ApplySplineSpeedChange(session, type, spline_speed.speed,
                                            movement_opcode);
  }

  return true;
}

bool QueueForcedMovementEvent(WorldSession &session, const ObjectGuid mover,
                              const movement::MoveEventType event_type,
                              const std::uint32_t counter,
                              const float value = 0.0f) {
  auto *const unit = session.objects().GetMutableUnit(mover);
  if (unit == nullptr) {
    return false;
  }
  const auto timestamp = session.CurrentClientTimeMs();
  unit->Movement().Data().QueueDeferredMoveEvent(
      timestamp, static_cast<std::uint32_t>(event_type), true, counter, value,
      false, timestamp);
  return true;
}

void ApplyMovementBroadcast(WorldSession &session, const ObjectGuid mover,
                            const MovementInfo &movement_info,
                            const std::uint32_t opcode) {
  const auto receive_tick = session.CurrentClientTimeMs();
  if (auto *const unit = session.objects().GetMutableUnit(mover);
      unit != nullptr) {

    (void)unit->Movement().DispatchClientMovementOpcode(
        session, opcode, movement_info, receive_tick);
  }
}

void ApplySplineMovementState(WorldSession &session, const ObjectGuid mover,
                              const net::wotlk::Opcode opcode) {
  if (auto *const unit = session.objects().GetMutableUnit(mover);
      unit != nullptr) {
    (void)unit->Movement().ApplySplineMovementStateOpcode(
        session, static_cast<std::uint32_t>(opcode));
  }
}

void ApplySpeedBroadcast(WorldSession &session,
                         const ObjectGuid mover,
                         const MovementInfoWire &wire_movement,
                         const float speed,
                         const std::uint32_t opcode) {
  const auto movement_info = MovementInfoFromWire(wire_movement);
  const auto receive_tick = session.CurrentClientTimeMs();
  if (auto *const unit = session.objects().GetMutableUnit(mover);
      unit != nullptr) {

    (void)unit->Movement().DispatchSpeedAckOpcode(
        session, opcode, movement_info, receive_tick, speed);
  }
}

void ApplySpeedBroadcast(WorldSession &session,
                         const MoveSpeedUpdate &speed_update,
                         const std::uint32_t opcode) {
  ApplySpeedBroadcast(session, ObjectGuid{speed_update.mover_guid},
                      speed_update.move_info, speed_update.speed, opcode);
}

std::optional<movement::MoveEventType> ForcedSpeedEventType(
    const SpeedType type) {
  using movement::MoveEventType;
  switch (type) {
    case kSpeedWalk: return MoveEventType::kSetWalkSpeed;
    case kSpeedRun: return MoveEventType::kSetRunSpeed;
    case kSpeedRunBack: return MoveEventType::kSetRunBackSpeed;
    case kSpeedSwim: return MoveEventType::kSetSwimSpeed;
    case kSpeedSwimBack: return MoveEventType::kSetSwimBackSpeed;
    case kSpeedFlight: return MoveEventType::kSetFlightSpeed;
    case kSpeedFlightBack: return MoveEventType::kSetFlightBackSpeed;
    case kSpeedTurnRate: return MoveEventType::kSetTurnRate;
    case kSpeedPitchRate: return MoveEventType::kSetPitchRate;
    default: return std::nullopt;
  }
}

void ApplyForcedSpeedChange(WorldSession &session, const ObjectGuid &mover, const SpeedType type,
                            const std::uint32_t counter, const float speed) {
  if (auto *const unit = session.objects().GetMutableUnit(mover);
      unit != nullptr) {
    if (const auto event_type = ForcedSpeedEventType(type);
        event_type.has_value()) {

      unit->Movement().Data().QueueForceSpeedChangeEvent(
          session.CurrentClientTimeMs(), *event_type, counter, speed);
    }
  }
}

bool HandleExtendedForceSpeedChange(WorldSession &session, const net::wotlk::WorldPacket &pkt,
                                    bool (MovementExtHandler::*handler)(const std::uint8_t *,
                                                                        std::size_t),
                                    const SpeedType type) {
  if (!(session.movement_ext().*handler)(pkt.payload.data(), pkt.payload.size())) {
    return false;
  }

  const auto &force_speed = session.movement_ext().last_force_speed();
  ApplyForcedSpeedChange(session, force_speed.mover, type, force_speed.counter, force_speed.speed);
  return true;
}

}

void WorldSession::HandleForceSpeedChange(const net::wotlk::WorldPacket &pkt, SpeedType type) {

  if (pkt.payload.size() < 6)
    return;

  const auto *d = pkt.payload.data();
  std::size_t off = 0;

  ObjectGuid mover_guid{0};
  std::uint8_t mask = d[off++];
  std::uint64_t raw_guid = 0;
  for (int i = 0; i < 8; ++i) {
    if (mask & (1u << i)) {
      if (off >= pkt.payload.size())
        return;
      raw_guid |= static_cast<std::uint64_t>(d[off++]) << (i * 8);
    }
  }
  mover_guid = ObjectGuid(raw_guid);

  if (off + 4 > pkt.payload.size())
    return;
  std::uint32_t counter = 0;
  std::memcpy(&counter, d + off, 4);
  off += 4;

  if (pkt.opcode == net::wotlk::Opcode::SMSG_FORCE_RUN_SPEED_CHANGE) {
    if (off >= pkt.payload.size()) {
      return;
    }
    ++off;
  }

  if (off + 4 > pkt.payload.size())
    return;
  float speed = 0.0f;
  std::memcpy(&speed, d + off, 4);

  ApplyForcedSpeedChange(*this, mover_guid, type, counter, speed);
}

void WorldSession::HandleMovement(const net::wotlk::WorldPacket &pkt) {

  ObjectGuid sender;
  MovementInfo move_info;
  if (!net::wotlk::ParseMovePacket(pkt.payload.data(), pkt.payload.size(), sender, move_info)) {
    return;
  }

  const auto *obj = objects().Get(sender);
  if (!obj) {
    return;
  }

  ApplyMovementBroadcast(*this, sender, move_info,
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::FeedSplineManager(const MonsterMoveInfo &info) {
  auto *const mutable_unit = objects().GetMutableUnit(info.mover);
  if (mutable_unit != nullptr && mutable_unit->State().IsDead()) {

    mutable_unit->Movement().StopLocomotionForDeath(*this);
    return;
  }
  const auto *const unit = mutable_unit;
  float start_facing = unit != nullptr ? unit->GetFacing() : 0.0f;
  std::optional<Vec3> current_position;
  std::optional<float> target_facing;
  if (unit != nullptr) {
    movement_spline_mgr_.SetSpeeds(
        info.mover.GetRawValue(),
        world::UnitSpeeds{
            .walk = unit->GetSpeed(kSpeedWalk),
            .run = unit->GetSpeed(kSpeedRun),
            .runBack = unit->GetSpeed(kSpeedRunBack),
            .swim = unit->GetSpeed(kSpeedSwim),
            .swimBack = unit->GetSpeed(kSpeedSwimBack),
            .flight = unit->GetSpeed(kSpeedFlight),
            .flightBack = unit->GetSpeed(kSpeedFlightBack),
            .turn = unit->GetSpeed(kSpeedTurnRate),
            .pitch = unit->GetSpeed(kSpeedPitchRate),
        });
    const auto &movement = unit->GetMovementInfo();
    const bool packet_parented = info.has_transport && !info.transport.IsEmpty();
    if (packet_parented && movement.transport.guid == info.transport) {
      start_facing = movement.transport.offset_o;
      current_position = Vec3{movement.transport.offset_x,
                              movement.transport.offset_y,
                              movement.transport.offset_z};
    } else if (packet_parented) {
      start_facing = 0.0f;
      current_position = info.position;
    } else {
      const auto position = unit->GetPosition();
      current_position = Vec3{position.x, position.y, position.z};
    }
  }
  if (info.move_type == MonsterMoveType::kFacingTarget && current_position) {
    if (const auto *target = objects().Get(ObjectGuid(info.facing_target_guid)); target != nullptr) {
      const auto source_position = unit->GetPosition();
      const auto target_position = target->GetPosition();
      target_facing = std::atan2(target_position.y - source_position.y,
                                 target_position.x - source_position.x);
      if (unit != nullptr) {
        if (info.has_transport && !info.transport.IsEmpty()) {
          *target_facing -= Movement_GetObjectOrientation(
              objects(), info.transport.GetRawValue());
        }
      }
    }
  }
  movement_spline_mgr_.ApplyMonsterMove(info, start_facing, current_position, target_facing);

  if (info.move_type == MonsterMoveType::kStop) {
    if (auto *const stop_unit = objects().GetMutableUnit(info.mover);
        stop_unit != nullptr) {
      const auto &stop_movement = stop_unit->GetMovementInfo();
      const float stop_facing =
          info.has_transport && stop_movement.transport.guid == info.transport
              ? stop_movement.transport.offset_o
              : (info.has_transport ? 0.0f : stop_unit->GetFacing());
      stop_unit->Movement().ApplySplineMovementPose(
          info.position, stop_facing, false, false, true,
          info.has_transport ? info.transport : ObjectGuid{},
          info.has_transport ? info.transport_seat : -1);
      (void)stop_unit->Movement().TrySettleSplineMovementPoseOwnership();
    }
  }
}

void WorldSession::HandleMonsterMove(const net::wotlk::WorldPacket &pkt) {
  if (!monster_move_.HandleMonsterMove(pkt.payload.data(), pkt.payload.size())) {
    LogRateLimitedMovementWarning(
        MovementWarning::kMonsterMove,
        "SMSG_MONSTER_MOVE: malformed payload ignored");
    return;
  }
  const auto &info = monster_move_.last_move();

  if (auto *unit = objects().GetMutableUnit(info.mover); unit != nullptr) {
    Unit_SetVehicleSeatTransferPacketBit(unit, info.unk_byte);
    auto packet_remainder =
        MakePacketRemainderStore(pkt, info.transition_payload_offset);

    if (TryVehicleSeatTransfer(*this, *unit,
                               static_cast<double>(CurrentClientTimeMs()),
                               &packet_remainder,
                               0, 0xFF)) {
      return;
    }
  }

  FeedSplineManager(info);
}

void WorldSession::HandleMonsterMoveTransport(const net::wotlk::WorldPacket &pkt) {
  if (!monster_move_.HandleMonsterMoveTransport(pkt.payload.data(), pkt.payload.size())) {
    LogRateLimitedMovementWarning(
        MovementWarning::kMonsterMoveTransport,
        "SMSG_MONSTER_MOVE_TRANSPORT: malformed payload ignored");
    return;
  }
  const auto &info = monster_move_.last_move();

  if (auto *unit = objects().GetMutableUnit(info.mover); unit != nullptr) {
    Unit_SetVehicleSeatTransferPacketBit(unit, info.unk_byte);
    const std::uint64_t target_guid =
        info.transport.IsEmpty() ? 0ULL : info.transport.GetRawValue();
    auto packet_remainder =
        MakePacketRemainderStore(pkt, info.transition_payload_offset);
    if (TryVehicleSeatTransfer(
            *this, *unit, static_cast<double>(CurrentClientTimeMs()),
            &packet_remainder,
            target_guid,
            static_cast<std::uint8_t>(info.transport_seat))) {
      return;
    }
  }

  FeedSplineManager(info);
}

void WorldSession::HandleCompressedMoves(const net::wotlk::WorldPacket &pkt) {
  std::vector<movement::BatchedMovePacket> packets;
  if (!movement::DecodeCompressedMovePacketBatch(
          pkt.payload.data(), pkt.payload.size(), packets)) {
    LogRateLimitedMovementWarning(
        MovementWarning::kCompressedMoves,
        "SMSG_COMPRESSED_MOVES: malformed payload ignored");
    return;
  }
  for (auto &packet : packets) {
    net::wotlk::WorldPacket sub_pkt{
        static_cast<net::wotlk::Opcode>(packet.opcode),
        std::move(packet.payload)};
    (void)HandlePacket(sub_pkt);
  }
}

void WorldSession::HandleMoveTeleport(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleTeleport(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &teleport = movement_ext_.last_teleport();
  ApplyMovementBroadcast(
      *this, teleport.mover, MovementInfoFromWire(teleport.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_TELEPORT));
}

void WorldSession::HandleMoveWaterWalk(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleWaterWalk(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover, movement::MoveEventType::kWaterWalkEnable,
      toggle.counter);
}

void WorldSession::HandleMoveLandWalk(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleLandWalk(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover, movement::MoveEventType::kWaterWalkDisable,
      toggle.counter);
}

void WorldSession::HandleMoveFeatherFall(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleFeatherFall(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover, movement::MoveEventType::kFeatherFallEnable,
      toggle.counter);
}

void WorldSession::HandleMoveNormalFall(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleNormalFall(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover, movement::MoveEventType::kFeatherFallDisable,
      toggle.counter);
}

void WorldSession::HandleLogoutCancelAck(const net::wotlk::WorldPacket &pkt) {
  movement_ext_.HandleLogoutCancelAck(pkt.payload.data(), pkt.payload.size());
  openwow::net::ClientServices::Instance().HandleLogoutCancelAck();
}

void WorldSession::HandleMoveSetRunSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSetRunSpeed(pkt.payload.data(), pkt.payload.size())) {
    const auto &speed = movement_ext_.last_speed();
    ApplySpeedBroadcast(*this, speed.mover, speed.move_info, speed.speed,
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMoveSetRunBackSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSetRunBackSpeed(pkt.payload.data(), pkt.payload.size())) {
    const auto &speed = movement_ext_.last_speed();
    ApplySpeedBroadcast(*this, speed.mover, speed.move_info, speed.speed,
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMoveSetWalkSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSetWalkSpeed(pkt.payload.data(), pkt.payload.size())) {
    const auto &speed = movement_ext_.last_speed();
    ApplySpeedBroadcast(*this, speed.mover, speed.move_info, speed.speed,
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMoveSetSwimSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSetSwimSpeed(pkt.payload.data(), pkt.payload.size())) {
    const auto &speed = movement_ext_.last_speed();
    ApplySpeedBroadcast(*this, speed.mover, speed.move_info, speed.speed,
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMoveSetFlightSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSetFlightSpeed(pkt.payload.data(), pkt.payload.size())) {
    const auto &speed = movement_ext_.last_speed();
    ApplySpeedBroadcast(*this, speed.mover, speed.move_info, speed.speed,
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMoveSetHover(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleSetHover(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover, movement::MoveEventType::kHoverEnable,
      toggle.counter);
}

void WorldSession::HandleMoveUnsetHover(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleUnsetHover(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover, movement::MoveEventType::kHoverDisable,
      toggle.counter);
}

void WorldSession::HandleMoveSetCanSwimFlyTransition(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleSetCanSwimFlyTransition(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover,
      movement::MoveEventType::kSwimFlyTransitionEnable, toggle.counter);
}

void WorldSession::HandleMoveUnsetCanSwimFlyTransition(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleUnsetCanSwimFlyTransition(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &toggle = movement_ext_.last_toggle();
  (void)QueueForcedMovementEvent(
      *this, toggle.mover,
      movement::MoveEventType::kSwimFlyTransitionDisable, toggle.counter);
}

void WorldSession::HandleSplineSetRunSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(*this, pkt, &MovementExtHandler::HandleSplineSetRunSpeed, kSpeedRun,
                          static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_RUN_SPEED));
}

void WorldSession::HandleSplineMoveRoot(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveRoot(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, movement_ext_.last_spline_root().mover,
                             net::wotlk::Opcode::SMSG_SPLINE_MOVE_ROOT);
  }
}

void WorldSession::HandleSplineMoveUnroot(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveUnroot(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, movement_ext_.last_spline_root().mover,
                             net::wotlk::Opcode::SMSG_SPLINE_MOVE_UNROOT);
  }
}

void WorldSession::HandleForceSwimBackSpeedChange(const net::wotlk::WorldPacket &pkt) {
  HandleExtendedForceSpeedChange(*this, pkt, &MovementExtHandler::HandleForceSwimBackSpeedChange,
                                 kSpeedSwimBack);
}

void WorldSession::HandleForceFlightBackSpeedChange(const net::wotlk::WorldPacket &pkt) {
  HandleExtendedForceSpeedChange(*this, pkt, &MovementExtHandler::HandleForceFlightBackSpeedChange,
                                 kSpeedFlightBack);
}

void WorldSession::HandleForcePitchRateChange(const net::wotlk::WorldPacket &pkt) {
  HandleExtendedForceSpeedChange(*this, pkt, &MovementExtHandler::HandleForcePitchRateChange,
                                 kSpeedPitchRate);
}

void WorldSession::HandleSplineSetWalkSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(*this, pkt, &MovementExtHandler::HandleSplineSetWalkSpeed, kSpeedWalk,
                          static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_WALK_SPEED));
}

void WorldSession::HandleSplineSetSwimSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(*this, pkt, &MovementExtHandler::HandleSplineSetSwimSpeed, kSpeedSwim,
                          static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_SWIM_SPEED));
}

void WorldSession::HandleSplineSetFlightSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(
      *this, pkt, &MovementExtHandler::HandleSplineSetFlightSpeed, kSpeedFlight,
      static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_FLIGHT_SPEED));
}

void WorldSession::HandleSplineSetRunBackSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(
      *this, pkt, &MovementExtHandler::HandleSplineSetRunBackSpeed, kSpeedRunBack,
      static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_RUN_BACK_SPEED));
}

void WorldSession::HandleSplineSetSwimBackSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(
      *this, pkt, &MovementExtHandler::HandleSplineSetSwimBackSpeed, kSpeedSwimBack,
      static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_SWIM_BACK_SPEED));
}

void WorldSession::HandleSplineSetTurnRate(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(*this, pkt, &MovementExtHandler::HandleSplineSetTurnRate, kSpeedTurnRate,
                          static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_TURN_RATE));
}

void WorldSession::HandleSplineSetFlightBackSpeed(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(
      *this, pkt, &MovementExtHandler::HandleSplineSetFlightBackSpeed, kSpeedFlightBack,
      static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_FLIGHT_BACK_SPEED));
}

void WorldSession::HandleSplineSetPitchRate(const net::wotlk::WorldPacket &pkt) {
  HandleSplineSpeedChange(*this, pkt, &MovementExtHandler::HandleSplineSetPitchRate, kSpeedPitchRate,
                          static_cast<std::uint32_t>(net::wotlk::Opcode::SMSG_SPLINE_SET_PITCH_RATE));
}

void WorldSession::HandleFlightSplineSync(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleFlightSplineSync(pkt.payload.data(),
                                             pkt.payload.size())) {
    return;
  }

  const auto& sync = movement_ext_.last_flight_sync();
  movement_spline_mgr_.SyncFlightSplineAnimation(
      sync.mover.IsEmpty() ? 0 : sync.mover.GetRawValue(),
      sync.distance);
}

void WorldSession::HandleMoveTimeSkipped(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveTimeSkipped(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &skipped = movement_ext_.last_time_skipped();
  if (auto *const unit = objects().GetMutableUnit(skipped.mover);
      unit != nullptr) {

    unit->Movement().ApplyServerMovementTimeSkipped(skipped.time_skipped);
  }
}

void WorldSession::HandleMoveSetPitch(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveSetPitch(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(
      *this, broadcast.mover, MovementInfoFromWire(broadcast.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_SET_PITCH));
  if (!VehicleSystem::Get().CanAim() &&
      IsPitchEventMover(movement_ext_, objects().GetLocalPlayerGuid())) {
    FirePitchEventFromBroadcast(movement_ext_);
  }
}

void WorldSession::HandleMoveStartPitchUp(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveStartPitchUp(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(
      *this, broadcast.mover, MovementInfoFromWire(broadcast.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_START_PITCH_UP));
  if (!VehicleSystem::Get().CanAim() &&
      IsPitchEventMover(movement_ext_, objects().GetLocalPlayerGuid())) {
    FirePitchEventFromBroadcast(movement_ext_);
  }
}

void WorldSession::HandleMoveStartPitchDown(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveStartPitchDown(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(
      *this, broadcast.mover, MovementInfoFromWire(broadcast.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_START_PITCH_DOWN));
  if (!VehicleSystem::Get().CanAim() &&
      IsPitchEventMover(movement_ext_, objects().GetLocalPlayerGuid())) {
    FirePitchEventFromBroadcast(movement_ext_);
  }
}

void WorldSession::HandleMoveStopPitch(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveStopPitch(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(
      *this, broadcast.mover, MovementInfoFromWire(broadcast.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_STOP_PITCH));
  if (!VehicleSystem::Get().CanAim() &&
      IsPitchEventMover(movement_ext_, objects().GetLocalPlayerGuid())) {
    FirePitchEventFromBroadcast(movement_ext_);
  }
}

void WorldSession::HandleMsgMoveRoot(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveRoot(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(
      *this, broadcast.mover, MovementInfoFromWire(broadcast.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_ROOT));
}

void WorldSession::HandleMsgMoveUnroot(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveUnroot(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(
      *this, broadcast.mover, MovementInfoFromWire(broadcast.move_info),
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_UNROOT));
}

void WorldSession::HandleMsgMoveKnockBack(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveKnockBack(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &knockback = movement_ext_.last_knockback();
  auto movement_info = MovementInfoFromWire(knockback.move_info);
  movement_info.flags |= kMoveFlagFalling;
  movement_info.fall_time = 0;
  movement_info.jump.cos_angle = knockback.cos_angle;
  movement_info.jump.sin_angle = knockback.sin_angle;
  movement_info.jump.xy_speed = knockback.speed_xy;
  movement_info.jump.z_speed = knockback.speed_z;
  ApplyMovementBroadcast(
      *this, knockback.mover, movement_info,
      static_cast<std::uint32_t>(net::wotlk::Opcode::MSG_MOVE_KNOCK_BACK));
}

void WorldSession::HandleMsgMoveTeleportAck(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveTeleportAck(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &teleport = movement_ext_.last_teleport_ack();
  const ObjectGuid mover_guid{teleport.mover_guid};

  {
    net::wotlk::WorldPacket ack(
        net::wotlk::Opcode::MSG_MOVE_TELEPORT_ACK);
    const auto packed = mover_guid.Pack();
    ack.payload.insert(ack.payload.end(), packed.begin(), packed.end());
    AppendU32(ack.payload, teleport.counter);
    AppendU32(ack.payload, CurrentClientTimeMs());
    Send(ack);
  }

  if (state_ != WorldState::kInWorld) {
    return;
  }

  const auto *existing_object = objects().Get(mover_guid);
  if (existing_object == nullptr) {
    return;
  }

  if (objects().GetMutableUnit(mover_guid) != nullptr) {
    player_control_runtime_.SetActiveMover(
        *this, objects(), missile_trajectory(), teleport.mover_guid);
  }

  if (teleport.move_info.has_pitch) {
    if (world_camera_ != nullptr) {
      world_camera_->SetPitch(teleport.move_info.pitch);
    }
  }

  const auto update = BuildTeleportMovementUpdate(
      existing_object, mover_guid, teleport.move_info, CurrentClientTimeMs());
  if (auto *const mover = objects().GetMutableUnit(mover_guid);
      mover != nullptr && mover->IsActiveMover()) {

    mover->Movement().ApplyActiveMoverTeleportAck(*this, update);
  } else {
    objects().ApplyMovementUpdate(update);
  }

  if (auto *const mover = objects().GetMutableUnit(mover_guid); mover != nullptr) {
    mover->Animation().RefreshSelectedStandAnimation(*this, 0u, ~0u);
  }
}

void WorldSession::HandleMsgMoveWorldportAck(const net::wotlk::WorldPacket &pkt) {
  movement_ext_.HandleMsgMoveWorldportAck(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleMsgMoveFeatherFall(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveFeatherFall(pkt.payload.data(), pkt.payload.size())) return;
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMsgMoveHover(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveHover(pkt.payload.data(), pkt.payload.size())) return;
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMsgMoveWaterWalk(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveWaterWalk(pkt.payload.data(), pkt.payload.size())) return;
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMsgMoveUpdateCanFly(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveUpdateCanFly(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMsgMoveSetRunMode(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveSetRunMode(pkt.payload.data(), pkt.payload.size())) return;
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMsgMoveSetWalkMode(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMsgMoveSetWalkMode(pkt.payload.data(), pkt.payload.size())) return;
  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMsgMoveSetSwimBackSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleMsgMoveSetSwimBackSpeed(pkt.payload.data(), pkt.payload.size())) {
    ApplySpeedBroadcast(*this, movement_ext_.last_swim_back_speed(),
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMsgMoveSetTurnRate(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleMsgMoveSetTurnRate(pkt.payload.data(), pkt.payload.size())) {
    ApplySpeedBroadcast(*this, movement_ext_.last_turn_rate(),
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMsgMoveSetFlightBackSpeed(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleMsgMoveSetFlightBackSpeed(pkt.payload.data(), pkt.payload.size())) {
    ApplySpeedBroadcast(*this, movement_ext_.last_flight_back_speed(),
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleMsgMoveSetPitchRate(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleMsgMoveSetPitchRate(pkt.payload.data(), pkt.payload.size())) {
    ApplySpeedBroadcast(*this, movement_ext_.last_pitch_rate(),
                        static_cast<std::uint32_t>(pkt.opcode));
  }
}

void WorldSession::HandleSplineMoveFeatherFall(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveFeatherFall(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_feather_fall_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveNormalFall(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveNormalFall(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_normal_fall_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveSetHover(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveSetHover(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_set_hover_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveUnsetHover(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveUnsetHover(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_unset_hover_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveWaterWalk(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveWaterWalk(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_water_walk_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveLandWalk(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveLandWalk(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_land_walk_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveStartSwim(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveStartSwim(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_start_swim_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveStopSwim(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveStopSwim(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_stop_swim_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveSetRunMode(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveSetRunMode(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_run_mode_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveSetWalkMode(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveSetWalkMode(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_walk_mode_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveSetFlying(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleSplineMoveSetFlying(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ApplySplineMovementState(*this,
                           ObjectGuid(movement_ext_.spline_set_flying_guid()),
                           pkt.opcode);
}

void WorldSession::HandleSplineMoveUnsetFlying(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleSplineMoveUnsetFlying(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ApplySplineMovementState(
      *this, ObjectGuid(movement_ext_.spline_unset_flying_guid()), pkt.opcode);
}

void WorldSession::HandleMoveGravityDisable(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveGravityDisable(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &gravity_disable = movement_ext_.last_gravity_disable();
  if (!gravity_disable.has_value()) {
    return;
  }

  const ObjectGuid mover{gravity_disable->mover_guid};
  (void)QueueForcedMovementEvent(
      *this, mover, movement::MoveEventType::kGravityDisable,
      gravity_disable->counter);
}

void WorldSession::HandleMoveGravityEnable(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveGravityEnable(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &gravity_enable = movement_ext_.last_gravity_enable();
  if (!gravity_enable.has_value()) {
    return;
  }

  const ObjectGuid mover{gravity_enable->mover_guid};
  (void)QueueForcedMovementEvent(
      *this, mover, movement::MoveEventType::kGravityEnable,
      gravity_enable->counter);
}

void WorldSession::HandleMoveSetCollisionHgt(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveSetCollisionHgt(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &collision_height = movement_ext_.last_collision_height();
  if (!collision_height.has_value()) {
    return;
  }

  (void)QueueForcedMovementEvent(
      *this, ObjectGuid{collision_height->mover_guid},
      movement::MoveEventType::kSetCollisionHeight,
      collision_height->counter, collision_height->height);
}

void WorldSession::HandleSplineMoveGravityDisable(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveGravityDisable(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_gravity_disable_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleSplineMoveGravityEnable(const net::wotlk::WorldPacket &pkt) {
  if (movement_ext_.HandleSplineMoveGravityEnable(pkt.payload.data(), pkt.payload.size())) {
    ApplySplineMovementState(*this, ObjectGuid(movement_ext_.spline_gravity_enable_guid()),
                             pkt.opcode);
  }
}

void WorldSession::HandleMoveGravityChng(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveGravityChng(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &broadcast = movement_ext_.last_move_broadcast();
  ApplyMovementBroadcast(*this, broadcast.mover,
                         MovementInfoFromWire(broadcast.move_info),
                         static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMoveSetCollisionHgtAck(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveSetCollisionHgtAck(pkt.payload.data(),
                                                   pkt.payload.size())) {
    return;
  }

  const auto &collision = movement_ext_.last_collision_hgt_ack();
  if (!collision.has_value()) {
    return;
  }

  const ObjectGuid mover{collision->mover_guid};
  const MovementInfo movement_info = MovementInfoFromWire(collision->move_info);
  const auto receive_tick = CurrentClientTimeMs();
  if (auto *const unit = objects().GetMutableUnit(mover); unit != nullptr) {
    (void)unit->Movement().DispatchSpeedAckOpcode(
        *this, static_cast<std::uint32_t>(pkt.opcode), movement_info,
        receive_tick, collision->height);
  }
}

void WorldSession::HandleMoveUpdateCanTransitionSwimFly(const net::wotlk::WorldPacket &pkt) {
  if (!movement_ext_.HandleMoveUpdateCanTransitionSwimFly(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  ApplyMovementBroadcast(
      *this, ObjectGuid(movement_ext_.last_swim_fly_transition_guid()),
      MovementInfoFromWire(movement_ext_.last_swim_fly_transition_move_info()),
      static_cast<std::uint32_t>(pkt.opcode));
}

void WorldSession::HandleMultipleMoves(const net::wotlk::WorldPacket &pkt) {
  std::vector<movement::BatchedMovePacket> packets;
  if (!movement::DecodeMovePacketBatch(pkt.payload.data(), pkt.payload.size(),
                                       packets)) {
    LogRateLimitedMovementWarning(
        MovementWarning::kMultipleMoves,
        "SMSG_MULTIPLE_MOVES: malformed payload ignored");
    return;
  }
  for (auto &packet : packets) {
    net::wotlk::WorldPacket sub_pkt{
        static_cast<net::wotlk::Opcode>(packet.opcode),
        std::move(packet.payload)};
    (void)HandlePacket(sub_pkt);
  }
}

}

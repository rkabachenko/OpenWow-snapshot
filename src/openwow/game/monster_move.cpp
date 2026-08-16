
#include "openwow/game/monster_move.h"

namespace openwow::game {

namespace {

Vec3 UnpackWaypoint(std::uint32_t packed) {

  auto sign11 = [](std::uint32_t v) -> float {
    if (v & 0x400) v |= 0xFFFFF800;
    return static_cast<float>(static_cast<std::int32_t>(v)) * 0.25f;
  };

  auto sign10 = [](std::uint32_t v) -> float {
    if (v & 0x200) v |= 0xFFFFFC00;
    return static_cast<float>(static_cast<std::int32_t>(v)) * 0.25f;
  };

  Vec3 v;
  v.x = sign11(packed & 0x7FF);
  v.y = sign11((packed >> 11) & 0x7FF);
  v.z = sign10((packed >> 22) & 0x3FF);
  return v;
}

}

bool MonsterMoveManager::ParseMonsterMove(PacketReader& r,
                                            MonsterMoveInfo& out,
                                            bool transport) {
  out = MonsterMoveInfo{};

  if (!r.ReadPackedGuid(out.mover)) return false;

  if (transport) {
    out.has_transport = true;
    if (!r.ReadPackedGuid(out.transport)) return false;
    std::uint8_t seat;
    if (!r.ReadU8(seat)) return false;
    out.transport_seat = static_cast<std::int8_t>(seat);
  }

  if (!r.ReadU8(out.unk_byte)) return false;
  out.transition_payload_offset = r.Position();

  if (!r.ReadFloat(out.position.x)) return false;
  if (!r.ReadFloat(out.position.y)) return false;
  if (!r.ReadFloat(out.position.z)) return false;

  if (!r.ReadU32(out.spline_id)) return false;

  std::uint8_t mt;
  if (!r.ReadU8(mt)) return false;
  if (mt > static_cast<std::uint8_t>(MonsterMoveType::kFacingAngle)) return false;
  out.move_type = static_cast<MonsterMoveType>(mt);

  if (out.move_type == MonsterMoveType::kStop) return true;

  switch (out.move_type) {
    case MonsterMoveType::kFacingSpot:
      if (!r.ReadFloat(out.facing_spot.x)) return false;
      if (!r.ReadFloat(out.facing_spot.y)) return false;
      if (!r.ReadFloat(out.facing_spot.z)) return false;
      break;
    case MonsterMoveType::kFacingTarget:
      if (!r.ReadU64(out.facing_target_guid)) return false;
      break;
    case MonsterMoveType::kFacingAngle:
      if (!r.ReadFloat(out.facing_angle)) return false;
      break;
    default:
      break;
  }

  if (!r.ReadU32(out.spline_flags)) return false;

  if (out.spline_flags & SplineFlag::kAnimation) {
    if (!r.ReadU8(out.animation_id)) return false;
    if (!r.ReadU32(out.anim_start_time)) return false;
  }

  if (!r.ReadU32(out.duration)) return false;

  if (out.spline_flags & SplineFlag::kParabolic) {
    if (!r.ReadFloat(out.vertical_acceleration)) return false;
    if (!r.ReadU32(out.parabolic_start_time)) return false;
  }

  return ParseWaypoints(r, out);
}

bool MonsterMoveManager::ParseWaypoints(PacketReader& r,
                                          MonsterMoveInfo& out) {
  std::uint32_t count;
  if (!r.ReadU32(count)) return false;

  bool use_catmull = (out.spline_flags &
                      (SplineFlag::kFlying | SplineFlag::kCatmullRom)) != 0;
  out.catmull_rom = use_catmull;

  if (use_catmull) {
    if (count > r.Remaining() / (sizeof(float) * 3u)) return false;

    out.waypoints.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      if (!r.ReadFloat(out.waypoints[i].x)) return false;
      if (!r.ReadFloat(out.waypoints[i].y)) return false;
      if (!r.ReadFloat(out.waypoints[i].z)) return false;
    }
  } else {

    const std::size_t packed_point_count =
        count >= 2u ? static_cast<std::size_t>(count - 1u) : 0u;
    const std::size_t required =
        sizeof(float) * 3u + packed_point_count * sizeof(std::uint32_t);
    if (required > r.Remaining()) return false;

    Vec3 dest;
    if (!r.ReadFloat(dest.x)) return false;
    if (!r.ReadFloat(dest.y)) return false;
    if (!r.ReadFloat(dest.z)) return false;

    if (count < 2u) {
      out.waypoints.push_back(dest);
      return true;
    }

    Vec3 mid;
    mid.x = (out.position.x + dest.x) * 0.5f;
    mid.y = (out.position.y + dest.y) * 0.5f;
    mid.z = (out.position.z + dest.z) * 0.5f;

    out.waypoints.resize(count);
    out.waypoints[count - 1] = dest;

    for (std::uint32_t i = 0; i < count - 1; ++i) {
      std::uint32_t packed;
      if (!r.ReadU32(packed)) return false;
      Vec3 offset = UnpackWaypoint(packed);

      out.waypoints[i].x = mid.x - offset.x;
      out.waypoints[i].y = mid.y - offset.y;
      out.waypoints[i].z = mid.z - offset.z;
    }
  }

  return true;
}

bool MonsterMoveManager::HandleMonsterMove(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  MonsterMoveInfo info;
  if (!ParseMonsterMove(r, info, false)) return false;
  last_ = std::move(info);
  ++total_count_;
  return true;
}

bool MonsterMoveManager::HandleMonsterMoveTransport(const std::uint8_t* data,
                                                      std::size_t len) {
  PacketReader r(data, len);
  MonsterMoveInfo info;
  if (!ParseMonsterMove(r, info, true)) return false;
  last_ = std::move(info);
  ++total_count_;
  return true;
}

void MonsterMoveManager::Clear() {
  last_ = MonsterMoveInfo{};
  total_count_ = 0;
}

}

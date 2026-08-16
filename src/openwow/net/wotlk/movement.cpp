#include "openwow/net/wotlk/movement.h"
#include "openwow/network/serialization/packed_guid_codec.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

inline void PushU8(WorldPacket& pkt, std::uint8_t v) {
  pkt.AppendU8(v);
}
inline void PushU16(WorldPacket& pkt, std::uint16_t v) {
  pkt.AppendU16(v);
}
inline void PushU32(WorldPacket& pkt, std::uint32_t v) {
  pkt.AppendU32(v);
}
inline void PushFloat(WorldPacket& pkt, float v) {
  pkt.AppendFloat(v);
}

template <typename T>
inline bool ReadVal(const std::uint8_t* data, std::size_t len,
                    std::size_t& off, T& out) {
  if (off + sizeof(T) > len) return false;
  std::memcpy(&out, data + off, sizeof(T));
  off += sizeof(T);
  return true;
}

inline bool ReadPackedGuidFromBuf(const std::uint8_t* data, std::size_t len,
                                  std::size_t& off, game::ObjectGuid& out) {
  if (!data || off >= len) return false;
  const auto decoded = openwow::net::DecodePackedGuid(data + off, len - off);
  if (!decoded) return false;
  off += decoded.bytes_consumed;
  out = game::ObjectGuid(decoded.value);
  return true;
}

}

void AppendPackedGuid(WorldPacket& pkt, const game::ObjectGuid& guid) {
  const auto encoded = openwow::net::EncodePackedGuid(guid.GetRawValue());
  pkt.AppendBytes(encoded.bytes.data(), encoded.size);
}

void WriteMovementInfo(WorldPacket& pkt, const game::MovementInfo& info,
                       const bool force_transport_block) {
  PushU32(pkt, info.flags);
  PushU16(pkt, info.flags2);
  PushU32(pkt, info.time);
  PushFloat(pkt, info.x);
  PushFloat(pkt, info.y);
  PushFloat(pkt, info.z);
  PushFloat(pkt, info.orientation);

  if (info.HasFlag(game::kMoveFlagOnTransport) || force_transport_block) {
    AppendPackedGuid(pkt, info.transport.guid);
    PushFloat(pkt, info.transport.offset_x);
    PushFloat(pkt, info.transport.offset_y);
    PushFloat(pkt, info.transport.offset_z);
    PushFloat(pkt, info.transport.offset_o);
    PushU32(pkt, info.transport.time);
    PushU8(pkt, info.transport.seat);
    if (info.HasFlag2(game::kMoveFlag2InterpolatedMovement)) {
      PushU32(pkt, info.transport.time2);
    }
  }

  if (info.IsSwimmingOrFlying() ||
      info.HasFlag2(game::kMoveFlag2AlwaysAllowPitching)) {
    PushFloat(pkt, info.pitch);
  }

  PushU32(pkt, info.fall_time);

  if (info.IsFalling()) {
    PushFloat(pkt, info.jump.z_speed);
    PushFloat(pkt, info.jump.sin_angle);
    PushFloat(pkt, info.jump.cos_angle);
    PushFloat(pkt, info.jump.xy_speed);
  }

  if (info.HasSplineElevation()) {
    PushFloat(pkt, info.spline_elevation);
  }
}

std::size_t ReadMovementInfoFromBuffer(const std::uint8_t* data, std::size_t len,
                                       std::size_t offset,
                                       game::MovementInfo& out) {
  std::size_t off = offset;

  game::ResetMovementInfoScratch(out);

  if (!ReadVal(data, len, off, out.flags)) return 0;
  if (!ReadVal(data, len, off, out.flags2)) return 0;
  if (!ReadVal(data, len, off, out.time)) return 0;
  if (!ReadVal(data, len, off, out.x)) return 0;
  if (!ReadVal(data, len, off, out.y)) return 0;
  if (!ReadVal(data, len, off, out.z)) return 0;
  if (!ReadVal(data, len, off, out.orientation)) return 0;

  if (out.HasFlag(game::kMoveFlagOnTransport)) {
    if (!ReadPackedGuidFromBuf(data, len, off, out.transport.guid)) return 0;
    if (!ReadVal(data, len, off, out.transport.offset_x)) return 0;
    if (!ReadVal(data, len, off, out.transport.offset_y)) return 0;
    if (!ReadVal(data, len, off, out.transport.offset_z)) return 0;
    if (!ReadVal(data, len, off, out.transport.offset_o)) return 0;
    if (!ReadVal(data, len, off, out.transport.time)) return 0;
    if (!ReadVal(data, len, off, out.transport.seat)) return 0;
    if (out.HasFlag2(game::kMoveFlag2InterpolatedMovement)) {
      if (!ReadVal(data, len, off, out.transport.time2)) return 0;
      out.flags2 &= ~static_cast<std::uint16_t>(game::kMoveFlag2InterpolatedMovement);
    } else {
      out.transport.time2 = out.transport.time;
    }
  } else {
    out.transport.guid = game::ObjectGuid{};
    out.transport.seat = -1;
  }

  if (out.IsSwimmingOrFlying() ||
      out.HasFlag2(game::kMoveFlag2AlwaysAllowPitching)) {
    if (!ReadVal(data, len, off, out.pitch)) return 0;
  }

  if (!ReadVal(data, len, off, out.fall_time)) return 0;

  if (out.IsFalling()) {
    if (!ReadVal(data, len, off, out.jump.z_speed)) return 0;
    if (!ReadVal(data, len, off, out.jump.sin_angle)) return 0;
    if (!ReadVal(data, len, off, out.jump.cos_angle)) return 0;
    if (!ReadVal(data, len, off, out.jump.xy_speed)) return 0;
  }

  if (out.HasSplineElevation()) {
    if (!ReadVal(data, len, off, out.spline_elevation)) return 0;
  }

  return off;
}

WorldPacket BuildMovePacket(Opcode opcode,
                            const game::ObjectGuid& mover,
                            const game::MovementInfo& info) {
  WorldPacket pkt(opcode);
  AppendPackedGuid(pkt, mover);

  WriteMovementInfo(pkt, info, opcode == Opcode::CMSG_MOVE_CHNG_TRANSPORT);
  return pkt;
}

bool ParseMovePacket(const std::uint8_t* data, std::size_t len,
                     game::ObjectGuid& sender,
                     game::MovementInfo& out) {
  std::size_t off = 0;
  if (!ReadPackedGuidFromBuf(data, len, off, sender)) return false;
  std::size_t result = ReadMovementInfoFromBuffer(data, len, off, out);
  return result > 0;
}

}

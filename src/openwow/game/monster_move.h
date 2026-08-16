
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/vec3.h"

namespace openwow::game {

enum class MonsterMoveType : std::uint8_t {
  kNormal       = 0,
  kStop         = 1,
  kFacingSpot   = 2,
  kFacingTarget = 3,
  kFacingAngle  = 4,
};

namespace SplineFlag {
  inline constexpr std::uint32_t kDone            = 0x00000100;

  inline constexpr std::uint32_t kStateQueryExempt = 0x00000400;
  inline constexpr std::uint32_t kFalling         = 0x00000200;
  inline constexpr std::uint32_t kParabolic       = 0x00000800;

  inline constexpr std::uint32_t kWalkMode        = 0x00001000;
  inline constexpr std::uint32_t kFlying          = 0x00002000;
  inline constexpr std::uint32_t kOrientFixed     = 0x00004000;
  inline constexpr std::uint32_t kCatmullRom      = 0x00040000;
  inline constexpr std::uint32_t kCyclic          = 0x00080000;
  inline constexpr std::uint32_t kEnterCycle      = 0x00100000;
  inline constexpr std::uint32_t kAnimation       = 0x00200000;
  inline constexpr std::uint32_t kFreeze          = 0x00400000;
  inline constexpr std::uint32_t kTransportEnter  = 0x00800000;
  inline constexpr std::uint32_t kTransportExit   = 0x01000000;

  inline constexpr std::uint32_t kBackward        = 0x08000000;

  [[nodiscard]] inline bool HasNonExemptFlag(const std::uint32_t spline_flags,
                                             const std::uint32_t queried_flag) {
    return (spline_flags & kStateQueryExempt) == 0 &&
           (spline_flags & queried_flag) != 0;
  }
}

struct MonsterMoveInfo {
  ObjectGuid mover{ObjectGuid(0)};
  bool has_transport = false;
  ObjectGuid transport{ObjectGuid(0)};
  std::int8_t transport_seat = 0;

  std::uint8_t unk_byte = 0;

  std::size_t transition_payload_offset = 0;
  Vec3 position{};
  std::uint32_t spline_id = 0;

  MonsterMoveType move_type = MonsterMoveType::kNormal;

  Vec3 facing_spot{};
  std::uint64_t facing_target_guid = 0;
  float facing_angle = 0.0f;

  std::uint32_t spline_flags = 0;

  std::uint8_t animation_id = 0;
  std::uint32_t anim_start_time = 0;

  std::uint32_t duration = 0;

  float vertical_acceleration = 0.0f;
  std::uint32_t parabolic_start_time = 0;

  std::vector<Vec3> waypoints;

  bool catmull_rom = false;
};

class MonsterMoveManager {
 public:

  bool HandleMonsterMove(const std::uint8_t* data, std::size_t len);
  bool HandleMonsterMoveTransport(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] const MonsterMoveInfo& last_move() const { return last_; }
  [[nodiscard]] std::size_t total_move_count() const { return total_count_; }

  void Clear();

 private:
  bool ParseMonsterMove(PacketReader& r, MonsterMoveInfo& out, bool transport);
  bool ParseWaypoints(PacketReader& r, MonsterMoveInfo& out);

  MonsterMoveInfo last_{};
  std::size_t total_count_ = 0;
};

}

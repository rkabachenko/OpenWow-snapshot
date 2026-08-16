#pragma once

#include "openwow/game/movement_info.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_types.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <array>
#include <cstdint>

namespace openwow::net::wotlk {

namespace movement_speeds {

inline constexpr float kWalk       = 2.5f;
inline constexpr float kRun        = 7.0f;
inline constexpr float kRunBack    = 4.5f;
inline constexpr float kSwim       = 4.722222f;
inline constexpr float kSwimBack   = 2.5f;
inline constexpr float kFlight     = 7.0f;
inline constexpr float kFlightBack = 4.5f;
inline constexpr float kTurnRate   = 3.141594f;
inline constexpr float kPitchRate  = 3.14f;

inline constexpr std::array<float, 9> BaseSpeedTable() {
  return {kWalk, kRun, kRunBack, kSwim, kSwimBack,
          kFlight, kFlightBack, kTurnRate, kPitchRate};
}

}

inline constexpr std::uint32_t kHeartbeatIntervalMs = 500;

WorldPacket BuildMovePacket(Opcode opcode,
                            const game::ObjectGuid& mover,
                            const game::MovementInfo& info);

bool ParseMovePacket(const std::uint8_t* data, std::size_t len,
                     game::ObjectGuid& sender,
                     game::MovementInfo& out);

void AppendPackedGuid(WorldPacket& pkt, const game::ObjectGuid& guid);

void WriteMovementInfo(WorldPacket& pkt, const game::MovementInfo& info,
                       bool force_transport_block = false);

std::size_t ReadMovementInfoFromBuffer(const std::uint8_t* data, std::size_t len,
                                       std::size_t offset,
                                       game::MovementInfo& out);

}

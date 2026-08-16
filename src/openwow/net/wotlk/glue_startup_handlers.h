#pragma once

#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <functional>
#include <string>

namespace openwow::net::wotlk {

struct GlueStartupDispatchContext {
  std::function<bool(const WorldPacket&)> send_packet;
  std::string account_name;
  std::uint64_t current_character_guid = 0;
};

bool HandleGlueStartupPacket(const WorldPacket& pkt,
                             const GlueStartupDispatchContext& context);

void AccountData_UnregisterOpcodeHandlers();

}

#pragma once

#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::client::offline_world_fixture {

[[nodiscard]] openwow::net::wotlk::WorldPacket BuildInitialSpells();

[[nodiscard]] openwow::net::wotlk::WorldPacket BuildActionAssignments();

}

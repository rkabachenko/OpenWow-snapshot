#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;

[[nodiscard]] bool IsPositionInSnowArea(const WorldSession& session);

}

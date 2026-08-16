#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;

inline constexpr std::uint32_t kNorthrendFlightPermissionSpellId = 0xD3B5u;

[[nodiscard]] bool IsFlyableAreaByLocation(const WorldSession& session);
[[nodiscard]] bool IsFlyableAreaForActivePlayer(const WorldSession& session);

}

#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game::targeting {

enum class WorldClickButton : std::uint8_t {
  kPrimary,
  kSecondary,
};

struct WorldClickPoint final {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct WorldObjectClick final {
  ObjectGuid object;
  WorldClickButton button = WorldClickButton::kPrimary;
};

struct WorldTerrainClick final {
  ObjectGuid reference_object;
  WorldClickPoint local_position;
  WorldClickButton button = WorldClickButton::kPrimary;
};

struct EmptyWorldClick final {
  WorldClickPoint ray_start;
  WorldClickPoint ray_end;
  WorldClickButton button = WorldClickButton::kPrimary;
};

}

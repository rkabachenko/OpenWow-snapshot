#pragma once

#include <cstdint>

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class ObjectManager;

bool GameObjectPrimaryModelSoundCallback(
    ObjectManager& objects,
    openwow::world::WorldCamera* camera,
    std::uint32_t event_fourcc,
    std::uint32_t data,
    const float* position,
    std::uint64_t guid);

bool GameObjectPerSequenceModelSoundCallback(
    ObjectManager& objects,
    openwow::world::WorldCamera* camera,
    std::uint32_t event_fourcc,
    std::uint32_t data,
    const float* position,
    std::uint64_t guid);

}

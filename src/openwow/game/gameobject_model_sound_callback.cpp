
#include "openwow/game/gameobject_model_sound_callback.h"

#include "openwow/game/gameobject_sound_event.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"

namespace openwow::game {

namespace {

bool DispatchModelSoundEventToTypeHandler(
    ObjectManager& objects,
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_fourcc,
    const std::uint32_t data,
    const float* position,
    const std::uint64_t guid) {

  auto* obj = CGObject_HasFlags(objects, guid, kTypeMaskGameObject);
  if (!obj) {
    return false;
  }

  auto* go = static_cast<CGGameObject_C*>(obj);

  return go->HandlePrimaryModelSoundEvent(camera, event_fourcc, data, position);
}

}

bool GameObjectPrimaryModelSoundCallback(
    ObjectManager& objects,
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_fourcc,
    const std::uint32_t data,
    const float* position,
    const std::uint64_t guid) {
  return DispatchModelSoundEventToTypeHandler(
      objects, camera, event_fourcc, data, position, guid);
}

bool GameObjectPerSequenceModelSoundCallback(
    ObjectManager& objects,
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_fourcc,
    const std::uint32_t data,
    const float* position,
    const std::uint64_t guid) {
  return DispatchModelSoundEventToTypeHandler(
      objects, camera, event_fourcc, data, position, guid);
}

}

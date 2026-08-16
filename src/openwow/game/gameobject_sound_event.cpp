
#include "openwow/game/gameobject_sound_event.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/world/camera/world_camera.h"

namespace openwow::game {

bool PlayGameObjectDisplayInfoSound(
    audio::SoundRuntime& sound_runtime,
    const data::dbc::DbcLoader& dbc, const std::uint32_t display_id,
    const int slot_index,
    const float* position, std::uint32_t* const sound_handle_id) {
  if (slot_index < 0 || slot_index > 9) {
    return false;
  }

  const auto* entry = dbc.gameobject_display_info().LookupEntry(display_id);
  if (entry == nullptr) {
    return false;
  }

  const std::uint32_t sound_kit_id =
      entry->sound_ids[static_cast<std::size_t>(slot_index)];
  if (sound_kit_id == 0) {
    return false;
  }

  auto& si = sound_runtime;

  if (!si.SoundKitHasLoopFlag(sound_kit_id)) {

    return si.PlaySoundKit(sound_kit_id, position) == 0;
  }

  if (sound_handle_id == nullptr) {
    return false;
  }

  if (*sound_handle_id != 0 && si.IsSoundHandlePlaying(*sound_handle_id)) {
    return false;
  }

  if (si.IsSoundKitPlayingNearby(sound_kit_id, position)) {
    return false;
  }

  audio::SoundKitPlaybackOptions options{};
  options.loop_mode = audio::SoundLoopMode::kForceLoop;
  return si.PlaySoundKit(sound_kit_id, position, sound_handle_id, options) == 0;
}

bool DispatchGameObjectSoundEvent(
    audio::SoundRuntime& sound_runtime,
    const data::dbc::DbcLoader& dbc,
    openwow::world::WorldCamera* const camera,
    const std::uint32_t display_id, const std::uint32_t event_fourcc,
    const std::uint32_t data, const float* position,
    std::uint32_t* const sound_handle_id) {
  using namespace go_sound_event;

  switch (event_fourcc) {

    case kOpen0:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 0, position,
                                            sound_handle_id);
    case kOpen1:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 1, position,
                                            sound_handle_id);
    case kOpen2:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 2, position,
                                            sound_handle_id);
    case kOpen3:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 3, position,
                                            sound_handle_id);
    case kOpen4:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 4, position,
                                            sound_handle_id);
    case kOpen5:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 5, position,
                                            sound_handle_id);

    case kCustom0:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 6, position,
                                            sound_handle_id);
    case kCustom1:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 7, position,
                                            sound_handle_id);
    case kCustom2:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 8, position,
                                            sound_handle_id);
    case kCustom3:
      return PlayGameObjectDisplayInfoSound(sound_runtime, dbc, display_id, 9, position,
                                            sound_handle_id);

    case kSound:
    case kDirectSoundObject: {
      if (data == 0) {
        return false;
      }
      auto& si = sound_runtime;
      return si.PlaySoundKit(data, position) == 0;
    }

    case kShake: {
      if (data == 0) {
        return false;
      }
      if (camera == nullptr || position == nullptr) {
        return false;
      }
      camera->TriggerSpellEffectCameraShakes(
          data, {position[0], position[1], position[2]});
      return true;
    }

    case kLoopSound: {
      if (data == 0 || sound_handle_id == nullptr) {
        return false;
      }
      auto& si = sound_runtime;
      if (*sound_handle_id != 0 && si.IsSoundHandlePlaying(*sound_handle_id)) {
        return false;
      }
      if (si.IsSoundKitPlayingNearby(data, position)) {
        return false;
      }

      audio::SoundKitPlaybackOptions options{};
      options.volume_scale = 1.0f;
      options.explicit_volume = 1.0f;
      options.loop_mode = audio::SoundLoopMode::kForceLoop;
      return si.PlaySoundKit(data, position, sound_handle_id, options) == 0;
    }

    default:
      return false;
  }
}

bool HandleGameObjectSoundEvent(
    const CGGameObject_C& game_object,
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_fourcc,
    const std::uint32_t data, const float* position,
    std::uint32_t* const sound_handle_id) {
  const std::uint32_t display_id = game_object.GetDisplayId();
  if (display_id == 0) {
    return false;
  }

  const auto* const objects = game_object.object_manager();
  if (objects == nullptr) {
    return false;
  }

  return DispatchGameObjectSoundEvent(
      game_object.sound_runtime(), objects->dbc_loader(), camera, display_id, event_fourcc, data, position,
      sound_handle_id);
}

bool HandleDestructibleBuildingSoundEvent(
    const CGGameObject_C& game_object,
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_fourcc,
    const std::uint32_t data, const float* position,
    std::uint32_t* const sound_handle_id) {
  const std::uint32_t display_id = game_object.GetDisplayId();
  if (display_id == 0) {
    return false;
  }

  const auto* const objects = game_object.object_manager();
  if (objects == nullptr) {
    return false;
  }

  return DispatchGameObjectSoundEvent(
      game_object.sound_runtime(), objects->dbc_loader(), camera, display_id, event_fourcc, data, position,
      sound_handle_id);
}

float GetDungeonDifficultyModelOpacity(const CGGameObject_C& game_object) {
  return (game_object.GetDynamic() & 0x2u) != 0u ? 1.0f : 0.5f;
}

}

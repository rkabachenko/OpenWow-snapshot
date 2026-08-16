
#include "openwow/world/environment/chunk_ambient_audio.h"

#include "openwow/audio/playback/sound_runtime.h"

#include <utility>

namespace openwow::world {
ChunkSoundInstanceSet::~ChunkSoundInstanceSet() {
  Reset();
}

ChunkSoundInstanceSet::ChunkSoundInstanceSet(
    ChunkSoundInstanceSet&& other) noexcept
    : instances(std::move(other.instances)),
      sound(std::exchange(other.sound, nullptr)) {}

ChunkSoundInstanceSet& ChunkSoundInstanceSet::operator=(
    ChunkSoundInstanceSet&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  Reset();
  instances = std::move(other.instances);
  sound = std::exchange(other.sound, nullptr);
  return *this;
}

void ChunkSoundInstanceSet::Reset() noexcept {
  if (sound != nullptr) {
    for (auto& instance : instances) {
      if (instance.binding.active_handle_id != 0u) {
        (void)sound->StopActiveSoundHandle(
            instance.binding.active_handle_id, true, -1.0f, true);
      }
    }
  }
  instances.clear();
  sound = nullptr;
}

void CreateChunkAmbientSounds(
    const std::span<const data::terrain::SoundEmitterEntry> emitters,
    audio::SoundRuntime& sound,
    ChunkSoundInstanceSet& out) {
  if (!out.instances.empty()) {
    return;
  }

  if (emitters.empty()) {
    return;
  }

  out.sound = &sound;
  out.instances.reserve(emitters.size());

  for (const auto& entry : emitters) {
    ChunkSoundInstance instance{};
    instance.sound_kit_id = entry.sound_entry_id;

    audio::SoundKitPlaybackOptions options = BuildChunkSoundEmitterOptions(entry);

    sound.PlaySoundKit(entry.sound_entry_id, entry.position,
                       &instance.binding.active_handle_id, options);

    out.instances.push_back(instance);
  }
}

audio::SoundKitPlaybackOptions BuildChunkSoundEmitterOptions(
    const data::terrain::SoundEmitterEntry& entry) {
  audio::SoundKitPlaybackOptions options{};

  options.cone_direction = entry.direction;

  options.force_ambient_loop = true;

  return options;
}

}

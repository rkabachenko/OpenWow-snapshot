#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "openwow/audio/playback/sound_playback_types.h"
#include "openwow/data/terrain/adt_file.h"

namespace openwow::audio { class SoundRuntime; }

namespace openwow::world {

struct ChunkSoundInstance {
  std::uint32_t sound_kit_id{0};
  audio::SoundHandleBinding binding{};
};

struct ChunkSoundInstanceSet {
  ChunkSoundInstanceSet() = default;
  ~ChunkSoundInstanceSet();
  ChunkSoundInstanceSet(const ChunkSoundInstanceSet&) = delete;
  ChunkSoundInstanceSet& operator=(const ChunkSoundInstanceSet&) = delete;
  ChunkSoundInstanceSet(ChunkSoundInstanceSet&& other) noexcept;
  ChunkSoundInstanceSet& operator=(ChunkSoundInstanceSet&& other) noexcept;

  void Reset() noexcept;

  std::vector<ChunkSoundInstance> instances;
  audio::SoundRuntime* sound = nullptr;
};

void CreateChunkAmbientSounds(
    std::span<const data::terrain::SoundEmitterEntry> emitters,
    audio::SoundRuntime& sound,
    ChunkSoundInstanceSet& out);

audio::SoundKitPlaybackOptions BuildChunkSoundEmitterOptions(
    const data::terrain::SoundEmitterEntry& entry);

}


#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::data::dbc {
struct SoundEntriesEntry;
class DbcLoader;
}

namespace openwow::audio {

class SoundRuntime;

struct ResolvedSoundEntry {
    std::string path;
    float volume{1.0f};
    std::uint32_t sound_type{0};
    std::uint32_t flags{0};
};

[[nodiscard]] std::optional<ResolvedSoundEntry> ResolveSoundEntry(
    const openwow::data::dbc::SoundEntriesEntry& entry);

[[nodiscard]] std::optional<ResolvedSoundEntry> ResolveSoundEntry(
    std::uint32_t soundKitId);

void PublishSoundRuntimeDbcData(
    SoundRuntime& runtime,
    const openwow::data::dbc::DbcLoader& loader);

}

namespace openwow::audio::detail {

void SetDbcLoaderForAudio(const ::openwow::data::dbc::DbcLoader* loader);

[[nodiscard]] const ::openwow::data::dbc::DbcLoader* GetDbcLoaderForAudio();

}

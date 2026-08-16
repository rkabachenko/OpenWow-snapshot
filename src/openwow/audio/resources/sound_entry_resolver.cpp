
#include "openwow/audio/resources/sound_entry_resolver.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <algorithm>
#include <limits>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

thread_local std::mt19937 g_rng{std::random_device{}()};

const openwow::data::dbc::DbcLoader* g_dbc_loader = nullptr;

struct VariantChoice {
    std::string_view file;
    std::uint32_t freq;
};

std::optional<VariantChoice> ChooseWeightedVariant(
    const openwow::data::dbc::SoundEntriesEntry& entry) {
    std::vector<VariantChoice> variants;
    variants.reserve(10);
    for (int i = 0; i < 10; ++i) {
        if (entry.file[static_cast<std::size_t>(i)].empty()) {
            break;
        }
        variants.push_back({
            entry.file[static_cast<std::size_t>(i)],
            std::max(entry.freq[static_cast<std::size_t>(i)], 1u)
        });
    }

    if (variants.empty()) {
        return std::nullopt;
    }

    std::uint32_t total_freq = 0;
    for (const auto& variant : variants) {
        total_freq += variant.freq;
    }

    VariantChoice chosen = variants.front();
    if (variants.size() > 1 && total_freq > 0) {
        std::uniform_int_distribution<std::uint32_t> dist(0, total_freq - 1);
        const std::uint32_t roll = dist(g_rng);
        std::uint32_t accum = 0;
        for (const auto& variant : variants) {
            accum += variant.freq;
            if (roll < accum) {
                chosen = variant;
                break;
            }
        }
    }

    return chosen;
}

std::string BuildSoundEntryPath(const openwow::data::dbc::SoundEntriesEntry& entry,
                                std::string_view file_name) {
    if (entry.directory_base.empty()) {
        return std::string(file_name);
    }

    std::string path(entry.directory_base);
    if (path.back() != '\\') {
        path.push_back('\\');
    }
    path.append(file_name);
    return path;
}

std::vector<openwow::audio::SoundKitData> BuildSoundKitData(
    const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::SoundKitData> kits;
    kits.reserve(loader.sound_entries().size());
    for (const auto& entry : loader.sound_entries()) {
        openwow::audio::SoundKitData kit;
        kit.id = entry.id;
        kit.name = entry.name;
        kit.volume = entry.volume;
        kit.min_distance = entry.min_distance;
        kit.max_distance = entry.distance_cutoff;
        kit.eax_def = entry.eax_definition;
        kit.dbc_sound_type = entry.sound_type;
        kit.flags = entry.flags;
        kit.advanced_id = entry.sound_entries_advanced_id;

        const std::size_t file_count =
            std::min<std::size_t>(entry.FileCount(), kit.file_paths.size());
        for (std::size_t index = 0; index < file_count; ++index) {
            kit.file_paths[index] = BuildSoundEntryPath(entry, entry.file[index]);
            kit.frequencies[index] = entry.freq[index];
        }
        kits.push_back(std::move(kit));
    }
    return kits;
}

std::vector<openwow::audio::AdvancedSoundEntryData> BuildAdvancedSoundData(
    const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::AdvancedSoundEntryData> entries;
    entries.reserve(loader.sound_entries_advanced().size());
    for (const auto& row : loader.sound_entries_advanced()) {
        entries.push_back({
            .id = row.id,
            .inner_radius_2d = row.inner_radius_2d,
            .outer_radius_2d = row.outer_radius_2d,
            .time_of_day_window = {
                .time_a_ms = row.time_a_ms,
                .time_b_ms = row.time_b_ms,
                .time_c_ms = row.time_c_ms,
                .time_d_ms = row.time_d_ms,
            },
            .random_offset_ms = row.random_offset_range_ms,
            .usage = row.usage,
            .inner_radius = row.inner_radius_of_influence,
            .outer_radius = row.outer_radius_of_influence,
            .time_interval_min_ms = row.time_interval_min_ms,
            .time_interval_max_ms = row.time_interval_max_ms,
            .volume_slider_category = row.volume_slider_category,
            .duck_to_sfx = row.duck_to_sfx,
            .duck_to_music = row.duck_to_music,
            .duck_to_ambience = row.duck_to_ambience,
            .duck_in_time_ms = row.time_to_duck_ms,
            .duck_out_time_ms = row.time_to_unduck_ms,
        });
    }
    return entries;
}

std::vector<openwow::audio::SoundAmbienceTableEntryData>
BuildSoundAmbienceData(const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::SoundAmbienceTableEntryData> entries;
    entries.reserve(loader.sound_ambience().size());
    for (const auto& row : loader.sound_ambience()) {
        entries.push_back({
            .id = row.id,
            .sound_kit_ids = {
                static_cast<std::int32_t>(row.ambience_day),
                static_cast<std::int32_t>(row.ambience_night),
            },
        });
    }
    return entries;
}

std::vector<openwow::audio::ChunkAudioBindingEntry>
BuildChunkAudioBindingData(const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::ChunkAudioBindingEntry> entries;
    entries.reserve(loader.world_chunk_sounds().size());
    for (const auto& row : loader.world_chunk_sounds()) {
        entries.push_back({
            .key = {
                .map_id = row.columns[0],
                .tile_y = row.columns[1],
                .tile_x = row.columns[2],
                .chunk_y = row.columns[3],
                .chunk_x = row.columns[4],
            },
            .value = {
                .sound_ambience_id = static_cast<std::int32_t>(row.columns[7]),
                .zone_music_id = static_cast<std::int32_t>(row.columns[6]),
                .zone_intro_music_id = static_cast<std::int32_t>(row.columns[5]),
                .sound_provider_preferences_id = static_cast<std::int32_t>(row.columns[8]),
            },
        });
    }
    return entries;
}

std::vector<openwow::audio::WorldStateZoneSoundEntryData>
BuildWorldStateZoneSoundData(const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::WorldStateZoneSoundEntryData> entries;
    entries.reserve(loader.world_state_zone_sounds().size());
    for (const auto& row : loader.world_state_zone_sounds()) {
        entries.push_back({
            .world_state_id = row.world_state_id,
            .world_state_value = row.world_state_value,
            .area_id = row.area_id,
            .wmo_area_id = row.wmo_area_id,
            .value = {
                .sound_ambience_id = static_cast<std::int32_t>(row.sound_ambience_id),
                .zone_music_id = static_cast<std::int32_t>(row.zone_music_id),
                .zone_intro_music_id = static_cast<std::int32_t>(row.zone_intro_music_id),
                .sound_provider_preferences_id =
                    static_cast<std::int32_t>(row.sound_provider_preferences_id),
            },
        });
    }
    return entries;
}

std::vector<openwow::audio::SoundProviderPreferenceData>
BuildSoundProviderPreferenceData(const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::SoundProviderPreferenceData> entries;
    entries.reserve(loader.sound_provider_preferences().size());
    for (const auto& row : loader.sound_provider_preferences()) {
        entries.push_back({
            .id = row.id,
            .description = std::string(row.description),
            .flags = row.flags,
            .room_flags = row.room_flags,
            .decay_time = row.decay_time,
            .environment_size = row.environment_size,
            .environment_diffusion = row.environment_diffusion,
            .room = row.room,
            .room_hf = row.room_hf,
            .decay_hf_ratio = row.decay_hf_ratio,
            .reflections = row.reflections,
            .reflections_delay = row.reflections_delay,
            .reverb = row.reverb,
            .reverb_delay = row.reverb_delay,
            .room_rolloff_factor = row.room_rolloff_factor,
            .air_absorption_hf = row.air_absorption_hf,
            .room_lf = row.room_lf,
            .decay_lf_ratio = row.decay_lf_ratio,
            .echo_time = row.echo_time,
            .echo_depth = row.echo_depth,
            .modulation_time = row.modulation_time,
            .modulation_depth = row.modulation_depth,
            .hf_reference = row.hf_reference,
            .lf_reference = row.lf_reference,
        });
    }
    return entries;
}

std::vector<openwow::audio::LiquidTypeSoundData>
BuildLiquidTypeSoundData(const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::LiquidTypeSoundData> entries;
    entries.reserve(loader.liquid_type().size());
    for (const auto& row : loader.liquid_type()) {
        entries.push_back({
            .liquid_type_id = row.id,
            .flags = row.flags,
            .sound_kit_id = row.sound_id,
        });
    }
    return entries;
}

std::pair<std::vector<openwow::audio::ZoneMusicTableEntryData>, std::uint32_t>
BuildZoneMusicData(const openwow::data::dbc::DbcLoader& loader) {
    std::vector<openwow::audio::ZoneMusicTableEntryData> entries;
    entries.reserve(loader.zone_music().size());
    std::uint32_t upper_bound = 0;
    for (const auto& row : loader.zone_music()) {
        entries.push_back({
            .id = row.id,
            .sound_kit_ids = {
                static_cast<std::int32_t>(row.sounds[0]),
                static_cast<std::int32_t>(row.sounds[1]),
            },
            .repeat_delay_min_ms = row.silence_interval_min,
            .repeat_delay_max_ms = row.silence_interval_max,
        });
        if (row.id >= upper_bound && row.id != std::numeric_limits<std::uint32_t>::max()) {
            upper_bound = row.id + 1;
        }
    }
    return {std::move(entries), upper_bound};
}

std::vector<openwow::audio::ZoneIntroMusicTableEntryData>
BuildZoneIntroMusicData(const openwow::data::dbc::DbcLoader& loader) {
    constexpr std::uint32_t kMillisecondsPerMinute = 60'000u;
    std::vector<openwow::audio::ZoneIntroMusicTableEntryData> entries;
    entries.reserve(loader.zone_intro_music_table().size());
    for (const auto& row : loader.zone_intro_music_table()) {
        entries.push_back({
            .id = row.id,
            .sound_kit_id = static_cast<std::int32_t>(row.sound_id),
            .priority = row.priority,
            .min_delay_ms = row.min_delay_minutes * kMillisecondsPerMinute,
        });
    }
    return entries;
}

std::vector<std::pair<std::string, std::vector<openwow::audio::DspFilterNode>>>
BuildSoundFilterData(const openwow::data::dbc::DbcLoader& loader) {
    struct OrderedNode {
        std::uint32_t order_index{0};
        openwow::audio::DspFilterNode node{};
    };

    std::unordered_map<std::uint32_t, std::vector<OrderedNode>> nodes_by_filter;
    for (const auto& row : loader.sound_filter_elem()) {
        if (row.filter_type > static_cast<std::uint32_t>(openwow::audio::DspEffectType::kVolume)) {
            continue;
        }
        nodes_by_filter[row.sound_filter_id].push_back({
            .order_index = row.order_index,
            .node = openwow::audio::DspFilterNode(
                static_cast<openwow::audio::DspEffectType>(row.filter_type),
                std::vector<float>(row.params.begin(), row.params.end()),
                row.params[0] == 0.0f),
        });
    }

    std::vector<std::pair<std::string, std::vector<openwow::audio::DspFilterNode>>> filters;
    filters.reserve(loader.sound_filter().size());
    for (const auto& row : loader.sound_filter()) {
        auto& ordered_nodes = nodes_by_filter[row.id];
        std::stable_sort(ordered_nodes.begin(), ordered_nodes.end(),
                         [](const OrderedNode& lhs, const OrderedNode& rhs) {
                             return lhs.order_index < rhs.order_index;
                         });
        std::vector<openwow::audio::DspFilterNode> nodes;
        nodes.reserve(ordered_nodes.size());
        for (auto& ordered : ordered_nodes) {
            nodes.push_back(std::move(ordered.node));
        }
        filters.emplace_back(std::string(row.name), std::move(nodes));
    }
    return filters;
}

using EmoteSoundRow =
    std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;

std::pair<std::vector<EmoteSoundRow>, std::uint32_t> BuildEmoteSoundData(
    const openwow::data::dbc::DbcLoader& loader) {
    std::vector<EmoteSoundRow> entries;
    entries.reserve(loader.emotes_text_sound().size());
    std::uint32_t upper_bound = 0;
    bool has_entries = false;
    for (const auto& row : loader.emotes_text_sound()) {
        entries.emplace_back(
            row.emotes_text_id, row.race_id, row.sex_id, row.sound_id);
        upper_bound = std::max(upper_bound, row.emotes_text_id);
        has_entries = true;
    }
    if (has_entries && upper_bound != std::numeric_limits<std::uint32_t>::max()) {
        ++upper_bound;
    }
    return {std::move(entries), upper_bound};
}

}

namespace openwow::audio::detail {

void SetDbcLoaderForAudio(const ::openwow::data::dbc::DbcLoader* loader) {
    g_dbc_loader = loader;
}

const ::openwow::data::dbc::DbcLoader* GetDbcLoaderForAudio() {
    return g_dbc_loader;
}

}

namespace openwow::audio {

std::optional<ResolvedSoundEntry> ResolveSoundEntry(
    const openwow::data::dbc::SoundEntriesEntry& entry) {
    const auto variant = ChooseWeightedVariant(entry);
    if (!variant.has_value()) {
        return std::nullopt;
    }

    ResolvedSoundEntry result;
    result.path = BuildSoundEntryPath(entry, variant->file);
    result.volume = entry.volume;
    result.sound_type = entry.sound_type;
    result.flags = entry.flags;
    return result;
}

std::optional<ResolvedSoundEntry> ResolveSoundEntry(std::uint32_t soundKitId) {
    if (!g_dbc_loader || soundKitId == 0) return std::nullopt;

    const auto* entry = g_dbc_loader->sound_entries().LookupEntry(soundKitId);
    return entry ? ResolveSoundEntry(*entry) : std::nullopt;
}

void PublishSoundRuntimeDbcData(
    SoundRuntime& runtime,
    const openwow::data::dbc::DbcLoader& loader) {
    runtime.LoadSoundEntries(BuildSoundKitData(loader));
    runtime.LoadSoundEntriesAdvanced(BuildAdvancedSoundData(loader));
    runtime.LoadWorldStateZoneSounds(BuildWorldStateZoneSoundData(loader));
    runtime.LoadSoundAmbienceTable(BuildSoundAmbienceData(loader));
    runtime.LoadChunkAudioBindings(BuildChunkAudioBindingData(loader));
    runtime.LoadSoundProviderPreferences(BuildSoundProviderPreferenceData(loader));
    runtime.LoadLiquidTypeSoundData(BuildLiquidTypeSoundData(loader));
    auto [zone_music, zone_music_upper_bound] = BuildZoneMusicData(loader);
    runtime.LoadZoneMusicTable(zone_music, zone_music_upper_bound);
    runtime.LoadZoneIntroMusicTable(BuildZoneIntroMusicData(loader));
    runtime.LoadSoundFilters(BuildSoundFilterData(loader));
    auto [emote_sounds, emote_text_upper_bound] = BuildEmoteSoundData(loader);
    runtime.LoadEmotesTextSound(emote_sounds, emote_text_upper_bound);
}

}

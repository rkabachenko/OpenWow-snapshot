#include "openwow/render/glue/glue_ghost_lighting.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/world/environment/sky.h"

#include <algorithm>
#include <cmath>

namespace openwow::render::glue {
namespace {

std::uint8_t InterpolateColorByte(const std::uint8_t from,
                                  const std::uint8_t to,
                                  const float factor) {
  const float value = static_cast<float>(from) +
                      (static_cast<float>(to) - static_cast<float>(from)) * factor;
  return static_cast<std::uint8_t>(
      std::clamp(std::nearbyint(value), 0.0f, 255.0f));
}

std::uint32_t SamplePackedColor(const data::dbc::LightIntBandEntry& band,
                                const std::uint32_t time_tick) {
  const std::uint32_t entry_count = std::min(band.num_entries, 16u);
  if (entry_count == 0u) {
    return 0xFF000000u;
  }
  if (entry_count == 1u) {
    return band.values[0];
  }

  std::uint32_t previous = entry_count - 1u;
  std::uint32_t next = 0u;
  for (std::uint32_t index = 0u; index < entry_count; ++index) {
    if (time_tick < band.times[index]) {
      next = index;
      previous = index == 0u ? entry_count - 1u : index - 1u;
      break;
    }
    if (index + 1u == entry_count) {
      previous = index;
      next = 0u;
    }
  }

  const std::uint32_t previous_time = band.times[previous];
  std::uint32_t next_time = band.times[next];
  std::uint32_t sample_time = time_tick;
  if (next_time <= previous_time) {
    next_time += 2880u;
    if (sample_time < previous_time) {
      sample_time += 2880u;
    }
  }

  const std::uint32_t duration = next_time - previous_time;
  const float factor = duration == 0u
                           ? 0.0f
                           : static_cast<float>(sample_time - previous_time) /
                                 static_cast<float>(duration);
  const std::uint32_t from = band.values[previous];
  const std::uint32_t to = band.values[next];
  const auto channel = [&](const int shift) {
    return static_cast<std::uint32_t>(InterpolateColorByte(
               static_cast<std::uint8_t>((from >> shift) & 0xFFu),
               static_cast<std::uint8_t>((to >> shift) & 0xFFu), factor))
           << shift;
  };
  return 0xFF000000u | channel(16) | channel(8) | channel(0);
}

std::array<float, 3> PackedArgbToRgb(const std::uint32_t color) {
  constexpr float kByteToFloat = 1.0f / 255.0f;
  return {
      static_cast<float>((color >> 16) & 0xFFu) * kByteToFloat,
      static_cast<float>((color >> 8) & 0xFFu) * kByteToFloat,
      static_cast<float>(color & 0xFFu) * kByteToFloat,
  };
}

}

ModelRenderCallbackGhostLightSample SampleCharacterSelectGhostLight(
    const data::dbc::LightIntBandEntry& diffuse_band,
    const data::dbc::LightIntBandEntry& ambient_band,
    const std::uint32_t time_tick) {
  ModelRenderCallbackGhostLightSample sample;
  sample.enabled = true;
  sample.ambient_rgb = PackedArgbToRgb(SamplePackedColor(ambient_band, time_tick));
  sample.diffuse_rgb = PackedArgbToRgb(SamplePackedColor(diffuse_band, time_tick));
  return sample;
}

std::optional<ModelRenderCallbackGhostLightSample>
ResolveCharacterSelectGhostLight(const data::dbc::DbcLoader* dbc_loader) {
  if (dbc_loader == nullptr) {
    return std::nullopt;
  }
  if (dbc_loader->light_params().LookupEntry(
          kCharacterSelectGhostLightParamsId) == nullptr) {
    return std::nullopt;
  }

  const std::uint32_t diffuse_band_id = LightIntBandIdForSkyColorSlot(
      kCharacterSelectGhostLightParamsId, world::SkyColorSlot::kGlobalDiffuse);
  const std::uint32_t ambient_band_id = LightIntBandIdForSkyColorSlot(
      kCharacterSelectGhostLightParamsId, world::SkyColorSlot::kGlobalAmbient);
  const auto* diffuse_band = dbc_loader->light_int_band().LookupEntry(diffuse_band_id);
  const auto* ambient_band = dbc_loader->light_int_band().LookupEntry(ambient_band_id);
  if (diffuse_band == nullptr || ambient_band == nullptr) {
    return std::nullopt;
  }

  return SampleCharacterSelectGhostLight(*diffuse_band, *ambient_band);
}

}

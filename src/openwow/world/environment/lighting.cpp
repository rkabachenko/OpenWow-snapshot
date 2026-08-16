
#include "openwow/world/environment/lighting.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/environment/day_night.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace openwow::world {

namespace {

struct ActiveLocalLight {
  float distance_squared{0.0f};
  const LightBandData *light{nullptr};
};

constexpr float kCoLocatedLightSeparation = 1.0f / 3.0f;

constexpr float kAdvancedFogMinimumFogEnd = 27.777779f;

float Distance3D(const LightBandData &lhs, const LightBandData &rhs) {
  const float dx = lhs.x - rhs.x;
  const float dy = lhs.y - rhs.y;
  const float dz = lhs.z - rhs.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

class LocalLightBlendQueue {
public:
  void Push(const ActiveLocalLight &entry) {
    if (heap_.empty()) {
      heap_.push_back({});
    }

    heap_.push_back(entry);
    std::size_t index = heap_.size() - 1;
    while (index > 1) {
      const std::size_t parent = index / 2;
      if (!Precedes(entry, heap_[parent])) {
        break;
      }

      heap_[index] = heap_[parent];
      index = parent;
    }

    heap_[index] = entry;
  }

  [[nodiscard]] bool Empty() const {
    return heap_.size() <= 1;
  }

  ActiveLocalLight Pop() {
    const ActiveLocalLight root = heap_[1];
    const ActiveLocalLight last = heap_.back();
    heap_.pop_back();
    if (heap_.size() <= 1) {
      return root;
    }

    std::size_t hole = 1;
    const std::size_t entry_count = heap_.size() - 1;
    while (hole <= entry_count / 2) {
      std::size_t child = hole * 2;
      if (child < entry_count && Precedes(heap_[child + 1], heap_[child])) {
        ++child;
      }
      if (Precedes(last, heap_[child])) {
        break;
      }

      heap_[hole] = heap_[child];
      hole = child;
    }

    heap_[hole] = last;
    return root;
  }

private:
  static bool Precedes(const ActiveLocalLight &lhs, const ActiveLocalLight &rhs) {

    if (Distance3D(*lhs.light, *rhs.light) <= kCoLocatedLightSeparation) {
      return lhs.light->falloff_start >= rhs.light->falloff_start;
    }
    return lhs.distance_squared >= rhs.distance_squared;
  }

  std::vector<ActiveLocalLight> heap_;
};

float DistanceSquared3D(float x1, float y1, float z1, float x2, float y2, float z2) {
  const float dx = x1 - x2;
  const float dy = y1 - y2;
  const float dz = z1 - z2;
  return dx * dx + dy * dy + dz * dz;
}

float Clamp01(float v) {
  return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

float Clamp(float value, float minimum, float maximum) {
  return std::max(minimum, std::min(value, maximum));
}

float LerpScalar(const float a, const float b, const float t) {
  return a + (b - a) * t;
}

std::uint8_t RoundColorByte(const float value) {
  return static_cast<std::uint8_t>(
      Clamp(std::nearbyint(value), 0.0f, 255.0f));
}

int RoundToNearestAfterHalfSubtraction(const float value) {
  return static_cast<int>(std::nearbyint(value - 0.5f));
}

int FloorDivideBy256(const int value) {
  return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

std::uint32_t BlendPackedLightBandRgbRetail(
    const std::uint32_t destination, const std::uint32_t source,
    const float weight) {
  const float clamped_weight = Clamp01(weight);

  const auto weight_byte = std::isnan(clamped_weight)
                               ? 0u
                               : static_cast<std::uint32_t>(std::clamp(
                                     std::nearbyint(clamped_weight * 255.0f),
                                     0.0f, 255.0f));
  if (weight_byte == 0u) {
    return destination;
  }
  if (weight_byte == 255u) {
    return (destination & 0xFF000000u) | (source & 0x00FFFFFFu);
  }
  const auto blend_channel = [&](const int shift) -> std::uint32_t {
    const int dest = static_cast<int>((destination >> shift) & 0xFFu);
    const int src = static_cast<int>((source >> shift) & 0xFFu);
    const int blended = dest + FloorDivideBy256(
        (src - dest) * static_cast<int>(weight_byte));
    return static_cast<std::uint32_t>(blended & 0xFF) << shift;
  };

  return (destination & 0xFF000000u) | blend_channel(16) |
         blend_channel(8) | blend_channel(0);
}

std::uint32_t BlendLocalLightBandRgbRetail(
    const std::uint32_t destination, const std::uint32_t source,
    const float weight) {

  const auto blend_channel = [&](const int shift) -> std::uint32_t {
    const float dest = static_cast<float>((destination >> shift) & 0xFFu);
    const float src = static_cast<float>((source >> shift) & 0xFFu);
    const float blended = (src < dest) ? (dest - (dest - src) * weight)
                                       : (dest + (src - dest) * weight);
    return static_cast<std::uint32_t>(
               RoundToNearestAfterHalfSubtraction(blended) & 0xFF)
           << shift;
  };

  return 0xFF000000u | blend_channel(16) | blend_channel(8) |
         blend_channel(0);
}

struct HsvColor {
  float hue{-1.0f};
  float saturation{0.0f};
  float value{0.0f};
};

std::uint8_t PackOpaqueColorByte(const float normalized) {
  return static_cast<std::uint8_t>(
      static_cast<int>(std::nearbyint(normalized * 255.0f)));
}

int FindDominantRgbChannel(const float red, const float green, const float blue) {
  if (red <= green) {
    return (green > blue) ? 1 : 2;
  }
  if (red > blue) {
    return 0;
  }
  return 2;
}

int FindWeakestRgbChannel(const float red, const float green, const float blue) {
  if (red >= green) {
    return (green <= blue) ? 1 : 2;
  }
  if (red >= blue) {
    return 2;
  }
  return 0;
}

HsvColor ConvertRgbToHsv(const float red, const float green, const float blue) {
  const int max_channel = FindDominantRgbChannel(red, green, blue);
  const int min_channel = FindWeakestRgbChannel(red, green, blue);
  const float rgb[3] = {red, green, blue};

  HsvColor hsv{};
  hsv.value = rgb[max_channel];
  hsv.saturation = (hsv.value == 0.0f) ? 0.0f : (rgb[max_channel] - rgb[min_channel]) / rgb[max_channel];
  if (hsv.saturation == 0.0f) {
    hsv.hue = -1.0f;
    return hsv;
  }

  const float delta = rgb[max_channel] - rgb[min_channel];
  switch (max_channel) {
  case 0:
    hsv.hue = (green - blue) / delta;
    break;
  case 1:
    hsv.hue = (blue - red) / delta + 2.0f;
    break;
  default:
    hsv.hue = (red - green) / delta + 4.0f;
    break;
  }

  hsv.hue *= 60.0f;
  if (hsv.hue < 0.0f) {
    hsv.hue += 360.0f;
  }
  return hsv;
}

std::array<float, 3> ConvertHsvToRgb(const HsvColor &hsv) {
  if (hsv.saturation == 0.0f) {
    return {hsv.value, hsv.value, hsv.value};
  }

  float hue = hsv.hue;
  if (hue >= 360.0f) {
    hue -= 360.0f;
  }

  const float scaled_hue = hue / 60.0f;
  int sector = RoundToNearestAfterHalfSubtraction(scaled_hue);
  if (sector > 5) {
    sector = 5;
  }

  const float fractional = scaled_hue - static_cast<float>(sector);
  const float saturation = (hsv.saturation >= 1.0f) ? 1.0f : hsv.saturation;
  const float one_minus_fractional = 1.0f - fractional;
  const float p = (1.0f - saturation) * hsv.value;
  const float q = (1.0f - saturation * fractional) * hsv.value;
  const float t = (1.0f - saturation * one_minus_fractional) * hsv.value;

  switch (sector) {
  case 0:
    return {hsv.value, t, p};
  case 1:
    return {q, hsv.value, p};
  case 2:
    return {p, hsv.value, t};
  case 3:
    return {p, q, hsv.value};
  case 4:
    return {t, p, hsv.value};
  case 5:
    return {hsv.value, p, q};
  default:
    return {0.0f, 0.0f, 0.0f};
  }
}

std::size_t ResolvePrimaryLightParamSlot(
    const bool use_underwater_light_params) {
  return use_underwater_light_params ? 1u : 0u;
}

std::size_t ResolveWeatherOverlayLightParamSlot(
    const bool use_underwater_light_params) {
  return ResolvePrimaryLightParamSlot(use_underwater_light_params) + 2u;
}

const LightBandData::LightParamBandSet &SelectBaseLightParamBandSet(
    const LightBandData &light, const bool use_underwater_light_params) {
  return light.light_param_band_sets[
      ResolvePrimaryLightParamSlot(use_underwater_light_params)];
}

const LightBandData::LightParamBandSet *
SelectScreenEffectOverrideBandSet(const LightBandData &light,
                                  const std::optional<std::uint8_t> light_param_slot_override) {
  if (!light_param_slot_override.has_value()) {
    return nullptr;
  }

  const auto override_index = static_cast<std::size_t>(*light_param_slot_override);
  if (override_index >= light.light_param_band_sets.size()) {
    return nullptr;
  }

  const auto &override_set = light.light_param_band_sets[override_index];
  if (override_set.light_params_id == 0) {
    return nullptr;
  }

  return &override_set;
}

constexpr float kMinimumLocalLightFalloffEnd = 3.0f;

bool LocalLightPassesFalloffCull(const LightBandData &light) {
  return light.falloff_end >= kMinimumLocalLightFalloffEnd;
}

float ComputeLocalLightBlendWeight(float distance_squared, const LightBandData &light) {
  if (light.falloff_end < 0.001f) {
    return -1.0f;
  }

  const float falloff_end_squared = light.falloff_end * light.falloff_end;
  if (distance_squared >= falloff_end_squared) {
    return -1.0f;
  }

  const float distance = std::sqrt(distance_squared);
  if (distance <= light.falloff_start) {
    return 1.0f;
  }

  const float weight =
      1.0f - (distance - light.falloff_start) / (light.falloff_end - light.falloff_start);
  return Clamp01(weight);
}

struct CircularBandInterval {
  std::size_t current{0u};
  std::size_t next{0u};
  float factor{0.0f};
};

template <std::size_t Capacity>
CircularBandInterval ResolveCircularBandInterval(
    const std::uint32_t requested_count,
    const std::array<std::uint32_t, Capacity> &times,
    const std::uint32_t time_tick) {
  const auto count =
      static_cast<std::size_t>(std::min<std::uint32_t>(requested_count, Capacity));
  if (count == 0u) {
    return {};
  }

  for (std::size_t current = 0; current < count; ++current) {
    const std::size_t next = (current + 1u) % count;
    const std::uint32_t start = times[current];
    std::uint32_t end = times[next];
    std::uint32_t tick = time_tick;

    if (start < end) {
      if (tick < start || tick > end) {
        continue;
      }
    } else {
      if (tick < start && tick > end) {
        continue;
      }
      end += 2880u;
      if (tick < start) {
        tick += 2880u;
      }
    }

    return {
        .current = current,
        .next = next,
        .factor = static_cast<float>(tick - start) /
                  static_cast<float>(end - start),
    };
  }

  return {.current = 0u, .next = count == 1u ? 0u : 1u, .factor = 0.0f};
}

}

void WorldLighting::LoadForMap(const data::dbc::DbcLoader &dbc, uint32_t map_id) {
  lights_.clear();
  zone_skyboxes_by_id_.clear();
  global_light_index_ = -1;

  const auto &light_store = dbc.light();
  const auto &int_band_store = dbc.light_int_band();
  const auto &float_band_store = dbc.light_float_band();
  const auto &light_params_store = dbc.light_params();
  const auto &light_skybox_store = dbc.light_skybox();

  if (light_store.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "WorldLighting: Light.dbc is empty");
    return;
  }

  const auto build_band_set = [&](const uint32_t params_id,
                                  LightBandData::LightParamBandSet &band_set) {
    band_set.light_params_id = params_id;
    if (params_id != 0) {
      if (const auto *params_entry = light_params_store.LookupEntry(params_id)) {
        band_set.highlight_sky = params_entry->highlight_sky;
        band_set.zone_skybox.light_skybox_id = params_entry->skybox_id;
        band_set.cloud_type_id = params_entry->cloud_type_id;
        band_set.glow = params_entry->glow;
        band_set.water_shallow_alpha = params_entry->water_shallow_alpha;
        band_set.water_deep_alpha = params_entry->water_deep_alpha;
        band_set.ocean_shallow_alpha = params_entry->ocean_shallow_alpha;
        band_set.ocean_deep_alpha = params_entry->ocean_deep_alpha;
        if (params_entry->skybox_id != 0) {
          if (const auto *skybox_entry = light_skybox_store.LookupEntry(params_entry->skybox_id)) {
            band_set.zone_skybox.model_path = std::string(skybox_entry->name);
            band_set.zone_skybox.flags = skybox_entry->flags;
            zone_skyboxes_by_id_[band_set.zone_skybox.light_skybox_id] = band_set.zone_skybox;
          }
        }
      }

      for (int color_slot = 0; color_slot < 18; ++color_slot) {
        const auto slot = static_cast<SkyColorSlot>(color_slot);
        const uint32_t band_id = LightIntBandIdForSkyColorSlot(params_id, slot);
        const auto *band_entry = int_band_store.LookupEntry(band_id);
        if (!band_entry) {
          continue;
        }

        auto &band = band_set.color_bands[color_slot];
        band.entry_count = std::min(band_entry->num_entries, 16u);
        for (uint32_t keyframe_index = 0; keyframe_index < band.entry_count; ++keyframe_index) {
          band.times[keyframe_index] = band_entry->times[keyframe_index];
          band.values[keyframe_index] = band_entry->values[keyframe_index];
        }
      }

      for (std::size_t float_band_index = 0; float_band_index < band_set.float_bands.size();
           ++float_band_index) {
        const uint32_t band_id = params_id * 6 - 5 + static_cast<uint32_t>(float_band_index);
        const auto *fb = float_band_store.LookupEntry(band_id);
        if (!fb) {
          continue;
        }

        auto &float_band = band_set.float_bands[float_band_index];
        float_band.entry_count = std::min(fb->num_entries, 16u);
        for (uint32_t keyframe_index = 0; keyframe_index < float_band.entry_count;
             ++keyframe_index) {
          float_band.times[keyframe_index] = fb->times[keyframe_index];
          constexpr float kDatabaseDistanceToWorld = 1.0f / 36.0f;
          float_band.values[keyframe_index] =
              float_band_index == static_cast<std::size_t>(LightFloatBandSlot::kFogDistance)
                  ? fb->values[keyframe_index] * kDatabaseDistanceToWorld
                  : fb->values[keyframe_index];
        }
      }
    }
  };

  const auto cache_light = [&](const data::dbc::LightEntry &le) {
    LightBandData lbd;
    lbd.light_id = le.id;
    lbd.map_id = le.map_id;
    lbd.x = le.x;
    lbd.y = le.y;
    lbd.z = le.z;
    lbd.falloff_start = le.falloff_start;
    lbd.falloff_end = le.falloff_end;
    lbd.light_params = le.light_params;

    for (std::size_t slot_index = 0; slot_index < lbd.light_params.size();
         ++slot_index) {
      build_band_set(le.light_params[slot_index],
                     lbd.light_param_band_sets[slot_index]);
    }
    lights_.push_back(std::move(lbd));
  };

  liquid_light_param_band_sets_.clear();
  for (const auto &liquid_type : dbc.liquid_type()) {
    if (liquid_type.light_id == 0u ||
        liquid_light_param_band_sets_.contains(liquid_type.light_id)) {
      continue;
    }
    build_band_set(liquid_type.light_id,
                   liquid_light_param_band_sets_[liquid_type.light_id]);
  }

  const data::dbc::LightEntry *default_light = nullptr;
  std::vector<const data::dbc::LightEntry *> local_lights;
  local_lights.reserve(light_store.size());
  for (const auto &entry : light_store) {
    if (entry.map_id != map_id) {
      continue;
    }

    if (entry.x == 0.0f && entry.y == 0.0f && entry.z == 0.0f) {
      default_light = &entry;
    } else {
      local_lights.push_back(&entry);
    }
  }

  if (default_light == nullptr) {
    default_light = light_store.LookupEntry(1u);
  }

  lights_.reserve(local_lights.size() + (default_light != nullptr ? 1u : 0u));
  if (default_light != nullptr) {
    global_light_index_ = 0;
    cache_light(*default_light);
  }
  for (const auto *entry : local_lights) {
    cache_light(*entry);
  }

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "WorldLighting: loaded " + std::to_string(lights_.size()) +
          " lights for map " + std::to_string(map_id) + " (global light id=" +
          (default_light != nullptr ? std::to_string(default_light->id)
                                    : std::string("none")) +
          ")");
}

namespace {

[[nodiscard]] std::uint32_t LiquidLightParamsId(
    const std::optional<WorldLighting::LiquidDarkeningState> &darkening) {
  return darkening.has_value() ? darkening->light_params_id : 0u;
}

}

WorldLighting::InterpolatedLightState
WorldLighting::BuildResolvedLightState(
    const float x, const float y, const float z, const float time_of_day,
    const std::optional<std::uint8_t> light_param_slot_override,
    const float weather_blend_factor, const bool use_underwater_light_params,
    const bool suppress_local_lights,
    const std::uint32_t liquid_light_params_id) const {
  InterpolatedLightState result_state;
  static_assert(std::is_trivially_copyable_v<InterpolatedLightState>);
  std::memset(&result_state, 0xFF, sizeof(result_state));
  result_state.packed_colors.fill(0xFFFFFFFFu);
  result_state.packed_colors[0] = 0xFF404040u;
  result_state.fog_distance = 1.0e10f;
  result_state.fog_multiplier = 0.5f;
  result_state.advanced_fog_density = 4.0f;
  result_state.highlight_sky = 0.0f;

  result_state.glow = kDefaultSceneVisibility;
  result_state.water_shallow_alpha = 0.5f;
  result_state.water_deep_alpha = 1.0f;
  result_state.ocean_shallow_alpha = 0.75f;
  result_state.ocean_deep_alpha = 1.0f;
  for (auto &skybox : result_state.accumulated_skyboxes) {
    skybox.light_skybox_id = 0u;
  }
  result_state.cloud_type_id = 0u;

  if (lights_.empty()) {
    return result_state;
  }

  const float clamped_time = Clamp01(time_of_day);

  const uint32_t time_tick =
      std::isnan(clamped_time)
          ? 0u
          : static_cast<uint32_t>(
                RoundToNearestAfterHalfSubtraction(clamped_time * 2880.0f));

  if (liquid_light_params_id != 0u) {
    const auto band_set =
        liquid_light_param_band_sets_.find(liquid_light_params_id);
    if (band_set != liquid_light_param_band_sets_.end()) {
      return BuildInterpolatedLightState(band_set->second, time_tick,
                                         advanced_fog_inputs_);
    }
  }

  const LightBandData *global_light = nullptr;

  if (global_light_index_ >= 0) {
    global_light = &lights_[static_cast<size_t>(global_light_index_)];
    result_state = BuildPreparedLightState(
        *global_light, time_tick, weather_blend_factor,
        use_underwater_light_params, advanced_fog_inputs_);
  }

  if (!suppress_local_lights) {
    LocalLightBlendQueue local_lights;
    for (size_t i = 0; i < lights_.size(); ++i) {
      if (static_cast<int>(i) == global_light_index_) {
        continue;
      }

      const auto &light = lights_[i];
      if (!LocalLightPassesFalloffCull(light)) {
        continue;
      }

      const float distance_squared = DistanceSquared3D(x, y, z, light.x, light.y, light.z);
      if (ComputeLocalLightBlendWeight(distance_squared, light) < 0.0f) {
        continue;
      }

      local_lights.Push({distance_squared, &light});
    }

    while (!local_lights.Empty()) {
      const ActiveLocalLight active_light = local_lights.Pop();
      const auto &light = *active_light.light;
      ApplyDistanceCappedLocalLightBlend(result_state, light, active_light.distance_squared,
                                         time_tick, weather_blend_factor,
                                         use_underwater_light_params, 1.0f,
                                         advanced_fog_inputs_);
    }
  }

  if (global_light != nullptr) {
    if (const auto *override_band_set =
            SelectScreenEffectOverrideBandSet(*global_light, light_param_slot_override);
        override_band_set != nullptr) {
      const InterpolatedLightState override_state =
          BuildInterpolatedLightState(*override_band_set, time_tick,
                                      advanced_fog_inputs_);
      result_state = ApplyScreenEffectLightParamOverride(result_state, override_state);
    }
  }

  return result_state;
}

SkyColors WorldLighting::GetSkyColors(float x, float y, float z, float time_of_day,
                                     std::optional<std::uint8_t> light_param_slot_override,
                                     const float weather_blend_factor,
                                     const bool use_underwater_light_params,
                                     std::optional<LiquidDarkeningState> liquid_darkening,
                                     const bool suppress_local_lights) const {
  const InterpolatedLightState result_state = BuildResolvedLightState(
      x, y, z, time_of_day, light_param_slot_override, weather_blend_factor,
      use_underwater_light_params, suppress_local_lights,
      LiquidLightParamsId(liquid_darkening));

  return BuildSkyColors(result_state, liquid_darkening);
}

WorldLighting::ResolvedEnvironment WorldLighting::ResolveEnvironment(
    const float x, const float y, const float z, const float time_of_day,
    const std::optional<std::uint8_t> light_param_slot_override,
    const float weather_blend_factor, const bool use_underwater_light_params,
    const std::optional<LiquidDarkeningState> liquid_darkening,
    const bool suppress_local_lights) const {
  const InterpolatedLightState state = BuildResolvedLightState(
      x, y, z, time_of_day, light_param_slot_override, weather_blend_factor,
      use_underwater_light_params, suppress_local_lights,
      LiquidLightParamsId(liquid_darkening));

  ResolvedEnvironment result{};
  result.sky = BuildSkyColors(state, liquid_darkening);
  result.fog.end_distance = result.sky.fog_distance;
  result.fog.start_distance =
      result.fog.end_distance * result.sky.fog_multiplier;
  result.fog.density = state.advanced_fog_density;
  result.fog.packed_argb =
      result.sky.colors[static_cast<std::size_t>(SkyColorSlot::kSkyFog)];
  result.fog.color = {
      static_cast<float>((result.fog.packed_argb >> 16u) & 0xFFu) / 255.0f,
      static_cast<float>((result.fog.packed_argb >> 8u) & 0xFFu) / 255.0f,
      static_cast<float>(result.fog.packed_argb & 0xFFu) / 255.0f,
      1.0f,
  };

  for (std::size_t index = 0; index < state.accumulated_skyboxes.size(); ++index) {
    const auto &source = state.accumulated_skyboxes[index];
    const auto skybox = zone_skyboxes_by_id_.find(source.light_skybox_id);
    if (source.light_skybox_id == 0u || source.weight <= 0.0f ||
        skybox == zone_skyboxes_by_id_.end() || skybox->second.model_path.empty()) {
      continue;
    }
    result.skybox_slots[index] = {
        .entry = skybox->second,
        .alpha = Clamp01(source.weight),
        .active = true,
    };
  }

  if (global_light_index_ >= 0 &&
      static_cast<std::size_t>(global_light_index_) < lights_.size()) {
    const auto &global = lights_[static_cast<std::size_t>(global_light_index_)];
    if (const auto *override_set = SelectScreenEffectOverrideBandSet(
            global, light_param_slot_override);
        override_set != nullptr &&
        override_set->zone_skybox.light_skybox_id != 0u &&
        !override_set->zone_skybox.model_path.empty()) {
      result.primary_skybox = override_set->zone_skybox;
      return result;
    }
  }

  const LightBandData *selected = nullptr;
  float best_weight = -1.0f;
  if (global_light_index_ >= 0 &&
      static_cast<std::size_t>(global_light_index_) < lights_.size()) {
    selected = &lights_[static_cast<std::size_t>(global_light_index_)];
    best_weight = 0.0f;
  }
  if (!suppress_local_lights) {
    for (const auto &light : lights_) {
      if (&light == selected) {
        continue;
      }
      const float weight = ComputeLocalLightBlendWeight(
          DistanceSquared3D(x, y, z, light.x, light.y, light.z), light);
      if (weight > best_weight) {
        selected = &light;
        best_weight = weight;
      }
    }
  }
  if (selected != nullptr) {
    const auto &band_set =
        SelectBaseLightParamBandSet(*selected, use_underwater_light_params);
    if (band_set.zone_skybox.light_skybox_id != 0u &&
        !band_set.zone_skybox.model_path.empty()) {
      result.primary_skybox = band_set.zone_skybox;
    }
  }
  return result;
}

SkyColors WorldLighting::BuildSkyColors(
    const InterpolatedLightState &state,
    const std::optional<LiquidDarkeningState> liquid_darkening) {
  SkyColors result{};
  for (std::size_t slot = 0; slot < state.packed_colors.size(); ++slot) {
    result.colors[slot] = LightBandColorToArgb(state.packed_colors[slot]);
  }
  result.fog_distance = state.fog_distance;
  result.fog_multiplier = state.fog_multiplier;
  result.highlight_sky = state.highlight_sky;
  result.scene_visibility = state.glow;
  result.water_shallow_alpha = state.water_shallow_alpha;
  result.water_deep_alpha = state.water_deep_alpha;
  result.ocean_shallow_alpha = state.ocean_shallow_alpha;
  result.ocean_deep_alpha = state.ocean_deep_alpha;
  if (liquid_darkening.has_value()) {
    ApplyLiquidDarkening(result, *liquid_darkening);
  }
  return result;
}

LightFogParams WorldLighting::GetFogParams(
    const float x, const float y, const float z, const float time_of_day,
    const std::optional<std::uint8_t> light_param_slot_override,
    const float weather_blend_factor,
    const bool use_underwater_light_params,
    const std::optional<LiquidDarkeningState> liquid_darkening,
    const bool suppress_local_lights) const {
  const InterpolatedLightState state = BuildResolvedLightState(
      x, y, z, time_of_day, light_param_slot_override, weather_blend_factor,
      use_underwater_light_params, suppress_local_lights,
      LiquidLightParamsId(liquid_darkening));
  const SkyColors colors = BuildSkyColors(state, liquid_darkening);

  LightFogParams result{};
  result.end_distance = colors.fog_distance;
  result.start_distance = result.end_distance * colors.fog_multiplier;
  result.density = state.advanced_fog_density;

  result.packed_argb = colors.colors[static_cast<int>(SkyColorSlot::kSkyFog)];
  result.color[0] = static_cast<float>((result.packed_argb >> 16) & 0xFF) / 255.0f;
  result.color[1] = static_cast<float>((result.packed_argb >> 8) & 0xFF) / 255.0f;
  result.color[2] = static_cast<float>(result.packed_argb & 0xFF) / 255.0f;
  result.color[3] = 1.0f;
  return result;
}

std::optional<ZoneSkyboxEntry>
WorldLighting::GetZoneSkybox(const float x, const float y, const float z,
                            const float ,
                            const std::optional<std::uint8_t> light_param_slot_override,
                            const bool use_underwater_light_params,
                            const bool suppress_local_lights) const {
  if (lights_.empty()) {
    return std::nullopt;
  }

  if (global_light_index_ >= 0 && static_cast<std::size_t>(global_light_index_) < lights_.size()) {
    const auto &global_light = lights_[static_cast<std::size_t>(global_light_index_)];
    if (const auto *override_band_set =
            SelectScreenEffectOverrideBandSet(global_light, light_param_slot_override);
        override_band_set != nullptr
        && override_band_set->zone_skybox.light_skybox_id != 0
        && !override_band_set->zone_skybox.model_path.empty()) {
      return override_band_set->zone_skybox;
    }
  }

  const LightBandData *selected_light = nullptr;
  float best_weight = -1.0f;

  if (global_light_index_ >= 0 && static_cast<std::size_t>(global_light_index_) < lights_.size()) {
    selected_light = &lights_[static_cast<std::size_t>(global_light_index_)];
    best_weight = 0.0f;
  }

  if (!suppress_local_lights) {
    for (const auto &light : lights_) {
      if (selected_light == &light) {
        continue;
      }
      const float distance_squared = DistanceSquared3D(x, y, z, light.x, light.y, light.z);
      const float weight = ComputeLocalLightBlendWeight(distance_squared, light);
      if (weight <= best_weight) {
        continue;
      }

      selected_light = &light;
      best_weight = weight;
    }
  }

  if (!selected_light) {
    return std::nullopt;
  }

  const auto &band_set =
      SelectBaseLightParamBandSet(*selected_light, use_underwater_light_params);
  if (band_set.zone_skybox.light_skybox_id == 0 || band_set.zone_skybox.model_path.empty()) {
    return std::nullopt;
  }

  return band_set.zone_skybox;
}

WorldLighting::ZoneSkyboxSlots
WorldLighting::GetZoneSkyboxSlots(
    const float x, const float y, const float z, const float time_of_day,
    const std::optional<std::uint8_t> light_param_slot_override,
    const float weather_blend_factor, const bool use_underwater_light_params,
    const bool suppress_local_lights) const {
  ZoneSkyboxSlots slots{};
  const InterpolatedLightState state =
      BuildResolvedLightState(x, y, z, time_of_day, light_param_slot_override,
                              weather_blend_factor, use_underwater_light_params,
                              suppress_local_lights);

  for (std::size_t index = 0; index < state.accumulated_skyboxes.size(); ++index) {
    const auto &source = state.accumulated_skyboxes[index];
    if (source.light_skybox_id == 0u || source.weight <= 0.0f) {
      continue;
    }

    const auto skybox = zone_skyboxes_by_id_.find(source.light_skybox_id);
    if (skybox == zone_skyboxes_by_id_.end() || skybox->second.model_path.empty()) {
      continue;
    }

    slots[index] = ZoneSkyboxSlot{
        .entry = skybox->second,
        .alpha = Clamp01(source.weight),
        .active = true,
    };
  }

  return slots;
}

void WorldLighting::ClearInterpolatedLightState(InterpolatedLightState &state) {
  static_assert(std::is_trivially_copyable_v<InterpolatedLightState>);
  std::memset(&state, 0, sizeof(state));
}

void WorldLighting::ApplyAdvancedFog(InterpolatedLightState &state,
                                     const AdvancedFogInputs &advanced_fog) {
  if (!advanced_fog.armed) {
    return;
  }

  if (state.fog_distance >= kAdvancedFogMinimumFogEnd) {

    const float fog_band_span =
        state.fog_distance - state.fog_distance * state.fog_multiplier;
    state.advanced_fog_density = openwow::game::DayNight_ComputeAdvancedFogDensity(
        fog_band_span, advanced_fog.camera_far_clip);
    state.fog_distance = advanced_fog.camera_far_clip;
  }

  if (state.fog_multiplier < 0.0f) {
    state.fog_multiplier = 0.0f;
  }
}

WorldLighting::InterpolatedLightState
WorldLighting::BuildPreparedLightState(const LightBandData &light, const uint32_t time_tick,
                                      const float weather_blend_factor,
                                      const bool use_underwater_light_params,
                                      const AdvancedFogInputs &advanced_fog) {
  InterpolatedLightState state = BuildInterpolatedLightState(
      SelectBaseLightParamBandSet(light, use_underwater_light_params),
      time_tick, advanced_fog);
  if (weather_blend_factor <= 0.0f) {
    return state;
  }

  const auto &overlay_band_set =
      light.light_param_band_sets[
          ResolveWeatherOverlayLightParamSlot(use_underwater_light_params)];
  const InterpolatedLightState overlay_state =
      BuildInterpolatedLightState(overlay_band_set, time_tick, advanced_fog);
  ApplyWeatherLightBlend(state, overlay_state, weather_blend_factor);
  return state;
}

WorldLighting::InterpolatedLightState
WorldLighting::BuildInterpolatedLightState(const LightBandData::LightParamBandSet &band_set,
                                          const uint32_t time_tick,
                                          const AdvancedFogInputs &advanced_fog) {
  InterpolatedLightState state;
  ClearInterpolatedLightState(state);

  for (std::size_t slot = 0; slot < state.packed_colors.size(); ++slot) {
    state.packed_colors[slot] =
        InterpolateBand(band_set.color_bands[slot], time_tick);
  }

  state.fog_distance = InterpolateFloatBand(
      band_set.float_bands[static_cast<std::size_t>(LightFloatBandSlot::kFogDistance)],
      time_tick);
  if (state.fog_distance < 10.0f) {
    state.fog_distance = 10.0f;
  }
  state.fog_multiplier = InterpolateFloatBand(
      band_set.float_bands[static_cast<std::size_t>(LightFloatBandSlot::kFogMultiplier)],
      time_tick);
  if (state.fog_multiplier < -1.0f) {
    state.fog_multiplier = -1.0f;
  } else if (state.fog_multiplier > 1.0f) {
    state.fog_multiplier = 1.0f;
  }

  state.advanced_fog_density = 1.0f;
  ApplyAdvancedFog(state, advanced_fog);
  state.highlight_sky = static_cast<float>(band_set.highlight_sky);
  state.glow = band_set.glow;
  state.float_band_2 = InterpolateFloatBand(
      band_set.float_bands[static_cast<std::size_t>(LightFloatBandSlot::kFloatBand2)], time_tick);
  state.float_band_3 = InterpolateFloatBand(
      band_set.float_bands[static_cast<std::size_t>(LightFloatBandSlot::kFloatBand3)], time_tick);
  state.float_band_4 = InterpolateFloatBand(
      band_set.float_bands[static_cast<std::size_t>(LightFloatBandSlot::kFloatBand4)], time_tick);
  state.float_band_5 = InterpolateFloatBand(
      band_set.float_bands[static_cast<std::size_t>(LightFloatBandSlot::kFloatBand5)], time_tick);
  state.water_shallow_alpha = band_set.water_shallow_alpha;
  state.water_deep_alpha = band_set.water_deep_alpha;
  state.ocean_shallow_alpha = band_set.ocean_shallow_alpha;
  state.ocean_deep_alpha = band_set.ocean_deep_alpha;
  state.accumulated_skyboxes[0] = {band_set.zone_skybox.light_skybox_id, 1.0f};
  state.cloud_type_id = band_set.cloud_type_id;
  state.last_local_light_weight = 1.0f;
  return state;
}

WorldLighting::InterpolatedLightState
WorldLighting::ApplyScreenEffectLightParamOverride(const InterpolatedLightState &base_state,
                                                  const InterpolatedLightState &override_state) {
  InterpolatedLightState merged_state = override_state;
  merged_state.glow = base_state.glow;
  merged_state.water_shallow_alpha = base_state.water_shallow_alpha;
  merged_state.water_deep_alpha = base_state.water_deep_alpha;
  merged_state.ocean_shallow_alpha = base_state.ocean_shallow_alpha;
  merged_state.ocean_deep_alpha = base_state.ocean_deep_alpha;
  merged_state.accumulated_skyboxes = base_state.accumulated_skyboxes;
  merged_state.cloud_type_id = base_state.cloud_type_id;
  merged_state.last_local_light_weight = base_state.last_local_light_weight;
  return merged_state;
}

void WorldLighting::ApplyWeatherLightBlend(InterpolatedLightState &destination,
                                          const InterpolatedLightState &source,
                                          const float weight) {
  const float clamped_weight = Clamp01(weight);
  if (clamped_weight <= 0.0f) {
    return;
  }

  for (std::size_t slot = 0; slot < destination.packed_colors.size(); ++slot) {
    destination.packed_colors[slot] = BlendPackedLightBandRgbRetail(
        destination.packed_colors[slot], source.packed_colors[slot],
        clamped_weight);
  }

  destination.fog_distance =
      LerpScalar(destination.fog_distance, source.fog_distance, clamped_weight);
  destination.fog_multiplier =
      LerpScalar(destination.fog_multiplier, source.fog_multiplier, clamped_weight);
  destination.advanced_fog_density =
      LerpScalar(destination.advanced_fog_density, source.advanced_fog_density, clamped_weight);
  destination.glow = LerpScalar(destination.glow, source.glow, clamped_weight);
  destination.float_band_3 =
      LerpScalar(destination.float_band_3, source.float_band_3, clamped_weight);
  destination.water_shallow_alpha =
      LerpScalar(destination.water_shallow_alpha, source.water_shallow_alpha, clamped_weight);
  destination.water_deep_alpha =
      LerpScalar(destination.water_deep_alpha, source.water_deep_alpha, clamped_weight);
  destination.ocean_shallow_alpha =
      LerpScalar(destination.ocean_shallow_alpha, source.ocean_shallow_alpha, clamped_weight);
  destination.ocean_deep_alpha =
      LerpScalar(destination.ocean_deep_alpha, source.ocean_deep_alpha, clamped_weight);
}

void WorldLighting::ApplyLocalLightBlend(InterpolatedLightState &destination,
                                        const InterpolatedLightState &source, const float weight) {
  for (std::size_t slot = 0; slot < destination.packed_colors.size(); ++slot) {
    destination.packed_colors[slot] = BlendLocalLightBandRgbRetail(
        destination.packed_colors[slot], source.packed_colors[slot], weight);
  }

  destination.fog_distance = LerpScalar(destination.fog_distance, source.fog_distance, weight);
  destination.fog_multiplier =
      LerpScalar(destination.fog_multiplier, source.fog_multiplier, weight);
  destination.advanced_fog_density =
      LerpScalar(destination.advanced_fog_density, source.advanced_fog_density, weight);
  destination.highlight_sky = LerpScalar(destination.highlight_sky, source.highlight_sky, weight);
  destination.glow = LerpScalar(destination.glow, source.glow, weight);
  destination.water_shallow_alpha =
      LerpScalar(destination.water_shallow_alpha, source.water_shallow_alpha, weight);
  destination.water_deep_alpha =
      LerpScalar(destination.water_deep_alpha, source.water_deep_alpha, weight);
  destination.ocean_shallow_alpha =
      LerpScalar(destination.ocean_shallow_alpha, source.ocean_shallow_alpha, weight);
  destination.ocean_deep_alpha =
      LerpScalar(destination.ocean_deep_alpha, source.ocean_deep_alpha, weight);
  destination.float_band_3 = LerpScalar(destination.float_band_3, source.float_band_3, weight);
  destination.cloud_type_id = source.cloud_type_id;
  destination.last_local_light_weight = weight;

  if (!source.accumulated_skyboxes.empty()) {
    const std::uint32_t light_skybox_id = source.accumulated_skyboxes[0].light_skybox_id;
    if (light_skybox_id != 0 && weight > 0.0f) {
      for (auto &entry : destination.accumulated_skyboxes) {
        if (entry.light_skybox_id == light_skybox_id) {
          entry.weight = std::min(1.0f, entry.weight + weight);
          return;
        }
        if (entry.light_skybox_id == 0) {
          entry.light_skybox_id = light_skybox_id;
          entry.weight = weight;
          return;
        }
      }
    }
  }
}

void WorldLighting::ApplyDistanceCappedLocalLightBlend(InterpolatedLightState &destination,
                                                      const LightBandData &light,
                                                      const float distance_squared,
                                                      const uint32_t time_tick,
                                                      const float weather_blend_factor,
                                                      const bool use_underwater_light_params,
                                                      const float max_weight,
                                                      const AdvancedFogInputs &advanced_fog) {
  const float distance_weight = ComputeLocalLightBlendWeight(distance_squared, light);
  if (distance_weight < 0.0f) {
    return;
  }

  const float applied_weight = std::min(std::max(max_weight, 0.0f), distance_weight);
  const InterpolatedLightState prepared_state = BuildPreparedLightState(
      light, time_tick, weather_blend_factor, use_underwater_light_params, advanced_fog);
  ApplyLocalLightBlend(destination, prepared_state, applied_weight);
}

uint32_t WorldLighting::InterpolateBand(const LightBandData::Band &band, uint32_t time_tick) {
  if (band.entry_count == 0)
    return 0xFF000000;

  const CircularBandInterval interval =
      ResolveCircularBandInterval(band.entry_count, band.times, time_tick);
  return BlendArgb(band.values[interval.current], band.values[interval.next],
                   interval.factor);
}

float WorldLighting::InterpolateFloatBand(const LightBandData::FloatBand &band, uint32_t time_tick) {
  if (band.entry_count == 0)
    return 0.0f;

  const CircularBandInterval interval =
      ResolveCircularBandInterval(band.entry_count, band.times, time_tick);
  return band.values[interval.current] +
         (band.values[interval.next] - band.values[interval.current]) *
             interval.factor;
}

uint32_t WorldLighting::BlendArgb(uint32_t a, uint32_t b, float weight) {
  const float t = Clamp01(weight);
  const auto mix = [&](const int shift) -> std::uint8_t {
    const float va = static_cast<float>((a >> shift) & 0xFFu);
    const float vb = static_cast<float>((b >> shift) & 0xFFu);
    return RoundColorByte(va + (vb - va) * t);
  };
  return 0xFF000000u | (static_cast<uint32_t>(mix(16)) << 16) |
         (static_cast<uint32_t>(mix(8)) << 8) |
         static_cast<uint32_t>(mix(0));
}

uint32_t WorldLighting::LightBandColorToArgb(const uint32_t packed_color) {

  constexpr uint32_t kOpaqueAlpha = 0xFF000000u;
  return (packed_color & 0x00FFFFFFu) | kOpaqueAlpha;
}

void WorldLighting::ApplyLiquidDarkening(SkyColors &colors, const LiquidDarkeningState &darkening) {
  if (darkening.max_darken_depth <= 0.0f) {
    return;
  }

  const float depth_fraction =
      Clamp01(-darkening.surface_height_delta / darkening.max_darken_depth);
  if (depth_fraction <= 0.0f) {
    return;
  }

  auto apply_slot = [&](const SkyColorSlot slot, const float intensity) {
    colors.colors[static_cast<std::size_t>(slot)] =
        ScalePackedArgbValue(colors.colors[static_cast<std::size_t>(slot)],
                             1.0f - intensity * depth_fraction);
  };

  apply_slot(SkyColorSlot::kSkyFog, darkening.fog_darken_intensity);
  apply_slot(SkyColorSlot::kGlobalAmbient, darkening.ambient_darken_intensity);
  apply_slot(SkyColorSlot::kGlobalDiffuse, darkening.directional_darken_intensity);
}

std::uint32_t WorldLighting::ScalePackedArgbValue(const std::uint32_t argb,
                                                 const float value_scale) {
  const float red = static_cast<float>((argb >> 16) & 0xFFu) / 255.0f;
  const float green = static_cast<float>((argb >> 8) & 0xFFu) / 255.0f;
  const float blue = static_cast<float>(argb & 0xFFu) / 255.0f;

  HsvColor hsv = ConvertRgbToHsv(red, green, blue);
  hsv.value *= value_scale;

  const auto rgb = ConvertHsvToRgb(hsv);
  return 0xFF000000u | (static_cast<std::uint32_t>(PackOpaqueColorByte(rgb[0])) << 16) |
         (static_cast<std::uint32_t>(PackOpaqueColorByte(rgb[1])) << 8) |
         static_cast<std::uint32_t>(PackOpaqueColorByte(rgb[2]));
}

}

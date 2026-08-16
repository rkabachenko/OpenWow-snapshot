#pragma once
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/world/environment/sky.h"
#include "openwow/world/environment/zone_skybox.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::world {

enum class LightFloatBandSlot : std::uint8_t {
  kFogDistance = 0,
  kFogMultiplier = 1,
  kFloatBand2 = 2,
  kFloatBand3 = 3,
  kFloatBand4 = 4,
  kFloatBand5 = 5,
  kCount = 6,
};

struct LightFogParams {
  float start_distance{300.0f};
  float end_distance{800.0f};
  float density{1.0f};
  std::uint32_t packed_argb{0xFF99B3D9u};
  std::array<float, 4> color{0.6f, 0.7f, 0.85f, 1.0f};
};

struct LightBandData {
  struct Band {
    uint32_t entry_count{0};
    std::array<uint32_t, 16> times{};

    std::array<uint32_t, 16> values{};
  };

  struct FloatBand {
    uint32_t entry_count{0};
    std::array<uint32_t, 16> times{};
    std::array<float, 16> values{};
  };

  struct LightParamBandSet {
    uint32_t light_params_id{0};
    std::array<Band, 18> color_bands{};
    std::array<FloatBand, static_cast<std::size_t>(LightFloatBandSlot::kCount)> float_bands{};
    std::uint32_t highlight_sky{0};
    ZoneSkyboxEntry zone_skybox{};
    std::uint32_t cloud_type_id{0};
    float glow{0.0f};
    float water_shallow_alpha{0.0f};
    float water_deep_alpha{0.0f};
    float ocean_shallow_alpha{0.0f};
    float ocean_deep_alpha{0.0f};
  };

  uint32_t light_id{0};
  uint32_t map_id{0};
  float x{0}, y{0}, z{0};
  float falloff_start{0};
  float falloff_end{0};
  std::array<uint32_t, 8> light_params{};
  std::array<LightParamBandSet, 8> light_param_band_sets{};
};

class WorldLighting {
public:
  struct LiquidDarkeningState {
    float surface_height_delta{0.0f};
    float max_darken_depth{0.0f};
    float fog_darken_intensity{0.0f};
    float ambient_darken_intensity{0.0f};
    float directional_darken_intensity{0.0f};

    std::uint32_t light_params_id{0};

    std::uint32_t liquid_type_id{0};
  };

  struct ZoneSkyboxSlot {
    ZoneSkyboxEntry entry{};
    float alpha{0.0f};
    bool active{false};
  };

  using ZoneSkyboxSlots = std::array<ZoneSkyboxSlot, 3>;

  struct ResolvedEnvironment {
    SkyColors sky{};
    LightFogParams fog{};
    ZoneSkyboxSlots skybox_slots{};
    std::optional<ZoneSkyboxEntry> primary_skybox{};
  };

  struct AdvancedFogInputs {
    bool armed{false};
    float camera_far_clip{0.0f};
  };

  WorldLighting() = default;
  ~WorldLighting() = default;

  WorldLighting(const WorldLighting &) = delete;
  WorldLighting &operator=(const WorldLighting &) = delete;

  void LoadForMap(const data::dbc::DbcLoader &dbc, uint32_t map_id);

  void SetAdvancedFogInputs(const AdvancedFogInputs &inputs) {
    advanced_fog_inputs_ = inputs;
  }
  [[nodiscard]] const AdvancedFogInputs &advanced_fog_inputs() const {
    return advanced_fog_inputs_;
  }

  [[nodiscard]] SkyColors
  GetSkyColors(float x, float y, float z, float time_of_day,
               std::optional<std::uint8_t> light_param_slot_override = std::nullopt,
               float weather_blend_factor = 0.0f,
               bool use_underwater_light_params = false,
               std::optional<LiquidDarkeningState> liquid_darkening = std::nullopt,
               bool suppress_local_lights = false) const;

  [[nodiscard]] ResolvedEnvironment
  ResolveEnvironment(
      float x, float y, float z, float time_of_day,
      std::optional<std::uint8_t> light_param_slot_override = std::nullopt,
      float weather_blend_factor = 0.0f,
      bool use_underwater_light_params = false,
      std::optional<LiquidDarkeningState> liquid_darkening = std::nullopt,
      bool suppress_local_lights = false) const;

  [[nodiscard]] LightFogParams
  GetFogParams(float x, float y, float z, float time_of_day,
               std::optional<std::uint8_t> light_param_slot_override,
               float weather_blend_factor = 0.0f,
               bool use_underwater_light_params = false,
               std::optional<LiquidDarkeningState> liquid_darkening = std::nullopt,
               bool suppress_local_lights = false) const;

  [[nodiscard]] std::optional<ZoneSkyboxEntry>
  GetZoneSkybox(float x, float y, float z, float time_of_day,
                std::optional<std::uint8_t> light_param_slot_override = std::nullopt,
                bool use_underwater_light_params = false,
                bool suppress_local_lights = false) const;

  [[nodiscard]] ZoneSkyboxSlots
  GetZoneSkyboxSlots(float x, float y, float z, float time_of_day,
                     std::optional<std::uint8_t> light_param_slot_override = std::nullopt,
                     float weather_blend_factor = 0.0f,
                     bool use_underwater_light_params = false,
                     bool suppress_local_lights = false) const;

  [[nodiscard]] bool loaded() const {
    return !lights_.empty();
  }
  [[nodiscard]] size_t light_count() const {
    return lights_.size();
  }

private:
  struct WeightedSkyboxBlend {
    std::uint32_t light_skybox_id{0};
    float weight{0.0f};
  };

  struct InterpolatedLightState {
    std::array<std::uint32_t, 18> packed_colors{};
    float fog_distance{500.0f};
    float fog_multiplier{1.0f};
    float advanced_fog_density{1.0f};
    float highlight_sky{0.0f};
    float glow{0.0f};
    float float_band_2{0.0f};
    float float_band_3{0.0f};
    float float_band_4{0.0f};
    float float_band_5{0.0f};
    float water_shallow_alpha{0.0f};
    float water_deep_alpha{0.0f};
    float ocean_shallow_alpha{0.0f};
    float ocean_deep_alpha{0.0f};
    std::array<WeightedSkyboxBlend, 3> accumulated_skyboxes{};
    std::uint32_t cloud_type_id{0};
    float last_local_light_weight{1.0f};
  };
  static void ClearInterpolatedLightState(InterpolatedLightState &state);

  [[nodiscard]] InterpolatedLightState
  BuildResolvedLightState(float x, float y, float z, float time_of_day,
                          std::optional<std::uint8_t> light_param_slot_override,
                          float weather_blend_factor,
                          bool use_underwater_light_params,
                          bool suppress_local_lights,
                          std::uint32_t liquid_light_params_id = 0u) const;
  [[nodiscard]] static SkyColors BuildSkyColors(
      const InterpolatedLightState &state,
      std::optional<LiquidDarkeningState> liquid_darkening);
  [[nodiscard]] static InterpolatedLightState
  BuildPreparedLightState(const LightBandData &light, uint32_t time_tick, float weather_blend_factor,
                          bool use_underwater_light_params,
                          const AdvancedFogInputs &advanced_fog);
  [[nodiscard]] static InterpolatedLightState
  BuildInterpolatedLightState(const LightBandData::LightParamBandSet &band_set, uint32_t time_tick,
                              const AdvancedFogInputs &advanced_fog);

  static void ApplyAdvancedFog(InterpolatedLightState &state,
                               const AdvancedFogInputs &advanced_fog);
  [[nodiscard]] static InterpolatedLightState ApplyScreenEffectLightParamOverride(
      const InterpolatedLightState &base_state,
      const InterpolatedLightState &override_state);
  static void ApplyWeatherLightBlend(InterpolatedLightState &destination,
                                     const InterpolatedLightState &source, float weight);
  static void ApplyDistanceCappedLocalLightBlend(InterpolatedLightState &destination,
                                                 const LightBandData &light,
                                                 float distance_squared, uint32_t time_tick,
                                                 float weather_blend_factor,
                                                 bool use_underwater_light_params,
                                                 float max_weight,
                                                 const AdvancedFogInputs &advanced_fog);
  static void ApplyLocalLightBlend(InterpolatedLightState &destination,
                                   const InterpolatedLightState &source, float weight);

  static uint32_t InterpolateBand(const LightBandData::Band &band, uint32_t time_tick);

  static float InterpolateFloatBand(const LightBandData::FloatBand &band, uint32_t time_tick);

  static uint32_t BlendArgb(uint32_t a, uint32_t b, float weight);

  static uint32_t LightBandColorToArgb(uint32_t packed_color);

  static void ApplyLiquidDarkening(SkyColors &colors, const LiquidDarkeningState &darkening);
  static std::uint32_t ScalePackedArgbValue(std::uint32_t argb, float value_scale);

  std::vector<LightBandData> lights_;

  std::unordered_map<std::uint32_t, LightBandData::LightParamBandSet>
      liquid_light_param_band_sets_;
  int global_light_index_{-1};
  AdvancedFogInputs advanced_fog_inputs_{};
  std::unordered_map<std::uint32_t, ZoneSkyboxEntry> zone_skyboxes_by_id_;
};

}

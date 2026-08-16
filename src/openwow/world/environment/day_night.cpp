#include "openwow/world/environment/day_night.h"

#include "openwow/core/console.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/math/exp2_approx.h"
#include "openwow/foundation/math/float_compare.h"
#include "openwow/render/resources/textures/texture_asset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

constexpr float kQuarterTurn = 0.7853981852531433f;
constexpr float kCloudSphereCenterOffsetZ = -0.7071067690849304f;
constexpr float kCloudTextureAngleScale = 0.63661975f;
constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831855f;
constexpr float kSkyAngleToUnitScale = 0.31830987f;
constexpr float kSkySphereRadius = 12.0f;
constexpr char kSunGlareEnabledMessage[] = "SunGlare enabled.  Don't look directly at it.";
constexpr char kSunGlareDisabledMessage[] = "SunGlare disabled";
constexpr std::uint32_t kAreaLightOverrideLockedGrowthQuantum = 10;
constexpr std::int32_t kDayNightCloudRenderTargetDataFormat = 2;
constexpr char kDayNightCloudRenderTargetName0[] = "DNClouds0";
constexpr char kDayNightCloudRenderTargetName1[] = "DNClouds1";
constexpr float kDayNightDefaultCloudAlpha = 0.6f;
constexpr float kDayNightDefaultCloudAnimationRate = 2.0f;
constexpr std::uint32_t kDayNightDefaultCloudRowsPerUpdate = 8u;
constexpr std::uint32_t kDayNightCloudNoiseOctaveCount = 4u;
constexpr float kDayNightCloudNoiseFadeAngleScale = 0.012271847f;
constexpr float kDayNightCloudNoiseGradientScale = 0.000030518509f;
constexpr std::uint32_t kFastInvSqrtMagic = 1597609915u;
constexpr std::size_t kLightEnvNormalizedTimeOfDayIndex = 1;
constexpr std::size_t kLightEnvDayCountIndex = 2;
constexpr std::size_t kLightEnvCameraPositionIndex = 6;
constexpr std::size_t kLightEnvDeltaSecondsIndex = 18;
constexpr float kWrappedCurveDeltaEpsilon = 0.001f;
constexpr float kMoonSecondaryPhaseWrapPeriod = 1.7f;

constexpr float kAdvancedFogWideFarClip = 700.0f;
constexpr float kAdvancedFogWideFadeWindow = 500.0f;

constexpr float kAdvancedFogNarrowWindowMargin = 200.0f;

constexpr float kAdvancedFogDensityRange = 5.5f;
constexpr float kAdvancedFogDensityFloor = 1.5f;

constexpr float kSunCenterProjectedScaleMultiplier = 1.0f;
constexpr float kMoonPrimaryProjectedScaleMultiplier = 1.75f;
constexpr float kMoonSecondaryProjectedScaleMultiplier = 1.0f;

constexpr std::array<std::array<std::uint16_t, 5>, 4> kCloudNoiseOctaveSteps = {{
    {16, 32, 64, 128, 256},
    {8, 16, 32, 64, 128},
    {4, 8, 16, 32, 64},
    {2, 4, 8, 16, 32},
}};

constexpr std::array<std::uint8_t, 256> kCloudNoisePermutation = {{
    225, 155, 210, 108, 175, 199, 221, 144, 203, 116, 70, 213, 69, 158, 33, 252,
    5, 82, 173, 133, 222, 139, 174, 27, 9, 71, 90, 246, 75, 130, 91, 191,
    169, 138, 2, 151, 194, 235, 81, 7, 25, 113, 228, 159, 205, 253, 134, 142,
    248, 65, 224, 217, 22, 121, 229, 63, 89, 103, 96, 104, 156, 17, 201, 129,
    36, 8, 165, 110, 237, 117, 231, 56, 132, 211, 152, 20, 181, 111, 239, 218,
    170, 163, 51, 172, 157, 47, 80, 212, 176, 250, 87, 49, 99, 242, 136, 189,
    162, 115, 44, 43, 124, 94, 150, 16, 141, 247, 32, 10, 198, 223, 255, 72,
    53, 131, 84, 57, 220, 197, 58, 50, 208, 11, 241, 28, 3, 192, 62, 202,
    18, 215, 153, 24, 76, 41, 15, 179, 39, 46, 55, 6, 128, 167, 23, 188,
    106, 34, 187, 140, 164, 73, 112, 182, 244, 195, 227, 13, 35, 77, 196, 185,
    26, 200, 226, 119, 31, 123, 168, 125, 249, 68, 183, 230, 177, 135, 160, 180,
    12, 1, 243, 148, 102, 166, 38, 238, 251, 37, 240, 126, 64, 74, 161, 40,
    184, 149, 171, 178, 101, 66, 29, 59, 146, 61, 254, 107, 42, 86, 154, 4,
    236, 232, 120, 21, 233, 209, 45, 98, 193, 114, 78, 19, 206, 14, 118, 127,
    48, 79, 147, 85, 30, 207, 219, 54, 88, 234, 190, 122, 95, 67, 143, 109,
    137, 214, 145, 93, 92, 100, 245, 0, 216, 186, 60, 83, 105, 97, 204, 52,
}};

struct WrappedFloatCurvePoint {
  float position;
  float value;
};

constexpr WrappedFloatCurvePoint kStarsModelAlphaCurve[] = {
    {0.125f, 1.0f},
    {0.1875f, 0.0f},
    {0.75f, 0.0f},
    {1.0f, 1.0f},
};

constexpr WrappedFloatCurvePoint kDerivedDirectionPolarAngleCurve[] = {
    {0.0f, 2.2165682f},
    {0.25f, 1.9198623f},
    {0.5f, 2.2165682f},
    {0.75f, 1.9198623f},
};

constexpr WrappedFloatCurvePoint kDerivedDirectionAzimuthAngleCurve[] = {
    {0.0f, 3.926991f},
    {0.25f, 3.926991f},
    {0.5f, 3.926991f},
    {0.75f, 3.926991f},
};

constexpr WrappedFloatCurvePoint kSunCenterPolarAngleCurve[] = {
    {0.22916667f, 1.7453293f},   {0.49652779f, 0.087266468f}, {0.5f, 0.087266468f},
    {0.50347221f, 0.087266468f}, {0.89583331f, 1.7453293f},
};

constexpr WrappedFloatCurvePoint kSunCenterAzimuthAngleCurve[] = {
    {0.22916667f, 0.78539819f},
    {0.5f, 0.78539819f},
    {0.89583331f, 0.78539819f},
};

constexpr WrappedFloatCurvePoint kMoonPrimaryPolarAngleCurve[] = {
    {0.0f, 0.61086524f},       {0.0034722222f, 0.61086524f}, {0.16666667f, 1.7453293f},
    {0.91666669f, 1.7453293f}, {0.99652779f, 0.61086524f},
};

constexpr WrappedFloatCurvePoint kMoonPrimaryAzimuthAngleCurve[] = {
    {0.0f, 0.78539819f},
    {0.16666667f, 0.78539819f},
    {0.91666669f, 0.78539819f},
};

constexpr WrappedFloatCurvePoint kMoonSecondaryAzimuthAngleCurve[] = {
    {0.0f, 2.3561945f},
    {0.16666667f, 2.6179938f},
    {0.91666669f, 2.8797934f},
};

constexpr WrappedFloatCurvePoint kSunAndMoonPrimaryProjectedScaleCurve[] = {
    {0.25f, 2.0f},
    {0.28125f, 1.0f},
    {0.84375f, 1.0f},
    {0.875f, 2.0f},
};

constexpr WrappedFloatCurvePoint kMoonSecondaryProjectedScaleCurve[] = {
    {0.041666672f, 1.0f},
    {0.16666667f, 1.5f},
    {0.91666669f, 1.5f},
    {0.99930561f, 1.0f},
};

struct AreaLightOverrideArrayState {
  std::uint32_t capacity = 0;
  std::uint32_t count = 0;
  std::uint32_t growthStep = 0;
  std::vector<AreaLightOverride> storage;
};

struct LightRecordLookupState {
  std::int32_t firstLightId = 0;
  std::vector<std::uint32_t> resolvedHandles;
};

struct TransitionLightRecordStore {
  std::uint32_t nextToken = 1u;
  std::unordered_map<std::uint32_t, openwow::data::dbc::LightEntry> records;
};

static_assert(sizeof(openwow::data::dbc::LightEntry) == 60u,
              "Light.dbc rows must retain the retail 60-byte value layout");

struct CameraLiquidState {
  float submersionDepth = 0.0f;
  std::uint32_t liquidTypeId = 0;
};

struct ScreenEffectFogOverrideState {
  bool active = false;
  float endDistance = 0.0f;
  float startFactor = 0.0f;
  float density = 0.0f;
  std::uint32_t colorArgb = 0;

  std::uint32_t skyDomeEnabled = 1;
  DayNightFogBand savedOutdoorBand{};
  std::uint32_t savedSkyDomeEnabled = 0;
};

struct DayNightCloudLayerRuntimeState {
  float alphaOverride = 0.0f;
  std::uint8_t alphaStartByte = 0;
  std::uint8_t lodLevel = 0;
  bool needsRebuild = false;
  bool enabled = false;
  std::uint8_t activeTextureIndex = 0;
  std::uint16_t animationPhaseWord = 0;
  std::uint32_t rowsPerUpdate = kDayNightDefaultCloudRowsPerUpdate;
  std::uint32_t currentRow = 0;
  std::uint32_t octaveCount = kDayNightCloudNoiseOctaveCount;
  std::int32_t dataFormat = kDayNightCloudRenderTargetDataFormat;
  std::uint32_t gridSize = 0;
  std::uint32_t indexCount = 0;
  std::uint32_t vertexStride = 0;
  float animationRate = kDayNightDefaultCloudAnimationRate;
  float animationTime = 0.0f;
  std::vector<std::uint32_t> colorBuffer;
  std::vector<std::uint8_t> coverageBuffer;
  std::vector<float> heightBuffer;
  std::array<openwow::render::TextureAssetPtr, 2> renderTargets{};
  std::array<DayNightCloudTextureUpload, 2> pendingUploads{};
};

struct DayNightSkyModelResourceState {
  std::uint32_t resourceId = 0;
  std::string path;
  std::uint32_t flags = 0;
};

float EvaluateWrappedFloatCurve(const WrappedFloatCurvePoint *points, const std::size_t point_count,
                                const float sample) {
  const float clamped_sample = std::clamp(sample, 0.0f, 1.0f);

  std::size_t upper_index = 0;
  while (upper_index < point_count && clamped_sample > points[upper_index].position) {
    ++upper_index;
  }
  if (upper_index == point_count) {
    upper_index = 0;
  }

  const std::size_t lower_index = (upper_index == 0) ? point_count - 1 : upper_index - 1;
  const WrappedFloatCurvePoint &lower = points[lower_index];
  const WrappedFloatCurvePoint &upper = points[upper_index];

  float span = upper.position - lower.position;
  if (std::fabs(span) < kWrappedCurveDeltaEpsilon) {
    return lower.value;
  }
  if (span < 0.0f) {
    span += 1.0f;
  }

  float sample_offset = clamped_sample - lower.position;
  if (sample_offset < 0.0f) {
    sample_offset += 1.0f;
  }

  const float factor = sample_offset / span;
  if (upper.value < lower.value) {
    return lower.value - factor * (lower.value - upper.value);
  }
  return factor * (upper.value - lower.value) + lower.value;
}

float EvaluateDayNightWave(float phase) {
  const int integerPart = (phase <= 0.0f) ? static_cast<int>(phase) - 1 : static_cast<int>(phase);
  const float fractionalPart = phase - static_cast<float>(integerPart);
  float component = 1.0f - fractionalPart * (6.0f - 4.0f * fractionalPart) * fractionalPart;
  if ((integerPart & 1) != 0) {
    component = -component;
  }
  return component;
}

std::uint32_t ResolveAreaLightOverrideGrowQuantum(const std::uint32_t requestedCount,
                                                  AreaLightOverrideArrayState &arrayState) {
  std::uint32_t result = requestedCount;
  if (requestedCount >= kAreaLightOverrideLockedGrowthQuantum) {
    arrayState.growthStep = kAreaLightOverrideLockedGrowthQuantum;
    return kAreaLightOverrideLockedGrowthQuantum;
  }

  for (std::uint32_t value = requestedCount & (requestedCount - 1U); value != 0U;
       value &= (value - 1U)) {
    result = value;
  }

  return result == 0U ? 1U : result;
}

void ResetAreaLightOverrideArray(AreaLightOverrideArrayState &arrayState) {
  arrayState.capacity = 0;
  arrayState.count = 0;
  arrayState.growthStep = 0;
  arrayState.storage.clear();
}

void ConstructDayNightGlareBillboardState(
    DayNightGlareState &state, const DayNightGlareCloudFadeTransform cloud_fade_transform) {
  state = {};
  state.cloudFadeTransform = cloud_fade_transform;
}

void InitializeGlareState(
    DayNightGlareState &state, const DayNightGlareCloudFadeTransform cloud_fade_transform,
    const char *texture_path, const DayNightGlareCelestialBinding linked_celestial_texture,
    const float base_scale, const float intensity_rise_rate, const float intensity_fall_rate,
    const float minimum_projected_scale, const float maximum_projected_scale,
    const float horizon_dot_clamp, const float minimum_alpha_multiplier,
    const float maximum_alpha_multiplier,
    const std::array<DayNightGlareVisibilityPoint, DayNightGlareProfile::kVisibilityPointCount>
        &visibility_curve) {
  ConstructDayNightGlareBillboardState(state, cloud_fade_transform);
  state.profile.texturePath = texture_path;
  state.profile.linkedCelestialTexture = linked_celestial_texture;
  state.profile.baseScale = base_scale;
  state.profile.intensityRiseRate = intensity_rise_rate;
  state.profile.intensityFallRate = intensity_fall_rate;
  state.profile.minimumProjectedScale = minimum_projected_scale;
  state.profile.maximumProjectedScale = maximum_projected_scale;
  state.profile.horizonDotClamp = horizon_dot_clamp;
  state.profile.minimumAlphaMultiplier = minimum_alpha_multiplier;
  state.profile.maximumAlphaMultiplier = maximum_alpha_multiplier;
  state.profile.visibilityCurve = visibility_curve;
  state.enabled = true;
  state.active = true;
}

[[nodiscard]] DayNightCelestialState MakeDefaultCelestialState() {
  DayNightCelestialState state{};
  state.sunCenter.texturePath = kDayNightSunCenterTexturePath;
  state.moonPrimary.texturePath = kDayNightMoonPrimaryTexturePath;
  state.moonSecondary.texturePath = kDayNightMoonSecondaryTexturePath;

  return state;
}

[[nodiscard]] DayNightVec3 EvaluateCelestialWorldPosition(
    const WrappedFloatCurvePoint *polar_curve, const std::size_t polar_point_count,
    const WrappedFloatCurvePoint *azimuth_curve, const std::size_t azimuth_point_count,
    const float sample, const DayNightVec3 &origin) {
  const float polar_angle = EvaluateWrappedFloatCurve(polar_curve, polar_point_count, sample);
  const float azimuth_angle = EvaluateWrappedFloatCurve(azimuth_curve, azimuth_point_count, sample);

  const float sin_polar = EvaluateDayNightWave(polar_angle * kSkyAngleToUnitScale - 0.5f);
  const float cos_polar = EvaluateDayNightWave(polar_angle * kSkyAngleToUnitScale);
  const float sin_azimuth = EvaluateDayNightWave(azimuth_angle * kSkyAngleToUnitScale - 0.5f);
  const float cos_azimuth = EvaluateDayNightWave(azimuth_angle * kSkyAngleToUnitScale);

  const float direction_x = cos_azimuth * sin_polar;
  const float direction_y = sin_polar * sin_azimuth;
  const float direction_z = cos_polar;
  const float inverse_length =
      kSkySphereRadius /
      std::sqrt(direction_x * direction_x + direction_y * direction_y + direction_z * direction_z);
  return {
      origin.x + direction_x * inverse_length,
      origin.y + direction_y * inverse_length,
      origin.z + direction_z * inverse_length,
  };
}

[[nodiscard]] float EvaluateProjectedScale(const WrappedFloatCurvePoint *curve,
                                           const std::size_t point_count, const float sample,
                                           const float multiplier) {
  return EvaluateWrappedFloatCurve(curve, point_count, sample) * multiplier;
}

[[nodiscard]] float ComputeMoonSecondarySample(const float day_count,
                                               const float normalized_time_of_day) {
  const auto floor_to_fixed16 = [](const float value) -> std::int32_t {
    return static_cast<std::int32_t>(std::floor(static_cast<double>(value) * 65536.0));
  };

  const std::int32_t combined_time_fixed =
      floor_to_fixed16(day_count) + floor_to_fixed16(normalized_time_of_day);
  const float wrapped_origin = std::floor(
      (day_count + normalized_time_of_day) / kMoonSecondaryPhaseWrapPeriod)
                               * kMoonSecondaryPhaseWrapPeriod;
  const std::int32_t wrapped_origin_fixed = floor_to_fixed16(wrapped_origin);
  const std::int32_t wrapped_time_fixed =
      combined_time_fixed - std::min(wrapped_origin_fixed, combined_time_fixed);
  return static_cast<float>(wrapped_time_fixed) / (65536.0f * kMoonSecondaryPhaseWrapPeriod);
}

[[nodiscard]] float ComputeCelestialHorizonFade(const float normalized_time_of_day) {
  if (normalized_time_of_day < 0.22916667f) {
    if (normalized_time_of_day < 0.0f || normalized_time_of_day >= 0.16666667f) {
      return 0.0f;
    }
    return 1.0f - normalized_time_of_day * 6.0f;
  }

  if (normalized_time_of_day < 0.5f) {
    return (normalized_time_of_day - 0.22916667f) * 3.6923077f;
  }

  if (normalized_time_of_day < 0.89583331f) {
    return 1.0f - (normalized_time_of_day - 0.5f) * 2.5263159f;
  }

  if (normalized_time_of_day < 0.91666669f) {
    return 0.0f;
  }

  if (normalized_time_of_day < 1.0f) {
    return (normalized_time_of_day - 0.91666669f) * 12.000003f;
  }

  return 0.0f;
}

[[nodiscard]] std::string NormalizeSkyModelPath(const std::string_view raw_path) {
  std::string path(raw_path);
  for (char &ch : path) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  while (!path.empty() && (path.back() == ' ' || path.back() == '\0')) {
    path.pop_back();
  }
  return path;
}

[[nodiscard]] std::string CanonicalSkyModelKey(const std::string_view raw_path) {
  std::string key = NormalizeSkyModelPath(raw_path);
  for (char &ch : key) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return key;
}

}

static DayNightLightEnv s_lightEnv{};
static uint32_t s_mapId = 0;
static DayNightGlareState s_sunGlareState{};
static DayNightGlareState s_moonGlareState{};
static float s_transitionProgress = 0.0f;
static uint32_t s_activeTransitions = 0;
static uint32_t s_transitionDataCount = 0;
static bool s_transitionDirty = false;
static std::uint32_t s_skyUpdateMask = 0;
static bool s_systemInitialized = false;
static bool s_renderReady = false;
static std::int32_t s_screenEffectLightParamsId = -1;
static DayNightSpellVisualLightingTint s_spellVisualLightingTint{};
static DayNightLightingAccumulatorState s_lightingAccumulatorState{};
static uint8_t s_cloudAlphaTable[257] = {};
static DayNightCloudDomeMesh s_cloudDomeMesh{};
static DayNightSkyDomeMesh s_skyDomeMesh{};
static DayNightStarsModelState s_starsModelState{};
static DayNightCelestialState s_celestialState = MakeDefaultCelestialState();
static AreaLightOverrideArrayState s_areaLightOverrides{};
static LightRecordLookupState s_lightRecordLookup{};
static TransitionLightRecordStore s_transitionLightRecords{};

static CameraLiquidState s_cameraLiquidState{};
static DayNightCloudLayerRuntimeState s_cloudLayerState{};
static std::uint32_t s_nextSkyModelResourceId = 1;
static std::unordered_map<std::string, DayNightSkyModelResourceState> s_skyModelCache{};
static DayNightSkyModelSlots s_skyModelSlots{};
static std::array<float, 256> s_cloudNoiseGradients{};
static std::array<float, 256> s_cloudNoiseFadeTable{};
static bool s_cloudNoiseTablesInitialized = false;
static ScreenEffectFogOverrideState s_screenEffectFogOverride{};
static std::int32_t s_fogMode = 0;
static float s_fogFadeFactor = 0.0f;
static float s_fogBandColorScale = 0.0f;
static float s_fogFadeDistanceScale = 0.0f;
static float s_fogFadeMaxDistance = 0.0f;
static float s_fogFadeBaseOffset = 0.0f;
static std::uint32_t s_fogFadeColor = 0;

namespace {

void InitializeCloudNoiseTables() {
  if (s_cloudNoiseTablesInitialized) {
    return;
  }

  std::uint32_t rand_state = 1u;
  for (float &gradient : s_cloudNoiseGradients) {
    rand_state = 214013u * rand_state + 2531011u;
    const std::uint32_t rand_value = (rand_state >> 16u) & 0x7FFFu;
    gradient =
        1.0f - 2.0f * (static_cast<float>(rand_value) * kDayNightCloudNoiseGradientScale);
  }

  for (std::size_t index = 0; index < s_cloudNoiseFadeTable.size(); ++index) {
    s_cloudNoiseFadeTable[index] =
        (1.0f - std::cos(static_cast<float>(index) * kDayNightCloudNoiseFadeAngleScale)) * 0.5f;
  }

  s_cloudNoiseTablesInitialized = true;
}

void ReleaseCloudLayerRenderTargets() {
  for (auto& texture : s_cloudLayerState.renderTargets) {
    texture.reset();
  }
}

void ResetCloudLayerState() {
  InitializeCloudNoiseTables();
  ReleaseCloudLayerRenderTargets();
  s_cloudLayerState.pendingUploads = {};
  s_cloudLayerState.colorBuffer.clear();
  s_cloudLayerState.coverageBuffer.clear();
  s_cloudLayerState.heightBuffer.clear();
  s_cloudLayerState.alphaOverride = 0.0f;
  s_cloudLayerState.alphaStartByte = 0;
  s_cloudLayerState.gridSize = 0;
  s_cloudLayerState.indexCount = 0;
  s_cloudLayerState.vertexStride = 0;
  s_cloudLayerState.needsRebuild = false;
  s_cloudLayerState.enabled = false;
  s_cloudLayerState.lodLevel = 0;
  s_cloudLayerState.activeTextureIndex = 0;
  s_cloudLayerState.animationPhaseWord = 0;
  s_cloudLayerState.rowsPerUpdate = kDayNightDefaultCloudRowsPerUpdate;
  s_cloudLayerState.currentRow = 0;
  s_cloudLayerState.octaveCount = kDayNightCloudNoiseOctaveCount;
  s_cloudLayerState.dataFormat = kDayNightCloudRenderTargetDataFormat;
  s_cloudLayerState.animationRate = kDayNightDefaultCloudAnimationRate;
  s_cloudLayerState.animationTime = 0.0f;
}

void ReleaseCelestialTextureHandle(DayNightCelestialTextureState &body) {
  body.texture.reset();
}

void ResetCelestialState() {
  ReleaseCelestialTextureHandle(s_celestialState.sunCenter);
  ReleaseCelestialTextureHandle(s_celestialState.moonPrimary);
  ReleaseCelestialTextureHandle(s_celestialState.moonSecondary);
  s_celestialState = MakeDefaultCelestialState();
}

struct PackedRgbChannels {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

struct HsvColor {
  float hue = -1.0f;
  float saturation = 0.0f;
  float value = 0.0f;
};

[[nodiscard]] PackedRgbChannels UnpackPackedArgbRgb(const std::uint32_t packed_argb) {
  return {
      static_cast<std::uint8_t>((packed_argb >> 16) & 0xFFu),
      static_cast<std::uint8_t>((packed_argb >> 8) & 0xFFu),
      static_cast<std::uint8_t>(packed_argb & 0xFFu),
  };
}

[[nodiscard]] std::uint8_t BlendColorChannelHalfTowards(const std::uint8_t base,
                                                        const std::uint8_t target) {
  const auto delta = static_cast<std::uint16_t>(target - base);
  return static_cast<std::uint8_t>(base + (delta >> 1));
}

[[nodiscard]] std::uint8_t SaturatingAddColorByte(const std::uint8_t value,
                                                  const std::uint8_t addend) {
  const auto sum = static_cast<std::uint16_t>(value) + static_cast<std::uint16_t>(addend);
  return static_cast<std::uint8_t>(std::min<std::uint16_t>(sum, 0xFFu));
}

[[nodiscard]] std::uint8_t ComputeReducedAmbientByte(const std::uint8_t value) {
  return static_cast<std::uint8_t>((85u * (static_cast<std::uint16_t>(value) + 3u)) >> 8);
}

[[nodiscard]] std::uint32_t PackArgb(const std::uint8_t alpha, const std::uint8_t red,
                                     const std::uint8_t green, const std::uint8_t blue) {
  return (static_cast<std::uint32_t>(alpha) << 24) | (static_cast<std::uint32_t>(red) << 16) |
         (static_cast<std::uint32_t>(green) << 8) | static_cast<std::uint32_t>(blue);
}

[[nodiscard]] std::uint32_t BlendPackedArgbRgb(const std::uint32_t destination_argb,
                                               const std::uint8_t blend_factor,
                                               const std::uint32_t source_argb) {
  if (blend_factor == 0u) {
    return destination_argb;
  }

  const std::uint32_t destination_alpha = destination_argb & 0xFF000000u;
  const std::uint32_t source_rgb = source_argb & 0x00FFFFFFu;
  if (blend_factor == 0xFFu) {
    return destination_alpha | source_rgb;
  }

  const auto blend_channel = [&](const int shift) -> std::uint32_t {
    const auto destination = static_cast<std::uint8_t>((destination_argb >> shift) & 0xFFu);
    const auto source = static_cast<std::uint8_t>((source_argb >> shift) & 0xFFu);
    const auto delta = static_cast<std::int32_t>(source) - static_cast<std::int32_t>(destination);
    const auto blended = static_cast<std::uint8_t>(destination + ((blend_factor * delta) >> 8));
    return static_cast<std::uint32_t>(blended) << shift;
  };

  return destination_alpha | blend_channel(16) | blend_channel(8) | blend_channel(0);
}

[[nodiscard]] int FindDominantRgbChannel(const float red, const float green, const float blue) {
  if (red <= green) {
    return (green > blue) ? 1 : 2;
  }
  if (red > blue) {
    return 0;
  }
  return 2;
}

[[nodiscard]] int FindWeakestRgbChannel(const float red, const float green, const float blue) {
  if (red >= green) {
    return (green <= blue) ? 1 : 2;
  }
  if (red >= blue) {
    return 2;
  }
  return 0;
}

[[nodiscard]] HsvColor ConvertRgbToHsv(const float red, const float green, const float blue) {
  const int max_channel = FindDominantRgbChannel(red, green, blue);
  const int min_channel = FindWeakestRgbChannel(red, green, blue);
  const float rgb[3] = {red, green, blue};

  HsvColor hsv{};
  hsv.value = rgb[max_channel];
  hsv.saturation =
      (hsv.value == 0.0f) ? 0.0f : (rgb[max_channel] - rgb[min_channel]) / rgb[max_channel];
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

[[nodiscard]] std::array<float, 3> ConvertHsvToRgb(const HsvColor &hsv) {
  if (hsv.saturation == 0.0f) {
    return {hsv.value, hsv.value, hsv.value};
  }

  float hue = hsv.hue;
  if (hue >= 360.0f) {
    hue -= 360.0f;
  }

  const float scaled_hue = hue * (1.0f / 60.0f);
  int sector = static_cast<int>(std::nearbyint(scaled_hue - 0.5f));
  if (sector > 5) {
    sector = 5;
  }

  const float fractional = scaled_hue - static_cast<float>(sector);
  const float saturation = std::min(hsv.saturation, 1.0f);
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

}

void DBClient_InitializeLightDB(uint8_t flags) {

  (void)flags;
}

std::uint32_t DayNight_SetScreenEffectLightParamsId(const std::uint32_t light_param_slot) {
  s_screenEffectLightParamsId = static_cast<std::int32_t>(light_param_slot);
  if (light_param_slot > 7u) {
    s_screenEffectLightParamsId = -1;
  }
  return light_param_slot;
}

void DayNight_ClearScreenEffectLightParamsId() {
  s_screenEffectLightParamsId = -1;
}

std::int32_t DayNight_GetScreenEffectLightParamsId() {
  return s_screenEffectLightParamsId;
}

std::uint32_t DayNight_SetSpellVisualLightingTint(const std::uint32_t packed_argb,
                                                  const float alpha) {
  s_spellVisualLightingTint.packed_argb = packed_argb;
  const auto rounded_alpha = static_cast<std::int32_t>(std::nearbyint(alpha * 255.0f));
  s_spellVisualLightingTint.blend_factor = static_cast<std::uint8_t>(rounded_alpha);
  return packed_argb;
}

void DayNight_ClearSpellVisualLightingTint() {
  s_spellVisualLightingTint = {};
}

const DayNightSpellVisualLightingTint &DayNight_GetSpellVisualLightingTint() {
  return s_spellVisualLightingTint;
}

std::uint32_t DayNight_BlendPackedArgbWithSpellVisualTint(const std::uint32_t destination_argb) {
  const auto &tint = s_spellVisualLightingTint;
  return BlendPackedArgbRgb(destination_argb, tint.blend_factor, tint.packed_argb);
}

namespace {

DayNightFogBand ReadOutdoorFogBandFromEnv(const DayNightLightEnv &env) {
  DayNightFogBand band{};
  band.packed_argb = env.dwords[kOutdoorFogColorIndex];
  const float end_distance = env.ReadFloat(kOutdoorFogEndDistanceIndex);
  band.end_distance = end_distance;
  band.start_distance = end_distance * env.ReadFloat(kOutdoorFogStartFactorIndex);
  band.density = env.ReadFloat(kOutdoorFogDensityIndex);
  return band;
}

void WriteOutdoorFogBandToEnv(DayNightLightEnv &env, const DayNightFogBand &band) {
  env.dwords[kOutdoorFogColorIndex] = band.packed_argb;
  env.WriteFloat(kOutdoorFogEndDistanceIndex, band.end_distance);
  const float start_factor =
      (std::abs(band.end_distance) > 1e-6f) ? band.start_distance / band.end_distance : 0.0f;
  env.WriteFloat(kOutdoorFogStartFactorIndex, start_factor);
  env.WriteFloat(kOutdoorFogDensityIndex, band.density);
}

float ComputeScreenEffectFogDensity(const float start_factor, const float end_distance,
                                    const float far_clip) {

  return DayNight_ComputeAdvancedFogDensity(end_distance - start_factor * end_distance,
                                            far_clip);
}

std::array<float, 3> PackedArgbToRgbFloats(const std::uint32_t packed_argb) {
  constexpr float kInv255 = 0.003921569f;
  return {
      static_cast<float>((packed_argb >> 16) & 0xFFu) * kInv255,
      static_cast<float>((packed_argb >> 8) & 0xFFu) * kInv255,
      static_cast<float>(packed_argb & 0xFFu) * kInv255,
  };
}

std::array<float, 3> NormalizeSceneLightingDirection(
    const std::array<float, 3> &direction) {
  constexpr float kNormalizeEpsilon = 2.3841858e-07f;
  const float length_sq = direction[0] * direction[0] + direction[1] * direction[1] +
                          direction[2] * direction[2];
  if (length_sq <= kNormalizeEpsilon) {
    return direction;
  }

  const float inv_length = 1.0f / std::sqrt(length_sq);
  return {direction[0] * inv_length, direction[1] * inv_length, direction[2] * inv_length};
}

}

std::uint32_t DayNight_ScalePackedArgbRgbByByte(const std::uint32_t packed_argb,
                                                const std::uint8_t scale_byte) {
  const auto scale_channel = [&](const int shift) -> std::uint32_t {
    const auto channel = static_cast<std::uint8_t>((packed_argb >> shift) & 0xFFu);
    const auto scaled = static_cast<std::uint8_t>(
        ((static_cast<std::uint16_t>(scale_byte) * channel) + 0xFFu) >> 8);
    return static_cast<std::uint32_t>(scaled) << shift;
  };

  return (packed_argb & 0xFF000000u) | scale_channel(16) | scale_channel(8) | scale_channel(0);
}

DayNightSceneLightingBranchResult
DayNight_EvaluateSceneLightingBranch(
    const DayNightSceneLightingBranchInput &input) {
  DayNightSceneLightingBranchResult result{};

  if (input.force_full_lighting) {
    result.visibility_scale = 1.0f;
    result.enable_full_lighting = true;
    result.reset_area_lighting_blend = true;
    if (input.has_override_position) {
      result.has_point_position = true;
      result.point_position = input.override_position;
    }
    return result;
  }

  const float overlay_factor = input.scene_visibility * 0.35f;
  result.overlay_factor = overlay_factor;
  result.visibility_scale = 1.0f - overlay_factor;
  if (overlay_factor != 0.0f || std::isnan(overlay_factor)) {
    result.apply_visibility_overlay = true;
    result.overlay_argb = 0xFF202020u;
  } else {
    result.enable_full_lighting = true;
  }
  return result;
}

DayNightSceneLightingSyncPlan
DayNight_BuildSceneLightingSyncPlan(
    const DayNightSceneLightingBranchInput &input) {
  DayNightSceneLightingSyncPlan plan{};
  plan.sample_override_position = input.has_override_position;
  plan.branch = DayNight_EvaluateSceneLightingBranch(input);
  return plan;
}

DayNightSceneLightingCache
DayNight_FinalizeSceneLightingCache(const DayNightSceneLightingTailInput &input) {
  const auto rounded_scale =
      static_cast<std::int32_t>(std::nearbyint(input.visibility_scale * 255.0f));
  const auto scale_byte = static_cast<std::uint8_t>(rounded_scale & 0xFF);

  DayNightSceneLightingCache cache{};
  cache.direction = NormalizeSceneLightingDirection(input.derived_direction);
  cache.scaled_ambient_argb = DayNight_ScalePackedArgbRgbByByte(input.ambient_argb, scale_byte);
  cache.scaled_diffuse_argb = DayNight_ScalePackedArgbRgbByByte(input.diffuse_argb, scale_byte);
  cache.ambient_rgb = PackedArgbToRgbFloats(cache.scaled_ambient_argb);
  cache.diffuse_rgb = PackedArgbToRgbFloats(cache.scaled_diffuse_argb);
  cache.unscaled_rgb = PackedArgbToRgbFloats(input.unscaled_argb);
  return cache;
}

float DayNight_ComputeAdvancedFogDensity(const float fog_band_span,
                                         const float camera_far_clip) {
  const float fade_window = (camera_far_clip >= kAdvancedFogWideFarClip)
                                ? kAdvancedFogWideFadeWindow
                                : camera_far_clip - kAdvancedFogNarrowWindowMargin;
  if (fog_band_span <= fade_window) {
    return (1.0f - fog_band_span / fade_window) * kAdvancedFogDensityRange +
           kAdvancedFogDensityFloor;
  }
  return kAdvancedFogDensityFloor;
}

std::uint32_t DayNight_ScaleFogBandColor(const std::uint32_t packed_argb,
                                         const float fog_band_color_scale) {
  const auto inverse_scale =
      static_cast<std::int32_t>(std::nearbyint((1.0f - fog_band_color_scale) * 255.0f));
  return DayNight_ScalePackedArgbRgbByByte(packed_argb,
                                           static_cast<std::uint8_t>(inverse_scale));
}

void DayNight_ApplyFadeToFogBand(DayNightFogBand &band, const float fade_factor,
                                 const std::uint32_t fade_color_argb) {
  band.start_distance *= fade_factor;
  band.end_distance *= fade_factor;
  band.density *= fade_factor;

  const auto inverse_blend =
      static_cast<std::int32_t>(std::nearbyint((1.0f - fade_factor) * 255.0f));
  const auto blend_factor = static_cast<std::uint8_t>(inverse_blend);
  band.packed_argb = BlendPackedArgbRgb(band.packed_argb, blend_factor, fade_color_argb);
}

void DayNight_SetScreenEffectFogOverride(const float start_factor, const float end_distance,
                                         const std::uint32_t color_argb,
                                         const std::uint32_t sky_dome_enabled) {
  if (!s_screenEffectFogOverride.active) {
    s_screenEffectFogOverride.savedOutdoorBand = ReadOutdoorFogBandFromEnv(s_lightEnv);
    s_screenEffectFogOverride.savedSkyDomeEnabled = s_screenEffectFogOverride.skyDomeEnabled;
  }

  s_screenEffectFogOverride.startFactor = start_factor;
  s_screenEffectFogOverride.endDistance = end_distance;
  s_screenEffectFogOverride.colorArgb = color_argb;
  s_screenEffectFogOverride.skyDomeEnabled = sky_dome_enabled;
  s_screenEffectFogOverride.density =
      ComputeScreenEffectFogDensity(start_factor, end_distance,
                                    s_lightEnv.ReadFloat(kFogFarClipIndex));
  s_screenEffectFogOverride.active = true;
}

void DayNight_ClearScreenEffectFogOverride() {
  if (!s_screenEffectFogOverride.active) {
    return;
  }

  const DayNightFogBand saved_band = s_screenEffectFogOverride.savedOutdoorBand;
  const std::uint32_t saved_sky_dome_enabled = s_screenEffectFogOverride.savedSkyDomeEnabled;
  s_screenEffectFogOverride = {};
  s_screenEffectFogOverride.skyDomeEnabled = saved_sky_dome_enabled;
  WriteOutdoorFogBandToEnv(s_lightEnv, saved_band);
}

bool DayNight_HasScreenEffectFogOverride() {
  return s_screenEffectFogOverride.active;
}

DayNightScreenEffectFogOverride DayNight_GetScreenEffectFogOverride() {
  return DayNightScreenEffectFogOverride{
      .active = s_screenEffectFogOverride.active,
      .start_factor = s_screenEffectFogOverride.startFactor,
      .end_distance = s_screenEffectFogOverride.endDistance,
      .density = s_screenEffectFogOverride.density,
      .color_argb = s_screenEffectFogOverride.colorArgb,
      .sky_dome_enabled = s_screenEffectFogOverride.skyDomeEnabled,
  };
}

bool DayNight_IsSkyDomeEnabled() {
  return s_screenEffectFogOverride.skyDomeEnabled != 0u;
}

namespace {

DayNightFogBand ReadFogBandFromEnv(const DayNightLightEnv &env, std::size_t base) {
  DayNightFogBand band;
  std::memcpy(&band, &env.dwords[base], sizeof(band));
  return band;
}

void WriteFogBandToEnv(DayNightLightEnv &env, std::size_t base,
                       const DayNightFogBand &band) {
  std::memcpy(&env.dwords[base], &band, sizeof(band));
}

}

void DayNight_SetOutdoorFogBand(const DayNightFogBand &band) {
  DayNight_WriteOutdoorFogBand(s_lightEnv, band);
}

void DayNight_WriteOutdoorFogBand(DayNightLightEnv &env,
                                  const DayNightFogBand &band) {
  WriteOutdoorFogBandToEnv(env, band);
}

DayNightFogBand DayNight_GetCurrentFogBand() {
  return ReadFogBandFromEnv(s_lightEnv, kCurrentFogBandBaseIndex);
}

DayNightFogBand DayNight_GetBlendedFogBand() {
  return ReadFogBandFromEnv(s_lightEnv, kBlendedFogBandBaseIndex);
}

void DayNight_UpdateFogBands() {

  float fog_end = s_lightEnv.ReadFloat(kFogFarClipIndex);
  float density;

  if (s_screenEffectFogOverride.active) {
    if (s_screenEffectFogOverride.endDistance < fog_end)
      fog_end = s_screenEffectFogOverride.endDistance;
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 2, fog_end);
    s_lightEnv.WriteInt32(kCurrentFogBandBaseIndex,
                          static_cast<std::int32_t>(s_screenEffectFogOverride.colorArgb));
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 1,
                          fog_end * s_screenEffectFogOverride.startFactor);
    density = s_screenEffectFogOverride.density;
  } else {
    const float outdoor_end = s_lightEnv.ReadFloat(kOutdoorFogEndDistanceIndex);
    if (outdoor_end < fog_end)
      fog_end = outdoor_end;
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 2, fog_end);
    s_lightEnv.dwords[kCurrentFogBandBaseIndex] =
        s_lightEnv.dwords[kOutdoorFogColorIndex];
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 1,
                          fog_end * s_lightEnv.ReadFloat(kOutdoorFogStartFactorIndex));
    density = s_lightEnv.ReadFloat(kOutdoorFogDensityIndex);
  }
  s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 3, density);

  const bool has_area_lighting = (s_areaLightOverrides.count > 0u);
  const float area_blend_weight =
      has_area_lighting
          ? s_lightEnv.ReadFloat(kDayNightActiveAreaBlendWeightIndex)
          : 0.0f;
  const bool transition_dirty = s_transitionDirty;
  if (transition_dirty) {
    s_transitionDirty = false;
  }

  const std::uint32_t camera_liquid_type_id =
      DayNight_GetCameraLiquidState().liquid_type_id;

  float current_density = density;

  float progress = area_blend_weight * 0.039999999f;
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;
  s_transitionProgress = progress;
  s_activeTransitions = 0;
  s_transitionDataCount = 0;

  float blended_density;
  if (has_area_lighting && transition_dirty) {

    const float cur_end = s_lightEnv.ReadFloat(kCurrentFogBandBaseIndex + 2);
    const float cur_start = s_lightEnv.ReadFloat(kCurrentFogBandBaseIndex + 1);
    const float prev_end = s_lightEnv.ReadFloat(kPreviousFogBandBaseIndex + 2);
    const float prev_start = s_lightEnv.ReadFloat(kPreviousFogBandBaseIndex + 1);
    const float prev_density = s_lightEnv.ReadFloat(kPreviousFogBandBaseIndex + 3);

    const float blended_end = cur_end + (prev_end - cur_end) * progress;
    const float blended_start = cur_start + (prev_start - cur_start) * progress;
    blended_density = current_density + (prev_density - current_density) * progress;

    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 2, blended_end);
    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 1, blended_start);
    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 3, blended_density);

    const auto blend_byte = static_cast<std::uint8_t>(
        static_cast<std::int32_t>(std::nearbyint(progress * 255.0f)));
    std::uint32_t blended_color = s_lightEnv.dwords[kCurrentFogBandBaseIndex];
    if (blend_byte != 0u) {
      blended_color = BlendPackedArgbRgb(
          blended_color, blend_byte, s_lightEnv.dwords[kPreviousFogBandBaseIndex]);
    }

    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 2, blended_end);
    s_lightEnv.dwords[kBlendedFogBandBaseIndex] = blended_color;
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 1, blended_start);
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 3, blended_density);
    current_density = blended_density;
  } else {

    s_lightEnv.dwords[kBlendedFogBandBaseIndex] =
        s_lightEnv.dwords[kCurrentFogBandBaseIndex];
    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 1,
                          s_lightEnv.ReadFloat(kCurrentFogBandBaseIndex + 1));
    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 2,
                          s_lightEnv.ReadFloat(kCurrentFogBandBaseIndex + 2));
    blended_density = current_density;
    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 3, blended_density);
  }

  if (s_fogMode == 1 && camera_liquid_type_id != 0) {
    s_lightEnv.WriteFloat(kCurrentFogBandBaseIndex + 3, current_density * 2.0f);
    s_lightEnv.WriteFloat(kBlendedFogBandBaseIndex + 3, blended_density * 2.0f);
  }

  const std::uint32_t sky_mask = DayNight_GetSkyUpdateMask();
  if ((sky_mask & 2u) != 0) {
    s_lightEnv.dwords[kCurrentFogBandBaseIndex] =
        DayNight_ScaleFogBandColor(s_lightEnv.dwords[kCurrentFogBandBaseIndex],
                                   s_fogBandColorScale);
    s_lightEnv.dwords[kBlendedFogBandBaseIndex] =
        DayNight_ScaleFogBandColor(s_lightEnv.dwords[kBlendedFogBandBaseIndex],
                                   s_fogBandColorScale);
  }

  if ((sky_mask & 1u) != 0) {
    const float fog_distance = s_lightEnv.ReadFloat(kFogDistanceIndex);
    if (fog_distance <= static_cast<double>(s_fogFadeMaxDistance)) {
      const double exponent =
          (static_cast<double>(fog_distance) - static_cast<double>(s_fogFadeBaseOffset)) /
          static_cast<double>(s_fogFadeDistanceScale) * 7.2134752;
      double inv_exp2 = 1.0 / math::exp2_approx::Evaluate(exponent);
      if (inv_exp2 < 0.0) inv_exp2 = 0.0;
      if (inv_exp2 > 1.0) inv_exp2 = 1.0;
      s_fogFadeFactor = static_cast<float>(1.0 - inv_exp2);

      DayNightFogBand current_band =
          ReadFogBandFromEnv(s_lightEnv, kCurrentFogBandBaseIndex);
      DayNight_ApplyFadeToFogBand(current_band, s_fogFadeFactor, s_fogFadeColor);
      WriteFogBandToEnv(s_lightEnv, kCurrentFogBandBaseIndex, current_band);

      DayNightFogBand blended_band =
          ReadFogBandFromEnv(s_lightEnv, kBlendedFogBandBaseIndex);
      DayNight_ApplyFadeToFogBand(blended_band, s_fogFadeFactor, s_fogFadeColor);
      WriteFogBandToEnv(s_lightEnv, kBlendedFogBandBaseIndex, blended_band);
    } else {
      DayNight_SetSkyUpdateMask(sky_mask & ~1u);
    }
  }
}

std::int32_t DayNight_GetFogMode() { return s_fogMode; }
void DayNight_SetFogMode(const std::int32_t mode) { DayNight_SetFogModeFromRenderPath(mode); }

void DayNight_SetFogModeFromRenderPath(std::int32_t mode) {
  if (mode > 1) {
    mode = s_fogMode;
  }
  s_fogMode = mode;
}

void DayNight_SetFogBandColorScale(const float scale) { s_fogBandColorScale = scale; }
void DayNight_SetFogFadeColor(const std::uint32_t color_argb) { s_fogFadeColor = color_argb; }

void DayNight_SetFogFadeDistanceParams(const float distance_scale, const float max_distance,
                                        const float base_offset) {
  s_fogFadeDistanceScale = distance_scale;
  s_fogFadeMaxDistance = max_distance;
  s_fogFadeBaseOffset = base_offset;
}

DayNightLightEnv *DayNight_GetLightEnv() {
  return &s_lightEnv;
}

float DayNight_GetSceneVisibility() {
  return s_lightEnv.ReadFloat(kLightEnvSceneVisibilityIndex);
}

void DayNight_SetSceneVisibility(const float visibility) {
  s_lightEnv.WriteFloat(kLightEnvSceneVisibilityIndex, visibility);
}

void DayNight_ResetLightingAccumulatorState() {
  s_lightingAccumulatorState = {};
}

void DayNight_SetLightingAccumulatorInputs(const DayNightVec3 &camera_position,
                                           const float normalized_time_of_day) {
  s_lightingAccumulatorState.camera_position = camera_position;
  s_lightingAccumulatorState.normalized_time_of_day = normalized_time_of_day;
}

void DayNight_SetLightingAccumulatorBlendState(const float blend_value_a,
                                               const float blend_value_b,
                                               const std::int32_t pending_refresh_count,
                                               const bool force_lighting_refresh) {
  s_lightingAccumulatorState.blend_value_a = blend_value_a;
  s_lightingAccumulatorState.blend_value_b = blend_value_b;
  s_lightingAccumulatorState.pending_refresh_count = pending_refresh_count;
  s_lightingAccumulatorState.force_lighting_refresh = force_lighting_refresh;
}

const DayNightLightingAccumulatorState &DayNight_GetLightingAccumulatorState() {
  return s_lightingAccumulatorState;
}

void DayNight_ResetAreaLightingBlendAndForceFlag(const bool force_lighting_refresh) {
  s_lightingAccumulatorState.blend_value_a = 0.0f;
  s_lightingAccumulatorState.blend_value_b = 0.0f;
  s_lightingAccumulatorState.pending_refresh_count = 0;
  if (force_lighting_refresh) {
    s_lightingAccumulatorState.force_lighting_refresh = true;
  }
}

void DayNight_RefreshLightingAccumulator() {
  ++s_lightingAccumulatorState.refresh_count;
}

void DayNight_PublishLightingVectorAndAlpha() {
  s_lightingAccumulatorState.published_camera_position =
      s_lightingAccumulatorState.camera_position;
  const float normalized_time_of_day = s_lightingAccumulatorState.normalized_time_of_day;
  ++s_lightingAccumulatorState.publish_update_count;
  s_lightingAccumulatorState.published_alpha =
      static_cast<std::uint8_t>(static_cast<std::int32_t>(
          normalized_time_of_day * 254.0f + 1.0f));
}

void DayNight_ResetNearbyLightQueue(DayNightLightEnv &env) {
  env.ResetNearbyLightQueue();
}

void DayNightLightEnv_Init(DayNightLightEnv &env) {
  static_assert(std::is_trivially_copyable_v<DayNightLightEnv>);
  std::memset(&env, 0, sizeof(env));
}

void DayNight_UpdateDerivedDirectionCache(DayNightLightEnv &env) {
  const float normalized_time_of_day = env.ReadFloat(kLightEnvNormalizedTimeOfDayIndex);
  const float polar_angle = EvaluateWrappedFloatCurve(
      kDerivedDirectionPolarAngleCurve,
      sizeof(kDerivedDirectionPolarAngleCurve) / sizeof(kDerivedDirectionPolarAngleCurve[0]),
      normalized_time_of_day);
  const float azimuth_angle = EvaluateWrappedFloatCurve(
      kDerivedDirectionAzimuthAngleCurve,
      sizeof(kDerivedDirectionAzimuthAngleCurve) / sizeof(kDerivedDirectionAzimuthAngleCurve[0]),
      normalized_time_of_day);

  const float sin_polar = EvaluateDayNightWave(polar_angle * kSkyAngleToUnitScale - 0.5f);
  const float cos_polar = EvaluateDayNightWave(polar_angle * kSkyAngleToUnitScale);
  const float sin_azimuth = EvaluateDayNightWave(azimuth_angle * kSkyAngleToUnitScale - 0.5f);
  const float cos_azimuth = EvaluateDayNightWave(azimuth_angle * kSkyAngleToUnitScale);

  env.WriteFloat(kDayNightDerivedDirectionCurrentBaseIndex + 0, cos_azimuth * sin_polar);
  env.WriteFloat(kDayNightDerivedDirectionCurrentBaseIndex + 1, sin_polar * sin_azimuth);
  env.WriteFloat(kDayNightDerivedDirectionCurrentBaseIndex + 2, cos_polar);
}

void DayNight_EvaluateFullLightState() {
  DayNight_UpdateAreaLightOverrideTransitions(
      openwow::core::GameClock::GetTickCount32());

  const DayNightCameraLiquidState camera_liquid = DayNight_GetCameraLiquidState();
  (void)camera_liquid;

  const bool has_active_override = (s_areaLightOverrides.count > 0) &&
                                    (!s_areaLightOverrides.storage.empty());

  if (has_active_override) {

  }

  std::uint32_t ambient_upper = s_lightEnv.dwords[kDayNightDbcAmbientUpperColorIndex];
  std::uint32_t ambient_lower = s_lightEnv.dwords[kDayNightDbcAmbientLowerColorIndex];
  std::uint32_t diffuse_upper = s_lightEnv.dwords[kDayNightDbcDiffuseUpperColorIndex];
  std::uint32_t diffuse_lower = s_lightEnv.dwords[kDayNightDbcDiffuseLowerColorIndex];
  std::uint32_t glow_upper = s_lightEnv.dwords[kDayNightDbcGlowUpperColorIndex];
  std::uint32_t glow_lower = s_lightEnv.dwords[kDayNightDbcGlowLowerColorIndex];

  const bool has_dbc_data = (ambient_upper != 0u || ambient_lower != 0u ||
                              diffuse_upper != 0u || diffuse_lower != 0u);
  if (!has_dbc_data) {
    ambient_upper = 0x00404040u;
    ambient_lower = 0x00202020u;
    diffuse_upper = 0x00FFFFFFu;
    diffuse_lower = 0x00C0C0C0u;
    glow_upper = 0x00808080u;
    glow_lower = 0x00404040u;
  }

  const float dir_z = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 2);
  const float vertical_blend = std::clamp(dir_z * 0.5f + 0.5f, 0.0f, 1.0f);

  const auto blend_hemispheres = [](std::uint32_t upper, std::uint32_t lower,
                                     float blend) -> std::uint32_t {

    const auto blend_byte = static_cast<std::uint8_t>(
        static_cast<std::int32_t>(std::nearbyint(blend * 255.0f)));
    if (blend_byte == 0u) {
      return (lower & 0xFF000000u) | (lower & 0x00FFFFFFu);
    }
    if (blend_byte >= 0xFFu) {
      return (upper & 0xFF000000u) | (upper & 0x00FFFFFFu);
    }
    return BlendPackedArgbRgb(lower, blend_byte, upper);
  };

  std::uint32_t ambient_color = blend_hemispheres(ambient_upper, ambient_lower, vertical_blend);
  std::uint32_t diffuse_color = blend_hemispheres(diffuse_upper, diffuse_lower, vertical_blend);
  std::uint32_t glow_color = blend_hemispheres(glow_upper, glow_lower, vertical_blend);

  const float glow_factor = s_lightEnv.ReadFloat(kLightEnvGlowBlendFactorIndex);
  if (glow_factor > 0.0f) {

    const auto glow_blend_byte = static_cast<std::uint8_t>(
        static_cast<std::int32_t>(std::nearbyint(glow_factor * 255.0f)));
    if (glow_blend_byte >= 0xFFu) {
      ambient_color = (ambient_color & 0xFF000000u) | (glow_color & 0x00FFFFFFu);
      diffuse_color = (diffuse_color & 0xFF000000u) | (glow_color & 0x00FFFFFFu);
    } else {
      ambient_color = BlendPackedArgbRgb(ambient_color, glow_blend_byte, glow_color);
      diffuse_color = BlendPackedArgbRgb(diffuse_color, glow_blend_byte, glow_color);
    }
  }

  const auto &tint = s_spellVisualLightingTint;
  if (tint.blend_factor != 0u) {
    if (tint.blend_factor >= 0xFFu) {
      ambient_color = (ambient_color & 0xFF000000u) | (tint.packed_argb & 0x00FFFFFFu);
      diffuse_color = (diffuse_color & 0xFF000000u) | (tint.packed_argb & 0x00FFFFFFu);
    } else {
      ambient_color = BlendPackedArgbRgb(ambient_color, tint.blend_factor, tint.packed_argb);
      diffuse_color = BlendPackedArgbRgb(diffuse_color, tint.blend_factor, tint.packed_argb);
    }
  }

  s_lightEnv.dwords[kDayNightInterpolatedAmbientColorIndex] = ambient_color;
  s_lightEnv.dwords[kDayNightInterpolatedDiffuseColorIndex] = diffuse_color;

}

void DayNight_ComputeLightHeadingAndGlow() {

  constexpr float kHorizontalEpsilonSq = 0.0001f;

  const float dir_x = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 0);
  const float dir_y = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 1);
  const float dir_z = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 2);

  float heading;
  if (dir_x * dir_x + dir_y * dir_y <= kHorizontalEpsilonSq) {
    heading = std::atan2(dir_z, dir_x);
  } else {
    heading = std::atan2(dir_y, dir_x);
  }

  if (heading < 0.0f) {
    heading += kTwoPi;
  }
  s_lightEnv.WriteFloat(kLightEnvLightHeadingAngleIndex, heading);

  float glow = s_lightEnv.ReadFloat(kLightEnvWeatherOverlayFactorIndex) * 4.0f;
  if (glow > 1.0f) {
    glow = 1.0f;
  }
  s_lightEnv.WriteFloat(kLightEnvGlowBlendFactorIndex, glow);

  DayNight_EvaluateFullLightState();
  DayNight_DeriveAmbientDiffuseColorCache(s_lightEnv);
  DayNight_UpdateDerivedDirectionCache(s_lightEnv);
  DayNight_UpdateCelestialTextureState();
}

void DayNight_DeriveAmbientDiffuseColorCache(DayNightLightEnv &env) {
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 0] =
      env.dwords[kDayNightDerivedDirectionCurrentBaseIndex + 0];
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 1] =
      env.dwords[kDayNightDerivedDirectionCurrentBaseIndex + 1];
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 2] =
      env.dwords[kDayNightDerivedDirectionCurrentBaseIndex + 2];

  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 7] =
      env.dwords[kDayNightDerivedColorCurrentHsvRgbBaseIndex + 0];
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 8] =
      env.dwords[kDayNightDerivedColorCurrentHsvRgbBaseIndex + 1];
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 9] =
      env.dwords[kDayNightDerivedColorCurrentHsvRgbBaseIndex + 2];
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 10] =
      env.dwords[kDayNightDerivedColorCurrentHsvRgbBaseIndex + 3];

  const std::uint32_t ambient_color = env.dwords[kDayNightInterpolatedAmbientColorIndex];
  const std::uint32_t diffuse_color = env.dwords[kDayNightInterpolatedDiffuseColorIndex];
  const PackedRgbChannels ambient_rgb = UnpackPackedArgbRgb(ambient_color);
  const PackedRgbChannels diffuse_rgb = UnpackPackedArgbRgb(diffuse_color);

  const PackedRgbChannels midpoint_rgb = {
      BlendColorChannelHalfTowards(diffuse_rgb.red, ambient_rgb.red),
      BlendColorChannelHalfTowards(diffuse_rgb.green, ambient_rgb.green),
      BlendColorChannelHalfTowards(diffuse_rgb.blue, ambient_rgb.blue),
  };
  const PackedRgbChannels brightened_midpoint_rgb = {
      SaturatingAddColorByte(BlendColorChannelHalfTowards(ambient_rgb.red, diffuse_rgb.red), 0x10u),
      SaturatingAddColorByte(BlendColorChannelHalfTowards(ambient_rgb.green, diffuse_rgb.green),
                             0x10u),
      SaturatingAddColorByte(BlendColorChannelHalfTowards(ambient_rgb.blue, diffuse_rgb.blue),
                             0x10u),
  };

  const std::uint32_t midpoint_color =
      PackArgb(0x00u, midpoint_rgb.red, midpoint_rgb.green, midpoint_rgb.blue);
  const std::uint32_t brightened_midpoint_color =
      PackArgb(0xFFu, brightened_midpoint_rgb.red, brightened_midpoint_rgb.green,
               brightened_midpoint_rgb.blue);

  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 3] = diffuse_color;
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 4] = ambient_color;
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 5] = midpoint_color;
  env.dwords[kDayNightDerivedColorHistoryBaseIndex + 6] = brightened_midpoint_color;

  env.dwords[kDayNightDerivedColorCurrentDiffuseIndex] = diffuse_color;
  env.dwords[kDayNightDerivedColorCurrentAmbientIndex] = ambient_color;
  env.dwords[kDayNightDerivedColorCurrentMidpointIndex] = midpoint_color;
  env.dwords[kDayNightDerivedColorCurrentBrightenedMidpointIndex] = brightened_midpoint_color;

  const float red = static_cast<float>(ambient_rgb.red) * (1.0f / 255.0f);
  const float green = static_cast<float>(ambient_rgb.green) * (1.0f / 255.0f);
  const float blue = static_cast<float>(ambient_rgb.blue) * (1.0f / 255.0f);
  HsvColor hsv = ConvertRgbToHsv(red, green, blue);
  hsv.saturation *= 0.33000001f;
  hsv.value *= 1.25f;

  const auto rgb = ConvertHsvToRgb(hsv);
  env.WriteFloat(kDayNightDerivedColorCurrentHsvRgbBaseIndex + 0, rgb[0]);
  env.WriteFloat(kDayNightDerivedColorCurrentHsvRgbBaseIndex + 1, rgb[1]);
  env.WriteFloat(kDayNightDerivedColorCurrentHsvRgbBaseIndex + 2, rgb[2]);
  env.WriteFloat(kDayNightDerivedColorCurrentHsvRgbBaseIndex + 3, 1.0f);

  const std::uint8_t reduced_alpha = env.ReadByte(kDayNightDerivedColorDimAmbientAlphaByteOffset);
  env.dwords[kDayNightDerivedColorCurrentDimAmbientIndex] = PackArgb(
      reduced_alpha, ComputeReducedAmbientByte(ambient_rgb.red),
      ComputeReducedAmbientByte(ambient_rgb.green), ComputeReducedAmbientByte(ambient_rgb.blue));
}

void DayNight_ClearActiveAreaOwner(const std::uint32_t owner_token) {
  auto *const env = DayNight_GetLightEnv();
  if (env->ReadInt32(kDayNightActiveAreaOwnerIndex) !=
      static_cast<std::int32_t>(owner_token)) {
    return;
  }

  env->WriteInt32(kDayNightActiveAreaOwnerIndex, 0);
  env->WriteFloat(kDayNightActiveAreaBlendWeightIndex, 0.0f);
  env->WriteInt32(kDayNightActiveAreaSkyboxIndex, 0);
}

void DayNight_SetLightRecordLookup(const std::int32_t first_light_id,
                                   const std::vector<std::uint32_t> &resolved_handles) {
  s_lightRecordLookup.firstLightId = first_light_id;
  s_lightRecordLookup.resolvedHandles = resolved_handles;
}

void DayNight_ClearLightRecordLookup() {
  s_lightRecordLookup.firstLightId = 0;
  s_lightRecordLookup.resolvedHandles.clear();
}

bool DayNight_QueueNearbyLightRecord(DayNightLightEnv &env, const std::int32_t light_rec_id,
                                     const float weight) {
  const std::int64_t lookup_index = static_cast<std::int64_t>(light_rec_id) -
                                    static_cast<std::int64_t>(s_lightRecordLookup.firstLightId);
  if (lookup_index < 0 ||
      static_cast<std::size_t>(lookup_index) >= s_lightRecordLookup.resolvedHandles.size()) {
    return false;
  }

  const std::uint32_t resolved_handle =
      s_lightRecordLookup.resolvedHandles[static_cast<std::size_t>(lookup_index)];
  return env.TryAppendNearbyLightRecord(resolved_handle, weight);
}

std::uint32_t DayNight_ClearSkyUpdateMaskBit(const std::int32_t bit_index) {
  const std::uint32_t shift =
      static_cast<std::uint32_t>(static_cast<std::uint8_t>(bit_index)) & 0x1Fu;
  const std::uint32_t clear_mask = ~(std::uint32_t{1} << shift);
  s_skyUpdateMask &= clear_mask;
  return clear_mask;
}

std::uint32_t DayNight_GetSkyUpdateMask() {
  return s_skyUpdateMask;
}

void DayNight_SetSkyUpdateMask(const std::uint32_t mask) {
  s_skyUpdateMask = mask;
}

bool DayNight_SolveOrderedQuadraticRootsStable(float a, float b, float c, float &smallerRoot,
                                               float &largerRoot) {
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant <= 0.0f) {
    return false;
  }

  const float sqrtDiscriminant = std::sqrt(discriminant);
  const float q = (b <= 0.0f) ? -0.5f * (b - sqrtDiscriminant) : -0.5f * (b + sqrtDiscriminant);
  const float root0 = c / q;
  const float root1 = q / a;

  if (root0 <= root1) {
    smallerRoot = root0;
    largerRoot = root1;
  } else {
    smallerRoot = root1;
    largerRoot = root0;
  }
  return true;
}

void DayNight_SetCameraLiquidState(const float submersion_depth,
                                   const std::uint32_t liquid_type_id) {
  s_cameraLiquidState.submersionDepth = submersion_depth;
  s_cameraLiquidState.liquidTypeId = liquid_type_id;
}

void DayNight_ClearCameraLiquidState() {
  s_cameraLiquidState = {};
}

DayNightCameraLiquidState DayNight_GetCameraLiquidState() {
  return DayNightCameraLiquidState{
      .submersion_depth = s_cameraLiquidState.submersionDepth,
      .liquid_type_id = s_cameraLiquidState.liquidTypeId,
  };
}

float DayNight_ComputeCameraLiquidDepthFade() {
  const DayNightCameraLiquidState query = DayNight_GetCameraLiquidState();
  if (query.liquid_type_id == 0u) {
    return 1.0f;
  }

  return 1.0f - std::clamp(query.submersion_depth * 0.1f, 0.0f, 1.0f);
}

bool DayNight_ProjectDirectionOntoCloudSphere(const DayNightVec3 &cameraPosition,
                                              const DayNightVec3 &direction, float &smallerRoot,
                                              float &largerRoot, DayNightVec3 &worldPoint) {
  const float a = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
  const float b = -2.0f * kCloudSphereCenterOffsetZ * direction.z;
  const float c = kCloudSphereCenterOffsetZ * kCloudSphereCenterOffsetZ - 1.0f;

  if (!DayNight_SolveOrderedQuadraticRootsStable(a, b, c, smallerRoot, largerRoot)) {
    return false;
  }

  worldPoint.x = cameraPosition.x + direction.x * largerRoot;
  worldPoint.y = cameraPosition.y + direction.y * largerRoot;
  worldPoint.z = cameraPosition.z + direction.z * largerRoot;
  return true;
}

DayNightVec2 DayNight_ProjectCloudPointToTexture(const DayNightVec3 &cameraPosition,
                                                 const DayNightVec3 &worldPoint,
                                                 float textureSize) {
  const float localX = worldPoint.x - cameraPosition.x;
  const float localY = worldPoint.y - cameraPosition.y;
  const float localZ = worldPoint.z - cameraPosition.z - kCloudSphereCenterOffsetZ;

  float angle = std::acos(localZ / std::sqrt(localX * localX + localY * localY + localZ * localZ));
  if (angle > kQuarterTurn) {
    angle = kQuarterTurn;
  }

  const float radial = angle * kCloudTextureAngleScale;
  const float planarLength = std::sqrt(localX * localX + localY * localY);

  float normalisedX = 0.0f;
  float normalisedY = 0.0f;
  if (planarLength > 0.00001f) {
    const float inversePlanarLength = 1.0f / planarLength;
    normalisedX = localX * inversePlanarLength;
    normalisedY = localY * inversePlanarLength;
  }

  return {
      (normalisedX * radial + 0.5f) * textureSize,
      (normalisedY * radial + 0.5f) * textureSize,
  };
}

bool DayNight_ProjectDirectionToCloudTexture(const DayNightVec3 &cameraPosition,
                                             const DayNightVec3 &direction, float textureSize,
                                             DayNightCloudTextureProjection &projection) {
  if (!DayNight_ProjectDirectionOntoCloudSphere(cameraPosition, direction, projection.nearRoot,
                                                projection.farRoot, projection.worldPoint)) {
    return false;
  }

  projection.texturePosition =
      DayNight_ProjectCloudPointToTexture(cameraPosition, projection.worldPoint, textureSize);
  return true;
}

float DayNight_SampleCloudLayerOpacityAtWorldPoint(const DayNightVec3 &worldPoint) {
  if (!s_cloudLayerState.enabled || s_cloudLayerState.gridSize == 0u ||
      s_cloudLayerState.coverageBuffer.empty()) {
    return 0.0f;
  }

  const DayNightVec3 cameraPosition = {
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 0),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 1),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 2),
  };
  const DayNightVec2 texturePosition = DayNight_ProjectCloudPointToTexture(
      cameraPosition, worldPoint, static_cast<float>(s_cloudLayerState.gridSize));
  const std::uint32_t maxIndex = s_cloudLayerState.gridSize - 1u;
  const auto clampTextureCoordinate = [maxIndex](const float coordinate) -> std::uint32_t {
    std::int32_t truncated = static_cast<std::int32_t>(std::trunc(coordinate));
    if (static_cast<std::uint32_t>(truncated) > maxIndex) {
      truncated = static_cast<std::int32_t>(maxIndex) & ~(truncated >> 31);
    }
    return static_cast<std::uint32_t>(truncated);
  };

  const std::uint32_t x = clampTextureCoordinate(texturePosition.x);
  const std::uint32_t y = clampTextureCoordinate(texturePosition.y);
  const std::size_t sampleIndex =
      (static_cast<std::size_t>(y) << s_cloudLayerState.vertexStride) + x;
  if (sampleIndex >= s_cloudLayerState.coverageBuffer.size()) {
    return 0.0f;
  }

  return static_cast<float>(s_cloudLayerState.coverageBuffer[sampleIndex]) * 0.0039215689f;
}

namespace {

constexpr float kCloudGlowDayStart = 0.2013889f;
constexpr float kCloudGlowDayEnd = 0.9236111f;
constexpr float kCloudGlowBaseRadius = 64.0f;
constexpr float kCloudGlowHorizonRadiusScale = 192.0f;
constexpr float kCloudGlowHorizonStrengthScale = 0.75f;
constexpr float kByteToUnitRgbScale = 0.0039215689f;
constexpr std::size_t kLightEnvCloudEdgeColorIndex = 63;
constexpr std::size_t kLightEnvCloudColorIndex = 64;
constexpr std::size_t kLightEnvCloudHilightColorIndex = 65;
constexpr WrappedFloatCurvePoint kCloudGlowStrengthCurve[] = {
    {0.16666667f, 1.0f},
    {0.19444445f, 1.0f},
    {0.2013889f, 1.0f},
    {0.22916667f, 1.0f},
    {0.89583331f, 1.0f},
    {0.9236111f, 1.0f},
    {0.8888889f, 1.0f},
    {0.91666669f, 1.0f},
};

[[nodiscard]] DayNightVec3 PackedArgbToUnitRgb(const std::uint32_t packed_argb) {
  const PackedRgbChannels rgb = UnpackPackedArgbRgb(packed_argb);
  return {
      static_cast<float>(rgb.red) * kByteToUnitRgbScale,
      static_cast<float>(rgb.green) * kByteToUnitRgbScale,
      static_cast<float>(rgb.blue) * kByteToUnitRgbScale,
  };
}

[[nodiscard]] float ApplyGlareCloudFadeTransform(
    const float sample, const DayNightGlareCloudFadeTransform transform) {
  switch (transform) {
  case DayNightGlareCloudFadeTransform::InvertCloudAlpha:
    return 1.0f - sample;
  case DayNightGlareCloudFadeTransform::MidpointCloudBand:
    return 1.0f - std::fabs((sample - 0.5f) + (sample - 0.5f));
  case DayNightGlareCloudFadeTransform::None:
    return 0.0f;
  }

  return 0.0f;
}

}

float DayNight_TransformSunGlareCloudFade(const DayNightVec3 &worldPoint) {
  return ApplyGlareCloudFadeTransform(
      DayNight_SampleCloudLayerOpacityAtWorldPoint(worldPoint),
      DayNightGlareCloudFadeTransform::InvertCloudAlpha);
}

float DayNight_TransformMoonGlareCloudFade(const DayNightVec3 &worldPoint) {
  return ApplyGlareCloudFadeTransform(
      DayNight_SampleCloudLayerOpacityAtWorldPoint(worldPoint),
      DayNightGlareCloudFadeTransform::MidpointCloudBand);
}

float DayNight_ComputeGlareCloudFade(const DayNightGlareState &state) {
  return ApplyGlareCloudFadeTransform(DayNight_SampleCloudLayerOpacityAtWorldPoint(state.worldPosition),
                                      state.cloudFadeTransform);
}

DayNightCloudGlowParameters DayNight_BuildCloudGlowParameters() {
  DayNightCloudGlowParameters parameters{};

  const DayNightVec3 cameraPosition = {
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 0),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 1),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 2),
  };
  const float normalizedTimeOfDay = s_lightEnv.ReadFloat(kLightEnvNormalizedTimeOfDayIndex);
  const DayNightVec3 &celestialPosition =
      (normalizedTimeOfDay < kCloudGlowDayStart || normalizedTimeOfDay > kCloudGlowDayEnd)
          ? s_celestialState.moonPrimary.worldPosition
          : s_celestialState.sunCenter.worldPosition;
  const DayNightVec3 celestialDirection = {
      celestialPosition.x - cameraPosition.x,
      celestialPosition.y - cameraPosition.y,
      celestialPosition.z - cameraPosition.z,
  };

  DayNightCloudTextureProjection projection{};
  const float textureSize = static_cast<float>(s_cloudLayerState.gridSize);
  if (!DayNight_ProjectDirectionToCloudTexture(cameraPosition, celestialDirection, textureSize,
                                               projection)) {
    return parameters;
  }

  parameters.textureSpaceCenter = projection.texturePosition;
  if (textureSize > 0.0f) {
    parameters.normalizedTextureSpaceCenter = {
        projection.texturePosition.x / textureSize,
        projection.texturePosition.y / textureSize,
    };
  }

  parameters.cloudEdgeColor =
      PackedArgbToUnitRgb(s_lightEnv.dwords[kLightEnvCloudEdgeColorIndex]);
  parameters.cloudColor = PackedArgbToUnitRgb(s_lightEnv.dwords[kLightEnvCloudColorIndex]);
  parameters.cloudHilightColor =
      PackedArgbToUnitRgb(s_lightEnv.dwords[kLightEnvCloudHilightColorIndex]);

  const float curveFactor =
      EvaluateWrappedFloatCurve(kCloudGlowStrengthCurve,
                                sizeof(kCloudGlowStrengthCurve) / sizeof(kCloudGlowStrengthCurve[0]),
                                normalizedTimeOfDay);
  parameters.radius =
      kCloudGlowBaseRadius + s_celestialState.horizonFadeFactor * kCloudGlowHorizonRadiusScale;
  parameters.glowStrength =
      curveFactor * (1.0f - s_celestialState.horizonFadeFactor * kCloudGlowHorizonStrengthScale);
  return parameters;
}

namespace {

[[nodiscard]] std::uint8_t ComputeCloudAlphaStartByte(const float cloud_alpha) {

  const auto scaled = static_cast<std::int32_t>(
      std::nearbyint((1.0f - cloud_alpha) * 255.0f));
  return static_cast<std::uint8_t>(std::clamp(scaled, 0, 255));
}

[[nodiscard]] float FastApproxReciprocalLength(const float value) {
  if (value <= 0.0f) {
    return 0.0f;
  }

  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = kFastInvSqrtMagic - ((bits >> 1u) & 0x3FFFFFFFu);

  float result = 0.0f;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

[[nodiscard]] float SampleCloudNoiseCorner(const std::uint8_t x_index, const std::uint8_t y_index,
                                           const std::uint8_t z_index) {
  const std::uint8_t yz =
      kCloudNoisePermutation[(static_cast<std::uint8_t>(y_index + kCloudNoisePermutation[z_index]))];
  const std::uint8_t corner =
      kCloudNoisePermutation[static_cast<std::uint8_t>(x_index + yz)];
  return s_cloudNoiseGradients[corner];
}

[[nodiscard]] float SampleCloudNoiseOctave(const std::uint16_t x_coord,
                                           const std::uint16_t y_coord,
                                           const std::uint16_t z_coord) {
  const std::uint8_t x_index = static_cast<std::uint8_t>((x_coord >> 8) & 0xFFu);
  const std::uint8_t y_index = static_cast<std::uint8_t>((y_coord >> 8) & 0xFFu);
  const std::uint8_t z_index = static_cast<std::uint8_t>((z_coord >> 8) & 0xFFu);
  const float x_fade = s_cloudNoiseFadeTable[static_cast<std::uint8_t>(x_coord & 0xFFu)];
  const float y_fade = s_cloudNoiseFadeTable[static_cast<std::uint8_t>(y_coord & 0xFFu)];
  const float z_fade = s_cloudNoiseFadeTable[static_cast<std::uint8_t>(z_coord & 0xFFu)];

  const float x00 =
      SampleCloudNoiseCorner(x_index, y_index, z_index) +
      (SampleCloudNoiseCorner(static_cast<std::uint8_t>(x_index + 1u), y_index, z_index) -
       SampleCloudNoiseCorner(x_index, y_index, z_index)) *
          x_fade;
  const float x10 =
      SampleCloudNoiseCorner(x_index, static_cast<std::uint8_t>(y_index + 1u), z_index) +
      (SampleCloudNoiseCorner(static_cast<std::uint8_t>(x_index + 1u),
                              static_cast<std::uint8_t>(y_index + 1u), z_index) -
       SampleCloudNoiseCorner(x_index, static_cast<std::uint8_t>(y_index + 1u), z_index)) *
          x_fade;
  const float x01 =
      SampleCloudNoiseCorner(x_index, y_index, static_cast<std::uint8_t>(z_index + 1u)) +
      (SampleCloudNoiseCorner(static_cast<std::uint8_t>(x_index + 1u), y_index,
                              static_cast<std::uint8_t>(z_index + 1u)) -
       SampleCloudNoiseCorner(x_index, y_index, static_cast<std::uint8_t>(z_index + 1u))) *
          x_fade;
  const float x11 =
      SampleCloudNoiseCorner(x_index, static_cast<std::uint8_t>(y_index + 1u),
                             static_cast<std::uint8_t>(z_index + 1u)) +
      (SampleCloudNoiseCorner(static_cast<std::uint8_t>(x_index + 1u),
                              static_cast<std::uint8_t>(y_index + 1u),
                              static_cast<std::uint8_t>(z_index + 1u)) -
       SampleCloudNoiseCorner(x_index, static_cast<std::uint8_t>(y_index + 1u),
                              static_cast<std::uint8_t>(z_index + 1u))) *
          x_fade;

  const float y0 = x00 + (x10 - x00) * y_fade;
  const float y1 = x01 + (x11 - x01) * y_fade;
  return y0 + (y1 - y0) * z_fade;
}

[[nodiscard]] std::uint32_t PackCloudPixel(const std::uint8_t alpha, const float red,
                                           const float green, const float blue) {
  const auto stock_byte = [](const float value) -> std::uint8_t {
    const float scaled = (value <= 1.0f) ? (value * 255.0f) : 255.0f;
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(std::nearbyint(scaled)) & 0xFF);
  };

  const std::uint8_t red_byte = stock_byte(red);
  const std::uint8_t green_byte = stock_byte(green);
  const std::uint8_t blue_byte = stock_byte(blue);
  return (static_cast<std::uint32_t>(alpha) << 24u) |
         (static_cast<std::uint32_t>(red_byte) << 16u) |
         (static_cast<std::uint32_t>(green_byte) << 8u) |
         static_cast<std::uint32_t>(blue_byte);
}

}

void DayNight_UpdateCloudLayerTexture() {
  if (!s_cloudLayerState.enabled || s_cloudLayerState.gridSize == 0u ||
      s_cloudLayerState.colorBuffer.empty() || s_cloudLayerState.coverageBuffer.empty()) {
    s_cloudLayerState.needsRebuild = false;
    return;
  }

  if (DayNight_HasOpaqueSkyModelSlot()) {
    s_cloudLayerState.needsRebuild = false;
    return;
  }

  const bool rebuild_requested = s_cloudLayerState.needsRebuild;
  const std::uint32_t grid_size = s_cloudLayerState.gridSize;
  const std::uint32_t first_row = rebuild_requested ? 0u : s_cloudLayerState.currentRow;
  const std::uint32_t rows_to_process =
      rebuild_requested ? grid_size : std::min(s_cloudLayerState.rowsPerUpdate, grid_size);

  s_cloudLayerState.currentRow = first_row;
  s_cloudLayerState.animationTime += s_lightEnv.ReadFloat(kLightEnvDeltaSecondsIndex);

  const float cloud_alpha =
      (s_cloudLayerState.alphaOverride != 0.0f) ? s_cloudLayerState.alphaOverride
                                                : kDayNightDefaultCloudAlpha;
  s_cloudLayerState.alphaStartByte = ComputeCloudAlphaStartByte(cloud_alpha);

  DayNightCloudGlowParameters glow = DayNight_BuildCloudGlowParameters();
  if (glow.radius == 0.0f) {
    glow.radius = kCloudGlowBaseRadius;
  }

  const std::uint16_t phase_word = s_cloudLayerState.animationPhaseWord;
  const float derivative_scale =
      static_cast<float>(1u << std::max<std::uint32_t>(0u, s_cloudLayerState.vertexStride - 7u));
  const std::size_t octave_count =
      std::min<std::size_t>(s_cloudLayerState.octaveCount, kDayNightCloudNoiseOctaveCount);

  for (std::uint32_t row = 0; row < rows_to_process && first_row + row < grid_size; ++row) {
    const std::uint32_t texture_row = first_row + row;
    float previous_height = 0.0f;

    for (std::uint32_t column = 0; column < grid_size; ++column) {
      float noise_sum = 0.0f;
      float slope_x = 0.0f;
      float slope_y = 0.0f;

      for (std::size_t octave = 0; octave < octave_count; ++octave) {
        const std::uint16_t octave_step = kCloudNoiseOctaveSteps[s_cloudLayerState.lodLevel][octave];

        const std::uint16_t x_coord =
            static_cast<std::uint16_t>((phase_word + column * octave_step) & 0xFFFFu);
        const std::uint16_t y_coord =
            static_cast<std::uint16_t>((texture_row * octave_step) & 0xFFFFu);
        const float octave_value = SampleCloudNoiseOctave(x_coord, y_coord, phase_word) *
                                   (1.0f / static_cast<float>(1u << octave));
        noise_sum += octave_value;

        if (octave == 2u) {
          const std::size_t slope_index = column;
          const float previous_row_height = s_cloudLayerState.heightBuffer[slope_index];
          slope_x = (previous_height - noise_sum) * derivative_scale;
          slope_y = (previous_row_height - noise_sum) * derivative_scale;
          s_cloudLayerState.heightBuffer[slope_index] = noise_sum;
          previous_height = noise_sum;
        }
      }

      const float density = noise_sum * 64.0f + 128.0f;
      const auto density_byte =
          static_cast<std::int32_t>(static_cast<std::int32_t>(std::nearbyint(density)) & 0xFF);
      std::uint8_t alpha = 0u;
      const auto alpha_start = static_cast<std::int32_t>(s_cloudLayerState.alphaStartByte);
      if (density_byte >= alpha_start) {

        alpha = s_cloudAlphaTable[static_cast<std::size_t>(density_byte - alpha_start) + 1u];
      }

      const std::size_t sample_index =
          (static_cast<std::size_t>(texture_row) << s_cloudLayerState.vertexStride) + column;
      if (sample_index >= s_cloudLayerState.coverageBuffer.size() ||
          sample_index >= s_cloudLayerState.colorBuffer.size()) {
        continue;
      }

      s_cloudLayerState.coverageBuffer[sample_index] = alpha;
      if (alpha == 0u) {

        if (column != 0u) {
          s_cloudLayerState.colorBuffer[sample_index] =
              s_cloudLayerState.colorBuffer[sample_index - 1u] & 0x00FFFFFFu;
        }
        continue;
      }

      const float alpha_scale =
          static_cast<float>((((0xFFu - alpha) >> 1u) + 64u)) * kByteToUnitRgbScale;
      float red = glow.cloudColor.x + glow.cloudEdgeColor.x * alpha_scale;
      float green = glow.cloudColor.y + glow.cloudEdgeColor.y * alpha_scale;
      float blue = glow.cloudColor.z + glow.cloudEdgeColor.z * alpha_scale;

      const float glow_dx = glow.textureSpaceCenter.x - static_cast<float>(column);
      const float glow_dy = glow.textureSpaceCenter.y - static_cast<float>(texture_row);
      const float glow_distance_sq =
          glow.radius * glow.radius + glow_dx * glow_dx + glow_dy * glow_dy;
      const float normal_length_sq = slope_x * slope_x + slope_y * slope_y + 1.0f;
      const float glow_alignment =
          (glow.radius + slope_x * glow_dx + slope_y * glow_dy) *
          (FastApproxReciprocalLength(glow_distance_sq) *
           FastApproxReciprocalLength(normal_length_sq));
      if (glow_alignment > 0.0f) {

        const float highlight = glow_alignment * glow.glowStrength;
        red += glow.cloudHilightColor.x * highlight;
        green += glow.cloudHilightColor.y * highlight;
        blue += glow.cloudHilightColor.z * highlight;
      }

      s_cloudLayerState.colorBuffer[sample_index] =
          PackCloudPixel(alpha, red, green, blue);
    }
  }

  const std::uint32_t upload_slot = (s_cloudLayerState.activeTextureIndex + 1u) & 1u;
  const std::uint32_t generated_end =
      first_row + std::min(rows_to_process, grid_size - first_row);
  DayNightCloudTextureUpload &pending = s_cloudLayerState.pendingUploads[upload_slot];
  if (pending.empty() || pending.gridSize != grid_size) {
    pending = DayNightCloudTextureUpload{first_row, generated_end, grid_size};
  } else {
    pending.firstRow = std::min(pending.firstRow, first_row);
    pending.endRow = std::max(pending.endRow, generated_end);
  }

  s_cloudLayerState.currentRow = first_row + rows_to_process;
  if (s_cloudLayerState.currentRow < grid_size) {
    s_cloudLayerState.needsRebuild = false;
    return;
  }

  const auto next_phase_word = static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(s_cloudLayerState.animationRate * s_cloudLayerState.animationTime));
  if (next_phase_word != s_cloudLayerState.animationPhaseWord || rebuild_requested) {
    s_cloudLayerState.activeTextureIndex ^= 1u;
    s_cloudLayerState.animationPhaseWord = next_phase_word;
  }
  s_cloudLayerState.currentRow = 0u;
  s_cloudLayerState.needsRebuild = false;
}

DayNightCloudTextureUpload DayNight_TakeCloudLayerUpload(const std::uint32_t textureIndex) {
  if (textureIndex >= s_cloudLayerState.pendingUploads.size()) {
    return {};
  }
  const DayNightCloudTextureUpload taken = s_cloudLayerState.pendingUploads[textureIndex];
  s_cloudLayerState.pendingUploads[textureIndex] = {};
  return taken;
}

const std::vector<std::uint32_t> &DayNight_GetCloudLayerTexels() {
  return s_cloudLayerState.colorBuffer;
}

std::uint32_t DayNight_GetCloudLayerGridSize() {
  return s_cloudLayerState.gridSize;
}

std::uint32_t DayNight_GetCloudLayerActiveTextureIndex() {
  return s_cloudLayerState.activeTextureIndex;
}

bool DayNight_IsCloudLayerEnabled() {
  return s_cloudLayerState.enabled;
}

void DayNight_SetSunGlareEnabled(const bool enabled, const bool showOutput) {
  if (enabled) {
    if (showOutput) {
      openwow::core::ida::ConsoleAddLine(kSunGlareEnabledMessage,
                                         openwow::core::ida::COLOR_DEFAULT);
    }
    s_sunGlareState.enabled = true;
    s_moonGlareState.enabled = true;
    return;
  }

  if (showOutput) {
    openwow::core::ida::ConsoleAddLine(kSunGlareDisabledMessage,
                                       openwow::core::ida::COLOR_DEFAULT);
  }
  s_sunGlareState.enabled = false;
  s_moonGlareState.enabled = false;
}

int DayNight_OnSunGlareToggle(int , const char *buffer) {
  int val = 0;
  if (std::sscanf(buffer, "%d", &val) == 1 && val != 0) {
    DayNight_SetSunGlareEnabled(true, true);
  } else {
    DayNight_SetSunGlareEnabled(false, true);
  }
  return 1;
}

bool DayNight_IsSunGlareEnabled() {
  return s_sunGlareState.enabled;
}

bool DayNight_IsMoonGlareEnabled() {
  return s_moonGlareState.enabled;
}

const DayNightGlareState &DayNight_GetSunGlareState() {
  return s_sunGlareState;
}

const DayNightGlareState &DayNight_GetMoonGlareState() {
  return s_moonGlareState;
}

std::uint8_t DayNight_UpdateStarsModelState(DayNightStarsModelState &state,
                                            const DayNightLightEnv &env) {
  state.position.x = env.ReadFloat(kLightEnvCameraPositionIndex + 0);
  state.position.y = env.ReadFloat(kLightEnvCameraPositionIndex + 1);
  state.position.z = env.ReadFloat(kLightEnvCameraPositionIndex + 2);

  const float alpha_factor = EvaluateWrappedFloatCurve(
      kStarsModelAlphaCurve, sizeof(kStarsModelAlphaCurve) / sizeof(kStarsModelAlphaCurve[0]),
      env.ReadFloat(kLightEnvNormalizedTimeOfDayIndex));
  state.alpha = static_cast<std::uint8_t>(std::trunc(alpha_factor * 254.0f + 1.0f));
  return state.alpha;
}

std::uint8_t DayNight_UpdateGlobalStarsModelState() {
  return DayNight_UpdateStarsModelState(s_starsModelState, s_lightEnv);
}

const DayNightStarsModelState &DayNight_GetStarsModelState() {
  return s_starsModelState;
}

void DayNight_UpdateCelestialTextureState() {
  const float normalized_time_of_day = s_lightEnv.ReadFloat(kLightEnvNormalizedTimeOfDayIndex);
  const float day_count = s_lightEnv.ReadFloat(kLightEnvDayCountIndex);
  const DayNightVec3 sky_origin = {
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 0),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 1),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 2),
  };

  s_celestialState.sunCenter.worldPosition = EvaluateCelestialWorldPosition(
      kSunCenterPolarAngleCurve,
      sizeof(kSunCenterPolarAngleCurve) / sizeof(kSunCenterPolarAngleCurve[0]),
      kSunCenterAzimuthAngleCurve,
      sizeof(kSunCenterAzimuthAngleCurve) / sizeof(kSunCenterAzimuthAngleCurve[0]),
      normalized_time_of_day, sky_origin);
  s_celestialState.sunCenter.projectedScale =
      EvaluateProjectedScale(kSunAndMoonPrimaryProjectedScaleCurve,
                             sizeof(kSunAndMoonPrimaryProjectedScaleCurve) /
                                 sizeof(kSunAndMoonPrimaryProjectedScaleCurve[0]),
                             normalized_time_of_day, kSunCenterProjectedScaleMultiplier);

  s_celestialState.moonPrimary.worldPosition = EvaluateCelestialWorldPosition(
      kMoonPrimaryPolarAngleCurve,
      sizeof(kMoonPrimaryPolarAngleCurve) / sizeof(kMoonPrimaryPolarAngleCurve[0]),
      kMoonPrimaryAzimuthAngleCurve,
      sizeof(kMoonPrimaryAzimuthAngleCurve) / sizeof(kMoonPrimaryAzimuthAngleCurve[0]),
      normalized_time_of_day, sky_origin);
  s_celestialState.moonPrimary.projectedScale =
      EvaluateProjectedScale(kMoonSecondaryProjectedScaleCurve,
                             sizeof(kMoonSecondaryProjectedScaleCurve) /
                                 sizeof(kMoonSecondaryProjectedScaleCurve[0]),
                             normalized_time_of_day, kMoonPrimaryProjectedScaleMultiplier);

  s_moonGlareState.profile.minimumProjectedScale =
      s_celestialState.moonPrimary.projectedScale;
  s_moonGlareState.profile.maximumProjectedScale =
      s_celestialState.moonPrimary.projectedScale;

  const float moon_secondary_sample = ComputeMoonSecondarySample(day_count, normalized_time_of_day);
  s_celestialState.moonSecondary.worldPosition = EvaluateCelestialWorldPosition(
      kMoonPrimaryPolarAngleCurve,
      sizeof(kMoonPrimaryPolarAngleCurve) / sizeof(kMoonPrimaryPolarAngleCurve[0]),
      kMoonSecondaryAzimuthAngleCurve,
      sizeof(kMoonSecondaryAzimuthAngleCurve) / sizeof(kMoonSecondaryAzimuthAngleCurve[0]),
      moon_secondary_sample, sky_origin);
  s_celestialState.moonSecondary.projectedScale = EvaluateProjectedScale(
      kMoonSecondaryProjectedScaleCurve,
      sizeof(kMoonSecondaryProjectedScaleCurve) / sizeof(kMoonSecondaryProjectedScaleCurve[0]),
      moon_secondary_sample, kMoonSecondaryProjectedScaleMultiplier);

  s_celestialState.horizonFadeFactor = ComputeCelestialHorizonFade(normalized_time_of_day);

  s_sunGlareState.worldPosition = s_celestialState.sunCenter.worldPosition;
  s_moonGlareState.worldPosition = s_celestialState.moonPrimary.worldPosition;

  DayNight_RefreshGlareBillboardColors();
}

void DayNight_RefreshGlareBillboardColors() {

  const std::uint32_t glowColor = s_lightEnv.dwords[kLightEnvSunHaloColorIndex];
  const float glowBlend = s_lightEnv.ReadFloat(kLightEnvGlowBlendFactorIndex);

  std::uint8_t alphaByte = static_cast<std::uint8_t>((glowColor >> 24u) & 0xFFu);
  if (glowBlend != 0.0f) {
    alphaByte = static_cast<std::uint8_t>(
        static_cast<std::int32_t>(std::nearbyint((1.0f - glowBlend) * 255.0f)) & 0xFF);
  }

  const std::uint32_t rgb = glowColor & 0x00FFFFFFu;

  s_celestialState.sunCenter.colorRgb = rgb;
  s_celestialState.sunCenter.alpha = alphaByte;
  s_celestialState.moonPrimary.colorRgb = rgb;
  s_celestialState.moonPrimary.alpha = alphaByte;
  s_celestialState.moonSecondary.colorRgb = rgb;
  s_celestialState.moonSecondary.alpha = alphaByte;

  s_sunGlareState.colorRgb = rgb;
  s_sunGlareState.alpha = alphaByte;
  s_moonGlareState.colorRgb = s_sunGlareState.colorRgb;
  s_moonGlareState.alpha = s_sunGlareState.alpha;
}

const DayNightCelestialState &DayNight_GetCelestialState() {
  return s_celestialState;
}

void DayNight_BuildCloudAlphaTable(float gamma) {
  const float step = static_cast<float>(255 - s_cloudLayerState.alphaStartByte) / 256.0f;
  float accum = 0.0f;
  s_cloudAlphaTable[0] = 0u;
  for (int i = 1; i <= 256; ++i) {
    s_cloudAlphaTable[i] = static_cast<uint8_t>(255.0f - std::pow(gamma, accum) * 255.0f);
    accum += step;
  }
}

int DayNight_InitSunGlare() {
  InitializeGlareState(s_sunGlareState, DayNightGlareCloudFadeTransform::InvertCloudAlpha,
                       kDayNightSunGlareTexturePath,
                       DayNightGlareCelestialBinding::SunCenter, 1.0f, 4.0f,
                       1.5151515f, 3.0f, 20.0f, 0.69999999f, 0.5f, 1.0f,
                       {{{0.27083334f, 0.0f}, {0.3125f, 1.0f}, {0.8125f, 1.0f}, {0.875f, 0.0f}}});
  return 1;
}

int DayNight_InitMoonGlare() {
  InitializeGlareState(
      s_moonGlareState, DayNightGlareCloudFadeTransform::MidpointCloudBand,
      kDayNightMoonGlareTexturePath,
      DayNightGlareCelestialBinding::MoonPrimary, 2.0f, 3.030303f, 1.5151515f, 1.0f, 1.0f,
      0.69999999f, 0.1f, 1.0f,
      {{{0.083333336f, 1.0f}, {0.13541667f, 0.0f}, {0.94791669f, 0.0f}, {0.99930561f, 1.0f}}});
  return 1;
}

AreaLightOverride &DayNight_AllocTransitionSlot() {
  const std::uint32_t newCount = s_areaLightOverrides.count + 1U;
  if (newCount > s_areaLightOverrides.capacity) {
    std::uint32_t growthQuantum = s_areaLightOverrides.growthStep;
    if (growthQuantum == 0U) {
      growthQuantum = ResolveAreaLightOverrideGrowQuantum(newCount, s_areaLightOverrides);
    }

    std::uint32_t alignedCapacity = newCount;
    const std::uint32_t remainder = newCount % growthQuantum;
    if (remainder != 0U) {
      alignedCapacity += growthQuantum - remainder;
    }

    AreaLightOverride_Resize(alignedCapacity);
  }

  AreaLightOverride &slot = s_areaLightOverrides.storage[s_areaLightOverrides.count];
  ++s_areaLightOverrides.count;
  return slot;
}

std::uint32_t DayNight_GetAreaLightOverrideCount() {
  return s_areaLightOverrides.count;
}

std::uint32_t DayNight_GetAreaLightOverrideCapacity() {
  return s_areaLightOverrides.capacity;
}

std::span<const AreaLightOverride> DayNight_GetAreaLightOverrideData() {
  return std::span<const AreaLightOverride>(s_areaLightOverrides.storage.data(),
                                            s_areaLightOverrides.storage.size());
}

void DayNight_ResetTransition(bool markDirty) {
  s_transitionProgress = 0.0f;
  s_activeTransitions = 0;
  s_transitionDataCount = 0;
  if (markDirty) {
    s_transitionDirty = true;
  }
}

bool DayNight_TransitionLight(const openwow::data::dbc::DbcLoader *const dbc,
                              const std::uint32_t srcLightId,
                              const std::uint32_t targetLightId,
                              const std::uint32_t fadeTimeMs) {
  if (dbc == nullptr) {
    return false;
  }

  const auto *const source = dbc->light().LookupEntry(srcLightId);
  const auto *const target = dbc->light().LookupEntry(targetLightId);
  if (source == nullptr || target == nullptr) {
    return false;
  }

  openwow::data::dbc::LightEntry interpolated = *target;
  interpolated.x = source->x;
  interpolated.y = source->y;
  interpolated.z = source->z;
  interpolated.falloff_start = source->falloff_start;
  interpolated.falloff_end = source->falloff_end;

  std::uint32_t token = s_transitionLightRecords.nextToken;
  while (token == 0u || s_transitionLightRecords.records.contains(token)) {
    ++token;
  }
  s_transitionLightRecords.nextToken = token + 1u;
  if (s_transitionLightRecords.nextToken == 0u) {
    s_transitionLightRecords.nextToken = 1u;
  }
  s_transitionLightRecords.records.emplace(token, std::move(interpolated));

  AreaLightOverride &slot = DayNight_AllocTransitionSlot();
  slot.lightRecId = srcLightId;
  slot.interpolatedLightRecToken = token;
  slot.progress = 0.0f;
  slot.fadeDirection = kFadingIn;
  slot.startTime = openwow::core::GameClock::GetTickCount32();
  slot.fadeTimeMs = fadeTimeMs;
  DayNight_ResetTransition(true);
  return true;
}

void DayNight_UpdateAreaLightOverrideTransitions(
    const std::uint32_t tickCountMs) {
  std::uint32_t index = 0u;
  while (index < s_areaLightOverrides.count &&
         index < s_areaLightOverrides.storage.size()) {
    AreaLightOverride &entry = s_areaLightOverrides.storage[index];
    const std::uint32_t elapsed = tickCountMs - entry.startTime;
    entry.startTime = tickCountMs;
    const float progress_delta =
        static_cast<float>(elapsed) / static_cast<float>(entry.fadeTimeMs);

    if (entry.fadeDirection == kFadingIn) {
      const float progress = entry.progress + progress_delta;
      entry.progress = progress <= 1.0f ? progress : 1.0f;
    } else if (entry.fadeDirection == kFadingOut) {
      const float progress = entry.progress - progress_delta;
      entry.progress = progress >= 0.0f ? progress : 0.0f;
    }

    if (entry.fadeDirection != kFadingOut || entry.progress != 0.0f ||
        std::isnan(entry.progress)) {
      ++index;
      continue;
    }

    s_transitionLightRecords.records.erase(entry.interpolatedLightRecToken);
    --s_areaLightOverrides.count;
    if (index < s_areaLightOverrides.count) {
      entry = s_areaLightOverrides.storage[s_areaLightOverrides.count];
    }
    s_areaLightOverrides.storage[s_areaLightOverrides.count] = {};
  }
}

void DayNight_BeginOverrideFadeOut(const std::uint32_t lightRecId,
                                   const std::uint32_t fadeTimeMs) {
  const std::uint32_t count = s_areaLightOverrides.count;
  if (count == 0) {
    return;
  }

  const std::uint32_t accessible_count =
      std::min<std::uint32_t>(count, s_areaLightOverrides.capacity);
  for (std::uint32_t i = 0; i < accessible_count; ++i) {
    AreaLightOverride &entry = s_areaLightOverrides.storage[i];
    if (entry.lightRecId == lightRecId) {
      entry.fadeDirection = kFadingOut;
      entry.startTime = openwow::core::GameClock::GetTickCount32();
      entry.fadeTimeMs = fadeTimeMs;
      DayNight_ResetTransition(true);
      return;
    }
  }
}

int DayNight_OnSkyCloudLODChanged(int , int showOutput, const char *value) {
  int val = 0;
  if (value) {
    val = std::atoi(value);
  }
  val = std::clamp(val, 0, 3);
  DayNight_InitCloudLayer(static_cast<std::uint8_t>(val), 0);
  if (showOutput) {
    openwow::core::ida::ConsoleLog("SkyCloudLOD set to %i", val);
  }
  return 1;
}

bool DayNight_IsInitialized() {
  return s_systemInitialized;
}

bool DayNight_ApplySkyCloudLod(const int lodLevel, const bool showOutput) {
  if (!s_systemInitialized) {
    return false;
  }

  const int clamped_lod = std::clamp(lodLevel, 0, 3);
  if (s_cloudLayerState.enabled &&
      s_cloudLayerState.lodLevel == static_cast<std::uint8_t>(clamped_lod)) {
    return true;
  }

  DayNight_InitCloudLayer(static_cast<std::uint8_t>(clamped_lod), 0);
  if (showOutput) {
    openwow::core::ida::ConsoleLog("SkyCloudLOD set to %i", clamped_lod);
  }
  return true;
}

void LightRec_CleanupAll() {

  s_sunGlareState = {};
  s_moonGlareState = {};
  s_starsModelState = {};
  ResetCelestialState();
  ResetAreaLightOverrideArray(s_areaLightOverrides);
  s_transitionLightRecords.records.clear();
  s_transitionLightRecords.nextToken = 1u;
  ResetCloudLayerState();
  s_renderReady = false;
  s_systemInitialized = false;
}

void DayNight_LoadCelestialTexture(DayNightCelestialTextureState &state, const char *filePath,
                                   const float billboardScaleX, const float billboardScaleY) {
  state.texture = openwow::render::TextureAsset::File(filePath);
  state.billboardScaleX = billboardScaleX;
  state.billboardScaleY = billboardScaleY;
}

void DayNight_Init(int mapId) {
  s_mapId = static_cast<uint32_t>(mapId);

  DayNightLightEnv_Init(s_lightEnv);
  s_skyUpdateMask = 0;
  s_screenEffectLightParamsId = -1;
  s_screenEffectFogOverride = {};
  s_fogMode = 0;
  s_fogFadeFactor = 0.0f;
  s_fogBandColorScale = 0.0f;
  s_fogFadeColor = 0;
  s_starsModelState = {};
  ResetCelestialState();
  ResetAreaLightOverrideArray(s_areaLightOverrides);
  s_transitionLightRecords.records.clear();
  s_transitionLightRecords.nextToken = 1u;
  DayNight_ClearSkyModelSlots();

  DayNight_BuildSkyDome(1.0f);

  DayNight_InitCloudLayer(0, 0);

  DayNight_BuildCloudDome(1.0f);

  s_cloudLayerState.alphaOverride = kDayNightDefaultCloudAlpha;
  s_cloudLayerState.alphaStartByte = ComputeCloudAlphaStartByte(kDayNightDefaultCloudAlpha);

  DayNight_BuildCloudAlphaTable(0.96f);

  DayNight_InitSunGlare();
  DayNight_InitMoonGlare();

  DayNight_LoadCelestialTexture(s_celestialState.sunCenter,
                                kDayNightSunCenterTexturePath, 1.0f, 1.0f);
  DayNight_LoadCelestialTexture(s_celestialState.moonPrimary,
                                kDayNightMoonPrimaryTexturePath, 1.75f, 1.0f);
  DayNight_LoadCelestialTexture(s_celestialState.moonSecondary,
                                kDayNightMoonSecondaryTexturePath, 1.0f, 1.7f);

  s_renderReady = true;
  s_systemInitialized = true;
}

std::uint32_t DayNight_LoadSkyModel(const std::string_view modelPath, const std::uint32_t flags) {
  const std::string normalized_path = NormalizeSkyModelPath(modelPath);
  if (normalized_path.empty()) {
    return 0;
  }

  const std::string key = CanonicalSkyModelKey(normalized_path);
  if (const auto existing = s_skyModelCache.find(key); existing != s_skyModelCache.end()) {
    return existing->second.resourceId;
  }

  const std::uint32_t resource_id = s_nextSkyModelResourceId++;
  s_skyModelCache.emplace(key, DayNightSkyModelResourceState{
                                   .resourceId = resource_id,
                                   .path = normalized_path,
                                   .flags = flags,
                               });
  return resource_id;
}

std::uint32_t DayNight_SetSkyModelSlot(const std::size_t slot, const std::string_view modelPath,
                                       const std::uint32_t flags, const float alpha) {
  if (slot >= s_skyModelSlots.size()) {
    return 0;
  }

  const std::string normalized_path = NormalizeSkyModelPath(modelPath);
  if (normalized_path.empty()) {
    DayNight_ClearSkyModelSlot(slot);
    return 0;
  }

  const std::uint32_t resource_id = DayNight_LoadSkyModel(normalized_path, flags);
  if (resource_id == 0) {
    DayNight_ClearSkyModelSlot(slot);
    return 0;
  }

  s_skyModelSlots[slot] = DayNightSkyModelSlot{
      .resourceId = resource_id,
      .path = normalized_path,
      .flags = flags,
      .alpha = std::clamp(alpha, 0.0f, 1.0f),
      .active = true,
  };
  return resource_id;
}

void DayNight_ClearSkyModelSlot(const std::size_t slot) {
  if (slot >= s_skyModelSlots.size()) {
    return;
  }
  s_skyModelSlots[slot] = {};
}

void DayNight_ClearSkyModelSlots() {
  for (DayNightSkyModelSlot &slot : s_skyModelSlots) {
    slot = {};
  }
}

DayNightSkyModelSlots DayNight_GetSkyModelSlots() {
  return s_skyModelSlots;
}

bool DayNight_HasOpaqueSkyModelSlot() {
  return std::any_of(s_skyModelSlots.begin(), s_skyModelSlots.end(),
                     [](const DayNightSkyModelSlot &slot) {
                       return slot.active && slot.alpha > 0.99f && (slot.flags & 0x2u) == 0u;
                     });
}

void AreaLightOverride_Resize(const uint32_t newCount) {
  const std::uint32_t oldCapacity = s_areaLightOverrides.capacity;
  const std::uint32_t preservedCount =
      std::min(std::min(s_areaLightOverrides.count, newCount), oldCapacity);
  std::vector<AreaLightOverride> rebuiltStorage(newCount);
  std::copy_n(s_areaLightOverrides.storage.begin(), preservedCount, rebuiltStorage.begin());

  s_areaLightOverrides.capacity = newCount;
  s_areaLightOverrides.storage = std::move(rebuiltStorage);
}

void DayNight_CloudLayerResize(const uint32_t newCount) {
  if (newCount == s_cloudLayerState.heightBuffer.size()) {
    return;
  }

  if (newCount != 0) {
    s_cloudLayerState.heightBuffer.resize(newCount);
  } else {
    s_cloudLayerState.heightBuffer.clear();
    s_cloudLayerState.heightBuffer.shrink_to_fit();
  }
}

void DayNight_InitCloudLayer(std::uint8_t lodLevel, int rowsPerUpdate) {
  ResetCloudLayerState();
  lodLevel = std::min(lodLevel, static_cast<std::uint8_t>(3));
  const std::uint32_t gridSize = kCloudLODDimensions[lodLevel];
  const std::uint32_t stride = kCloudLODStrides[lodLevel];
  const std::uint32_t gridSq = gridSize * gridSize;
  const std::uint32_t effective_rows_per_update =
      rowsPerUpdate > 0 ? static_cast<std::uint32_t>(rowsPerUpdate)
                        : kDayNightDefaultCloudRowsPerUpdate;

  s_cloudLayerState.lodLevel = lodLevel;
  s_cloudLayerState.rowsPerUpdate = effective_rows_per_update;
  s_cloudLayerState.gridSize = gridSize;
  s_cloudLayerState.indexCount = gridSize - 1u;
  s_cloudLayerState.vertexStride = stride;
  s_cloudLayerState.pendingUploads = {};
  s_cloudLayerState.colorBuffer.assign(gridSq, 0xFFFFFFFFu);
  s_cloudLayerState.coverageBuffer.assign(gridSq, 0u);
  s_cloudLayerState.heightBuffer.assign(gridSize, 0.0f);

  s_cloudLayerState.renderTargets[0] =
      openwow::render::TextureAsset::RenderTarget(
          kDayNightCloudRenderTargetName0, gridSize, gridSize);
  s_cloudLayerState.renderTargets[1] =
      openwow::render::TextureAsset::RenderTarget(
          kDayNightCloudRenderTargetName1, gridSize, gridSize);
  s_cloudLayerState.activeTextureIndex = 0;
  s_cloudLayerState.needsRebuild = true;
  s_cloudLayerState.enabled = true;
}

int DayNight_BuildCloudDome(float radius) {
  constexpr int kLonSegments = 16;
  constexpr float kLonStep = 0.39269909f;
  constexpr std::size_t kReservedVertexCapacity = 192;
  constexpr std::size_t kIndexCount = 374;
  constexpr std::size_t kLatitudeBandCount =
      sizeof(kCloudDomeLatitudes) / sizeof(kCloudDomeLatitudes[0]);

  s_cloudDomeMesh.positions.clear();
  s_cloudDomeMesh.texcoords.clear();
  s_cloudDomeMesh.colors.clear();
  s_cloudDomeMesh.indices.clear();
  s_cloudDomeMesh.positions.reserve(kReservedVertexCapacity);
  s_cloudDomeMesh.texcoords.reserve(kReservedVertexCapacity);
  s_cloudDomeMesh.colors.reserve(kReservedVertexCapacity);
  s_cloudDomeMesh.indices.reserve(kIndexCount);

  const float baseOffset = -std::cos(kQuarterTurn);
  std::uint16_t previousBandStart = 0;
  float previousLatitudeAngle = 0.0f;

  for (std::size_t latitudeIndex = 0; latitudeIndex < kLatitudeBandCount; ++latitudeIndex) {
    const float latitudeAngle = kCloudDomeLatitudes[latitudeIndex] * kPi;
    const float sinLatitude = std::sin(latitudeAngle);

    const float cloudTop = radius * (std::cos(latitudeAngle) + baseOffset);
    const float uvScale = 0.5f * (static_cast<float>(latitudeIndex) * 0.090909094f);
    const bool isPole = openwow::math::float_compare::WithinWideClientEpsilon(latitudeAngle, 0.0f);
    const std::uint16_t bandStart = static_cast<std::uint16_t>(s_cloudDomeMesh.positions.size());

    float longitudeAngle = 0.0f;
    int longitudeIndex = 0;
    while (true) {
      const float sinLongitude = std::sin(longitudeAngle);
      const float cosLongitude = std::cos(longitudeAngle);

      s_cloudDomeMesh.positions.push_back({
          sinLongitude * sinLatitude * radius,
          sinLatitude * cosLongitude * radius,
          cloudTop,
      });
      s_cloudDomeMesh.texcoords.push_back({
          sinLongitude * uvScale + 0.5f,
          cosLongitude * uvScale + 0.5f,
      });
      s_cloudDomeMesh.colors.push_back(
          0x00FFFFFFu | (static_cast<std::uint32_t>(kCloudDomeAlpha[latitudeIndex]) << 24));

      if (isPole) {
        break;
      }

      if (!openwow::math::float_compare::WithinWideClientEpsilon(latitudeAngle, kTwoPi)) {
        longitudeAngle += kLonStep;
        ++longitudeIndex;
        if (longitudeIndex < kLonSegments) {
          continue;
        }
      }
      break;
    }

    if (latitudeIndex > 0) {
      const bool previousIsPole =
          openwow::math::float_compare::WithinWideClientEpsilon(previousLatitudeAngle, 0.0f);
      const bool currentIsWrapBand =
          openwow::math::float_compare::WithinWideClientEpsilon(latitudeAngle, kTwoPi);
      for (int index = 0; index <= kLonSegments; ++index) {
        const std::uint16_t previousVertex =
            previousIsPole ? previousBandStart
                           : static_cast<std::uint16_t>(previousBandStart + (index & 0xF));

        const std::uint16_t currentVertex =
            currentIsWrapBand ? bandStart : static_cast<std::uint16_t>(bandStart + (index & 0xF));
        s_cloudDomeMesh.indices.push_back(previousVertex);
        s_cloudDomeMesh.indices.push_back(currentVertex);
      }
    }

    previousLatitudeAngle = latitudeAngle;
    previousBandStart = bandStart;
  }

  return static_cast<int>(s_cloudDomeMesh.positions.size());
}

const DayNightCloudDomeMesh &DayNight_GetCloudDomeMesh() {
  return s_cloudDomeMesh;
}

int DayNight_BuildSkyDome(float radius) {
  constexpr int kLonSegments = 24;
  constexpr float kLonStep = kTwoPi / 24.0f;
  constexpr std::size_t kReservedVertexCapacity = 168;
  constexpr std::size_t kIndexCount = 300;
  constexpr std::size_t kLatitudeBandCount =
      sizeof(kSkyDomeLatitudes) / sizeof(kSkyDomeLatitudes[0]);

  s_skyDomeMesh.positions.clear();
  s_skyDomeMesh.indices.clear();
  s_skyDomeMesh.positions.reserve(kReservedVertexCapacity);
  s_skyDomeMesh.indices.reserve(kIndexCount);

  const float baseOffset = -std::cos(kQuarterTurn);
  std::uint16_t previousBandStart = 0;
  float previousLatitudeAngle = 0.0f;

  for (std::size_t latitudeIndex = 0; latitudeIndex < kLatitudeBandCount; ++latitudeIndex) {
    const float latitudeAngle = kSkyDomeLatitudes[latitudeIndex] * kPi;
    const float verticalComponent = EvaluateDayNightWave(latitudeAngle * kSkyAngleToUnitScale);
    const float horizontalComponent =
        EvaluateDayNightWave(latitudeAngle * kSkyAngleToUnitScale - 0.5f);

    const float height = radius * (verticalComponent + baseOffset);
    const bool isNorthPole =
        openwow::math::float_compare::WithinWideClientEpsilon(latitudeAngle, 0.0f);
    const std::uint16_t bandStart = static_cast<std::uint16_t>(s_skyDomeMesh.positions.size());

    int longitudeIndex = 0;
    while (true) {
      const float longitudeAngle = static_cast<float>(longitudeIndex) * kLonStep;
      s_skyDomeMesh.positions.push_back({
          std::sin(longitudeAngle) * horizontalComponent * radius,
          horizontalComponent * std::cos(longitudeAngle) * radius,
          height,
      });

      if (isNorthPole) {
        break;
      }

      if (!openwow::math::float_compare::WithinWideClientEpsilon(latitudeAngle, kPi)) {
        ++longitudeIndex;
        if (longitudeIndex < kLonSegments) {
          continue;
        }
      }
      break;
    }

    if (latitudeIndex > 0) {
      const bool previousIsPole =
          openwow::math::float_compare::WithinWideClientEpsilon(previousLatitudeAngle, 0.0f);
      const bool currentIsPole =
          openwow::math::float_compare::WithinWideClientEpsilon(latitudeAngle, kPi);
      for (int index = 0; index <= kLonSegments; ++index) {
        const std::uint16_t previousVertex =
            previousIsPole ? previousBandStart
                           : static_cast<std::uint16_t>(previousBandStart + (index % kLonSegments));

        const std::uint16_t currentVertex =
            currentIsPole ? bandStart
                          : static_cast<std::uint16_t>(bandStart + (index % kLonSegments));
        s_skyDomeMesh.indices.push_back(previousVertex);
        s_skyDomeMesh.indices.push_back(currentVertex);
      }
    }

    previousLatitudeAngle = latitudeAngle;
    previousBandStart = bandStart;
  }

  return static_cast<int>(s_skyDomeMesh.positions.size());
}

const DayNightSkyDomeMesh &DayNight_GetSkyDomeMesh() {
  return s_skyDomeMesh;
}

namespace {

constexpr float kGlareHorizonVisibilityExponent = 10.0f;

constexpr float kGlareSubmersionFadeScale = 0.1f;

constexpr std::size_t kLightEnvGlobalCloudVolumeIndex = 23;
constexpr std::size_t kLightEnvGlobalCloudOpacityIndex = 24;
constexpr std::size_t kLightEnvCloudLayerPresentBaseIndex = 25;
constexpr std::size_t kLightEnvCloudLayerOpacityBaseIndex = 28;
constexpr std::size_t kLightEnvCloudLayerCount = 3;

[[nodiscard]] float EvaluateGlareSubmersionFade(const bool effectActive,
                                                const float submersionDepth) {
  if (!effectActive) {
    return 1.0f;
  }
  const float fade = kGlareSubmersionFadeScale * submersionDepth;
  if (fade < 0.0f) {
    return 1.0f;
  }
  if (fade >= 1.0f) {
    return 0.0f;
  }
  return 1.0f - fade;
}

[[nodiscard]] float EvaluateGlareCloudOcclusion() {
  if (s_lightEnv.dwords[kLightEnvGlobalCloudVolumeIndex] != 0u) {
    const float globalOpacity =
        s_lightEnv.ReadFloat(kLightEnvGlobalCloudOpacityIndex);
    if (globalOpacity > 0.0f) {
      return globalOpacity;
    }
  }

  float occlusion = 0.0f;
  for (std::size_t layer = 0; layer < kLightEnvCloudLayerCount; ++layer) {
    if (s_lightEnv.dwords[kLightEnvCloudLayerPresentBaseIndex + layer] == 0u) {
      continue;
    }
    occlusion = std::max(
        occlusion, s_lightEnv.ReadFloat(kLightEnvCloudLayerOpacityBaseIndex + layer));
  }
  return occlusion;
}

[[nodiscard]] float EvaluateGlareOcclusionVisibility(const DayNightGlareState &glare) {
  if (!glare.enabled || glare.alpha == 0u) {
    return 0.0f;
  }
  return 1.0f;
}

}

void UpdateGlareBillboardState(DayNightGlareState &glare,
                                const float normalizedTimeOfDay,
                                const float frameDelta) {
  if (!glare.enabled) {
    glare.currentVisibility = 0.0f;
    glare.targetVisibility = 0.0f;
    glare.currentProjectedScale = 0.0f;
    glare.alpha = 0u;
    return;
  }

  const DayNightVec3 cameraPosition = {
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 0),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 1),
      s_lightEnv.ReadFloat(kLightEnvCameraPositionIndex + 2),
  };
  const float offsetX = glare.worldPosition.x - cameraPosition.x;
  const float offsetY = glare.worldPosition.y - cameraPosition.y;
  const float offsetZ = glare.worldPosition.z - cameraPosition.z;
  const float inverseLength =
      1.0f / std::sqrt(offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);
  const float directionX = offsetX * inverseLength;
  const float directionY = offsetY * inverseLength;
  const float directionZ = offsetZ * inverseLength;

  float target = DayNight_ComputeGlareCloudFade(glare);
  glare.targetVisibility = target;

  target *= EvaluateGlareSubmersionFade(false,
                                        0.0f);
  glare.targetVisibility = target;

  target *= 1.0f - EvaluateGlareCloudOcclusion();
  glare.targetVisibility = target;

  target *= EvaluateWrappedFloatCurve(
      reinterpret_cast<const WrappedFloatCurvePoint *>(
          glare.profile.visibilityCurve.data()),
      glare.profile.visibilityCurve.size(), normalizedTimeOfDay);
  glare.targetVisibility = target;

  if (target != 0.0f) {
    target *= EvaluateGlareOcclusionVisibility(glare);
    glare.targetVisibility = target;
  }

  if (target > glare.currentVisibility) {
    glare.currentVisibility =
        std::min(glare.currentVisibility + glare.profile.intensityRiseRate * frameDelta,
                 target);
  } else if (target < glare.currentVisibility) {
    glare.currentVisibility =
        std::max(glare.currentVisibility - glare.profile.intensityFallRate * frameDelta,
                 target);
  }

  const float cameraForwardX = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 0);
  const float cameraForwardY = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 1);
  const float cameraForwardZ = s_lightEnv.ReadFloat(kLightEnvLightDirectionIndex + 2);
  const float viewDot = directionX * cameraForwardX + directionY * cameraForwardY +
                        directionZ * cameraForwardZ;
  const float dotClamp = glare.profile.horizonDotClamp;
  const float clampedDot = std::max(dotClamp, viewDot);
  const float viewAlignment = (clampedDot - dotClamp) / (1.0f - dotClamp);

  glare.currentProjectedScale =
      (glare.profile.minimumProjectedScale +
       (glare.profile.maximumProjectedScale - glare.profile.minimumProjectedScale) *
           viewAlignment) *
      glare.profile.baseScale;

  const float alphaMultiplier =
      glare.profile.minimumAlphaMultiplier +
      (glare.profile.maximumAlphaMultiplier - glare.profile.minimumAlphaMultiplier) *
          viewAlignment;
  const float alphaScaled = alphaMultiplier * glare.currentVisibility *
                            (static_cast<float>(glare.alpha) * kByteToUnitRgbScale) * 255.0f;
  glare.alpha = static_cast<std::uint8_t>(
      static_cast<std::int32_t>(std::nearbyint(alphaScaled)) & 0xFF);

  glare.horizonVisibilityPower =
      std::pow(clampedDot, kGlareHorizonVisibilityExponent) * glare.currentVisibility;
}

namespace {

void WriteBillboardSubmission(DayNightBillboardSubmission &submission,
                              const DayNightVec3 &position, const float half_extent,
                              const std::uint32_t color_rgb, const std::uint8_t alpha_byte,
                              const DayNightBillboardBlendMode blend_mode,
                              const char *texture_path) {
  submission.position = position;
  submission.halfExtent = half_extent;
  submission.colorRgba[0] =
      static_cast<float>((color_rgb >> 16u) & 0xFFu) * kByteToUnitRgbScale;
  submission.colorRgba[1] =
      static_cast<float>((color_rgb >> 8u) & 0xFFu) * kByteToUnitRgbScale;
  submission.colorRgba[2] =
      static_cast<float>(color_rgb & 0xFFu) * kByteToUnitRgbScale;
  submission.colorRgba[3] = static_cast<float>(alpha_byte) * kByteToUnitRgbScale;
  submission.blendMode = blend_mode;
  submission.texturePath = texture_path;
}

}

std::size_t DayNight_UpdateGlareBillboards(DayNightBillboardSubmission *out,
                                           const std::size_t capacity) {
  if (!s_renderReady) {
    return 0u;
  }

  const float frameDelta = s_lightEnv.ReadFloat(kLightEnvFrameDeltaIndex);
  const float normalizedTimeOfDay = s_lightEnv.ReadFloat(kLightEnvNormalizedTimeOfDayIndex);
  std::size_t count = 0u;

  auto append_submission = [&](const DayNightGlareState &glare) {
    if (out != nullptr && count < capacity) {
      WriteBillboardSubmission(out[count], glare.worldPosition,
                               glare.currentProjectedScale, glare.colorRgb,
                               glare.alpha, DayNightBillboardBlendMode::Additive,
                               glare.profile.texturePath);
    }
    ++count;
  };

  const auto should_draw = [](const DayNightGlareState &glare) {
    return glare.enabled && glare.alpha != 0u;
  };

  UpdateGlareBillboardState(s_sunGlareState, normalizedTimeOfDay, frameDelta);
  if (should_draw(s_sunGlareState)) {
    append_submission(s_sunGlareState);
  }

  UpdateGlareBillboardState(s_moonGlareState, normalizedTimeOfDay, frameDelta);
  if (should_draw(s_moonGlareState)) {
    append_submission(s_moonGlareState);
  }

  DayNight_UpdateGlobalStarsModelState();

  return count;
}

void DayNight_UpdateGlareBillboards() {
  (void)DayNight_UpdateGlareBillboards(nullptr, 0u);
}

std::size_t DayNight_UpdateCelestialDiscBillboards(
    DayNightBillboardSubmission *out, const std::size_t capacity) {
  if (!s_renderReady) {
    return 0u;
  }

  constexpr float kOvercastOpacityThreshold = 0.99f;
  if (EvaluateGlareCloudOcclusion() > kOvercastOpacityThreshold) {
    return 0u;
  }

  std::size_t count = 0u;
  const auto append = [&](const DayNightCelestialTextureState &disc) {
    if (disc.alpha == 0u || disc.projectedScale <= 0.0f) {
      return;
    }
    if (out != nullptr && count < capacity) {
      WriteBillboardSubmission(out[count], disc.worldPosition,
                               disc.projectedScale, disc.colorRgb, disc.alpha,
                               DayNightBillboardBlendMode::Alpha,
                               disc.texturePath);
    }
    ++count;
  };

  append(s_celestialState.sunCenter);
  append(s_celestialState.moonPrimary);
  append(s_celestialState.moonSecondary);
  return count;
}

}

#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
struct LightEntry;
}

namespace openwow::render {
class TextureAsset;
}

namespace openwow::game {

inline constexpr std::size_t kDayNightLightEnvDwordCount = 133;
inline constexpr char kDayNightSunGlareTexturePath[] =
    "Textures\\sunGlare.blp";
inline constexpr char kDayNightMoonGlareTexturePath[] =
    "Textures\\moonGlare.blp";
inline constexpr char kDayNightSunCenterTexturePath[] =
    "Textures\\sunCenter.blp";
inline constexpr char kDayNightMoonPrimaryTexturePath[] =
    "Textures\\moon.blp";
inline constexpr char kDayNightMoonSecondaryTexturePath[] =
    "Textures\\moon02.blp";
inline constexpr char kDayNightStarsModelPath[] =
    "Environments\\Stars\\stars.mdl";
inline constexpr std::uint8_t kDayNightStarsModelFirstVisibleAlpha = 2u;
inline constexpr float kDayNightStarsModelAlphaScale = 0.003921569f;

[[nodiscard]] constexpr bool DayNight_ShouldRenderStarsModel(
    const std::uint8_t alpha) noexcept {
  return alpha >= kDayNightStarsModelFirstVisibleAlpha;
}

[[nodiscard]] constexpr float DayNight_NormalizeStarsModelAlpha(
    const std::uint8_t alpha) noexcept {
  return static_cast<float>(alpha) * kDayNightStarsModelAlphaScale;
}
inline constexpr std::size_t kDayNightNameplateHighlightBlueByteOffset = 0x1ACu;
inline constexpr std::size_t kDayNightNameplateHighlightGreenByteOffset = 0x1ADu;
inline constexpr std::size_t kDayNightNameplateHighlightRedByteOffset = 0x1AEu;

struct DayNightLightEnv {
  static constexpr std::size_t kNearbyLightRecordCountIndex = 121;
  static constexpr std::size_t kNearbyLightRecordBaseIndex = 123;
  static constexpr std::size_t kNearbyLightWeightBaseIndex = 128;
  static constexpr std::size_t kMaxNearbyLightRecords = 5;

  std::uint32_t dwords[kDayNightLightEnvDwordCount]{};

  [[nodiscard]] float ReadFloat(const std::size_t index) const {
    return std::bit_cast<float>(dwords[index]);
  }

  [[nodiscard]] std::int32_t ReadInt32(const std::size_t index) const {
    return std::bit_cast<std::int32_t>(dwords[index]);
  }

  [[nodiscard]] std::uint8_t ReadByte(const std::size_t byte_offset) const {
    const std::size_t dword_index = byte_offset / sizeof(std::uint32_t);
    const std::size_t byte_index = byte_offset % sizeof(std::uint32_t);
    const std::uint32_t shift = static_cast<std::uint32_t>(byte_index * 8u);
    return static_cast<std::uint8_t>((dwords[dword_index] >> shift) & 0xFFu);
  }

  void WriteFloat(const std::size_t index, const float value) {
    dwords[index] = std::bit_cast<std::uint32_t>(value);
  }

  void WriteInt32(const std::size_t index, const std::int32_t value) {
    dwords[index] = std::bit_cast<std::uint32_t>(value);
  }

  void WriteByte(const std::size_t byte_offset, const std::uint8_t value) {
    const std::size_t dword_index = byte_offset / sizeof(std::uint32_t);
    const std::size_t byte_index = byte_offset % sizeof(std::uint32_t);
    const std::uint32_t shift = static_cast<std::uint32_t>(byte_index * 8u);
    const std::uint32_t mask = 0xFFu << shift;
    dwords[dword_index] = (dwords[dword_index] & ~mask) |
                          (static_cast<std::uint32_t>(value) << shift);
  }

  void ResetNearbyLightQueue() {
    WriteInt32(kNearbyLightRecordCountIndex, 0);
    for (std::size_t index = 0; index < kMaxNearbyLightRecords; ++index) {
      WriteInt32(kNearbyLightRecordBaseIndex + index, 0);
      WriteFloat(kNearbyLightWeightBaseIndex + index, 0.0f);
    }
  }

  [[nodiscard]] std::int32_t GetNearbyLightQueueCount() const {
    return ReadInt32(kNearbyLightRecordCountIndex);
  }

  [[nodiscard]] bool TryAppendNearbyLightRecord(const std::uint32_t resolved_handle,
                                                const float weight) {
    const std::int32_t count = GetNearbyLightQueueCount();
    if (resolved_handle == 0U || count < 0 ||
        count >= static_cast<std::int32_t>(kMaxNearbyLightRecords)) {
      return false;
    }

    const std::size_t slot = static_cast<std::size_t>(count);
    dwords[kNearbyLightRecordBaseIndex + slot] = resolved_handle;
    WriteFloat(kNearbyLightWeightBaseIndex + slot, weight);
    WriteInt32(kNearbyLightRecordCountIndex, count + 1);
    return true;
  }
};

static_assert(sizeof(DayNightLightEnv) == kDayNightLightEnvDwordCount * sizeof(std::uint32_t));
static_assert(kDayNightLightEnvDwordCount >= DayNightLightEnv::kNearbyLightWeightBaseIndex +
                                                 DayNightLightEnv::kMaxNearbyLightRecords);

struct CloudLayerState {
  std::uint8_t padding_00[9];
  std::uint8_t lodLevel;
  std::uint8_t rebuildRequested;
  std::uint8_t activeTextureIndex;
  std::uint8_t padding_0C[4];
  std::uint32_t rowsPerUpdate;
  std::uint32_t currentRow;
  std::uint32_t dataFormat;
  std::uint32_t gridSize;
  std::uint32_t vertexStride;
  std::uint32_t indexCount;
  std::uint32_t octaveCount;

};

struct DayNightVec2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct DayNightVec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct DayNightStarsModelState {
  DayNightVec3 position{};
  std::uint8_t alpha = 0;
};

enum class DayNightBillboardBlendMode : std::uint8_t {
  Additive = 0,
  Alpha,
};

struct DayNightBillboardSubmission {
  DayNightVec3 position{};

  float halfExtent = 0.0f;
  float colorRgba[4] = {1.0f, 1.0f, 1.0f, 0.0f};
  DayNightBillboardBlendMode blendMode = DayNightBillboardBlendMode::Alpha;
  const char *texturePath = nullptr;
};

enum class DayNightGlareCelestialBinding : std::uint8_t {
  None = 0,
  SunCenter,
  MoonPrimary,
};

enum class DayNightGlareCloudFadeTransform : std::uint8_t {
  None = 0,
  InvertCloudAlpha,
  MidpointCloudBand,
};

struct DayNightGlareVisibilityPoint {
  float normalizedTimeOfDay = 0.0f;
  float visibility = 0.0f;
};

struct DayNightGlareProfile {
  static constexpr std::size_t kVisibilityPointCount = 4;

  const char *texturePath = nullptr;
  DayNightGlareCelestialBinding linkedCelestialTexture = DayNightGlareCelestialBinding::None;
  float baseScale = 0.0f;
  float intensityRiseRate = 0.0f;
  float intensityFallRate = 0.0f;
  float minimumProjectedScale = 0.0f;
  float maximumProjectedScale = 0.0f;
  float horizonDotClamp = 0.0f;
  float minimumAlphaMultiplier = 0.0f;
  float maximumAlphaMultiplier = 0.0f;
  std::array<DayNightGlareVisibilityPoint, kVisibilityPointCount> visibilityCurve{};
};

struct DayNightGlareState {
  DayNightGlareProfile profile{};
  DayNightVec3 worldPosition{};

  DayNightGlareCloudFadeTransform cloudFadeTransform = DayNightGlareCloudFadeTransform::None;
  bool enabled = false;
  bool active = false;

  std::uint32_t colorRgb = 0x00FFFFFFu;
  std::uint8_t alpha = 0;
  float currentProjectedScale = 0.0f;
  float currentVisibility = 0.0f;
  float targetVisibility = 0.0f;
  float horizonVisibilityPower = 0.0f;
};

struct DayNightCelestialTextureState {
  const char *texturePath = nullptr;
  DayNightVec3 worldPosition{};
  float projectedScale = 0.0f;
  std::shared_ptr<openwow::render::TextureAsset> texture;
  float billboardScaleX = 1.0f;
  float billboardScaleY = 1.0f;

  std::uint32_t colorRgb = 0x00FFFFFFu;
  std::uint8_t alpha = 0;
};

struct DayNightCelestialState {
  DayNightCelestialTextureState sunCenter{};
  DayNightCelestialTextureState moonPrimary{};
  DayNightCelestialTextureState moonSecondary{};
  float horizonFadeFactor = 0.0f;
};

struct DayNightCloudTextureProjection {
  float nearRoot = 0.0f;
  float farRoot = 0.0f;
  DayNightVec3 worldPoint{};
  DayNightVec2 texturePosition{};
};

struct DayNightCloudGlowParameters {
  DayNightVec2 textureSpaceCenter{};
  DayNightVec2 normalizedTextureSpaceCenter{};
  DayNightVec3 cloudEdgeColor{};
  DayNightVec3 cloudColor{};
  DayNightVec3 cloudHilightColor{};
  float radius = 0.0f;
  float glowStrength = 0.0f;
};

struct DayNightCloudDomeMesh {
  std::vector<DayNightVec3> positions;
  std::vector<DayNightVec2> texcoords;
  std::vector<std::uint32_t> colors;
  std::vector<std::uint16_t> indices;
};

struct DayNightSkyDomeMesh {
  std::vector<DayNightVec3> positions;
  std::vector<std::uint16_t> indices;
};

inline constexpr std::size_t kDayNightSkyModelSlotCount = 4;

struct DayNightSkyModelSlot {
  std::uint32_t resourceId = 0;
  std::string path;
  std::uint32_t flags = 0;
  float alpha = 0.0f;
  bool active = false;
};

using DayNightSkyModelSlots = std::array<DayNightSkyModelSlot, kDayNightSkyModelSlotCount>;

inline constexpr uint32_t kCloudLODDimensions[] = {128, 256, 512, 1024};

inline constexpr uint32_t kCloudLODStrides[] = {7, 8, 9, 10};

inline constexpr float kCloudDomeLatitudes[] = {
    0.0f,
    0.02500000037252903f,
    0.05000000074505806f,
    0.07500000298023224f,
    0.10000000149011612f,
    0.125f,
    0.15000000596046448f,
    0.17499999701976776f,
    0.20499999821186066f,
    0.23000000417232513f,
    0.24500000476837158f,
    0.25f,
};

inline constexpr uint8_t kCloudDomeAlpha[] = {255, 255, 255, 255, 255, 255,
                                              255, 255, 255, 128, 0,   0};

inline constexpr float kSkyDomeLatitudes[] = {
    0.0f,
    0.17000000178813934f,
    0.20000000298023224f,
    0.23000000417232513f,
    0.23999999463558197f,
    0.25f,
    1.0f,
};

inline constexpr float kIndoorZoomRadii[] = {150.0f, 120.0f, 90.0f, 60.0f, 40.0f, 25.0f};

enum AreaLightFadeDirection : std::uint32_t {
  kFadingIn = 1,
  kFadingOut = 2,
};

struct AreaLightOverride {
  uint32_t lightRecId;
  uint32_t interpolatedLightRecToken;
  float progress;
  uint32_t fadeDirection;
  uint32_t startTime;
  uint32_t fadeTimeMs;
};

static_assert(sizeof(AreaLightOverride) == 24);

void DBClient_InitializeLightDB(uint8_t flags);

std::uint32_t DayNight_SetScreenEffectLightParamsId(std::uint32_t light_param_slot);
void DayNight_ClearScreenEffectLightParamsId();
[[nodiscard]] std::int32_t DayNight_GetScreenEffectLightParamsId();

struct DayNightSpellVisualLightingTint {
  std::uint32_t packed_argb = 0;
  std::uint8_t blend_factor = 0;
};

struct DayNightFogBand {
  std::uint32_t packed_argb = 0;
  float start_distance = 0.0f;
  float end_distance = 0.0f;
  float density = 0.0f;
};

static_assert(sizeof(DayNightFogBand) == 16);

struct DayNightScreenEffectFogOverride {
  bool active = false;
  float start_factor = 0.0f;
  float end_distance = 0.0f;
  float density = 0.0f;
  std::uint32_t color_argb = 0;

  std::uint32_t sky_dome_enabled = 0;
};

struct DayNightLightingAccumulatorState {
  float blend_value_a = 0.0f;
  float blend_value_b = 0.0f;
  std::int32_t pending_refresh_count = 0;
  bool force_lighting_refresh = false;
  DayNightVec3 camera_position{};
  float normalized_time_of_day = 0.0f;
  DayNightVec3 published_camera_position{};
  std::uint8_t published_alpha = 0;
  std::uint32_t refresh_count = 0;
  std::uint32_t publish_update_count = 0;
};

void DayNight_ResetLightingAccumulatorState();
void DayNight_SetLightingAccumulatorInputs(const DayNightVec3 &camera_position,
                                           float normalized_time_of_day);
void DayNight_SetLightingAccumulatorBlendState(float blend_value_a,
                                               float blend_value_b,
                                               std::int32_t pending_refresh_count,
                                               bool force_lighting_refresh);
[[nodiscard]] const DayNightLightingAccumulatorState &
DayNight_GetLightingAccumulatorState();

void DayNight_ResetAreaLightingBlendAndForceFlag(bool force_lighting_refresh);

void DayNight_RefreshLightingAccumulator();

void DayNight_PublishLightingVectorAndAlpha();

std::uint32_t DayNight_SetSpellVisualLightingTint(std::uint32_t packed_argb, float alpha);
void DayNight_ClearSpellVisualLightingTint();
[[nodiscard]] const DayNightSpellVisualLightingTint &DayNight_GetSpellVisualLightingTint();
[[nodiscard]] std::uint32_t
DayNight_BlendPackedArgbWithSpellVisualTint(std::uint32_t destination_argb);

[[nodiscard]] std::uint32_t DayNight_ScalePackedArgbRgbByByte(std::uint32_t packed_argb,
                                                              std::uint8_t scale_byte);

[[nodiscard]] std::uint32_t DayNight_ScaleFogBandColor(std::uint32_t packed_argb,
                                                       float fog_band_color_scale);

[[nodiscard]] float DayNight_ComputeAdvancedFogDensity(float fog_band_span,
                                                       float camera_far_clip);

void DayNight_ApplyFadeToFogBand(DayNightFogBand &band, float fade_factor,
                                 std::uint32_t fade_color_argb);

void DayNight_UpdateFogBands();

void DayNight_SetOutdoorFogBand(const DayNightFogBand &band);
void DayNight_WriteOutdoorFogBand(DayNightLightEnv &env,
                                  const DayNightFogBand &band);
[[nodiscard]] DayNightFogBand DayNight_GetCurrentFogBand();
[[nodiscard]] DayNightFogBand DayNight_GetBlendedFogBand();

void DayNight_SetScreenEffectFogOverride(float start_factor, float end_distance,
                                         std::uint32_t color_argb,
                                         std::uint32_t sky_dome_enabled);
void DayNight_ClearScreenEffectFogOverride();
[[nodiscard]] bool DayNight_HasScreenEffectFogOverride();
[[nodiscard]] DayNightScreenEffectFogOverride DayNight_GetScreenEffectFogOverride();

[[nodiscard]] bool DayNight_IsSkyDomeEnabled();

[[nodiscard]] std::int32_t DayNight_GetFogMode();
void DayNight_SetFogMode(std::int32_t mode);

void DayNight_SetFogModeFromRenderPath(std::int32_t mode);

void DayNight_SetFogBandColorScale(float scale);

void DayNight_SetFogFadeColor(std::uint32_t color_argb);

void DayNight_SetFogFadeDistanceParams(float distance_scale, float max_distance,
                                        float base_offset);

DayNightLightEnv *DayNight_GetLightEnv();
[[nodiscard]] float DayNight_GetSceneVisibility();
void DayNight_SetSceneVisibility(float visibility);
void DayNight_ResetNearbyLightQueue(DayNightLightEnv &env);

void DayNightLightEnv_Init(DayNightLightEnv &env);

inline constexpr std::size_t kLightEnvLightDirectionIndex = 12;

inline constexpr std::size_t kLightEnvLightHeadingAngleIndex = 15;

inline constexpr std::size_t kLightEnvWeatherOverlayFactorIndex = 19;

inline constexpr std::size_t kLightEnvSceneVisibilityIndex = 75;

inline constexpr std::size_t kLightEnvGlowBlendFactorIndex = 34;

inline constexpr std::size_t kDayNightDbcAmbientUpperColorIndex = 55;
inline constexpr std::size_t kDayNightDbcAmbientLowerColorIndex = 56;
inline constexpr std::size_t kDayNightDbcDiffuseUpperColorIndex = 57;
inline constexpr std::size_t kDayNightDbcDiffuseLowerColorIndex = 58;
inline constexpr std::size_t kDayNightDbcGlowUpperColorIndex = 59;
inline constexpr std::size_t kDayNightDbcGlowLowerColorIndex = 60;

inline constexpr std::size_t kLightEnvSunHaloColorIndex = 62;
inline constexpr std::size_t kDayNightInterpolatedAmbientColorIndex = 53;
inline constexpr std::size_t kDayNightInterpolatedDiffuseColorIndex = 54;
inline constexpr std::size_t kDayNightDerivedColorHistoryBaseIndex = 92;
inline constexpr std::size_t kDayNightDerivedDirectionCurrentBaseIndex = 103;
inline constexpr std::size_t kDayNightDerivedColorCurrentDiffuseIndex = 106;
inline constexpr std::size_t kDayNightDerivedColorCurrentAmbientIndex = 107;
inline constexpr std::size_t kDayNightDerivedColorCurrentMidpointIndex = 108;
inline constexpr std::size_t kDayNightDerivedColorCurrentBrightenedMidpointIndex = 109;
inline constexpr std::size_t kDayNightDerivedColorCurrentHsvRgbBaseIndex = 110;
inline constexpr std::size_t kDayNightDerivedColorCurrentDimAmbientIndex = 114;
inline constexpr std::size_t kDayNightDerivedColorDimAmbientAlphaByteOffset = 0xDE;

struct DayNightSceneLightingBranchInput {
  bool force_full_lighting = false;
  bool has_override_position = false;
  std::array<float, 3> override_position{};
  float scene_visibility = 0.0f;
};

struct DayNightSceneLightingBranchResult {
  float visibility_scale = 1.0f;
  bool apply_visibility_overlay = false;
  float overlay_factor = 0.0f;
  std::uint32_t overlay_argb = 0;
  bool enable_full_lighting = false;
  bool reset_area_lighting_blend = false;
  bool has_point_position = false;
  std::array<float, 3> point_position{};
};

[[nodiscard]] DayNightSceneLightingBranchResult
DayNight_EvaluateSceneLightingBranch(
    const DayNightSceneLightingBranchInput &input);

struct DayNightSceneLightingSyncPlan {
  bool reset_nearby_light_queue = true;
  bool sample_override_position = false;
  DayNightSceneLightingBranchResult branch{};
  bool rebuild_derived_light_state = true;
  bool refresh_lighting_accumulator = true;
  bool publish_lighting_vector_and_alpha = true;
  bool resolve_area_lighting = true;
  bool update_shadow_mod = true;
  bool finalize_scene_light_cache = true;
};

[[nodiscard]] DayNightSceneLightingSyncPlan
DayNight_BuildSceneLightingSyncPlan(
    const DayNightSceneLightingBranchInput &input);

struct DayNightSceneLightingTailInput {

  std::array<float, 3> derived_direction{};

  std::uint32_t ambient_argb = 0;
  std::uint32_t diffuse_argb = 0;

  std::uint32_t unscaled_argb = 0;
  float visibility_scale = 1.0f;
};

struct DayNightSceneLightingCache {
  std::array<float, 3> direction{};
  std::uint32_t scaled_ambient_argb = 0;
  std::uint32_t scaled_diffuse_argb = 0;
  std::array<float, 3> ambient_rgb{};
  std::array<float, 3> diffuse_rgb{};
  std::array<float, 3> unscaled_rgb{};
};

[[nodiscard]] DayNightSceneLightingCache
DayNight_FinalizeSceneLightingCache(const DayNightSceneLightingTailInput &input);

inline constexpr std::size_t kFogFarClipIndex = 16;
inline constexpr std::size_t kFogDistanceIndex = 17;
inline constexpr std::size_t kLightEnvFrameDeltaIndex = 18;
inline constexpr std::size_t kCurrentFogBandBaseIndex = 35;
inline constexpr std::size_t kBlendedFogBandBaseIndex = 40;
inline constexpr std::size_t kPreviousFogBandBaseIndex = 44;
inline constexpr std::size_t kOutdoorFogColorIndex = 61;
inline constexpr std::size_t kOutdoorFogEndDistanceIndex = 71;
inline constexpr std::size_t kOutdoorFogStartFactorIndex = 72;
inline constexpr std::size_t kOutdoorFogDensityIndex = 73;

void DayNight_UpdateDerivedDirectionCache(DayNightLightEnv &env);

void DayNight_EvaluateFullLightState();

void DayNight_ComputeLightHeadingAndGlow();

void DayNight_DeriveAmbientDiffuseColorCache(DayNightLightEnv &env);

inline constexpr std::size_t kDayNightActiveAreaBlendWeightIndex = 39;
inline constexpr std::size_t kDayNightActiveAreaOwnerIndex = 48;
inline constexpr std::size_t kDayNightActiveAreaSkyboxIndex = 50;

void DayNight_ClearActiveAreaOwner(std::uint32_t owner_token);

void DayNight_SetLightRecordLookup(std::int32_t first_light_id,
                                   const std::vector<std::uint32_t> &resolved_handles);
void DayNight_ClearLightRecordLookup();
[[nodiscard]] bool DayNight_QueueNearbyLightRecord(DayNightLightEnv &env, std::int32_t light_rec_id,
                                                   float weight);

[[nodiscard]] std::uint32_t DayNight_ClearSkyUpdateMaskBit(std::int32_t bit_index);
[[nodiscard]] std::uint32_t DayNight_GetSkyUpdateMask();
void DayNight_SetSkyUpdateMask(std::uint32_t mask);

bool DayNight_SolveOrderedQuadraticRootsStable(float a, float b, float c, float &smallerRoot,
                                               float &largerRoot);

struct DayNightCameraLiquidState {
  float submersion_depth = 0.0f;
  std::uint32_t liquid_type_id = 0;
};

void DayNight_SetCameraLiquidState(float submersion_depth, std::uint32_t liquid_type_id);
void DayNight_ClearCameraLiquidState();
[[nodiscard]] DayNightCameraLiquidState DayNight_GetCameraLiquidState();
[[nodiscard]] float DayNight_ComputeCameraLiquidDepthFade();

bool DayNight_ProjectDirectionOntoCloudSphere(const DayNightVec3 &cameraPosition,
                                              const DayNightVec3 &direction, float &smallerRoot,
                                              float &largerRoot, DayNightVec3 &worldPoint);

DayNightVec2 DayNight_ProjectCloudPointToTexture(const DayNightVec3 &cameraPosition,
                                                 const DayNightVec3 &worldPoint, float textureSize);

bool DayNight_ProjectDirectionToCloudTexture(const DayNightVec3 &cameraPosition,
                                             const DayNightVec3 &direction, float textureSize,
                                             DayNightCloudTextureProjection &projection);

[[nodiscard]] float DayNight_SampleCloudLayerOpacityAtWorldPoint(
    const DayNightVec3 &worldPoint);

[[nodiscard]] float DayNight_TransformSunGlareCloudFade(
    const DayNightVec3 &worldPoint);
[[nodiscard]] float DayNight_TransformMoonGlareCloudFade(
    const DayNightVec3 &worldPoint);
[[nodiscard]] float DayNight_ComputeGlareCloudFade(const DayNightGlareState &state);

[[nodiscard]] DayNightCloudGlowParameters DayNight_BuildCloudGlowParameters();

int DayNight_OnSunGlareToggle(int cmdId, const char *buffer);
void DayNight_SetSunGlareEnabled(bool enabled, bool showOutput);
[[nodiscard]] bool DayNight_IsSunGlareEnabled();
[[nodiscard]] bool DayNight_IsMoonGlareEnabled();
[[nodiscard]] const DayNightGlareState &DayNight_GetSunGlareState();
[[nodiscard]] const DayNightGlareState &DayNight_GetMoonGlareState();

std::uint8_t DayNight_UpdateStarsModelState(DayNightStarsModelState &state,
                                            const DayNightLightEnv &env);
std::uint8_t DayNight_UpdateGlobalStarsModelState();
[[nodiscard]] const DayNightStarsModelState &DayNight_GetStarsModelState();

void DayNight_UpdateCelestialTextureState();

void DayNight_RefreshGlareBillboardColors();
[[nodiscard]] const DayNightCelestialState &DayNight_GetCelestialState();

void DayNight_BuildCloudAlphaTable(float gamma);

int DayNight_InitSunGlare();

int DayNight_InitMoonGlare();

AreaLightOverride &DayNight_AllocTransitionSlot();
[[nodiscard]] std::uint32_t DayNight_GetAreaLightOverrideCount();
[[nodiscard]] std::uint32_t DayNight_GetAreaLightOverrideCapacity();
[[nodiscard]] std::span<const AreaLightOverride> DayNight_GetAreaLightOverrideData();

void DayNight_ResetTransition(bool markDirty);

[[nodiscard]] bool DayNight_TransitionLight(
    const openwow::data::dbc::DbcLoader *dbc, std::uint32_t srcLightId,
    std::uint32_t targetLightId, std::uint32_t fadeTimeMs);

void DayNight_UpdateAreaLightOverrideTransitions(std::uint32_t tickCountMs);

void DayNight_BeginOverrideFadeOut(std::uint32_t lightRecId,
                                   std::uint32_t fadeTimeMs);

int DayNight_OnSkyCloudLODChanged(int cvarId, int showOutput, const char *value);

void DayNight_Init(int mapId);

void DayNight_LoadCelestialTexture(DayNightCelestialTextureState &state, const char *filePath,
                                   float billboardScaleX, float billboardScaleY);

void LightRec_CleanupAll();

std::uint32_t DayNight_LoadSkyModel(std::string_view modelPath, std::uint32_t flags);
std::uint32_t DayNight_SetSkyModelSlot(std::size_t slot, std::string_view modelPath,
                                       std::uint32_t flags, float alpha);
void DayNight_ClearSkyModelSlot(std::size_t slot);
void DayNight_ClearSkyModelSlots();
[[nodiscard]] DayNightSkyModelSlots DayNight_GetSkyModelSlots();
[[nodiscard]] bool DayNight_HasOpaqueSkyModelSlot();

void AreaLightOverride_Resize(uint32_t newCount);

void DayNight_CloudLayerResize(uint32_t newCount);

void DayNight_InitCloudLayer(std::uint8_t lodLevel, int rowsPerUpdate);

[[nodiscard]] bool DayNight_IsInitialized();
bool DayNight_ApplySkyCloudLod(int lodLevel, bool showOutput);

struct DayNightCloudTextureUpload {
  std::uint32_t firstRow = 0;
  std::uint32_t endRow = 0;
  std::uint32_t gridSize = 0;

  [[nodiscard]] bool empty() const { return endRow <= firstRow; }
};

void DayNight_UpdateCloudLayerTexture();

[[nodiscard]] DayNightCloudTextureUpload DayNight_TakeCloudLayerUpload(
    std::uint32_t textureIndex);

[[nodiscard]] const std::vector<std::uint32_t> &DayNight_GetCloudLayerTexels();

[[nodiscard]] std::uint32_t DayNight_GetCloudLayerGridSize();

[[nodiscard]] std::uint32_t DayNight_GetCloudLayerActiveTextureIndex();

[[nodiscard]] bool DayNight_IsCloudLayerEnabled();

int DayNight_BuildCloudDome(float radius);
const DayNightCloudDomeMesh &DayNight_GetCloudDomeMesh();

int DayNight_BuildSkyDome(float radius);
const DayNightSkyDomeMesh &DayNight_GetSkyDomeMesh();

[[nodiscard]] std::size_t DayNight_UpdateGlareBillboards(
    DayNightBillboardSubmission *out, std::size_t capacity);
void DayNight_UpdateGlareBillboards();

[[nodiscard]] std::size_t DayNight_UpdateCelestialDiscBillboards(
    DayNightBillboardSubmission *out, std::size_t capacity);

}

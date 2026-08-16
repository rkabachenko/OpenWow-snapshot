#pragma once

#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::render::m2 {

enum class M2ResultStatus { kReady, kNotReady, kFailed, kUnsupported };
enum class M2ResultReason {
  kNone, kInvalidHandle, kInvalidQuery, kInvalidTransform, kMissingFile,
  kParseFailed, kSkinProfileUnavailable, kUnsupportedSkinProfile,
  kInvalidSkinGeometry, kInvalidTextureUnit, kInvalidMaterial,
  kUnsupportedBlendMode, kUnsupportedShader, kMissingTexture,
  kTextureNotReady, kShaderNotReady, kGpuGeometryNotReady, kBonePoseFailed,
  kMissingAttachment, kMissingCamera, kMissingLight, kInvalidParticle,
  kInvalidRibbon, kParticleUnsupported, kRibbonUnsupported,
  kNoDrawableGeometry, kNotVisible,
};

[[nodiscard]] inline constexpr const char* M2ResultStatusName(M2ResultStatus value) noexcept {
  switch (value) {
  case M2ResultStatus::kReady: return "Ready";
  case M2ResultStatus::kNotReady: return "NotReady";
  case M2ResultStatus::kFailed: return "Failed";
  case M2ResultStatus::kUnsupported: return "Unsupported";
  }
  return "Unknown";
}

[[nodiscard]] inline constexpr const char* M2ResultReasonName(M2ResultReason value) noexcept {
  switch (value) {
  case M2ResultReason::kNone: return "None";
  case M2ResultReason::kInvalidHandle: return "InvalidHandle";
  case M2ResultReason::kInvalidQuery: return "InvalidQuery";
  case M2ResultReason::kInvalidTransform: return "InvalidTransform";
  case M2ResultReason::kMissingFile: return "MissingFile";
  case M2ResultReason::kParseFailed: return "ParseFailed";
  case M2ResultReason::kSkinProfileUnavailable: return "SkinProfileUnavailable";
  case M2ResultReason::kUnsupportedSkinProfile: return "UnsupportedSkinProfile";
  case M2ResultReason::kInvalidSkinGeometry: return "InvalidSkinGeometry";
  case M2ResultReason::kInvalidTextureUnit: return "InvalidTextureUnit";
  case M2ResultReason::kInvalidMaterial: return "InvalidMaterial";
  case M2ResultReason::kUnsupportedBlendMode: return "UnsupportedBlendMode";
  case M2ResultReason::kUnsupportedShader: return "UnsupportedShader";
  case M2ResultReason::kMissingTexture: return "MissingTexture";
  case M2ResultReason::kTextureNotReady: return "TextureNotReady";
  case M2ResultReason::kShaderNotReady: return "ShaderNotReady";
  case M2ResultReason::kGpuGeometryNotReady: return "GpuGeometryNotReady";
  case M2ResultReason::kBonePoseFailed: return "BonePoseFailed";
  case M2ResultReason::kMissingAttachment: return "MissingAttachment";
  case M2ResultReason::kMissingCamera: return "MissingCamera";
  case M2ResultReason::kMissingLight: return "MissingLight";
  case M2ResultReason::kInvalidParticle: return "InvalidParticle";
  case M2ResultReason::kInvalidRibbon: return "InvalidRibbon";
  case M2ResultReason::kParticleUnsupported: return "ParticleUnsupported";
  case M2ResultReason::kRibbonUnsupported: return "RibbonUnsupported";
  case M2ResultReason::kNoDrawableGeometry: return "NoDrawableGeometry";
  case M2ResultReason::kNotVisible: return "NotVisible";
  }
  return "Unknown";
}

[[nodiscard]] inline constexpr M2ResultStatus MergeM2ResultStatus(
    M2ResultStatus current, M2ResultStatus next) noexcept {
  if (current == M2ResultStatus::kFailed || next == M2ResultStatus::kReady) return current;
  if (next == M2ResultStatus::kFailed) return M2ResultStatus::kFailed;
  if (next == M2ResultStatus::kUnsupported) return M2ResultStatus::kUnsupported;
  return current == M2ResultStatus::kReady ? M2ResultStatus::kNotReady : current;
}

[[nodiscard]] inline constexpr bool IsTerminalM2ResultStatus(M2ResultStatus value) noexcept {
  return value == M2ResultStatus::kFailed || value == M2ResultStatus::kUnsupported;
}

struct M2TransformVisibilityResult {
  M2ResultStatus transform_status = M2ResultStatus::kReady;
  M2ResultStatus visibility_status = M2ResultStatus::kReady;
};

struct M2OperationSummary {
  M2ResultStatus status = M2ResultStatus::kReady;
  std::uint32_t attempted_instance_count = 0;
  std::uint32_t ready_instance_count = 0;
  std::uint32_t not_ready_instance_count = 0;
  std::uint32_t failed_instance_count = 0;
  std::uint32_t unsupported_instance_count = 0;
  void AddStatus(M2ResultStatus next) noexcept {
    ++attempted_instance_count;
    switch (next) {
    case M2ResultStatus::kReady: ++ready_instance_count; break;
    case M2ResultStatus::kNotReady: ++not_ready_instance_count; break;
    case M2ResultStatus::kFailed: ++failed_instance_count; break;
    case M2ResultStatus::kUnsupported: ++unsupported_instance_count; break;
    }
    status = MergeM2ResultStatus(status, next);
  }
  void AddSummary(const M2OperationSummary& other) noexcept {
    status = MergeM2ResultStatus(status, other.status);
    attempted_instance_count += other.attempted_instance_count;
    ready_instance_count += other.ready_instance_count;
    not_ready_instance_count += other.not_ready_instance_count;
    failed_instance_count += other.failed_instance_count;
    unsupported_instance_count += other.unsupported_instance_count;
  }
};

struct M2BatchUniforms {
  RenderVec4 uv_transform0{1.0f, 0.0f, 0.0f, 0.0f};
  RenderVec4 uv_transform0_row1{0.0f, 1.0f, 0.0f, 0.0f};
  RenderVec4 uv_transform1{1.0f, 0.0f, 0.0f, 0.0f};
  RenderVec4 uv_transform1_row1{0.0f, 1.0f, 0.0f, 0.0f};
  RenderVec4 tex_gen_flags{0.0f, 1.0f, 0.0f, 0.0f};
  RenderVec4 material_color{1.0f, 1.0f, 1.0f, 1.0f};
  RenderVec4 combiner_mode{0.0f, 0.0f, 1.0f, 0.0f};
  RenderVec4 alpha_ref{0.0f, 0.0f, 0.0f, 0.0f};
  RenderVec4 material_flags{0.0f, 0.0f, 0.0f, 0.0f};
  RenderVec4 fog_params{900.0f, 1200.0f, 0.0f, 0.0f};
  RenderVec4 fog_color{0.6f, 0.7f, 0.85f, 1.0f};
  RenderVec4 emissive_color{0.0f, 0.0f, 0.0f, 0.0f};
  static constexpr int kMaxM2Lights = 4;
  RenderVec4 light_count{0.0f, 0.0f, 0.0f, 0.0f};
  std::array<RenderVec4, kMaxM2Lights> light_pos_range{};
  std::array<RenderVec4, kMaxM2Lights> light_attenuation{};
  std::array<RenderVec4, kMaxM2Lights> light_color{};
  RenderVec4 light_ambient{1.0f, 1.0f, 1.0f, 0.0f};
};

namespace detail {
struct M2SharedBatchUniforms;
}

struct M2SharedBatchUniformsHandle {
  detail::M2SharedBatchUniforms* block = nullptr;
  [[nodiscard]] explicit operator bool() const noexcept {
    return block != nullptr;
  }
};

inline constexpr std::uint16_t kInvalidM2AnimationSequenceIndex = 0xFFFFu;
inline constexpr std::size_t kMaxM2AnimationAliasDepth = 506u;
inline constexpr std::uint32_t kInvalidM2AnimationBehaviorId =
    static_cast<std::uint32_t>(kMaxM2AnimationAliasDepth);
inline constexpr std::int32_t kM2BaseAnimationSlotIndex = -1;
inline constexpr std::size_t kM2RetailAnimationSlotCount = 35u;
inline constexpr std::uint32_t kM2RetailInvalidAnimationIdThreshold = 0x1FAu;
inline constexpr float kM2DefaultCameraFovRad = 0.785398163f;

struct M2AnimationRequest {
  std::int32_t animation_lookup_id = -1;
  std::uint32_t animation_id = 0;
  std::int32_t sub_animation_index = -1;
  std::int32_t loop_count = 0;
  float speed = 1.0f;

  bool zero_blend = false;
};

struct M2AnimationSelectionResult {
  bool resolved = false;
  std::uint32_t requested_animation_id = 0;
  std::uint32_t resolved_animation_id = 0;
  std::uint16_t resolved_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::int32_t resolved_sub_animation_index = -1;
  std::uint16_t resolved_lookup_sequence_index = kInvalidM2AnimationSequenceIndex;
  bool used_random_variant = false;
};

struct M2PoseBlendState {
  std::uint16_t source_sequence_index = 0xFFFFu;
  float source_time = 0.0f;
  float duration = 0.0f;
  float remaining = 0.0f;
  std::uint32_t source_duration_ms = 0;
  bool source_clamped_at_end = false;
  float source_speed = 1.0f;

  [[nodiscard]] bool IsBlending() const noexcept {
    return source_sequence_index != 0xFFFFu && remaining > 0.0f;
  }

  friend bool operator==(const M2PoseBlendState&,
                         const M2PoseBlendState&) = default;
};

[[nodiscard]] inline float M2PoseBlendFactor(
    const M2PoseBlendState& blend) noexcept {
  if (!blend.IsBlending() || blend.duration <= 0.0f) {
    return 1.0f;
  }
  float elapsed = 1.0f - blend.remaining / blend.duration;
  elapsed = elapsed < 0.0f ? 0.0f : (elapsed > 1.0f ? 1.0f : elapsed);
  return elapsed * elapsed * (elapsed * -2.0f + 3.0f);
}

struct M2AnimationSlotState {
  bool active = false;
  std::uint32_t requested_animation_id = 0;
  std::uint32_t resolved_animation_id = 0;
  std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
  std::int32_t sub_animation_index = -1;
  std::int32_t animation_lookup_id = -1;
  std::uint16_t animation_lookup_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::int32_t loop_count = 0;
  bool used_random_variant = false;
  float time_seconds = 0.0f;
  float speed = 1.0f;
  std::uint32_t duration_ms = 0;

  M2PoseBlendState blend;

  friend bool operator==(const M2AnimationSlotState&,
                         const M2AnimationSlotState&) = default;
};

using M2AnimationSlotStates =
    std::array<M2AnimationSlotState, kM2RetailAnimationSlotCount>;

struct M2CameraPose {
  RenderVec3 position{0.0f, 0.0f, 0.0f};
  RenderVec3 target{0.0f, 0.0f, 0.0f};
  float fov_rad{kM2DefaultCameraFovRad};
  float near_clip{0.1f};
  float far_clip{1000.0f};
  float roll_rad{0.0f};
};

struct M2CameraBasis {
  RenderVec3 forward{0.0f, 0.0f, -1.0f};
  RenderVec3 right{1.0f, 0.0f, 0.0f};
  RenderVec3 up{0.0f, 1.0f, 0.0f};
};

struct M2UvTransform {
  RenderVec4 row0{1.0f, 0.0f, 0.0f, 0.0f};
  RenderVec4 row1{0.0f, 1.0f, 0.0f, 0.0f};
};

struct M2ColorSample {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
  float a{1.0f};
};

struct M2LightSample {
  std::uint16_t type{0};
  RenderVec3 position{0.0f, 0.0f, 0.0f};
  RenderVec3 ambient_color{0.0f, 0.0f, 0.0f};
  float ambient_intensity{0.0f};
  RenderVec3 diffuse_color{0.0f, 0.0f, 0.0f};
  float diffuse_intensity{0.0f};
  float attenuation_start{0.0f};
  float attenuation_end{0.0f};
  bool visible{true};
};

struct M2RibbonEmitterSample {
  RenderVec3 color{1.0f, 1.0f, 1.0f};
  float alpha{1.0f};
  float height_above{10.0f};
  float height_below{10.0f};
  std::uint16_t texture_slot{0};
  bool visible{true};
};

struct M2ParticleEmitterSample {
  float emission_speed{0.0f};
  float speed_variation{0.0f};
  float vertical_range{0.0f};
  float horizontal_range{0.0f};
  float gravity{0.0f};
  float lifespan{0.0f};
  float emission_rate{0.0f};
  float emission_area_length{0.0f};
  float emission_area_width{0.0f};
  float z_source{0.0f};
  bool enabled{true};
};

struct M2TriggeredEvent {
  std::uint32_t identifier{0};
  std::uint32_t data{0};
  std::int16_t bone{-1};
  RenderVec3 model_position{0.0f, 0.0f, 0.0f};
  RenderVec3 world_position{0.0f, 0.0f, 0.0f};
  std::uint32_t delay_to_interval_end_ms{0};
};

struct M2ModelLoadResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t model_id = 0;
};

struct M2RenderInstanceResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t submitted_draw_count = 0;

  std::uint32_t submitted_geometry_draw_count = 0;
};

[[nodiscard]] inline bool M2RenderResultBlocksComposite(
    const M2RenderInstanceResult &result,
    const M2RenderInstanceResult &other) noexcept {
  if (result.status == M2ResultStatus::kFailed) {
    return true;
  }
  if (result.status != M2ResultStatus::kUnsupported) {
    return false;
  }

  return result.submitted_draw_count == 0u && other.submitted_draw_count == 0u;
}

struct M2RenderFrameResult {
  M2ResultStatus status = M2ResultStatus::kReady;
  std::uint32_t attempted_instance_count = 0;
  std::uint32_t ready_instance_count = 0;
  std::uint32_t not_ready_instance_count = 0;
  std::uint32_t failed_instance_count = 0;
  std::uint32_t unsupported_instance_count = 0;
  std::uint32_t submitted_draw_count = 0;
  std::uint32_t submitted_geometry_draw_count = 0;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;

  void AddInstanceResult(const M2RenderInstanceResult &instance_result) {
    ++attempted_instance_count;
    submitted_draw_count += instance_result.submitted_draw_count;
    submitted_geometry_draw_count += instance_result.submitted_geometry_draw_count;

    switch (instance_result.status) {
    case M2ResultStatus::kReady:
      ++ready_instance_count;
      break;
    case M2ResultStatus::kNotReady:
      ++not_ready_instance_count;
      status = MergeM2ResultStatus(status, instance_result.status);
      break;
    case M2ResultStatus::kFailed:
      ++failed_instance_count;
      status = MergeM2ResultStatus(status, instance_result.status);
      break;
    case M2ResultStatus::kUnsupported:
      ++unsupported_instance_count;
      status = MergeM2ResultStatus(status, instance_result.status);
      break;
    }

    if (instance_result.status != M2ResultStatus::kReady &&
        reason == M2ResultReason::kNone) {
      reason = instance_result.reason;
      detail = instance_result.detail;
    }
  }
};

enum class M2RenderPassScope : std::uint8_t {
  kAll = 0,
  kOpaqueOnly,
  kTransparentOnly,
};

[[nodiscard]] constexpr bool M2RenderPassScopeIncludesOpaque(
    const M2RenderPassScope scope) noexcept {
  return scope == M2RenderPassScope::kAll ||
         scope == M2RenderPassScope::kOpaqueOnly;
}

[[nodiscard]] constexpr bool M2RenderPassScopeIncludesTransparent(
    const M2RenderPassScope scope) noexcept {
  return scope == M2RenderPassScope::kAll ||
         scope == M2RenderPassScope::kTransparentOnly;
}

struct M2InstanceCreateResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t instance_id = 0;
};

struct M2ModelInstanceLoadResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t model_id = 0;
  std::uint32_t instance_id = 0;
  bool used_fallback = false;
};

struct M2ShadowDrawCall {
  std::uint32_t instance_id = 0;
  std::uint32_t model_id = 0;
  std::uint32_t skin_profile = 0;
  std::uint32_t material_index = 0;
  std::uint32_t texture_unit_index = 0;
  std::uint32_t sort_key = 0;
  std::uint32_t batch_count = 1;
};

struct M2ShadowDrawCallLists {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<M2ShadowDrawCall> opaque;
  std::vector<M2ShadowDrawCall> transparent;
};

struct M2ShadowClassQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint16_t shadow_class = 0;
};

struct M2PrimaryVisualStateResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  bool reset_on_detach = true;
};

enum class M2ChildDestroyPolicy {
  kDestroyWithParent,
  kDetachOnParentDestroy,
};

inline constexpr std::string_view kDefaultM2FallbackModelPath = "Spells\\ErrorCube.mdx";

inline constexpr std::uint32_t kM2AttachmentVisualSelectorFlag = 0x00400000u;
inline constexpr std::uint32_t kM2AttachedPrimaryVisualEnabledFlag = 0x00000002u;
inline constexpr std::uint32_t kM2PrimaryVisualEntryActiveFlag = 0x00000001u;
inline constexpr std::uint32_t kM2PrimaryVisualEntryEnabledFlag = 0x00000008u;
inline constexpr std::uint32_t kM2InstanceDirtyWorldTransform = 0x00008000u;
inline constexpr std::uint32_t kM2InstanceDirtyVisibleSubmeshes = 0x00010000u;

inline constexpr std::int32_t kM2NoAttachmentLookupIndex = -1;
inline constexpr std::uint32_t kM2AttachmentLookupLeftWrist = 0u;
inline constexpr std::uint32_t kM2AttachmentLookupRightPalm = 1u;
inline constexpr std::uint32_t kM2AttachmentLookupLeftPalm = 2u;
inline constexpr std::uint32_t kM2AttachmentLookupRightElbow = 3u;
inline constexpr std::uint32_t kM2AttachmentLookupLeftElbow = 4u;
inline constexpr std::uint32_t kM2AttachmentLookupRightShoulder = 5u;
inline constexpr std::uint32_t kM2AttachmentLookupLeftShoulder = 6u;
inline constexpr std::uint32_t kM2AttachmentLookupRightKnee = 7u;
inline constexpr std::uint32_t kM2AttachmentLookupLeftKnee = 8u;
inline constexpr std::uint32_t kM2AttachmentLookupRightHip = 9u;
inline constexpr std::uint32_t kM2AttachmentLookupLeftHip = 10u;
inline constexpr std::uint32_t kM2AttachmentLookupHelm = 11u;
inline constexpr std::uint32_t kM2AttachmentLookupBack = 12u;
inline constexpr std::uint32_t kM2AttachmentLookupShoulderFlapRight = 13u;
inline constexpr std::uint32_t kM2AttachmentLookupShoulderFlapLeft = 14u;
inline constexpr std::uint32_t kM2AttachmentLookupChestBloodFront = 15u;
inline constexpr std::uint32_t kM2AttachmentLookupChestBloodBack = 16u;
inline constexpr std::uint32_t kM2AttachmentLookupBreath = 17u;
inline constexpr std::uint32_t kM2AttachmentLookupPlayerName = 18u;
inline constexpr std::uint32_t kM2AttachmentLookupBase = 19u;
inline constexpr std::uint32_t kM2AttachmentLookupHead = 20u;
inline constexpr std::uint32_t kM2AttachmentLookupSpellLeftHand = 21u;
inline constexpr std::uint32_t kM2AttachmentLookupSpellRightHand = 22u;
inline constexpr std::uint32_t kM2AttachmentLookupSpecial1 = 23u;
inline constexpr std::uint32_t kM2AttachmentLookupSpecial2 = 24u;
inline constexpr std::uint32_t kM2AttachmentLookupSpecial3 = 25u;
inline constexpr std::uint32_t kM2AttachmentLookupSheathMainHand = 26u;
inline constexpr std::uint32_t kM2AttachmentLookupSheathOffHand = 27u;
inline constexpr std::uint32_t kM2AttachmentLookupSheathShield = 28u;
inline constexpr std::uint32_t kM2AttachmentLookupPlayerNameMounted = 29u;
inline constexpr std::uint32_t kM2AttachmentLookupLargeWeaponLeft = 30u;
inline constexpr std::uint32_t kM2AttachmentLookupLargeWeaponRight = 31u;
inline constexpr std::uint32_t kM2AttachmentLookupHipWeaponLeft = 32u;
inline constexpr std::uint32_t kM2AttachmentLookupHipWeaponRight = 33u;
inline constexpr std::uint32_t kM2AttachmentLookupChest = 34u;
inline constexpr std::uint32_t kM2AttachmentLookupHandArrow = 35u;

using M2WorldBoundingSphere = std::array<float, 4>;
using M2ModelBoundingSphere = std::array<float, 4>;

using M2WorldBoundingBox = RenderAabb;

struct M2WorldBoundingSphereQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2WorldBoundingSphere sphere{};
};

struct M2WorldBoundingBoxQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2WorldBoundingBox box{};
};

struct M2ModelAttachmentInfo {
  std::uint16_t attachment_index = 0;
  std::uint16_t bone_index = 0;
  RenderVec3 local_position{};
};

struct M2AttachmentPositionQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  RenderVec3 position{};
};

struct M2AttachmentTransformQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  RenderMatrix4x4 matrix{kRenderIdentityMatrix4x4};
};

struct M2ModelWorldPointQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  RenderVec3 position{};
};

struct M2ModelWorldTransformMatrixQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  RenderMatrix4x4 matrix{kRenderIdentityMatrix4x4};
};

struct M2RootBoneWorldMatrixQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  RenderMatrix4x4 matrix{kRenderIdentityMatrix4x4};
};

struct M2AttachmentInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2ModelAttachmentInfo attachment;
};

struct M2ModelCollisionGeometry {
  std::vector<RenderVec3> vertices;
  std::vector<std::uint16_t> triangles;
  float radius = 0.0f;
};

struct M2ModelCollisionGeometryQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;

  std::shared_ptr<const M2ModelCollisionGeometry> geometry;
};

struct M2ModelAnimationSequenceInfo {
  std::uint32_t animation_id = 0;
  std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint32_t duration_ms = 0;

  float move_speed = 0.0f;

  std::uint32_t flags = 0;
};

struct M2FirstAnimationDurationQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t duration_ms = 0;
};

struct M2ModelAnimationSequenceQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool has_sequence = false;
  M2ModelAnimationSequenceInfo sequence;
};

struct M2ModelSpatialInfo {
  std::array<float, 6> local_bounds{};
  std::array<float, 4> local_bounding_sphere{};
};

struct M2ModelSpatialInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2ModelSpatialInfo spatial;
};

struct M2InstanceSpatialInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2ModelSpatialInfo spatial;
};

struct M2ModelBoundingSphereQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2ModelBoundingSphere sphere{};
};

struct M2ModelGeometryInfo {
  bool has_vertices = false;
  std::array<float, 6> vertex_bounds{};
};

struct M2ModelGeometryInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2ModelGeometryInfo info;
};

struct M2ModelInfo {
  std::string model_path;
  std::string name;
  std::uint32_t version = 0;
  std::uint32_t global_flags = 0;
  std::uint32_t view_count = 0;
  std::uint32_t selected_skin_profile = 0;
  bool loaded = false;
  bool render_ready = false;
  std::size_t vertex_count = 0;
  std::size_t bone_count = 0;
  std::size_t key_bone_lookup_count = 0;
  std::size_t texture_count = 0;
  std::size_t texture_unit_count = 0;
  std::size_t submesh_count = 0;
  std::size_t gpu_texture_count = 0;
  std::size_t color_count = 0;
  std::size_t transparency_count = 0;
  std::size_t global_sequence_count = 0;
  std::size_t animation_duration_count = 0;
  std::size_t light_count = 0;
  std::size_t camera_count = 0;
  std::size_t attachment_count = 0;
  std::size_t event_count = 0;
  std::size_t particle_emitter_count = 0;
  std::size_t ribbon_emitter_count = 0;
};

struct M2ModelInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2ModelInfo info;
};

struct M2ModelReadinessQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool loaded = false;
  bool render_ready = false;
};

struct M2ModelTextureDependency {
  std::uint16_t texture_index = 0;
  std::string texture_path;
};

struct M2ModelTextureDependenciesQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<M2ModelTextureDependency> dependencies;
};

enum class M2StreamState : std::uint8_t {
  kInvalid = 0, kPreparing, kCommitPending, kReady, kFailed,
};

using M2ModelEarlyReadyCallback = std::function<void(
    const M2ModelSpatialInfo&,
    std::shared_ptr<const M2ModelCollisionGeometry>)>;

struct M2StreamTicket {
  std::uint64_t resource_id = 0;
  std::uint64_t generation = 0;
  [[nodiscard]] explicit operator bool() const noexcept {
    return resource_id != 0u && generation != 0u;
  }
  [[nodiscard]] bool operator==(const M2StreamTicket&) const = default;
};

enum class M2StreamPriority : std::uint8_t {
  kAmbient,
  kNormal,
  kVisibleUnit,
  kCritical,
};

struct M2StreamQuery {
  M2StreamState state = M2StreamState::kInvalid;
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t model_id = 0;
  M2ModelSpatialInfo spatial_info;

  std::shared_ptr<const M2ModelCollisionGeometry> collision_geometry;

  bool cpu_model_ready = false;

  bool header_bounds_ready = false;
  std::vector<M2ModelTextureDependency> texture_dependencies;
  std::size_t committed_texture_count = 0;
};

struct M2StreamCommitBudget {
  std::chrono::microseconds wall_time{1500};
  std::uint64_t upload_bytes = 4u * 1024u * 1024u;
  std::uint32_t textures = 4;
  std::uint32_t models = 2;
};

struct M2StreamPumpResult {
  std::uint32_t completions = 0;
  std::uint32_t textures_committed = 0;
  std::uint32_t models_committed = 0;
  std::uint32_t models_recycled = 0;
  std::uint64_t upload_bytes = 0;
};

using M2StreamFileLoader =
    std::function<std::vector<std::uint8_t>(const std::string&)>;

using M2StreamPrefixFileLoader =
    std::function<std::vector<std::uint8_t>(const std::string&, std::size_t)>;
using M2StreamTexturePreparer =
    std::function<PreparedTextureUpload(const std::string&)>;

struct M2PreparedModelAccess;

class M2PreparedModel {
 public:
  M2PreparedModel();
  ~M2PreparedModel();
  M2PreparedModel(M2PreparedModel&&) noexcept;
  M2PreparedModel& operator=(M2PreparedModel&&) noexcept;
  M2PreparedModel(const M2PreparedModel&) = delete;
  M2PreparedModel& operator=(const M2PreparedModel&) = delete;

  [[nodiscard]] const std::vector<M2ModelTextureDependency>&
  TextureDependencies() const noexcept;
  [[nodiscard]] const M2ModelSpatialInfo& SpatialInfo() const noexcept;

  [[nodiscard]] std::shared_ptr<const M2ModelCollisionGeometry>
  CollisionGeometry() const noexcept;
  [[nodiscard]] bool Valid() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend class M2System;
  friend struct M2PreparedModelAccess;
};

struct M2ModelPrepareResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::unique_ptr<M2PreparedModel> prepared;
};

using M2StreamModelPreparer = std::function<M2ModelPrepareResult(
    const std::string&, const M2StreamFileLoader&,
    const M2ModelEarlyReadyCallback&)>;

struct M2PreparedResourceBundle {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<M2ModelTextureDependency> texture_dependencies;
  M2ModelSpatialInfo spatial_info;
  std::shared_ptr<const M2ModelCollisionGeometry> collision_geometry;
  std::vector<PreparedTextureUpload> textures;
  std::unique_ptr<M2PreparedModel> model;
};

struct M2ResourceStreamerBackend {
  std::function<M2PreparedResourceBundle(
      const std::string&, const M2StreamFileLoader&,
      const M2StreamTexturePreparer&, const M2ModelEarlyReadyCallback&)> prepare;
  std::function<bool(PreparedTextureUpload)> commit_texture;
  std::function<M2ModelLoadResult(std::unique_ptr<M2PreparedModel>)> commit_model;
  std::function<M2ResultStatus(std::uint32_t)> recycle_model;
  std::function<M2InstanceCreateResult(std::uint32_t)> create_instance;
};

struct M2ModelAnimationListEntry {
  std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint32_t animation_id = 0;
  std::uint32_t duration_ms = 0;
};

struct M2ModelAnimationListQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<M2ModelAnimationListEntry> animations;
};

struct M2ModelSubmeshSectionIdsQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<std::uint16_t> section_ids;
};

struct M2ModelLightCountQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::size_t light_count = 0;
};

struct M2SampleBoneMatricesQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<float> bone_matrices;
};

struct M2CameraSampleQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2CameraPose pose;
};

struct M2LightSampleQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2LightSample light;
};

struct M2InstanceAnimationInfo {
  std::uint32_t requested_animation_id = 0;
  std::uint32_t resolved_animation_id = 0;
  std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint32_t time_ms = 0;
  std::uint32_t duration_ms = 0;
};

struct M2InstanceAnimationInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2InstanceAnimationInfo info;
};

struct M2InstanceEventInfo {
  std::uint32_t identifier = 0;
  std::uint32_t animation_id = 0;
  std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint32_t time_ms = 0;
  RenderVec3 model_position{};
  RenderVec3 world_position{};
};

struct M2InstanceEventQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool has_event = false;
  M2InstanceEventInfo event;
};

struct M2SegmentIntersection {
  float fraction = 0.0f;
  float distance = 0.0f;
  RenderVec3 point{};
};

struct M2SegmentIntersectionQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool has_intersection = false;
  M2SegmentIntersection intersection;
};

struct M2ChildInstanceLinkInfo {
  std::uint32_t instance_id = 0;
  M2ChildDestroyPolicy destroy_policy = M2ChildDestroyPolicy::kDestroyWithParent;
  std::int32_t attachment_slot = -1;
};

struct M2ChildLinksQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<M2ChildInstanceLinkInfo> child_links;
};

struct M2VisibleSubmeshFilterInfo {
  bool enabled = false;
  bool dirty = false;
  std::vector<std::size_t> indices;
};

struct M2VisibleSubmeshFilterQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2VisibleSubmeshFilterInfo filter;
};

struct M2TextureOverrideInfo {
  std::uint32_t type_id = 0;
  std::string texture_path;
  bool texture_loaded = false;
};

struct M2ParticleColorOverrideInfo {
  std::uint32_t color_index = 0;
  std::uint32_t start_raw = 0;
  std::uint32_t mid_raw = 0;
  std::uint32_t end_raw = 0;
};

struct M2ParticleColorRecord {
  std::array<std::uint32_t, 3> start{};
  std::array<std::uint32_t, 3> mid{};
  std::array<std::uint32_t, 3> end{};

  [[nodiscard]] bool operator==(const M2ParticleColorRecord&) const = default;
};

using M2AnimationChangedCallback = std::function<void(std::uint32_t)>;

struct M2AnimationRequestEvent {
  std::uint32_t requested_animation_id = 0;
  std::uint32_t resolved_animation_id = 0;
  std::int32_t resolved_sub_animation_index = -1;
};

using M2AnimationRequestCallback =
    std::function<void(const M2AnimationRequestEvent &)>;
using M2AnimationCompletionCallback = std::function<void(std::uint32_t)>;
using M2TriggeredEventCallback = std::function<void(const M2TriggeredEvent &)>;

struct M2InstanceAttachmentVisualState {
  bool ready_for_updates = true;
  bool data_initialized = true;
  std::uint32_t flags = 0;
  std::uint32_t primary_visual_count = 1;
  std::uint32_t active_primary_visual_count = 0;
  std::uint32_t current_animation_duration_ms = 0;
  std::uint32_t active_primary_visual_duration_ms = 0;
  float primary_visual_weight = 1.0f;
  std::vector<std::uint32_t> primary_visual_entry_flags;
};

struct M2InstanceAttachmentVisualStateQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2InstanceAttachmentVisualState visual_state;
};

struct M2PosePlaybackInfo {
  bool bound = false;
  bool blending = false;
  std::uint16_t current_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint16_t blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
  float current_time = 0.0f;
  float speed = 1.0f;
};

struct M2InstanceEffectContext {
  bool spell_attributes_ex5_bit30 = false;
};

struct M2TransientWeaponTrailState {
  std::uint32_t packed_argb = 0;
  std::uint32_t duration_ms = 0;
  std::uint32_t start_tick_ms = 0;
  std::uint32_t previous_tick_ms = 0;

  friend constexpr bool operator==(M2TransientWeaponTrailState,
                                   M2TransientWeaponTrailState) = default;
};

struct M2InstanceInfo {
  std::uint32_t model_id = 0;
  std::uint64_t visual_revision = 0;
  RenderVec3 position{};
  RenderVec3 rotation_degrees{};
  float scale = 1.0f;
  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};
  bool has_explicit_world_transform = false;
  bool world_transform_dirty = false;
  std::uint32_t requested_animation_id = 0;
  std::uint32_t resolved_animation_id = 0;
  std::uint16_t animation_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::int32_t animation_sub_index = -1;
  std::int32_t animation_lookup_id = -1;
  std::uint16_t animation_lookup_sequence_index = 0;
  std::int32_t animation_loop_count = 0;
  bool animation_used_random_variant = false;
  float animation_time_seconds = 0.0f;
  float animation_speed = 1.0f;
  std::uint32_t processed_event_time_ms = 0;
  M2AnimationSlotStates animation_slots{};
  bool visible = true;
  std::uint32_t parent_instance_id = 0;
  std::vector<M2ChildInstanceLinkInfo> child_links;
  bool has_animation_changed_callback = false;
  bool has_animation_request_callback = false;
  bool has_animation_completion_callback = false;
  bool has_triggered_event_callback = false;
  bool destroy_in_progress = false;
  M2InstanceEffectContext effect_context;
  M2InstanceAttachmentVisualState attachment_visual_state;
  std::optional<M2PosePlaybackInfo> pose_playback;
  std::vector<M2TextureOverrideInfo> texture_overrides;
  std::vector<M2ParticleColorOverrideInfo> particle_color_overrides;
  bool has_replacement_colors = false;
  std::array<std::uint32_t, 3> replacement_colors{};
  bool has_visible_submesh_filter = false;
  bool visible_submesh_filter_dirty = false;
  std::vector<std::size_t> visible_submesh_indices;
  std::array<float, 4> tint_color = {1.0f, 1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;
  std::array<float, 4> selection_glow_color = {0.0f, 0.0f, 0.0f, 1.0f};
  bool has_batch_uniforms = false;
  bool effect_emitters_enabled = true;
  std::optional<M2TransientWeaponTrailState> transient_weapon_trail;
  bool particle_system_bound = false;
  bool ribbon_system_initialized = false;
};

struct M2InstanceInfoQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2InstanceInfo info;
};

class M2InstanceStore;

class M2InstanceStoreLease {
 public:
  M2InstanceStoreLease();
  ~M2InstanceStoreLease();
  M2InstanceStoreLease(M2InstanceStoreLease&&) noexcept;
  M2InstanceStoreLease& operator=(M2InstanceStoreLease&&) noexcept;
  M2InstanceStoreLease(const M2InstanceStoreLease&) = delete;
  M2InstanceStoreLease& operator=(const M2InstanceStoreLease&) = delete;

 private:
  struct State;
  explicit M2InstanceStoreLease(std::unique_ptr<State> state);
  [[nodiscard]] static M2InstanceStoreLease Create(
      std::weak_ptr<std::function<void(std::uint32_t)>> destroy,
      std::uint32_t root_instance_id);
  std::unique_ptr<State> state_;
  friend class M2InstanceStore;
};

class M2VisualCloneLease {
 public:
  M2VisualCloneLease();
  ~M2VisualCloneLease();
  M2VisualCloneLease(M2VisualCloneLease&&) noexcept;
  M2VisualCloneLease& operator=(M2VisualCloneLease&&) noexcept;
  M2VisualCloneLease(const M2VisualCloneLease&) = delete;
  M2VisualCloneLease& operator=(const M2VisualCloneLease&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::uint64_t visual_revision() const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept;

 private:
  struct State;
  explicit M2VisualCloneLease(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
  friend class M2System;
};

struct M2VisualTreeRevisionQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint64_t revision = 0;
};

struct M2VisualCloneCreateResult {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  M2VisualCloneLease lease;
};

struct M2VisualCloneNodeSnapshot {
  std::uint32_t model_id = 0;
  bool has_parent = false;
  std::size_t parent_index = 0;
  std::int32_t attachment_slot = kM2NoAttachmentLookupIndex;
  bool visible = true;
  std::vector<M2TextureOverrideInfo> texture_overrides;
  std::vector<M2ParticleColorOverrideInfo> particle_color_overrides;
  bool has_replacement_colors = false;
  std::array<std::uint32_t, 3> replacement_colors{};
  bool has_visible_submesh_filter = false;
  std::vector<std::size_t> visible_submesh_indices;
  RenderVec4 tint_color{1.0f, 1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;
};

struct M2VisualCloneSnapshotQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<M2VisualCloneNodeSnapshot> nodes;
};

struct M2InstanceModelQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::uint32_t model_id = 0;
};

struct M2SelectionGlowColorQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::array<float, 4> rgba = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct M2InstanceReadinessQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool render_ready = false;
};

struct M2AnimationSlotStateQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool has_slot = false;
  M2AnimationSlotState slot;
};

struct M2BoneMatricesQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  std::vector<float> bone_matrices;
};

struct M2KeyBoneQuery {
  M2ResultStatus status = M2ResultStatus::kNotReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
  bool present = false;
};

struct M2BoneBasisOverride {
  std::uint32_t bone_index = 0;
  RenderMatrix4x4 basis{};
};

struct M2StaticInstancingProfile {
  bool opaque_instanceable{false};
  bool has_transparent_residue{false};
};

struct M2InstancedDrawRecord {
  RenderMatrix4x4 transform{};
  RenderVec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct M2DoodadFrameRenderRequest {
  std::uint32_t instance_id = 0;
  const RenderMatrix4x4* world_transform = nullptr;
  const M2BatchUniforms* uniforms = nullptr;
  M2SharedBatchUniformsHandle shared_uniforms{};
  RenderVec4 tint_rgba{1.0f, 1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;

  std::uint64_t world_transform_revision = 0;
};

struct M2AttachmentFrameRenderRequest {
  std::uint32_t instance_id = 0;
  const RenderMatrix4x4* world_transform = nullptr;
  const M2BatchUniforms* uniforms = nullptr;
  M2SharedBatchUniformsHandle shared_uniforms{};
  bool visible = true;
  RenderVec4 tint_rgba{1.0f, 1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;
};

struct M2AttachmentPlacementRequest {

  std::uint32_t child_instance_id = 0;

  std::uint32_t parent_instance_id = 0;

  std::uint32_t attachment_lookup_index = 0;
};

struct M2AttachmentPlacementQuery {
  M2InstanceReadinessQuery readiness;
  M2AttachmentTransformQuery transform;
};

struct M2InstanceFrameSpatialRequest {
  std::uint32_t instance_id = 0;

  std::uint32_t model_id = 0;

  const RenderMatrix4x4* world_transform = nullptr;

  bool visible_submeshes_applied = false;

  bool query_spatial = true;
};

struct M2InstanceFrameSpatialResult {

  bool pose_installed = false;

  bool spatial_ready = false;
  M2ModelSpatialInfo spatial{};
};

struct M2InstanceFramePresentationRequest {
  std::uint32_t instance_id = 0;

  std::uint32_t model_id = 0;

  bool sample_animation = false;
  std::uint32_t animation_id = 0;
  std::uint32_t sample_time_ms = 0;

  bool clamp_to_duration = false;
  float speed = 1.0f;
  bool zero_blend = false;
  bool visible = true;
  RenderVec4 tint_rgba{1.0f, 1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;

  bool visible_submeshes_applied = false;
};

struct M2ProjectedTextureDraw {
  std::uint32_t instance_id = 0;

  std::uint32_t batch_index = 0;

  std::array<float, 6> world_bounds{};

  std::array<float, 2> reference_xy{};
  std::array<float, 2> reference_uv{};

  std::array<float, 4> uv_jacobian{};

  std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};

  std::uint64_t render_state = 0;

  std::uint64_t sampler_flags = 0;
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
};

}

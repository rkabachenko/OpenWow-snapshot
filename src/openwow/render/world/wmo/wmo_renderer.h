#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/scene/occlusion/occlusion_depth_buffer.h"
#include "openwow/render/world/wmo/wmo_mesh.h"
#include "openwow/render/world/wmo/wmo_material_pipeline.h"
#include "openwow/world/wmo/wmo_visibility.h"

namespace openwow::render {

inline constexpr std::uint64_t kWmoOneSidedCullState = BGFX_STATE_CULL_CW;
inline constexpr std::uint64_t kWmoDefaultDepthTestState =
    BGFX_STATE_DEPTH_TEST_LESS;
inline constexpr std::uint64_t kWmoTransitionExteriorDepthTestState =
    BGFX_STATE_DEPTH_TEST_LEQUAL;

struct WmoShaderResources;

struct WmoShaderHandleBudget {
  std::size_t renderer_instances{0};
  std::size_t programs{0};
  std::size_t uniforms{0};

  [[nodiscard]] constexpr std::size_t total_handles() const noexcept {
    return programs + uniforms;
  }
};

struct WmoSharedShaderResourceStats {
  std::size_t renderer_instances{0};
  std::size_t programs{0};
  std::size_t uniforms{0};
  bool initialized{false};
  bool creation_failed{false};
};

void InvalidateSharedWmoShaderResources();

struct WmoMaterialGpu {
  TextureLease diffuse_lease;
  TextureLease env_map_lease;
  bgfx::TextureHandle diffuse = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle env_map = BGFX_INVALID_HANDLE;
  uint32_t blend_mode{0};
  uint32_t shader{0};
  uint32_t effective_shader{0};
  uint32_t flags{0};
  uint32_t sidn_color_bgra{0};
  bool two_sided{false};
  bool clamp_s{false};
  bool clamp_t{false};
  bool alpha_test{false};
  bool unfogged{false};
};

struct PreparedWmoMaterialTextures {
  std::vector<std::shared_ptr<const PreparedTextureUpload>> uploads;
};

uint32_t ResolveWmoMaterialShader(uint32_t shader, uint32_t blend_mode,
                                  bool diffuse_texture_is_opaque);

struct WmoMergedGeometry {
  bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
  std::uint32_t vertex_count{0u};
  std::uint32_t index_count{0u};
};

inline constexpr std::size_t kWmoMergedVertexFormBasic = 0u;
inline constexpr std::size_t kWmoMergedVertexFormComposite = 1u;
inline constexpr std::size_t kWmoMergedVertexFormCount = 2u;

[[nodiscard]] inline constexpr std::size_t WmoMergedVertexFormIndex(
    const bool composite_vertices) noexcept {
  return composite_vertices ? kWmoMergedVertexFormComposite
                            : kWmoMergedVertexFormBasic;
}

enum class WmoMergedGeometryState : std::uint8_t {

  kAccumulating,

  kResident,

  kUnavailable,
};

inline constexpr std::size_t kMaxWmoOccluderPolygonsPerGroup = 32u;
inline constexpr float kMinWmoOccluderPolygonArea = 4.0f;

struct WmoOccluderBatchSpan {
  std::uint32_t first{0u};
  std::uint32_t count{0u};
};

struct WmoGroupGpu {
  bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle collision_ib = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle outdoor_surface_ib = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle primary_debug_ib = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle renderable_ib = BGFX_INVALID_HANDLE;

  std::vector<WmoBatchMesh> batches;
  std::vector<std::uint32_t> batch_submit_generations;

  std::vector<occlusion::OccluderPolygon> occluder_polygons;
  std::vector<WmoOccluderBatchSpan> occluder_spans;

  std::vector<WmoVertex> staged_vertices;
  std::vector<WmoCompositeVertex> staged_composite_vertices;
  std::vector<std::uint16_t> staged_indices;

  std::uint32_t merged_vertex_base{0u};
  std::uint32_t merged_index_base{0u};

  std::uint32_t vertex_count{0u};
  float bounds_min[3]{};
  float bounds_max[3]{};
  uint32_t flags{0};
  bool composite_vertices{false};
  bool merged_resident{false};
  bool collision_valid{false};
  bool outdoor_surface_valid{false};
  bool primary_debug_valid{false};
  bool renderable_valid{false};
  bool valid{false};
  world::WmoGroupPublicationStatus publication_status{
      world::WmoGroupPublicationStatus::kPending};
};

enum class WmoLightingMode : std::uint8_t {
  Unlit,
  Outdoor,
  Window,
  Interior,
};

[[nodiscard]] WmoLightingMode ResolveRetailWmoLightingMode(
    std::uint32_t group_flags, WmoBatchMesh::Region region,
    std::uint32_t material_flags, bool unified_render_path) noexcept;

enum WmoSubmitSkipReason : std::uint32_t {
  kWmoSubmitSkipNone = 0u,
  kWmoSubmitSkipRendererUnavailable = 1u << 0u,
  kWmoSubmitSkipGroupOutOfRange = 1u << 1u,
  kWmoSubmitSkipVisibilityMask = 1u << 2u,
  kWmoSubmitSkipGpuNotReady = 1u << 3u,
  kWmoSubmitSkipFrustum = 1u << 4u,
  kWmoSubmitSkipEmptyBatch = 1u << 5u,

  kWmoSubmitSkipBatchAlreadySubmitted = 1u << 7u,
  kWmoSubmitSkipOcclusion = 1u << 8u,
};

struct WmoGroupSubmitTelemetry {
  std::uint16_t group_index{};
  std::size_t moba_batch_count{};
  std::size_t material_ready_count{};
  std::size_t material_skipped_count{};
  std::size_t batch_already_submitted_count{};
  std::size_t submit_count{};
  std::uint32_t skip_reasons{kWmoSubmitSkipNone};
  world::WmoGroupPublicationStatus publication_status{
      world::WmoGroupPublicationStatus::kPending};
  bool gpu_ready{false};
};

struct WmoSubmitTelemetry {
  std::uint16_t view_id{};
  std::uint16_t viewport_width{};
  std::uint16_t viewport_height{};
  std::size_t queued_group_path_count{};
  std::size_t submit_count{};
  std::size_t group_occlusion_culled_count{};
  std::size_t occluder_polygon_count{};
  std::vector<WmoGroupSubmitTelemetry> records;
};

class WmoRenderer {
public:
  static constexpr std::size_t kSharedProgramHandleCount = 4u;

  static constexpr std::size_t kSharedUniformHandleCount = 4u;

  explicit WmoRenderer(TextureManager& texture_manager)
      : texture_manager_(texture_manager) {}
  ~WmoRenderer();

  WmoRenderer(const WmoRenderer &) = delete;
  WmoRenderer &operator=(const WmoRenderer &) = delete;

  bool Initialize();

  void BeginStreaming(const data::wmo::WmoRoot& root,
                      std::size_t group_count);

  [[nodiscard]] world::WmoGroupPublicationStatus UploadWmoGroup(
      std::size_t group_index, const data::wmo::WmoGroup& group,
      const WmoGroupMesh& mesh);

  void LoadMaterials(const data::wmo::WmoRoot &root);

  [[nodiscard]] static PreparedWmoMaterialTextures PrepareMaterialTextures(
      const data::wmo::WmoRoot& root,
      const std::function<std::vector<std::uint8_t>(const std::string&)>& loader);

  [[nodiscard]] static PreparedWmoMaterialTextures PrepareMaterialTextures(
      const data::wmo::WmoRoot& root,
      const std::vector<std::uint32_t>& material_indices,
      const std::function<std::vector<std::uint8_t>(const std::string&)>& loader);

  [[nodiscard]] static std::vector<std::string> ResolveMaterialTexturePaths(
      const data::wmo::WmoRoot& root,
      const std::vector<std::uint32_t>& material_indices);

  void LoadPreparedMaterials(const data::wmo::WmoRoot& root,
                             const PreparedWmoMaterialTextures& prepared);

  [[nodiscard]] bool PublishPreparedMaterialSubset(
      const data::wmo::WmoRoot& root,
      const std::vector<std::uint32_t>& material_indices);

  [[nodiscard]] const WmoSubmitTelemetry& Render(
      uint8_t view_id, const float *view_mtx, const float *proj_mtx,
      const RenderMatrix4x4 &model_mtx,
      const world::WmoVisibilityMask *visibility_mask = nullptr,
      std::span<const world::WmoVisibleGroupPath> visible_group_paths = {},
      std::uint16_t viewport_width = 0,
      std::uint16_t viewport_height = 0,
      occlusion::OcclusionDepthBuffer* occlusion = nullptr,
      bgfx::Encoder *encoder = nullptr);

  void RenderCollisionGeometry(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                               const RenderMatrix4x4 &model_mtx,
                               const world::WmoVisibilityMask *visibility_mask = nullptr,
                               bgfx::Encoder *encoder = nullptr);

  void RenderOutdoorSurfaceGeometry(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                                    const RenderMatrix4x4 &model_mtx,
                                    const world::WmoVisibilityMask *visibility_mask = nullptr,
                                    bgfx::Encoder *encoder = nullptr);

  void RenderPrimaryDebugGeometry(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                                  const RenderMatrix4x4 &model_mtx,
                                  const world::WmoVisibilityMask *visibility_mask = nullptr,
                                  bgfx::Encoder *encoder = nullptr);

  void RenderTwoSidedBatch(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                           const RenderMatrix4x4 &model_mtx,
                           const world::WmoVisibilityMask *visibility_mask = nullptr,
                           bgfx::Encoder *encoder = nullptr);

  void Shutdown();

  void Clear();

  void SetFogParams(const RenderFogState &fog);

  void SetSunDirection(float x, float y, float z);
  [[nodiscard]] const float *sun_direction() const {
    return sun_dir_;
  }

  void SetLightColors(const RenderVec3 &ambient, const RenderVec3 &diffuse);
  void SetLightingPalette(const WmoLightingPalette &palette);

  void SetNightGlowIntensity(float intensity);
  void SetSpecularEnabled(bool enabled) noexcept {
    specular_enabled_ = enabled;
  }

  void SetFrustum(const world::Frustum *frustum) {
    frustum_ = frustum;
  }

  [[nodiscard]] std::size_t group_count() const {
    return groups_.size();
  }
  [[nodiscard]] std::size_t material_count() const {
    return materials_.size();
  }
  [[nodiscard]] bool IsGroupResident(std::size_t group_index) const noexcept;
  [[nodiscard]] bool IsMaterialResident(std::size_t material_index) const noexcept;

  [[nodiscard]] static constexpr WmoShaderHandleBudget
  SharedShaderHandleBudget(const std::size_t renderer_instances) noexcept {
    return {
        .renderer_instances = renderer_instances,
        .programs = renderer_instances == 0u ? 0u : kSharedProgramHandleCount,
        .uniforms = renderer_instances == 0u ? 0u : kSharedUniformHandleCount,
    };
  }

  [[nodiscard]] static WmoSharedShaderResourceStats SharedShaderResourceStats() noexcept;

  [[nodiscard]] static constexpr uint64_t BlendStateForMode(
      const uint32_t mode) {
    switch (mode) {
    case 0:
    case 1:
      return 0;
    case 2:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_INV_SRC_ALPHA);
    case 3:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_ONE);
    case 4:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR,
                                   BGFX_STATE_BLEND_ZERO);
    case 5:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR,
                                   BGFX_STATE_BLEND_SRC_COLOR);
    case 6:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR,
                                   BGFX_STATE_BLEND_ONE);
    case 7:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                   BGFX_STATE_BLEND_ONE);
    case 9:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                   BGFX_STATE_BLEND_ZERO);
    case 10:
      return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                   BGFX_STATE_BLEND_ONE);
    default:
      return 0;
    }
  }

 private:
  TextureManager& texture_manager_;
  void LoadMaterialsImpl(const data::wmo::WmoRoot& root,
                         bool allow_synchronous_texture_load);
  [[nodiscard]] bool PublishMaterial(
      const data::wmo::WmoRoot& root, std::size_t material_index,
      bool allow_synchronous_texture_load);
  [[nodiscard]] bool UploadGroupIntoSlot(std::size_t group_index,
                                         const WmoGroupMesh& mesh);
  void RenderDebugGeometry(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                           const RenderMatrix4x4 &model_mtx, WmoDebugGeometryMode mode,
                           const world::WmoVisibilityMask *visibility_mask,
                           bgfx::Encoder *encoder);

  void TryBuildMergedGeometry();
  void ReleaseMergedGeometry();
  void ReleaseStagedGeometry();
  [[nodiscard]] const WmoMergedGeometry& MergedGeometryFor(
      const WmoGroupGpu& gpu) const noexcept;

  void BindGroupVertexStream(const WmoGroupGpu& gpu,
                             bgfx::Encoder *encoder) const;

  void DestroyGroupGpu(WmoGroupGpu &gpu);

  static std::string ResolveTexturePath(const std::vector<uint8_t> &motx, uint32_t offset);

  bgfx::ProgramHandle SelectShaderProgram(uint32_t effective_shader) const;
  const WmoShaderResources *shader_resources_{nullptr};

  RenderFogState fog_{};
  float sun_dir_[4]{0.5f, 0.5f, 1.0f, 0.0f};
  RenderVec4 light_ambient_{1.0f, 1.0f, 1.0f, 0.0f};
  RenderVec4 light_diffuse_{0.0f, 0.0f, 0.0f, 0.0f};
  WmoLightingPalette lighting_palette_{};
  RenderVec4 root_ambient_{0.0f, 0.0f, 0.0f, 0.0f};
  float night_glow_intensity_{0.0f};
  bool specular_enabled_{true};
  bool unified_render_path_{false};

  const world::Frustum *frustum_{nullptr};
  bgfx::VertexLayout layout_{};
  bgfx::VertexLayout composite_layout_{};
  std::vector<WmoGroupGpu> groups_;
  std::array<WmoMergedGeometry, kWmoMergedVertexFormCount> merged_geometry_{};
  WmoMergedGeometryState merged_state_{
      WmoMergedGeometryState::kAccumulating};
  std::uint32_t submit_generation_{0u};
  std::vector<WmoMaterialGpu> materials_;
  std::vector<std::uint8_t> material_resident_;
  WmoSubmitTelemetry submit_telemetry_;
  bool initialized_{false};
};

[[nodiscard]] constexpr bool WmoBatchTriangleOrderIsUnobservable(
    const std::uint32_t blend_mode, const std::uint32_t material_flags,
    const WmoBatchMesh::Region region) noexcept {
  return region != WmoBatchMesh::Region::Transition &&
         WmoRenderer::BlendStateForMode(blend_mode) == 0u &&
         (material_flags & data::wmo::kMatTwoSided) == 0u;
}

}

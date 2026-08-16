#include "openwow/render/world/wmo/wmo_renderer.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/data/texture_cache.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/api/packed_color.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/world/wmo/wmo_material_pipeline.h"
#include "openwow/render/scene/occlusion/occluder_polygon_builder.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <cstddef>
#include <cstring>
#include <functional>
#include <utility>

namespace openwow::render {

namespace wmo_vs_param {

inline constexpr std::size_t kWorldColumn0 = 0u;
inline constexpr std::size_t kWorldColumn1 = 1u;
inline constexpr std::size_t kWorldColumn2 = 2u;
inline constexpr std::size_t kSunDirection = 3u;
inline constexpr std::size_t kLightAmbient = 4u;
inline constexpr std::size_t kLightDiffuse = 5u;
inline constexpr std::size_t kEmissiveColor = 6u;
inline constexpr std::size_t kMaterialParams = 7u;
inline constexpr std::size_t kExtraParams = 8u;
inline constexpr std::size_t kCount = 9u;
}

namespace wmo_fs_param {
inline constexpr std::size_t kGroupColor = 0u;
inline constexpr std::size_t kFogParams = 1u;
inline constexpr std::size_t kFogColor = 2u;
inline constexpr std::size_t kSunDirection = 3u;
inline constexpr std::size_t kMaterialParams = 4u;
inline constexpr std::size_t kExtraParams = 5u;
inline constexpr std::size_t kCount = 6u;
}

using WmoVsParamBlock = std::array<RenderVec4, wmo_vs_param::kCount>;
using WmoFsParamBlock = std::array<RenderVec4, wmo_fs_param::kCount>;

struct WmoShaderResources {
  bgfx::ProgramHandle diffuse = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle specular = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle environment = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle two_layer = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle vs_params = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle fs_params = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle diffuse_sampler = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle environment_sampler = BGFX_INVALID_HANDLE;
};

namespace {

struct WmoShaderResourceCache {
  WmoShaderResources handles{};
  bgfx::RendererType::Enum renderer_type{bgfx::RendererType::Noop};
  std::size_t renderer_instances{0};
  bool creation_attempted{false};
  bool invalidation_pending{false};
};

WmoShaderResourceCache &MutableWmoShaderResourceCache() {
  static WmoShaderResourceCache cache;
  return cache;
}

void SafeDestroy(bgfx::ProgramHandle &handle) {
  if (bgfx::isValid(handle)) {
    bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
  }
}

void SafeDestroy(bgfx::UniformHandle &handle) {
  if (bgfx::isValid(handle)) {
    bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
  }
}

void DestroyWmoShaderResources(WmoShaderResources &handles) {
  SafeDestroy(handles.diffuse);
  SafeDestroy(handles.specular);
  SafeDestroy(handles.environment);
  SafeDestroy(handles.two_layer);
  SafeDestroy(handles.vs_params);
  SafeDestroy(handles.fs_params);
  SafeDestroy(handles.diffuse_sampler);
  SafeDestroy(handles.environment_sampler);
}

[[nodiscard]] bool AreWmoShaderResourcesValid(const WmoShaderResources &handles) {
  return bgfx::isValid(handles.diffuse) && bgfx::isValid(handles.specular) &&
         bgfx::isValid(handles.environment) && bgfx::isValid(handles.two_layer) &&
         bgfx::isValid(handles.vs_params) && bgfx::isValid(handles.fs_params) &&
         bgfx::isValid(handles.diffuse_sampler) && bgfx::isValid(handles.environment_sampler);
}

[[nodiscard]] WmoShaderResources
CreateWmoShaderResources(const bgfx::RendererType::Enum renderer_type) {
  WmoShaderResources handles;
  handles.diffuse = CreateEmbeddedProgram(ShaderProgramId::Wmo, renderer_type);
  handles.specular = CreateEmbeddedProgram(ShaderProgramId::WmoSpecular, renderer_type);
  handles.environment = CreateEmbeddedProgram(ShaderProgramId::WmoEnv, renderer_type);
  handles.two_layer = CreateEmbeddedProgram(ShaderProgramId::WmoTwoLayer, renderer_type);

  if (!bgfx::isValid(handles.diffuse) || !bgfx::isValid(handles.specular) ||
      !bgfx::isValid(handles.environment) || !bgfx::isValid(handles.two_layer)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WmoRenderer: shared shader program creation failed");
    DestroyWmoShaderResources(handles);
    return handles;
  }

  handles.vs_params =
      bgfx::createUniform("u_wmoVsParams", bgfx::UniformType::Vec4,
                          static_cast<std::uint16_t>(wmo_vs_param::kCount));
  handles.fs_params =
      bgfx::createUniform("u_wmoFsParams", bgfx::UniformType::Vec4,
                          static_cast<std::uint16_t>(wmo_fs_param::kCount));
  handles.diffuse_sampler = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);
  handles.environment_sampler = bgfx::createUniform("s_envMap", bgfx::UniformType::Sampler);

  if (!AreWmoShaderResourcesValid(handles)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WmoRenderer: shared uniform creation failed");
    DestroyWmoShaderResources(handles);
  }
  return handles;
}

[[nodiscard]] const WmoShaderResources *AcquireSharedWmoShaderResources() {
  auto &cache = MutableWmoShaderResourceCache();
  const auto renderer_type = bgfx::getRendererType();

  if (!AreWmoShaderResourcesValid(cache.handles) || cache.renderer_type != renderer_type) {
    if (cache.renderer_instances != 0u) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                         "WmoRenderer: renderer changed while WMO resources are borrowed");
      return nullptr;
    }
    if (cache.creation_attempted && cache.renderer_type == renderer_type) {
      return nullptr;
    }

    DestroyWmoShaderResources(cache.handles);
    cache.renderer_type = renderer_type;
    cache.creation_attempted = true;
    cache.handles = CreateWmoShaderResources(renderer_type);
    if (!AreWmoShaderResourcesValid(cache.handles)) {
      return nullptr;
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "WmoRenderer: initialized shared shader resources programs=" +
                           std::to_string(WmoRenderer::kSharedProgramHandleCount) +
                           " uniforms=" + std::to_string(WmoRenderer::kSharedUniformHandleCount));
  }

  ++cache.renderer_instances;
  return &cache.handles;
}

void ReleaseSharedWmoShaderResources(const WmoShaderResources *handles) {
  auto &cache = MutableWmoShaderResourceCache();
  if (handles == &cache.handles && cache.renderer_instances != 0u) {
    --cache.renderer_instances;
    if (cache.renderer_instances == 0u && cache.invalidation_pending) {
      DestroyWmoShaderResources(cache.handles);
      cache.renderer_type = bgfx::RendererType::Noop;
      cache.creation_attempted = false;
      cache.invalidation_pending = false;
    }
  }
}

[[nodiscard]] std::size_t ValidProgramCount(const WmoShaderResources &handles) {
  return static_cast<std::size_t>(bgfx::isValid(handles.diffuse)) +
         static_cast<std::size_t>(bgfx::isValid(handles.specular)) +
         static_cast<std::size_t>(bgfx::isValid(handles.environment)) +
         static_cast<std::size_t>(bgfx::isValid(handles.two_layer));
}

[[nodiscard]] std::size_t ValidUniformCount(const WmoShaderResources &handles) {
  return static_cast<std::size_t>(bgfx::isValid(handles.vs_params)) +
         static_cast<std::size_t>(bgfx::isValid(handles.fs_params)) +
         static_cast<std::size_t>(bgfx::isValid(handles.diffuse_sampler)) +
         static_cast<std::size_t>(bgfx::isValid(handles.environment_sampler));
}
static_assert(WmoRenderer::kSharedUniformHandleCount == 4u,
              "kSharedUniformHandleCount is the budget ValidUniformCount and "
              "CreateWmoShaderResources actually allocate");

constexpr RenderVec4 kDisabledFogParams{100000000.0f, 200000000.0f, 0.0f, 0.0f};
constexpr RenderVec4 kCollisionTint{
    128.0f / 255.0f,
    128.0f / 255.0f,
    1.0f,
    1.0f,
};
constexpr RenderVec4 kOutdoorSurfaceTint{0.0f, 1.0f, 0.0f, 1.0f};
constexpr RenderVec4 kPrimaryDebugTint{
    127.0f / 255.0f,
    127.0f / 255.0f,
    127.0f / 255.0f,
    1.0f,
};
constexpr RenderVec4 kDebugMaterialParams{0.0f, 1.0f, 0.0f, 0.0f};
constexpr RenderVec4 kPrimaryDebugMaterialParams{0.0f, 1.0f, 1.0f, 0.0f};
constexpr RenderVec4 kZeroWmoParams{0.0f, 0.0f, 0.0f, 0.0f};

const RenderVec4 &ResolveDebugTint(WmoDebugGeometryMode mode) {
  switch (mode) {
  case WmoDebugGeometryMode::Collision:
    return kCollisionTint;
  case WmoDebugGeometryMode::OutdoorSurface:
    return kOutdoorSurfaceTint;
  case WmoDebugGeometryMode::Primary:
    return kPrimaryDebugTint;
  case WmoDebugGeometryMode::TwoPass:
    return kCollisionTint;
  }

  return kCollisionTint;
}

[[nodiscard]] WmoVsParamBlock MakeWmoVsParamBlock(
    const RenderMatrix4x4 &model_mtx, const float (&sun_direction)[4],
    const RenderVec4 &light_ambient, const RenderVec4 &light_diffuse,
    const RenderVec4 &emissive_color, const RenderVec4 &material_params,
    const RenderVec4 &extra_params) noexcept {
  WmoVsParamBlock block{};
  block[wmo_vs_param::kWorldColumn0] = {model_mtx[0], model_mtx[1],
                                        model_mtx[2], model_mtx[3]};
  block[wmo_vs_param::kWorldColumn1] = {model_mtx[4], model_mtx[5],
                                        model_mtx[6], model_mtx[7]};
  block[wmo_vs_param::kWorldColumn2] = {model_mtx[8], model_mtx[9],
                                        model_mtx[10], model_mtx[11]};
  block[wmo_vs_param::kSunDirection] = {sun_direction[0], sun_direction[1],
                                        sun_direction[2], sun_direction[3]};
  block[wmo_vs_param::kLightAmbient] = light_ambient;
  block[wmo_vs_param::kLightDiffuse] = light_diffuse;
  block[wmo_vs_param::kEmissiveColor] = emissive_color;
  block[wmo_vs_param::kMaterialParams] = material_params;
  block[wmo_vs_param::kExtraParams] = extra_params;
  return block;
}

[[nodiscard]] WmoFsParamBlock MakeWmoFsParamBlock(
    const RenderVec4 &group_color, const RenderVec4 &fog_params,
    const RenderVec4 &fog_color, const float (&sun_direction)[4],
    const RenderVec4 &material_params,
    const RenderVec4 &extra_params) noexcept {
  WmoFsParamBlock block{};
  block[wmo_fs_param::kGroupColor] = group_color;
  block[wmo_fs_param::kFogParams] = fog_params;
  block[wmo_fs_param::kFogColor] = fog_color;
  block[wmo_fs_param::kSunDirection] = {sun_direction[0], sun_direction[1],
                                        sun_direction[2], sun_direction[3]};
  block[wmo_fs_param::kMaterialParams] = material_params;
  block[wmo_fs_param::kExtraParams] = extra_params;
  return block;
}

const RenderVec4 &ResolveDebugMaterialParams(WmoDebugGeometryMode mode) {
  switch (mode) {
  case WmoDebugGeometryMode::Primary:
    return kPrimaryDebugMaterialParams;
  case WmoDebugGeometryMode::Collision:
  case WmoDebugGeometryMode::OutdoorSurface:
  case WmoDebugGeometryMode::TwoPass:
    return kDebugMaterialParams;
  }

  return kDebugMaterialParams;
}

[[nodiscard]] RenderVec3 TransformPoint(const RenderMatrix4x4 &model_mtx, const RenderVec3 &point) {
  return TransformAffinePoint4x4(point, model_mtx);
}

bool IsBoundsFrustumVisible(const world::Frustum *frustum,
                            const float (&bounds_min)[3],
                            const float (&bounds_max)[3],
                            const RenderMatrix4x4 &model_mtx) {
  if (frustum == nullptr) {
    return true;
  }

  RenderVec3 world_min{};
  RenderVec3 world_max{};

  for (int corner = 0; corner < 8; ++corner) {
    const float x = (corner & 1) != 0 ? bounds_max[0] : bounds_min[0];
    const float y = (corner & 2) != 0 ? bounds_max[1] : bounds_min[1];
    const float z = (corner & 4) != 0 ? bounds_max[2] : bounds_min[2];
    const RenderVec3 world = TransformPoint(model_mtx, {x, y, z});

    if (corner == 0) {
      world_min = world;
      world_max = world;
      continue;
    }

    world_min[0] = std::min(world_min[0], world[0]);
    world_min[1] = std::min(world_min[1], world[1]);
    world_min[2] = std::min(world_min[2], world[2]);
    world_max[0] = std::max(world_max[0], world[0]);
    world_max[1] = std::max(world_max[1], world[1]);
    world_max[2] = std::max(world_max[2], world[2]);
  }

  return frustum->TestAABB(world_min[0], world_min[1], world_min[2], world_max[0], world_max[1],
                           world_max[2]);
}

bool IsFrustumVisible(const world::Frustum *frustum, const WmoGroupGpu &gpu,
                      const RenderMatrix4x4 &model_mtx) {
  return IsBoundsFrustumVisible(frustum, gpu.bounds_min, gpu.bounds_max,
                                model_mtx);
}

void BuildWmoGroupOccluders(const WmoGroupMesh& mesh, WmoGroupGpu& gpu) {
  gpu.occluder_polygons.clear();
  gpu.occluder_spans.clear();
  if constexpr (!occlusion::kOcclusionCullingEnabled) {

    return;
  }
  gpu.occluder_spans.assign(mesh.batches.size(), WmoOccluderBatchSpan{});
  const bool composite = mesh.uses_composite_vertices();
  occlusion::OccluderSourceMesh source{};
  source.vertex_data =
      composite
          ? reinterpret_cast<const std::byte*>(mesh.composite_vertices.data())
          : reinterpret_cast<const std::byte*>(mesh.vertices.data());
  source.vertex_count =
      composite ? mesh.composite_vertices.size() : mesh.vertices.size();
  source.vertex_stride =
      composite ? sizeof(WmoCompositeVertex) : sizeof(WmoVertex);
  source.position_offset =
      composite ? offsetof(WmoCompositeVertex, position)
                : offsetof(WmoVertex, position);
  source.normal_offset = composite ? offsetof(WmoCompositeVertex, normal)
                                   : offsetof(WmoVertex, normal);
  source.indices = mesh.indices;
  if (source.vertex_count == 0u || mesh.indices.empty()) {
    return;
  }

  struct RankedPolygon {
    occlusion::OccluderPolygon polygon;
    float area{0.0f};
    std::uint32_t batch_index{0u};
  };
  std::vector<RankedPolygon> ranked;
  for (std::size_t batch_index = 0; batch_index < mesh.batches.size();
       ++batch_index) {
    const WmoBatchMesh& batch = mesh.batches[batch_index];

    if (batch.region == WmoBatchMesh::Region::Transition ||
        batch.index_count < 3u) {
      continue;
    }
    for (occlusion::OccluderPolygon& polygon :
         occlusion::BuildOccluderPolygons(source, batch.start_index,
                                          batch.index_count,
                                          kMinWmoOccluderPolygonArea)) {
      ranked.push_back({polygon, occlusion::OccluderPolygonArea(polygon),
                        static_cast<std::uint32_t>(batch_index)});
    }
  }

  if (ranked.empty()) {
    return;
  }
  if (ranked.size() > kMaxWmoOccluderPolygonsPerGroup) {
    std::nth_element(ranked.begin(),
                     ranked.begin() + kMaxWmoOccluderPolygonsPerGroup,
                     ranked.end(),
                     [](const RankedPolygon& lhs, const RankedPolygon& rhs) {
                       return lhs.area > rhs.area;
                     });
    ranked.resize(kMaxWmoOccluderPolygonsPerGroup);
  }

  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const RankedPolygon& lhs, const RankedPolygon& rhs) {
                     return lhs.batch_index < rhs.batch_index;
                   });
  gpu.occluder_polygons.reserve(ranked.size());
  for (const RankedPolygon& entry : ranked) {
    WmoOccluderBatchSpan& span = gpu.occluder_spans[entry.batch_index];
    if (span.count == 0u) {
      span.first = static_cast<std::uint32_t>(gpu.occluder_polygons.size());
    }
    ++span.count;
    gpu.occluder_polygons.push_back(entry.polygon);
  }
}

bool IsVisibleInMask(const world::WmoVisibilityMask *visibility_mask, std::size_t group_index) {
  return visibility_mask == nullptr ||
         (group_index < visibility_mask->size() &&
          (*visibility_mask)[group_index] != 0);
}

[[nodiscard]] bool IsWmoGroupPublicationTerminal(
    const world::WmoGroupPublicationStatus status) noexcept {
  return status != world::WmoGroupPublicationStatus::kPending &&
         status != world::WmoGroupPublicationStatus::kRetryableFailure;
}

[[nodiscard]] std::size_t StagedVertexCount(const WmoGroupGpu& gpu) noexcept {
  return gpu.composite_vertices ? gpu.staged_composite_vertices.size()
                                : gpu.staged_vertices.size();
}

[[nodiscard]] bool WmoMaterialNeedsEnvironmentMap(
    const std::uint32_t shader) noexcept {
  return shader == data::wmo::kShaderEnv ||
         shader == data::wmo::kShaderEnvMetal ||
         shader == data::wmo::kShaderTwoLayer;
}

}

void InvalidateSharedWmoShaderResources() {
  auto &cache = MutableWmoShaderResourceCache();
  if (cache.renderer_instances != 0u) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WmoRenderer: deferring shared shader invalidation; live borrowers=" +
                           std::to_string(cache.renderer_instances));
    cache.invalidation_pending = true;
    return;
  }
  DestroyWmoShaderResources(cache.handles);
  cache.renderer_type = bgfx::RendererType::Noop;
  cache.renderer_instances = 0u;
  cache.creation_attempted = false;
  cache.invalidation_pending = false;
}

WmoSharedShaderResourceStats WmoRenderer::SharedShaderResourceStats() noexcept {
  const auto &cache = MutableWmoShaderResourceCache();
  return {
      .renderer_instances = cache.renderer_instances,
      .programs = ValidProgramCount(cache.handles),
      .uniforms = ValidUniformCount(cache.handles),
      .initialized = AreWmoShaderResourcesValid(cache.handles),
      .creation_failed = cache.creation_attempted && !AreWmoShaderResourcesValid(cache.handles),
  };
}

bool WmoRenderer::IsGroupResident(const std::size_t group_index) const noexcept {
  return group_index < groups_.size() && groups_[group_index].valid;
}

bool WmoRenderer::IsMaterialResident(
    const std::size_t material_index) const noexcept {
  return material_index < material_resident_.size() &&
         material_resident_[material_index] != 0u;
}

uint32_t ResolveWmoMaterialShader(uint32_t shader, uint32_t blend_mode,
                                  bool diffuse_texture_is_opaque) {

  if (shader == data::wmo::kShaderDiffuse && blend_mode == 0u && !diffuse_texture_is_opaque) {
    return data::wmo::kShaderOpaque;
  }
  return shader;
}

WmoLightingMode ResolveRetailWmoLightingMode(
    const std::uint32_t group_flags, const WmoBatchMesh::Region region,
    const std::uint32_t material_flags,
    const bool unified_render_path) noexcept {
  const bool unlit = (material_flags & data::wmo::kMatUnlit) != 0u;
  const bool window = (material_flags & data::wmo::kMatWindow) != 0u;

  if (region == WmoBatchMesh::Region::Transition) {
    if (unified_render_path && unlit) {
      return WmoLightingMode::Unlit;
    }
    return window ? WmoLightingMode::Window : WmoLightingMode::Outdoor;
  }

  if (unified_render_path) {
    const bool group_exterior =
        (group_flags &
         (data::wmo::kMogpExterior | data::wmo::kMogpExteriorLit)) != 0u;
    if (group_exterior) {
      return unlit ? WmoLightingMode::Unlit : WmoLightingMode::Outdoor;
    }
    return window ? WmoLightingMode::Window : WmoLightingMode::Interior;
  }

  if (region == WmoBatchMesh::Region::Interior) {
    return WmoLightingMode::Unlit;
  }
  return window ? WmoLightingMode::Window : WmoLightingMode::Outdoor;
}

WmoRenderer::~WmoRenderer() {
  Shutdown();
}

bool WmoRenderer::Initialize() {
  if (initialized_)
    return true;

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();

  composite_layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
      .end();

  shader_resources_ = AcquireSharedWmoShaderResources();
  if (shader_resources_ == nullptr) {
    return false;
  }

  initialized_ = true;
  return true;
}

void WmoRenderer::BeginStreaming(const data::wmo::WmoRoot& root,
                                 const std::size_t group_count) {
  if (!initialized_) {
    return;
  }
  Clear();
  groups_.resize(group_count);
  materials_.resize(root.materials.size());
  material_resident_.assign(root.materials.size(), 0u);
  unified_render_path_ =
      (root.header.flags & data::wmo::kWmoFlagUnifiedRender) != 0u;
  const auto root_ambient = PackedArgbToUnitRgb(root.header.ambientColor);
  root_ambient_ = {root_ambient[0], root_ambient[1], root_ambient[2], 0.0f};
}

world::WmoGroupPublicationStatus WmoRenderer::UploadWmoGroup(
    const std::size_t group_index, const data::wmo::WmoGroup& group,
    const WmoGroupMesh& mesh) {
  if (!initialized_ || group_index >= groups_.size()) {
    return world::WmoGroupPublicationStatus::kRetryableFailure;
  }
  const auto retry = [&]() {
    if (!groups_[group_index].valid) {
      groups_[group_index].publication_status =
          world::WmoGroupPublicationStatus::kRetryableFailure;
    }
    return world::WmoGroupPublicationStatus::kRetryableFailure;
  };
  const world::WmoGroupPublicationStatus classification =
      ClassifyWmoGroupPublication(group, materials_.size());
  if (classification == world::WmoGroupPublicationStatus::kFailed) {
    if (!groups_[group_index].valid) {
      groups_[group_index].publication_status = classification;
    }

    TryBuildMergedGeometry();
    return classification;
  }
  if (classification == world::WmoGroupPublicationStatus::kNoGeometry) {
    DestroyGroupGpu(groups_[group_index]);
    groups_[group_index].publication_status = classification;
    TryBuildMergedGeometry();
    return classification;
  }
  for (const WmoBatchMesh& batch : mesh.batches) {
    if (!IsMaterialResident(batch.material_index)) {
      return retry();
    }
  }
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return retry();
  }
  if (!UploadGroupIntoSlot(group_index, mesh)) {
    return retry();
  }
  groups_[group_index].publication_status =
      world::WmoGroupPublicationStatus::kDrawableReady;
  TryBuildMergedGeometry();
  return world::WmoGroupPublicationStatus::kDrawableReady;
}

bool WmoRenderer::UploadGroupIntoSlot(const std::size_t group_index,
                                      const WmoGroupMesh& mesh) {
  if (!initialized_ || group_index >= groups_.size() ||
      mesh.vertices.empty() || mesh.indices.empty()) {
    return false;
  }

  WmoGroupGpu gpu;
  const bool composite = mesh.uses_composite_vertices();
  const void* vertex_data = composite
                                ? static_cast<const void*>(mesh.composite_vertices.data())
                                : static_cast<const void*>(mesh.vertices.data());
  const std::size_t vertex_bytes =
      composite ? mesh.composite_vertices.size() * sizeof(WmoCompositeVertex)
                : mesh.vertices.size() * sizeof(WmoVertex);
  const bgfx::Memory* vb_mem = bgfx::copy(
      vertex_data, static_cast<std::uint32_t>(vertex_bytes));
  const bgfx::Memory* ib_mem = bgfx::copy(
      mesh.indices.data(), static_cast<std::uint32_t>(
          mesh.indices.size() * sizeof(std::uint16_t)));
  gpu.vb = bgfx::createVertexBuffer(
      vb_mem, composite ? composite_layout_ : layout_);
  gpu.ib = bgfx::createIndexBuffer(ib_mem);
  gpu.batches = mesh.batches;
  gpu.batch_submit_generations.assign(gpu.batches.size(), 0u);
  std::memcpy(gpu.bounds_min, mesh.bounds_min, sizeof(gpu.bounds_min));
  std::memcpy(gpu.bounds_max, mesh.bounds_max, sizeof(gpu.bounds_max));
  gpu.flags = mesh.flags;
  gpu.composite_vertices = composite;
  gpu.vertex_count = static_cast<std::uint32_t>(
      composite ? mesh.composite_vertices.size() : mesh.vertices.size());

  if (!bgfx::isValid(gpu.vb) || !bgfx::isValid(gpu.ib)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WmoRenderer: buffer creation failed for group");
    DestroyGroupGpu(gpu);
    return false;
  }

  if (merged_state_ == WmoMergedGeometryState::kAccumulating) {
    if (composite) {
      gpu.staged_composite_vertices = mesh.composite_vertices;
    } else {
      gpu.staged_vertices = mesh.vertices;
    }
    gpu.staged_indices = mesh.indices;
  }

  BuildWmoGroupOccluders(mesh, gpu);

  gpu.valid = true;
  DestroyGroupGpu(groups_[group_index]);
  groups_[group_index] = std::move(gpu);
  return true;
}

void WmoRenderer::TryBuildMergedGeometry() {
  if (merged_state_ != WmoMergedGeometryState::kAccumulating ||
      groups_.empty()) {
    return;
  }

  for (const WmoGroupGpu& gpu : groups_) {
    if (!IsWmoGroupPublicationTerminal(gpu.publication_status)) {
      return;
    }
  }

  if ((bgfx::getCaps()->supported & BGFX_CAPS_INDEX32) == 0u) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WmoRenderer: renderer lacks BGFX_CAPS_INDEX32; drawing map-object "
        "groups from per-group buffers");
    merged_state_ = WmoMergedGeometryState::kUnavailable;
    ReleaseStagedGeometry();
    return;
  }

  struct MergedFormTotals {
    std::uint64_t vertex_count{0u};
    std::uint64_t index_count{0u};
  };
  std::array<MergedFormTotals, kWmoMergedVertexFormCount> totals{};
  for (const WmoGroupGpu& gpu : groups_) {
    if (!gpu.valid || gpu.staged_indices.empty()) {
      continue;
    }
    MergedFormTotals& form_totals =
        totals[WmoMergedVertexFormIndex(gpu.composite_vertices)];
    form_totals.vertex_count += StagedVertexCount(gpu);
    form_totals.index_count += gpu.staged_indices.size();
  }

  constexpr std::uint64_t kMaxMergedBufferBytes =
      static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());

  std::array<WmoMergedGeometry, kWmoMergedVertexFormCount> built{};
  std::vector<std::uint32_t> merged_vertex_base(groups_.size(), 0u);
  std::vector<std::uint32_t> merged_index_base(groups_.size(), 0u);
  bool merge_refused = false;
  for (std::size_t form = 0u;
       form < kWmoMergedVertexFormCount && !merge_refused; ++form) {
    if (totals[form].index_count == 0u) {
      continue;
    }
    const bool composite = form == kWmoMergedVertexFormComposite;
    const std::size_t stride =
        composite ? sizeof(WmoCompositeVertex) : sizeof(WmoVertex);
    const std::uint64_t vertex_bytes = totals[form].vertex_count * stride;
    const std::uint64_t index_bytes =
        totals[form].index_count * sizeof(std::uint32_t);
    if (vertex_bytes > kMaxMergedBufferBytes ||
        index_bytes > kMaxMergedBufferBytes) {
      merge_refused = true;
      break;
    }

    const bgfx::Memory* vb_mem =
        bgfx::alloc(static_cast<std::uint32_t>(vertex_bytes));
    const bgfx::Memory* ib_mem =
        bgfx::alloc(static_cast<std::uint32_t>(index_bytes));
    auto* merged_indices = reinterpret_cast<std::uint32_t*>(ib_mem->data);
    std::uint32_t vertex_cursor = 0u;
    std::uint32_t index_cursor = 0u;

    for (std::size_t group_index = 0u; group_index < groups_.size();
         ++group_index) {
      const WmoGroupGpu& gpu = groups_[group_index];
      if (!gpu.valid || gpu.staged_indices.empty() ||
          WmoMergedVertexFormIndex(gpu.composite_vertices) != form) {
        continue;
      }
      const std::size_t group_vertex_count = StagedVertexCount(gpu);
      const void* source_vertices =
          composite
              ? static_cast<const void*>(gpu.staged_composite_vertices.data())
              : static_cast<const void*>(gpu.staged_vertices.data());
      std::memcpy(vb_mem->data + static_cast<std::size_t>(vertex_cursor) * stride,
                  source_vertices, group_vertex_count * stride);

      for (std::size_t index = 0u; index < gpu.staged_indices.size(); ++index) {
        merged_indices[index_cursor + index] =
            static_cast<std::uint32_t>(gpu.staged_indices[index]) +
            vertex_cursor;
      }
      merged_vertex_base[group_index] = vertex_cursor;
      merged_index_base[group_index] = index_cursor;
      vertex_cursor += static_cast<std::uint32_t>(group_vertex_count);
      index_cursor += static_cast<std::uint32_t>(gpu.staged_indices.size());
    }

    built[form].vb = bgfx::createVertexBuffer(
        vb_mem, composite ? composite_layout_ : layout_);
    built[form].ib = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
    built[form].vertex_count = vertex_cursor;
    built[form].index_count = index_cursor;
    merge_refused = !bgfx::isValid(built[form].vb) ||
                    !bgfx::isValid(built[form].ib);
  }

  if (merge_refused) {
    for (WmoMergedGeometry& merged : built) {
      if (bgfx::isValid(merged.vb)) {
        bgfx::destroy(merged.vb);
      }
      if (bgfx::isValid(merged.ib)) {
        bgfx::destroy(merged.ib);
      }
    }
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WmoRenderer: merged group buffer creation failed; drawing map-object "
        "groups from per-group buffers");
    merged_state_ = WmoMergedGeometryState::kUnavailable;
    ReleaseStagedGeometry();
    return;
  }

  merged_geometry_ = built;
  std::size_t merged_group_count = 0u;
  for (std::size_t group_index = 0u; group_index < groups_.size();
       ++group_index) {
    WmoGroupGpu& gpu = groups_[group_index];
    if (!gpu.valid || gpu.staged_indices.empty()) {
      continue;
    }
    gpu.merged_vertex_base = merged_vertex_base[group_index];
    gpu.merged_index_base = merged_index_base[group_index];
    gpu.merged_resident = true;

    if (bgfx::isValid(gpu.vb)) {
      bgfx::destroy(gpu.vb);
      gpu.vb = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(gpu.ib)) {
      bgfx::destroy(gpu.ib);
      gpu.ib = BGFX_INVALID_HANDLE;
    }
    ++merged_group_count;
  }
  merged_state_ = WmoMergedGeometryState::kResident;
  ReleaseStagedGeometry();
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "WmoRenderer: merged " + std::to_string(merged_group_count) +
          " group buffers into " +
          std::to_string(
              static_cast<std::size_t>(
                  bgfx::isValid(built[kWmoMergedVertexFormBasic].vb)) +
              static_cast<std::size_t>(
                  bgfx::isValid(built[kWmoMergedVertexFormComposite].vb))) +
          " vertex/index buffer pairs");
}

void WmoRenderer::ReleaseStagedGeometry() {
  for (WmoGroupGpu& gpu : groups_) {
    gpu.staged_vertices.clear();
    gpu.staged_vertices.shrink_to_fit();
    gpu.staged_composite_vertices.clear();
    gpu.staged_composite_vertices.shrink_to_fit();
    gpu.staged_indices.clear();
    gpu.staged_indices.shrink_to_fit();
  }
}

void WmoRenderer::ReleaseMergedGeometry() {
  for (WmoMergedGeometry& merged : merged_geometry_) {
    if (bgfx::isValid(merged.vb)) {
      bgfx::destroy(merged.vb);
    }
    if (bgfx::isValid(merged.ib)) {
      bgfx::destroy(merged.ib);
    }
    merged = {};
  }
  merged_state_ = WmoMergedGeometryState::kAccumulating;
}

const WmoMergedGeometry& WmoRenderer::MergedGeometryFor(
    const WmoGroupGpu& gpu) const noexcept {
  return merged_geometry_[WmoMergedVertexFormIndex(gpu.composite_vertices)];
}

void WmoRenderer::BindGroupVertexStream(const WmoGroupGpu& gpu,
                                        bgfx::Encoder *const encoder) const {
  const DrawEncoder draw{encoder};
  if (gpu.merged_resident) {
    const WmoMergedGeometry& merged = MergedGeometryFor(gpu);
    draw.setVertexBuffer(0, merged.vb, gpu.merged_vertex_base,
                         gpu.vertex_count);
    return;
  }
  draw.setVertexBuffer(0, gpu.vb);
}

void WmoRenderer::LoadMaterials(const data::wmo::WmoRoot &root) {
  LoadMaterialsImpl(root, true);
}

PreparedWmoMaterialTextures WmoRenderer::PrepareMaterialTextures(
    const data::wmo::WmoRoot& root,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& loader) {
  std::vector<std::uint32_t> material_indices(root.materials.size());
  for (std::size_t index = 0; index < material_indices.size(); ++index) {
    material_indices[index] = static_cast<std::uint32_t>(index);
  }
  return PrepareMaterialTextures(root, material_indices, loader);
}

std::vector<std::string> WmoRenderer::ResolveMaterialTexturePaths(
    const data::wmo::WmoRoot& root,
    const std::vector<std::uint32_t>& material_indices) {
  std::vector<std::string> paths;
  std::unordered_set<std::uint32_t> unique_rows;
  unique_rows.reserve(material_indices.size() * 2u);
  const auto append = [&](std::string path) {
    if (!path.empty() &&
        unique_rows.insert(openwow::data::HashTextureCachePath(path)).second) {
      paths.push_back(std::move(path));
    }
  };
  for (const std::uint32_t material_index : material_indices) {
    if (material_index >= root.materials.size()) {
      continue;
    }
    const auto& material = root.materials[material_index];
    std::string diffuse =
        ResolveTexturePath(root.textureNames, material.texture1Ofs);
    if (diffuse.empty()) {
      diffuse = "createcrappygreentexture.blp";
    }
    append(std::move(diffuse));
    if (WmoMaterialNeedsEnvironmentMap(material.shader)) {
      append(ResolveTexturePath(root.textureNames, material.texture2Ofs));
    }
  }
  return paths;
}

PreparedWmoMaterialTextures WmoRenderer::PrepareMaterialTextures(
    const data::wmo::WmoRoot& root,
    const std::vector<std::uint32_t>& material_indices,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& loader) {
  PreparedWmoMaterialTextures prepared;
  const auto paths = ResolveMaterialTexturePaths(root, material_indices);
  prepared.uploads.reserve(paths.size());
  for (const std::string& path : paths) {
    auto upload = TextureManager::PrepareTextureUploadFromLoader(path, loader);
    if (upload.valid) {
      prepared.uploads.push_back(
          std::make_shared<const PreparedTextureUpload>(std::move(upload)));
      continue;
    }

    prepared.failed_paths.push_back(path);
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WmoRenderer: material texture unavailable " + path +
            (upload.error.empty() ? std::string()
                                  : (" detail=" + upload.error)));
  }
  return prepared;
}

void WmoRenderer::LoadPreparedMaterials(
    const data::wmo::WmoRoot& root,
    const PreparedWmoMaterialTextures& prepared) {
  for (const auto& upload : prepared.uploads) {
    if (upload) {
      static_cast<void>(texture_manager_.CommitPreparedTexture(*upload));
    }
  }
  const std::unordered_set<std::string> failed(prepared.failed_paths.begin(),
                                               prepared.failed_paths.end());
  LoadMaterialsImpl(root, false, &failed);
}

void WmoRenderer::LoadMaterialsImpl(
    const data::wmo::WmoRoot &root,
    const bool allow_synchronous_texture_load,
    const std::unordered_set<std::string>* terminally_failed_paths) {
  if (!initialized_) {
    return;
  }

  materials_.clear();
  materials_.resize(root.materials.size());
  material_resident_.assign(root.materials.size(), 0u);
  unified_render_path_ =
      (root.header.flags & data::wmo::kWmoFlagUnifiedRender) != 0u;
  const auto root_ambient = PackedArgbToUnitRgb(root.header.ambientColor);
  root_ambient_ = {root_ambient[0], root_ambient[1], root_ambient[2], 0.0f};
  for (std::size_t material_index = 0; material_index < root.materials.size();
       ++material_index) {
    static_cast<void>(PublishMaterial(root, material_index,
                                      allow_synchronous_texture_load,
                                      terminally_failed_paths));
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WmoRenderer: loaded " + std::to_string(materials_.size()) + " materials");
}

bool WmoRenderer::PublishPreparedMaterialSubset(
    const data::wmo::WmoRoot& root,
    const std::vector<std::uint32_t>& material_indices,
    const std::vector<std::string>& terminally_failed_paths) {
  if (!initialized_ || materials_.size() != root.materials.size()) {
    return false;
  }
  const std::unordered_set<std::string> failed(terminally_failed_paths.begin(),
                                               terminally_failed_paths.end());
  bool ready = true;
  for (const std::uint32_t material_index : material_indices) {
    if (material_index >= root.materials.size()) {
      ready = false;
      continue;
    }
    if (IsMaterialResident(material_index)) {
      continue;
    }
    ready = PublishMaterial(root, material_index, false, &failed) && ready;
  }
  return ready;
}

bool WmoRenderer::PublishMaterial(
    const data::wmo::WmoRoot& root, const std::size_t material_index,
    const bool allow_synchronous_texture_load,
    const std::unordered_set<std::string>* terminally_failed_paths) {
  if (!initialized_ || material_index >= root.materials.size() ||
      material_index >= materials_.size()) {
    return false;
  }

  auto& tex_mgr = texture_manager_;
  const auto acquire_texture = [&](const std::string& path) {
    return allow_synchronous_texture_load
               ? tex_mgr.AcquireTextureStrict(path)
               : tex_mgr.AcquireCachedTexture(path);
  };
  const auto& motx = root.textureNames;
  const auto& mat = root.materials[material_index];
  WmoMaterialGpu gpu_mat;
  gpu_mat.blend_mode = mat.blendMode;
  gpu_mat.shader = mat.shader;
  gpu_mat.flags = mat.flags;
  gpu_mat.sidn_color_bgra = mat.sidnColor;
  gpu_mat.two_sided = (mat.flags & data::wmo::kMatTwoSided) != 0;
  gpu_mat.clamp_s = (mat.flags & data::wmo::kMatClampS) != 0;
  gpu_mat.clamp_t = (mat.flags & data::wmo::kMatClampT) != 0;
  gpu_mat.unfogged = (mat.flags & data::wmo::kMatUnfogged) != 0;
  gpu_mat.alpha_test = (mat.blendMode == 1);

  std::string texture_path = ResolveTexturePath(motx, mat.texture1Ofs);
  if (texture_path.empty()) {
    texture_path = "createcrappygreentexture.blp";
  }
  if (!texture_path.empty()) {
    gpu_mat.diffuse_lease = acquire_texture(texture_path);
    gpu_mat.diffuse = BgfxTextureLeaseAccess::Get(gpu_mat.diffuse_lease);
  }

  bool needs_environment = WmoMaterialNeedsEnvironmentMap(mat.shader);
  if (needs_environment) {
    std::string environment_path =
        ResolveTexturePath(motx, mat.texture2Ofs);
    if (!environment_path.empty()) {
      gpu_mat.env_map_lease = acquire_texture(environment_path);
      gpu_mat.env_map = BgfxTextureLeaseAccess::Get(gpu_mat.env_map_lease);
    }
  }
  if (needs_environment && !bgfx::isValid(gpu_mat.env_map) &&
      ResolveTexturePath(motx, mat.texture2Ofs).empty()) {

    gpu_mat.shader = data::wmo::kShaderOpaque;
    needs_environment = false;
  }

  const auto source_failed_terminally = [&](const std::string& path) {
    if (path.empty()) {
      return true;
    }

    return allow_synchronous_texture_load ||
           (terminally_failed_paths != nullptr &&
            terminally_failed_paths->contains(path));
  };

  const bool diffuse_arrived = bgfx::isValid(gpu_mat.diffuse);
  const bool diffuse_absent =
      !diffuse_arrived && source_failed_terminally(texture_path);
  std::string environment_path;
  if (needs_environment) {
    environment_path = ResolveTexturePath(motx, mat.texture2Ofs);
  }
  const bool environment_arrived = bgfx::isValid(gpu_mat.env_map);
  const bool environment_absent =
      !environment_arrived && source_failed_terminally(environment_path);

  if (!bgfx::isValid(gpu_mat.diffuse)) {
    gpu_mat.diffuse = tex_mgr.GetWhiteTexture();
  }
  if (needs_environment && !bgfx::isValid(gpu_mat.env_map)) {

    gpu_mat.env_map = tex_mgr.GetWhiteTexture();
  }

  const bool diffuse_texture_is_opaque =
      !texture_path.empty() && tex_mgr.IsOpaque(texture_path);
  gpu_mat.effective_shader = ResolveWmoMaterialShader(
      gpu_mat.shader, gpu_mat.blend_mode, diffuse_texture_is_opaque);

  const bool diffuse_ready = diffuse_arrived || diffuse_absent;
  const bool environment_ready =
      !needs_environment || environment_arrived || environment_absent;
  const bool ready = diffuse_ready && environment_ready;
  materials_[material_index] = std::move(gpu_mat);
  material_resident_[material_index] = ready ? 1u : 0u;
  return ready;
}

void WmoRenderer::SetFogParams(const RenderFogState &fog) {
  fog_ = fog;
}

void WmoRenderer::SetSunDirection(float x, float y, float z) {
  const float len = std::sqrt(x * x + y * y + z * z);
  sun_dir_[3] = 0.0f;
  if (len > 1e-6f) {
    sun_dir_[0] = x / len;
    sun_dir_[1] = y / len;
    sun_dir_[2] = z / len;
  } else {
    sun_dir_[0] = 0.5f;
    sun_dir_[1] = 0.5f;
    sun_dir_[2] = 1.0f;
  }
}

void WmoRenderer::SetLightColors(const RenderVec3 &ambient, const RenderVec3 &diffuse) {
  std::copy(ambient.begin(), ambient.end(), light_ambient_.begin());
  std::copy(diffuse.begin(), diffuse.end(), light_diffuse_.begin());
  light_ambient_[3] = 0.0f;
  light_diffuse_[3] = 0.0f;
}

void WmoRenderer::SetLightingPalette(const WmoLightingPalette &palette) {
  lighting_palette_ = palette;
  SetLightColors(palette.outdoor_ambient, palette.outdoor_diffuse);
}

void WmoRenderer::SetNightGlowIntensity(const float intensity) {
  night_glow_intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}

bgfx::ProgramHandle WmoRenderer::SelectShaderProgram(uint32_t effective_shader) const {
  if (shader_resources_ == nullptr) {
    return BGFX_INVALID_HANDLE;
  }
  switch (effective_shader) {
  case data::wmo::kShaderOpaque:
  case data::wmo::kShaderDiffuse:
    return shader_resources_->diffuse;
  case data::wmo::kShaderSpecular:
  case data::wmo::kShaderMetal:

    return shader_resources_->specular;
  case data::wmo::kShaderEnv:
  case data::wmo::kShaderEnvMetal:
    return shader_resources_->environment;
  case data::wmo::kShaderTwoLayer:

    if (unified_render_path_) {
      return shader_resources_->two_layer;
    }
    return {bgfx::kInvalidHandle};
  default:
    return {bgfx::kInvalidHandle};
  }
}

std::string WmoRenderer::ResolveTexturePath(const std::vector<uint8_t> &motx, uint32_t offset) {
  if (motx.empty() || offset >= motx.size())
    return {};

  const char *start = reinterpret_cast<const char *>(motx.data() + offset);
  const char *end = reinterpret_cast<const char *>(motx.data() + motx.size());

  std::string path;
  for (const char *p = start; p < end && *p != '\0'; ++p) {
    path.push_back(*p);
  }

  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

const WmoSubmitTelemetry& WmoRenderer::Render(
    const uint8_t view_id, const float *view_mtx, const float *proj_mtx,
    const RenderMatrix4x4 &model_mtx,
    const world::WmoVisibilityMask *visibility_mask,
    const std::span<const world::WmoVisibleGroupPath> visible_group_paths,
    const std::uint16_t viewport_width,
    const std::uint16_t viewport_height,
    occlusion::OcclusionDepthBuffer* const occlusion,
    bgfx::Encoder* const encoder) {
  WmoSubmitTelemetry& telemetry = submit_telemetry_;
  telemetry.view_id = view_id;
  telemetry.viewport_width = viewport_width;
  telemetry.viewport_height = viewport_height;
  telemetry.group_occlusion_culled_count = 0u;
  telemetry.occluder_polygon_count = 0u;
  const bool use_visible_group_paths = visibility_mask != nullptr;
  ++submit_generation_;
  if (submit_generation_ == 0u) {
    for (WmoGroupGpu& group : groups_) {
      std::fill(group.batch_submit_generations.begin(),
                group.batch_submit_generations.end(), 0u);
    }
    submit_generation_ = 1u;
  }
  telemetry.queued_group_path_count =
      use_visible_group_paths ? visible_group_paths.size() : groups_.size();
  telemetry.submit_count = 0u;
  telemetry.records.clear();
  telemetry.records.reserve(use_visible_group_paths
                                ? visible_group_paths.size()
                                : groups_.size());
  if (!initialized_ || shader_resources_ == nullptr || groups_.empty()) {
    for (const world::WmoVisibleGroupPath& visible_path :
         visible_group_paths) {
      telemetry.records.push_back({
          .group_index = visible_path.group_index,
          .skip_reasons = kWmoSubmitSkipRendererUnavailable,
      });
    }
    return telemetry;
  }
  const auto &shaders = *shader_resources_;
  const DrawEncoder draw{encoder};

  const RenderVec4 group_color{1.0f, 1.0f, 1.0f, 1.0f};

  auto &tex_mgr = texture_manager_;
  const bgfx::TextureHandle fallback_tex = tex_mgr.GetWhiteTexture();

  const uint64_t base_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                              BGFX_STATE_WRITE_Z | kWmoDefaultDepthTestState |
                              BGFX_STATE_MSAA;
  const RenderMatrix4x4 model_view = MultiplyMatrix4x4(
      model_mtx, RenderMatrix4x4View{view_mtx, 16u});
  const RenderMatrix4x4 model_view_projection = MultiplyMatrix4x4(
      model_view, RenderMatrix4x4View{proj_mtx, 16u});

  constexpr std::uint32_t kUnallocatedTransformCache =
      std::numeric_limits<std::uint32_t>::max();
  std::uint32_t transform_cache = kUnallocatedTransformCache;
  const auto bind_placement_transform = [&]() {
    if (transform_cache == kUnallocatedTransformCache) {
      transform_cache = draw.setTransform(model_mtx.data());
      return;
    }
    draw.setTransform(transform_cache, 1u);
  };

  const bool occlusion_active = occlusion != nullptr && occlusion->active();
  RenderVec3 eye_in_model_space{};
  bool occluders_allowed = false;
  if (occlusion_active) {
    const double basis_determinant =
        static_cast<double>(model_mtx[0]) *
            (static_cast<double>(model_mtx[5]) * model_mtx[10] -
             static_cast<double>(model_mtx[6]) * model_mtx[9]) -
        static_cast<double>(model_mtx[1]) *
            (static_cast<double>(model_mtx[4]) * model_mtx[10] -
             static_cast<double>(model_mtx[6]) * model_mtx[8]) +
        static_cast<double>(model_mtx[2]) *
            (static_cast<double>(model_mtx[4]) * model_mtx[9] -
             static_cast<double>(model_mtx[5]) * model_mtx[8]);
    if (basis_determinant > 0.0) {
      const RenderVec3 eye_in_world = ExtractCameraPositionFromRetailViewMatrix(
          RenderMatrix4x4View{view_mtx, 16u});
      const RenderMatrix4x4 inverse_model =
          InvertMatrix4x4(RenderMatrix4x4View{model_mtx});
      eye_in_model_space = TransformAffinePoint4x4(
          RenderVec3View{eye_in_world}, RenderMatrix4x4View{inverse_model});
      occluders_allowed = std::isfinite(eye_in_model_space[0]) &&
                          std::isfinite(eye_in_model_space[1]) &&
                          std::isfinite(eye_in_model_space[2]);
    }
  }

  const auto lighting_colors = [&](const WmoLightingMode mode) {
    std::pair<RenderVec4, RenderVec4> colors{};
    switch (mode) {
    case WmoLightingMode::Outdoor:
      colors.first = light_ambient_;
      colors.second = light_diffuse_;
      break;
    case WmoLightingMode::Window:
      colors.first = {lighting_palette_.window_ambient[0],
                      lighting_palette_.window_ambient[1],
                      lighting_palette_.window_ambient[2], 0.0f};
      colors.second = {lighting_palette_.window_diffuse[0],
                       lighting_palette_.window_diffuse[1],
                      lighting_palette_.window_diffuse[2], 0.0f};
      break;
    case WmoLightingMode::Interior:
      colors.first = root_ambient_;
      break;
    case WmoLightingMode::Unlit:
      break;
    }
    return colors;
  };

  struct SubmitRun {
    bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
    std::uint32_t start_index{0u};
    std::uint32_t index_count{0u};
    std::uint32_t material_index{0u};
    WmoBatchMesh::Region region{WmoBatchMesh::Region::Exterior};

    WmoLightingMode lighting_mode{WmoLightingMode::Unlit};
    RenderVec4 group_color{};
    std::size_t record_index{0u};
    bool open{false};
  };
  SubmitRun run;

  const auto flush_run = [&]() {
    if (!run.open) {
      return;
    }
    const SubmitRun submitted = run;
    run.open = false;
    WmoGroupSubmitTelemetry& record_telemetry =
        telemetry.records[submitted.record_index];
    if (submitted.material_index >= materials_.size() ||
        !IsMaterialResident(submitted.material_index)) {
      ++record_telemetry.material_skipped_count;
      return;
    }
    const WmoMaterialGpu* mat = &materials_[submitted.material_index];
    ++record_telemetry.material_ready_count;

    const uint32_t effective_shader = mat->effective_shader;
    bgfx::ProgramHandle prog = SelectShaderProgram(effective_shader);
    if (!bgfx::isValid(prog)) {
      ++record_telemetry.material_skipped_count;
      return;
    }

    uint64_t state = base_state;

    if (mat && mat->two_sided) {

    } else {
      state |= kWmoOneSidedCullState;
    }

    const bool transition_batch =
        submitted.region == WmoBatchMesh::Region::Transition;
    if (transition_batch) {

      state |= BlendStateForMode(9u);
      state = (state & ~BGFX_STATE_DEPTH_TEST_MASK) |
              kWmoTransitionPassDepthTestState;
    } else if (mat) {
      state |= BlendStateForMode(mat->blend_mode);
    }

    bgfx::TextureHandle tex = fallback_tex;
    if (mat && bgfx::isValid(mat->diffuse)) {
      tex = mat->diffuse;
    }

    const auto bind_environment_sampler = [&]() {
      if (!WmoMaterialNeedsEnvironmentMap(effective_shader)) {
        return;
      }
      const bgfx::TextureHandle env_tex =
          (mat != nullptr && bgfx::isValid(mat->env_map)) ? mat->env_map
                                                         : fallback_tex;
      if (bgfx::isValid(env_tex)) {
        draw.setTexture(1, shaders.environment_sampler, env_tex);
      }
    };
    const WmoLightingMode lighting_mode = submitted.lighting_mode;
    const auto [batch_ambient, batch_diffuse] = lighting_colors(lighting_mode);
    const bool specular_shader =
        effective_shader == data::wmo::kShaderSpecular ||
        effective_shader == data::wmo::kShaderMetal;
    const RenderVec4 material_params{
        lighting_mode == WmoLightingMode::Unlit ? 1.0f : 0.0f,
        specular_shader ? -1.0f : 0.0f,
        specular_enabled_ ? 1.0f : 0.0f,
        mat && mat->unfogged ? 1.0f : 0.0f,
    };

    const RenderVec4 extra_params{
        unified_render_path_ ? (transition_batch ? 2.0f : 1.0f) : 0.0f,
        effective_shader == data::wmo::kShaderOpaque ? 1.0f : 0.0f,
        effective_shader == data::wmo::kShaderEnvMetal ? 1.0f : 0.0f,
        mat && mat->alpha_test ? 1.0f : 0.0f,
    };
    const RenderVec4 emissive = EvaluateRetailWmoMaterialEmissive(
        lighting_palette_.material_ambient_argb,
        mat != nullptr ? mat->sidn_color_bgra : 0u,
        night_glow_intensity_,
        mat != nullptr && (mat->flags & data::wmo::kMatSidnNight) != 0u);
    uint32_t sampler_flags = 0;
    if (mat) {
      if (mat->clamp_s)
        sampler_flags |= BGFX_SAMPLER_U_CLAMP;
      if (mat->clamp_t)
        sampler_flags |= BGFX_SAMPLER_V_CLAMP;
    }

    const WmoVsParamBlock vs_params =
        MakeWmoVsParamBlock(model_mtx, sun_dir_, batch_ambient, batch_diffuse,
                            emissive, material_params, extra_params);
    const WmoFsParamBlock fs_params =
        MakeWmoFsParamBlock(submitted.group_color, fog_.params, fog_.color,
                            sun_dir_, material_params, extra_params);

    bind_placement_transform();
    draw.setVertexBuffer(0, submitted.vb);
    draw.setIndexBuffer(submitted.ib, submitted.start_index,
                        submitted.index_count);
    draw.setUniform(shaders.vs_params, vs_params.data(),
                    static_cast<std::uint16_t>(wmo_vs_param::kCount));
    draw.setUniform(shaders.fs_params, fs_params.data(),
                    static_cast<std::uint16_t>(wmo_fs_param::kCount));
    draw.setTexture(0, shaders.diffuse_sampler, tex, sampler_flags);
    bind_environment_sampler();
    draw.setState(state);
    draw.submit(view_id, prog);
    ++record_telemetry.submit_count;
    ++telemetry.submit_count;

    if (transition_batch) {
      const WmoLightingMode interior_mode = unified_render_path_
                                                ? WmoLightingMode::Interior
                                                : WmoLightingMode::Unlit;
      RenderVec4 interior_material_params = material_params;
      interior_material_params[0] =
          interior_mode == WmoLightingMode::Unlit ? 1.0f : 0.0f;
      const auto [interior_ambient, interior_diffuse] =
          lighting_colors(interior_mode);

      uint64_t interior_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                kWmoTransitionPassDepthTestState |
                                BGFX_STATE_MSAA | BlendStateForMode(7u);
      if (!(mat && mat->two_sided)) {
        interior_state |= kWmoOneSidedCullState;
      }

      const WmoVsParamBlock interior_vs_params = MakeWmoVsParamBlock(
          model_mtx, sun_dir_, interior_ambient, interior_diffuse, emissive,
          interior_material_params, extra_params);
      const WmoFsParamBlock interior_fs_params = MakeWmoFsParamBlock(
          submitted.group_color, fog_.params, fog_.color, sun_dir_,
          interior_material_params, extra_params);

      bind_placement_transform();
      draw.setVertexBuffer(0, submitted.vb);
      draw.setIndexBuffer(submitted.ib, submitted.start_index,
                          submitted.index_count);
      draw.setUniform(shaders.vs_params, interior_vs_params.data(),
                      static_cast<std::uint16_t>(wmo_vs_param::kCount));
      draw.setUniform(shaders.fs_params, interior_fs_params.data(),
                      static_cast<std::uint16_t>(wmo_fs_param::kCount));
      draw.setTexture(0, shaders.diffuse_sampler, tex, sampler_flags);
      bind_environment_sampler();
      draw.setState(interior_state);
      draw.submit(view_id, prog);
      ++record_telemetry.submit_count;
      ++telemetry.submit_count;

      if (unified_render_path_) {

        RenderVec4 additive_material_params = material_params;
        additive_material_params[0] = 1.0f;

        uint64_t additive_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                  kWmoTransitionPassDepthTestState |
                                  BGFX_STATE_MSAA | BlendStateForMode(10u);
        if (!(mat && mat->two_sided)) {
          additive_state |= kWmoOneSidedCullState;
        }

        const WmoVsParamBlock additive_vs_params = MakeWmoVsParamBlock(
            model_mtx, sun_dir_, kZeroWmoParams, kZeroWmoParams,
            kZeroWmoParams, additive_material_params, extra_params);
        const WmoFsParamBlock additive_fs_params = MakeWmoFsParamBlock(
            submitted.group_color, fog_.params, fog_.color, sun_dir_,
            additive_material_params, extra_params);

        bind_placement_transform();
        draw.setVertexBuffer(0, submitted.vb);
        draw.setIndexBuffer(submitted.ib, submitted.start_index,
                            submitted.index_count);
        draw.setUniform(shaders.vs_params, additive_vs_params.data(),
                        static_cast<std::uint16_t>(wmo_vs_param::kCount));
        draw.setUniform(shaders.fs_params, additive_fs_params.data(),
                        static_cast<std::uint16_t>(wmo_fs_param::kCount));
        draw.setTexture(0, shaders.diffuse_sampler, tex, sampler_flags);
        bind_environment_sampler();
        draw.setState(additive_state);
        draw.submit(view_id, prog);
        ++record_telemetry.submit_count;
        ++telemetry.submit_count;
      }
    }
  };

  const auto render_group = [&](const std::size_t group_index,
                                [[maybe_unused]] const world::WmoPortalClipRect*
                                    clip_rect) {

    const std::size_t record_index = telemetry.records.size();
    WmoGroupSubmitTelemetry& record_telemetry =
        telemetry.records.emplace_back();
    record_telemetry.group_index = static_cast<std::uint16_t>(group_index);
    if (!IsVisibleInMask(visibility_mask, group_index)) {
      record_telemetry.skip_reasons |= kWmoSubmitSkipVisibilityMask;
      return;
    }

    auto &gpu = groups_[group_index];
    record_telemetry.gpu_ready = gpu.valid;
    record_telemetry.publication_status = gpu.publication_status;
    record_telemetry.moba_batch_count = gpu.batches.size();
    if (!gpu.valid) {
      if (gpu.publication_status !=
          world::WmoGroupPublicationStatus::kNoGeometry) {
        record_telemetry.skip_reasons |= kWmoSubmitSkipGpuNotReady;
      }
      return;
    }

    if (visibility_mask == nullptr &&
        !IsFrustumVisible(frustum_, gpu, model_mtx)) {
      record_telemetry.skip_reasons |= kWmoSubmitSkipFrustum;
      return;
    }

    if (gpu.batches.empty()) {
      record_telemetry.skip_reasons |= kWmoSubmitSkipEmptyBatch;
      return;
    }

    if (occlusion_active &&
        occlusion->IsBoundsOccluded(
            std::span<const float, 3>{gpu.bounds_min, 3u},
            std::span<const float, 3>{gpu.bounds_max, 3u},
            RenderMatrix4x4View{model_view_projection})) {
      record_telemetry.skip_reasons |= kWmoSubmitSkipOcclusion;
      ++telemetry.group_occlusion_culled_count;
      return;
    }

    const WmoMergedGeometry& merged = MergedGeometryFor(gpu);
    const bgfx::VertexBufferHandle group_vb =
        gpu.merged_resident ? merged.vb : gpu.vb;
    const bgfx::IndexBufferHandle group_ib =
        gpu.merged_resident ? merged.ib : gpu.ib;
    const std::uint32_t group_index_base =
        gpu.merged_resident ? gpu.merged_index_base : 0u;

    for (std::size_t batch_index = 0; batch_index < gpu.batches.size();
         ++batch_index) {
      const WmoBatchMesh& batch = gpu.batches[batch_index];
      if (gpu.batch_submit_generations[batch_index] == submit_generation_) {
        ++record_telemetry.batch_already_submitted_count;
        record_telemetry.skip_reasons |= kWmoSubmitSkipBatchAlreadySubmitted;
        continue;
      }

      gpu.batch_submit_generations[batch_index] = submit_generation_;
      if (batch.index_count == 0)
      {
        ++record_telemetry.material_skipped_count;
        record_telemetry.skip_reasons |= kWmoSubmitSkipEmptyBatch;
        continue;
      }

      const std::uint32_t material_flags =
          batch.material_index < materials_.size()
              ? materials_[batch.material_index].flags
              : 0u;
      const WmoLightingMode lighting_mode = ResolveRetailWmoLightingMode(
          gpu.flags, batch.region, material_flags, unified_render_path_);
      const std::uint32_t batch_start_index =
          group_index_base + batch.start_index;

      if (occluders_allowed && gpu.occluder_spans.size() == gpu.batches.size()) {
        const WmoOccluderBatchSpan& span = gpu.occluder_spans[batch_index];
        const WmoMaterialGpu* const occluder_material =
            batch.material_index < materials_.size() &&
                    IsMaterialResident(batch.material_index)
                ? &materials_[batch.material_index]
                : nullptr;
        if (span.count > 0u && occluder_material != nullptr &&
            occluder_material->blend_mode == 0u &&
            bgfx::isValid(
                SelectShaderProgram(occluder_material->effective_shader))) {
          occlusion->AddOccluders(
              std::span<const occlusion::OccluderPolygon>{
                  gpu.occluder_polygons.data() + span.first, span.count},
              RenderMatrix4x4View{model_view_projection},
              RenderVec3View{eye_in_model_space},
              occluder_material->two_sided);
          telemetry.occluder_polygon_count += span.count;
        }
      }

      if (run.open) {
        if (run.vb.idx == group_vb.idx && run.ib.idx == group_ib.idx &&
            run.material_index == batch.material_index &&
            run.region == batch.region &&
            run.lighting_mode == lighting_mode &&
            run.group_color == group_color &&
            run.start_index + run.index_count == batch_start_index) {
          run.index_count += batch.index_count;
          continue;
        }
        flush_run();
      }
      run = SubmitRun{
          .vb = group_vb,
          .ib = group_ib,
          .start_index = batch_start_index,
          .index_count = batch.index_count,
          .material_index = batch.material_index,
          .region = batch.region,
          .lighting_mode = lighting_mode,
          .group_color = group_color,
          .record_index = record_index,
          .open = true,
      };
    }
  };

  if (use_visible_group_paths) {
    for (const world::WmoVisibleGroupPath& visible_path :
         visible_group_paths) {
      const std::uint16_t group_index = visible_path.group_index;
      if (group_index < groups_.size()) {
        render_group(group_index, &visible_path.clip_rect);
      } else {
        telemetry.records.push_back({
            .group_index = group_index,
            .skip_reasons = kWmoSubmitSkipGroupOutOfRange,
        });
      }
    }
  } else {
    for (std::size_t group_index = 0; group_index < groups_.size();
         ++group_index) {
      render_group(group_index, nullptr);
    }
  }

  flush_run();
  return telemetry;
}

void WmoRenderer::RenderCollisionGeometry(uint8_t view_id, const float *view_mtx,
                                          const float *proj_mtx, const RenderMatrix4x4 &model_mtx,
                                          const world::WmoVisibilityMask *visibility_mask,
                                          bgfx::Encoder *encoder) {
  RenderDebugGeometry(view_id, view_mtx, proj_mtx, model_mtx, WmoDebugGeometryMode::Collision,
                      visibility_mask, encoder);
}

void WmoRenderer::RenderOutdoorSurfaceGeometry(uint8_t view_id, const float *view_mtx,
                                               const float *proj_mtx,
                                               const RenderMatrix4x4 &model_mtx,
                                               const world::WmoVisibilityMask *visibility_mask,
                                               bgfx::Encoder *encoder) {
  RenderDebugGeometry(view_id, view_mtx, proj_mtx, model_mtx, WmoDebugGeometryMode::OutdoorSurface,
                      visibility_mask, encoder);
}

void WmoRenderer::RenderPrimaryDebugGeometry(uint8_t view_id, const float *view_mtx,
                                             const float *proj_mtx,
                                             const RenderMatrix4x4 &model_mtx,
                                             const world::WmoVisibilityMask *visibility_mask,
                                             bgfx::Encoder *encoder) {
  RenderDebugGeometry(view_id, view_mtx, proj_mtx, model_mtx, WmoDebugGeometryMode::Primary,
                      visibility_mask, encoder);
}

void WmoRenderer::RenderDebugGeometry(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                                      const RenderMatrix4x4 &model_mtx, WmoDebugGeometryMode mode,
                                      const world::WmoVisibilityMask *visibility_mask,
                                      bgfx::Encoder *const encoder) {
  if (!initialized_ || shader_resources_ == nullptr || groups_.empty())
    return;
  const auto &shaders = *shader_resources_;
  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  auto &tex_mgr = texture_manager_;
  const bgfx::TextureHandle fallback_tex = tex_mgr.GetWhiteTexture();
  const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                         BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | BGFX_STATE_CULL_CW;

  const WmoVsParamBlock vs_params =
      MakeWmoVsParamBlock(model_mtx, sun_dir_, light_ambient_, light_diffuse_,
                          kZeroWmoParams, ResolveDebugMaterialParams(mode),
                          kZeroWmoParams);
  const WmoFsParamBlock fs_params = MakeWmoFsParamBlock(
      ResolveDebugTint(mode), kDisabledFogParams, fog_.color, sun_dir_,
      ResolveDebugMaterialParams(mode), kZeroWmoParams);

  for (std::size_t group_index = 0; group_index < groups_.size(); ++group_index) {
    if (!IsVisibleInMask(visibility_mask, group_index)) {
      continue;
    }

    const auto &gpu = groups_[group_index];
    bgfx::IndexBufferHandle debug_ib = BGFX_INVALID_HANDLE;
    switch (mode) {
    case WmoDebugGeometryMode::Collision:
      if (gpu.collision_valid) {
        debug_ib = gpu.collision_ib;
      }
      break;
    case WmoDebugGeometryMode::OutdoorSurface:
      if (gpu.outdoor_surface_valid) {
        debug_ib = gpu.outdoor_surface_ib;
      }
      break;
    case WmoDebugGeometryMode::Primary:
      if (gpu.primary_debug_valid) {
        debug_ib = gpu.primary_debug_ib;
      }
      break;
    case WmoDebugGeometryMode::TwoPass:

      break;
    }

    if (!gpu.valid || !bgfx::isValid(debug_ib))
      continue;

    if (!IsFrustumVisible(frustum_, gpu, model_mtx)) {
      continue;
    }

    draw.setTransform(model_mtx.data());

    BindGroupVertexStream(gpu, encoder);
    draw.setIndexBuffer(debug_ib);
    draw.setUniform(shaders.vs_params, vs_params.data(),
                    static_cast<std::uint16_t>(wmo_vs_param::kCount));
    draw.setUniform(shaders.fs_params, fs_params.data(),
                    static_cast<std::uint16_t>(wmo_fs_param::kCount));
    if (bgfx::isValid(fallback_tex)) {
      draw.setTexture(0, shaders.diffuse_sampler, fallback_tex);
    }
    draw.setState(state);
    draw.submit(view_id, shaders.diffuse);
  }
}

void WmoRenderer::RenderTwoSidedBatch(uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                                      const RenderMatrix4x4 &model_mtx,
                                      const world::WmoVisibilityMask *visibility_mask,
                                      bgfx::Encoder *const encoder) {
  if (!initialized_ || shader_resources_ == nullptr || groups_.empty())
    return;
  const auto &shaders = *shader_resources_;
  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  auto &tex_mgr = texture_manager_;
  const bgfx::TextureHandle fallback_tex = tex_mgr.GetWhiteTexture();

  constexpr RenderVec4 kPass1Tint{0xCC / 255.0f, 0xCC / 255.0f, 0xCC / 255.0f, 0x80 / 255.0f};
  constexpr RenderVec4 kPass2Tint{0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 0x80 / 255.0f};

  const uint64_t state_pass1 = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                               BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                               BGFX_STATE_CULL_CW |
                               BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                     BGFX_STATE_BLEND_INV_SRC_ALPHA);

  const uint64_t state_pass2 = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                               BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                               BGFX_STATE_CULL_CW |
                               BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                                     BGFX_STATE_BLEND_INV_SRC_ALPHA);

  struct PassDesc {
    std::reference_wrapper<const RenderVec4> tint;
    std::reference_wrapper<const RenderVec4> fog;
    uint64_t state;
  };
  const PassDesc passes[2] = {
      {kPass1Tint, kDisabledFogParams, state_pass1},
      {kPass2Tint, fog_.params, state_pass2},
  };

  const WmoVsParamBlock vs_params = MakeWmoVsParamBlock(
      model_mtx, sun_dir_, light_ambient_, light_diffuse_, kZeroWmoParams,
      kDebugMaterialParams, kZeroWmoParams);

  for (int pass = 0; pass < 2; ++pass) {
    const WmoFsParamBlock fs_params = MakeWmoFsParamBlock(
        passes[pass].tint.get(), passes[pass].fog.get(), fog_.color, sun_dir_,
        kDebugMaterialParams, kZeroWmoParams);
    for (std::size_t gi = 0; gi < groups_.size(); ++gi) {
      if (!IsVisibleInMask(visibility_mask, gi))
        continue;

      const auto &gpu = groups_[gi];
      if (!gpu.valid || !gpu.renderable_valid)
        continue;
      if (!IsFrustumVisible(frustum_, gpu, model_mtx))
        continue;

      draw.setTransform(model_mtx.data());

      BindGroupVertexStream(gpu, encoder);
      draw.setIndexBuffer(gpu.renderable_ib);
      draw.setUniform(shaders.vs_params, vs_params.data(),
                      static_cast<std::uint16_t>(wmo_vs_param::kCount));
      draw.setUniform(shaders.fs_params, fs_params.data(),
                      static_cast<std::uint16_t>(wmo_fs_param::kCount));
      if (bgfx::isValid(fallback_tex)) {
        draw.setTexture(0, shaders.diffuse_sampler, fallback_tex);
      }
      draw.setState(passes[pass].state);
      draw.submit(view_id, shaders.diffuse);
    }
  }
}

void WmoRenderer::Clear() {
  for (auto &gpu : groups_) {
    DestroyGroupGpu(gpu);
  }
  ReleaseMergedGeometry();
  groups_.clear();
  submit_generation_ = 0u;

  materials_.clear();
  material_resident_.clear();
}

void WmoRenderer::Shutdown() {
  if (!initialized_)
    return;
  Clear();
  ReleaseSharedWmoShaderResources(shader_resources_);
  shader_resources_ = nullptr;
  initialized_ = false;
}

void WmoRenderer::DestroyGroupGpu(WmoGroupGpu &gpu) {
  if (bgfx::isValid(gpu.vb))
    bgfx::destroy(gpu.vb);
  if (bgfx::isValid(gpu.ib))
    bgfx::destroy(gpu.ib);
  if (bgfx::isValid(gpu.collision_ib))
    bgfx::destroy(gpu.collision_ib);
  if (bgfx::isValid(gpu.outdoor_surface_ib))
    bgfx::destroy(gpu.outdoor_surface_ib);
  if (bgfx::isValid(gpu.primary_debug_ib))
    bgfx::destroy(gpu.primary_debug_ib);
  if (bgfx::isValid(gpu.renderable_ib))
    bgfx::destroy(gpu.renderable_ib);
  gpu.vb = BGFX_INVALID_HANDLE;
  gpu.ib = BGFX_INVALID_HANDLE;
  gpu.collision_ib = BGFX_INVALID_HANDLE;
  gpu.outdoor_surface_ib = BGFX_INVALID_HANDLE;
  gpu.primary_debug_ib = BGFX_INVALID_HANDLE;
  gpu.renderable_ib = BGFX_INVALID_HANDLE;
  gpu.batches.clear();
  gpu.batch_submit_generations.clear();
  gpu.staged_vertices.clear();
  gpu.staged_vertices.shrink_to_fit();
  gpu.staged_composite_vertices.clear();
  gpu.staged_composite_vertices.shrink_to_fit();
  gpu.staged_indices.clear();
  gpu.staged_indices.shrink_to_fit();

  gpu.merged_resident = false;
  gpu.merged_vertex_base = 0u;
  gpu.merged_index_base = 0u;
  gpu.vertex_count = 0u;
  gpu.composite_vertices = false;
  gpu.collision_valid = false;
  gpu.outdoor_surface_valid = false;
  gpu.primary_debug_valid = false;
  gpu.renderable_valid = false;
  gpu.valid = false;
}

}

#include "openwow/render/m2/m2_skinned_mesh.h"

#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/m2/m2_shaders.h"
#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <mutex>
#include <vector>

namespace openwow::render::m2 {
namespace {

std::uint64_t NextM2CapturedResourceId() noexcept {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

}

bgfx::VertexLayout M2SkinnedMesh::SkinnedVertex::layout;

void M2SkinnedMesh::SkinnedVertex::InitLayout() {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)

        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Uint8, true)
        .end();
  });
}

M2SkinnedMesh::~M2SkinnedMesh() {
  Shutdown();
}

bool M2SkinnedMesh::Init(const openwow::data::model::M2Model &model,
                         const openwow::data::model::M2Skin &skin,
                         const M2ShaderHandles &shaders) {
  M2SkinGeometry geometry;
  if (!BuildM2SkinGeometry(model, skin, &geometry)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "M2SkinnedMesh: " + geometry.error);
    return false;
  }
  return InitSkinnedFromGeometry(geometry, shaders);
}

bool M2SkinnedMesh::InitSkinned(const openwow::data::model::M2Model &model,
                                const openwow::data::model::M2Skin &skin,
                                const M2ShaderHandles &shaders) {
  return Init(model, skin, shaders);
}

bool M2SkinnedMesh::InitSkinnedFromGeometry(
    const M2SkinGeometry &geometry, const M2ShaderHandles &shaders) {
  Shutdown();
  shaders_ = &shaders;
  SkinnedVertex::InitLayout();

  if (geometry.vertices.empty() || geometry.indices.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "M2SkinnedMesh::InitSkinned: missing prepared vertices/indices");
    return false;
  }

  const auto* const smallest_palette_shader =
      FindM2BonePaletteShader(shaders, 1u);
  if (smallest_palette_shader == nullptr ||
      !bgfx::isValid(smallest_palette_shader->program)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "M2SkinnedMesh::InitSkinned: skinned shader not "
                       "available; GPU skinning required");
    return false;
  }

  auto vertices = PrepareSkinnedVertices(geometry.vertices);
  if (vertices.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(SkinnedVertex) ||
      geometry.indices.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint16_t)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "M2SkinnedMesh::InitSkinned: prepared buffer exceeds backend limits");
    return false;
  }

  const bgfx::Memory *vertex_memory = bgfx::copy(
      vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(SkinnedVertex)));
  skinned_vb_ = bgfx::createVertexBuffer(vertex_memory, SkinnedVertex::layout);
  if (!bgfx::isValid(skinned_vb_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "M2SkinnedMesh::InitSkinned: createVertexBuffer failed");
    Shutdown();
    return false;
  }

  const M2VertexCacheOptimizedIndices &optimized = geometry.vertex_cache_optimized;
  const bool upload_optimized_region =
      !optimized.indices.empty() &&
      optimized.indices.size() <=
          std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint16_t) -
              geometry.indices.size();
  std::vector<std::uint16_t> index_upload;
  index_upload.reserve(geometry.indices.size() +
                       (upload_optimized_region ? optimized.indices.size() : 0u));
  index_upload.assign(geometry.indices.begin(), geometry.indices.end());
  if (upload_optimized_region) {
    index_upload.insert(index_upload.end(), optimized.indices.begin(),
                        optimized.indices.end());
  }

  const bgfx::Memory *index_memory = bgfx::copy(
      index_upload.data(),
      static_cast<std::uint32_t>(index_upload.size() * sizeof(std::uint16_t)));
  ib_ = bgfx::createIndexBuffer(index_memory);
  if (!bgfx::isValid(ib_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "M2SkinnedMesh::InitSkinned: createIndexBuffer failed");
    Shutdown();
    return false;
  }

  index_count_ = static_cast<std::uint32_t>(geometry.indices.size());
  submesh_bone_index_bound_ = geometry.submesh_bone_index_bound;
  if (upload_optimized_region) {
    vertex_cache_optimized_base_ = index_count_;
    vertex_cache_optimized_spans_ = optimized.spans;
  }
  vertex_capture_id_ = NextM2CapturedResourceId();
  index_capture_id_ = NextM2CapturedResourceId();
  skinned_ok_ = true;
  return true;
}

std::vector<M2SkinnedMesh::SkinnedVertex>
M2SkinnedMesh::PrepareSkinnedVertices(
    const std::span<const openwow::data::model::M2Vertex> source_vertices) {
  std::vector<SkinnedVertex> vertices;
  vertices.reserve(source_vertices.size());
  for (const auto &source : source_vertices) {
    SkinnedVertex vertex{};
    vertex.x = source.position[0];
    vertex.y = source.position[1];
    vertex.z = source.position[2];
    vertex.nx = source.normal[0];
    vertex.ny = source.normal[1];
    vertex.nz = source.normal[2];
    vertex.u0 = source.texcoord0[0];
    vertex.v0 = source.texcoord0[1];
    vertex.u1 = source.texcoord1[0];
    vertex.v1 = source.texcoord1[1];
    vertex.bone_indices = {
        source.bone_indices[0],
        source.bone_indices[1],
        source.bone_indices[2],
        source.bone_indices[3],
    };
    vertex.bone_weights = {source.bone_weights[0], source.bone_weights[1],
                           source.bone_weights[2], source.bone_weights[3]};
    vertices.push_back(vertex);
  }
  return vertices;
}

void M2SkinnedMesh::Shutdown() {
  if (bgfx::isValid(skinned_vb_)) {
    bgfx::destroy(skinned_vb_);
  }
  if (bgfx::isValid(ib_)) {
    bgfx::destroy(ib_);
  }
  skinned_vb_ = BGFX_INVALID_HANDLE;
  ib_ = BGFX_INVALID_HANDLE;
  index_count_ = 0;
  vertex_cache_optimized_base_ = 0;
  vertex_cache_optimized_spans_.clear();
  submesh_bone_index_bound_.clear();
  vertex_capture_id_ = 0;
  index_capture_id_ = 0;
  skinned_ok_ = false;
  shaders_ = nullptr;
}

bool M2SkinnedMesh::ValidateBatchSubmitInputs(
    const bgfx::TextureHandle texture0, const bgfx::TextureHandle texture1,
    const std::uint32_t first_index, const std::uint32_t index_count,
    const M2BatchUniforms &uniforms, const std::uint32_t bone_matrix_count) const {
  const bool requires_texture0 = uniforms.combiner_mode[2] > 0.5f;
  const bool requires_texture1 = uniforms.combiner_mode[2] > 1.5f;
  if (!skinned_ok_ || !bgfx::isValid(skinned_vb_) || !bgfx::isValid(ib_) ||
      (requires_texture0 && !bgfx::isValid(texture0)) ||
      (requires_texture1 && !bgfx::isValid(texture1)) || index_count == 0 ||
      bone_matrix_count == 0u) {
    return false;
  }
  if (first_index >= index_count_) {
    return false;
  }
  return shaders_ != nullptr;
}

std::uint32_t M2SkinnedMesh::SubmeshBoneIndexBound(
    const std::size_t submesh_index) const noexcept {
  if (submesh_index >= submesh_bone_index_bound_.size()) {
    return 0u;
  }
  return submesh_bone_index_bound_[submesh_index];
}

void M2SkinnedMesh::UploadBonePalette(
    const M2DrawEncoder &draw, const M2BonePaletteShader &palette_shader,
    const std::span<const float> bone_matrices,
    const std::uint32_t bone_index_bound) const {
  const std::uint32_t bone_matrix_count =
      static_cast<std::uint32_t>(bone_matrices.size() / 16u);
  std::array<float, kMaxBoneMatrices * 3u * 4u> uploaded_palette;
  const std::uint32_t palette_capacity = palette_shader.capacity;

  const std::uint32_t uploaded_matrix_count =
      bone_index_bound == 0u ? palette_capacity
                             : std::min(bone_index_bound, palette_capacity);

  const std::uint32_t written_matrix_count =
      std::min(bone_matrix_count, uploaded_matrix_count);
  for (std::uint32_t matrix = written_matrix_count;
       matrix < uploaded_matrix_count; ++matrix) {
    const std::size_t offset = static_cast<std::size_t>(matrix) * 12u;
    std::fill_n(uploaded_palette.begin() + static_cast<std::ptrdiff_t>(offset), 12u,
                0.0f);
    uploaded_palette[offset + 0u] = 1.0f;
    uploaded_palette[offset + 5u] = 1.0f;
    uploaded_palette[offset + 10u] = 1.0f;
  }
  for (std::uint32_t matrix = 0; matrix < written_matrix_count; ++matrix) {
    const float* const source = bone_matrices.data() + static_cast<std::size_t>(matrix) * 16u;
    float* const destination = uploaded_palette.data() + static_cast<std::size_t>(matrix) * 12u;
    destination[0] = source[0];
    destination[1] = source[4];
    destination[2] = source[8];
    destination[3] = source[12];
    destination[4] = source[1];
    destination[5] = source[5];
    destination[6] = source[9];
    destination[7] = source[13];
    destination[8] = source[2];
    destination[9] = source[6];
    destination[10] = source[10];
    destination[11] = source[14];
  }
  draw.setUniform(palette_shader.bone_columns, uploaded_palette.data(),
                  static_cast<std::uint16_t>(uploaded_matrix_count * 3u));
}

void M2SkinnedMesh::UploadPackedBatchUniforms(
    const M2DrawEncoder &draw, const M2BatchUniforms &uniforms,
    const bool upload_lighting_uniforms) const {
  const M2ShaderHandles& shader = *shaders_;

  std::array<RenderVec4, kM2VertexParamCount> vertex_params;
  vertex_params[kM2VertexParamUvTransform0Row0] = uniforms.uv_transform0;
  vertex_params[kM2VertexParamUvTransform0Row1] = uniforms.uv_transform0_row1;
  vertex_params[kM2VertexParamUvTransform1Row0] = uniforms.uv_transform1;
  vertex_params[kM2VertexParamUvTransform1Row1] = uniforms.uv_transform1_row1;
  vertex_params[kM2VertexParamTexGenFlags] = uniforms.tex_gen_flags;
  vertex_params[kM2VertexParamMaterialColor] = uniforms.material_color;
  vertex_params[kM2VertexParamMaterialFlags] = uniforms.material_flags;
  vertex_params[kM2VertexParamEmissiveColor] = uniforms.emissive_color;
  vertex_params[kM2VertexParamLightCount] = uniforms.light_count;
  vertex_params[kM2VertexParamLightAmbient] = uniforms.light_ambient;
  draw.setUniform(shader.u_vertex_params, vertex_params.data(),
                  kM2VertexParamCount);

  if (upload_lighting_uniforms) {
    std::array<RenderVec4, kM2LightUniformCount> lights;
    for (std::size_t light = 0; light < M2BatchUniforms::kMaxM2Lights; ++light) {
      const std::size_t base = light * kM2LightSlotStride;
      lights[base + kM2LightOffsetPosRange] = uniforms.light_pos_range[light];
      lights[base + kM2LightOffsetAttenuation] = uniforms.light_attenuation[light];
      lights[base + kM2LightOffsetColor] = uniforms.light_color[light];
    }
    draw.setUniform(shader.u_lights, lights.data(), kM2LightUniformCount);
  }

  std::array<RenderVec4, kM2FragmentParamCount> fragment_params;
  fragment_params[kM2FragmentParamCombinerMode] = uniforms.combiner_mode;
  fragment_params[kM2FragmentParamAlphaRef] = uniforms.alpha_ref;
  fragment_params[kM2FragmentParamMaterialFlags] = uniforms.material_flags;
  fragment_params[kM2FragmentParamFogParams] = uniforms.fog_params;
  fragment_params[kM2FragmentParamFogColor] = uniforms.fog_color;
  draw.setUniform(shader.u_fragment_params, fragment_params.data(),
                  kM2FragmentParamCount);
}

M2ResultStatus M2SkinnedMesh::SubmitSkinnedBatch(
    int view_id, bgfx::TextureHandle texture0, bgfx::TextureHandle texture1,
    const RenderMatrix4x4 &model_mtx, std::uint32_t first_index, std::uint32_t index_count,
    std::uint64_t state, const M2BatchUniforms &uniforms, std::span<const float> bone_matrices,
    std::uint64_t sampler_flags0, std::uint64_t sampler_flags1,
    const M2DrawEncoder draw, std::uint32_t *transform_cache_index,
    const std::size_t submesh_index) const {
  const std::uint32_t bone_matrix_count =
      static_cast<std::uint32_t>(bone_matrices.size() / 16u);
  if (!ValidateBatchSubmitInputs(texture0, texture1, first_index, index_count,
                                 uniforms, bone_matrix_count)) {
    return M2ResultStatus::kNotReady;
  }
  const M2ShaderHandles& shader = *shaders_;
  const auto* const palette_shader =
      FindM2BonePaletteShader(shader, bone_matrix_count);
  if (palette_shader == nullptr ||
      !bgfx::isValid(palette_shader->program) ||
      !bgfx::isValid(palette_shader->bone_columns) ||
      bone_matrices.size() % 16u != 0u) {
    return M2ResultStatus::kNotReady;
  }
  index_count = std::min(index_count, index_count_ - first_index);

  const M2ProgramSelection selection = SelectM2Program(*palette_shader, uniforms);

  if (transform_cache_index == nullptr) {
    draw.setTransform(model_mtx.data());
  } else if (*transform_cache_index == kNoMatrixCacheIndex) {
    *transform_cache_index = draw.setTransformCached(model_mtx.data());
  } else {
    draw.setTransform(*transform_cache_index);
  }
  draw.setVertexBuffer(0, skinned_vb_);

  draw.setIndexBuffer(ib_,
                      ResolveM2DrawFirstIndex(state, first_index, index_count,
                                              vertex_cache_optimized_base_,
                                              vertex_cache_optimized_spans_),
                      index_count);

  UploadBonePalette(draw, *palette_shader, bone_matrices,
                    SubmeshBoneIndexBound(submesh_index));

  if (selection.reads_lighting_uniforms) {
    draw.setUniform(shader.u_world_matrix, model_mtx.data());
  }

  UploadPackedBatchUniforms(draw, uniforms, selection.reads_lighting_uniforms);

  if (uniforms.combiner_mode[2] > 0.5f) {
    draw.setTexture(0, shader.s_tex0, texture0, sampler_flags0);
  }
  if (uniforms.combiner_mode[2] > 1.5f) {
    draw.setTexture(1, shader.s_tex1, texture1, sampler_flags1);
  }

  draw.setState(state);
  draw.submit(static_cast<std::uint16_t>(view_id), selection.program);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2SkinnedMesh::SubmitInstancedBatch(
    int view_id, bgfx::TextureHandle texture0, bgfx::TextureHandle texture1,
    const bgfx::InstanceDataBuffer &instance_buffer, std::uint32_t instance_count,
    std::uint32_t first_index, std::uint32_t index_count, std::uint64_t state,
    const M2BatchUniforms &uniforms, std::span<const float> bone_matrices,
    std::uint64_t sampler_flags0, std::uint64_t sampler_flags1,
    const M2DrawEncoder draw, const std::size_t submesh_index) const {
  const std::uint32_t bone_matrix_count =
      static_cast<std::uint32_t>(bone_matrices.size() / 16u);
  if (instance_count == 0u ||
      !ValidateBatchSubmitInputs(texture0, texture1, first_index, index_count,
                                 uniforms, bone_matrix_count)) {
    return M2ResultStatus::kNotReady;
  }
  const M2ShaderHandles& shader = *shaders_;
  const M2BonePaletteShader& palette_shader = shader.instanced_shader;
  if (!bgfx::isValid(palette_shader.program) ||
      !bgfx::isValid(palette_shader.bone_columns) ||
      bone_matrices.size() % 16u != 0u ||
      bone_matrix_count > palette_shader.capacity) {
    return M2ResultStatus::kNotReady;
  }
  index_count = std::min(index_count, index_count_ - first_index);

  const M2ProgramSelection selection = SelectM2Program(palette_shader, uniforms);

  draw.setVertexBuffer(0, skinned_vb_);

  draw.setIndexBuffer(ib_,
                      ResolveM2DrawFirstIndex(state, first_index, index_count,
                                              vertex_cache_optimized_base_,
                                              vertex_cache_optimized_spans_),
                      index_count);
  draw.setInstanceDataBuffer(&instance_buffer, 0u, instance_count);

  UploadBonePalette(draw, palette_shader, bone_matrices,
                    SubmeshBoneIndexBound(submesh_index));
  UploadPackedBatchUniforms(draw, uniforms, selection.reads_lighting_uniforms);

  if (uniforms.combiner_mode[2] > 0.5f) {
    draw.setTexture(0, shader.s_tex0, texture0, sampler_flags0);
  }
  if (uniforms.combiner_mode[2] > 1.5f) {
    draw.setTexture(1, shader.s_tex1, texture1, sampler_flags1);
  }

  draw.setState(state);
  draw.submit(static_cast<std::uint16_t>(view_id), selection.program);
  return M2ResultStatus::kReady;
}

}

#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/m2/m2_index_order.h"
#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/render/m2/m2_shaders.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/resources/render_resource_identity.h"
#include "openwow/render/m2/m2_draw_encoder.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace openwow::render::m2 {

inline constexpr std::size_t kM2UnknownSubmeshIndex =
    std::numeric_limits<std::size_t>::max();

class M2SkinnedMesh {
public:
  M2SkinnedMesh() = default;
  ~M2SkinnedMesh();
  M2SkinnedMesh(const M2SkinnedMesh &) = delete;
  M2SkinnedMesh &operator=(const M2SkinnedMesh &) = delete;

  bool Init(const openwow::data::model::M2Model &model,
            const openwow::data::model::M2Skin &skin,
            const M2ShaderHandles &shaders);
  void Shutdown();

  [[nodiscard]] M2ResultStatus SubmitSkinnedBatch(
      int view_id, bgfx::TextureHandle texture0, bgfx::TextureHandle texture1,
      const RenderMatrix4x4 &model_mtx, std::uint32_t first_index, std::uint32_t index_count,
      std::uint64_t state, const M2BatchUniforms &uniforms,
      std::span<const float> bone_matrices,
      std::uint64_t sampler_flags0 = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      std::uint64_t sampler_flags1 = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      M2DrawEncoder draw = {},

      std::uint32_t *transform_cache_index = nullptr,

      std::size_t submesh_index = kM2UnknownSubmeshIndex) const;

  [[nodiscard]] M2ResultStatus SubmitInstancedBatch(
      int view_id, bgfx::TextureHandle texture0, bgfx::TextureHandle texture1,
      const bgfx::InstanceDataBuffer &instance_buffer, std::uint32_t instance_count,
      std::uint32_t first_index, std::uint32_t index_count,
      std::uint64_t state, const M2BatchUniforms &uniforms,
      std::span<const float> bone_matrices,
      std::uint64_t sampler_flags0 = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      std::uint64_t sampler_flags1 = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      M2DrawEncoder draw = {},

      std::size_t submesh_index = kM2UnknownSubmeshIndex) const;

  bool InitSkinned(const openwow::data::model::M2Model &model,
                   const openwow::data::model::M2Skin &skin,
                   const M2ShaderHandles &shaders);
  bool InitSkinnedFromGeometry(const M2SkinGeometry &geometry,
                               const M2ShaderHandles &shaders);

  bool skinned_ok() const {
    return skinned_ok_;
  }

  [[nodiscard]] std::optional<RenderResourceKey>
  CapturedVertexBufferKey() const noexcept {
    return bgfx::isValid(skinned_vb_) && vertex_capture_id_ != 0
               ? std::optional<RenderResourceKey>{RenderResourceKey{
                     RenderResourceKind::VertexBuffer,
                     vertex_capture_id_}}
               : std::nullopt;
  }
  [[nodiscard]] std::optional<RenderResourceKey>
  CapturedIndexBufferKey() const noexcept {
    return bgfx::isValid(ib_) && index_capture_id_ != 0
               ? std::optional<RenderResourceKey>{RenderResourceKey{
                     RenderResourceKind::IndexBuffer,
                     index_capture_id_}}
               : std::nullopt;
  }

  struct SkinnedVertex {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float nx{0.0F};
    float ny{0.0F};
    float nz{0.0F};
    float u0{0.0F};
    float v0{0.0F};
    float u1{0.0F};
    float v1{0.0F};
    std::uint32_t color{0xFFFFFFFF};
    std::array<std::uint8_t, 4> bone_indices{0, 0, 0, 0};
    std::array<std::uint8_t, 4> bone_weights{0, 0, 0, 0};

    static bgfx::VertexLayout layout;
    static void InitLayout();
  };

  [[nodiscard]] static std::vector<SkinnedVertex> PrepareSkinnedVertices(
      std::span<const openwow::data::model::M2Vertex> vertices);

  [[nodiscard]] static constexpr RenderResourceKey
  CapturedVertexFormatKey() noexcept {
    return {RenderResourceKind::VertexFormat, 0x4D32534B494E0001ULL};
  }

  static constexpr std::uint32_t kMaxBoneMatrices = kM2MaxGpuBonePaletteMatrices;

private:

  void UploadBonePalette(const M2DrawEncoder &draw,
                         const M2BonePaletteShader &palette_shader,
                         std::span<const float> bone_matrices,
                         std::uint32_t bone_index_bound) const;

  [[nodiscard]] std::uint32_t SubmeshBoneIndexBound(
      std::size_t submesh_index) const noexcept;

  void UploadPackedBatchUniforms(const M2DrawEncoder &draw,
                                 const M2BatchUniforms &uniforms,
                                 bool upload_lighting_uniforms) const;
  [[nodiscard]] bool ValidateBatchSubmitInputs(
      bgfx::TextureHandle texture0, bgfx::TextureHandle texture1,
      std::uint32_t first_index, std::uint32_t index_count,
      const M2BatchUniforms &uniforms,
      std::uint32_t bone_matrix_count) const;

  const M2ShaderHandles *shaders_{nullptr};
  bool skinned_ok_{false};
  bgfx::VertexBufferHandle skinned_vb_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle ib_ = BGFX_INVALID_HANDLE;

  std::uint32_t index_count_{0};

  std::uint32_t vertex_cache_optimized_base_{0};
  std::vector<M2IndexSpan> vertex_cache_optimized_spans_;

  std::vector<std::uint16_t> submesh_bone_index_bound_;
  std::uint64_t vertex_capture_id_{0};
  std::uint64_t index_capture_id_{0};
};

}

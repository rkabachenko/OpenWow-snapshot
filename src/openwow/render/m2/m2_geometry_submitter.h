#pragma once

#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/m2/m2_draw_encoder.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <optional>
#include <span>

namespace openwow::render::m2 {

class M2Animator;
class M2GpuResources;

class M2GeometrySubmitter {
 public:
  explicit M2GeometrySubmitter(M2GpuResources& gpu_resources) noexcept;

  [[nodiscard]] M2ShadowClassQuery QueryShadowClass(
      std::uint32_t instance_id, const detail::M2Instance& instance,
      const detail::M2ModelResource& resource) const;
  [[nodiscard]] M2ShadowDrawCallLists BuildShadowDrawCalls(
      std::uint32_t instance_id, const detail::M2Instance& instance,
      const detail::M2ModelResource& resource) const;

  [[nodiscard]] M2RenderInstanceResult SubmitInstanced(
      std::uint16_t view_id, M2SkinnedMesh* skinned,
      const std::vector<bgfx::TextureHandle>& textures,
      const data::model::M2Model& model, const data::model::M2Skin& skin,
      const std::vector<detail::M2ModelResource::RenderBatch>& render_batches,
      std::span<const M2InstancedDrawRecord> instance_records,
      const M2BatchUniforms* base_uniforms, const M2Animator* animator,
      int animation_index, std::uint32_t animation_time_ms,
      std::span<const float> bone_matrices, std::string_view model_path,
      M2DrawEncoder draw = {}) const;

  [[nodiscard]] M2RenderInstanceResult Submit(
      std::uint16_t view_id, M2SkinnedMesh* skinned,
      const std::vector<bgfx::TextureHandle>& textures,
      const data::model::M2Model& model, const data::model::M2Skin& skin,
      const std::vector<detail::M2ModelResource::RenderBatch>& render_batches,
      const RenderMatrix4x4& model_matrix, const RenderVec4& tint_color,
      float alpha, const std::vector<std::size_t>* visible_submesh_indices,
      const M2BatchUniforms* base_uniforms, M2RenderPassScope pass_scope,
      M2DrawBatchScope batch_scope, const M2Animator* animator,
      int animation_index, std::uint32_t animation_time_ms,
      std::span<const float> bone_matrices,
      std::optional<RenderMatrix4x4View> view_matrix,
      const std::optional<RenderSubmitTraceBinding>& submit_trace,
      std::string_view model_path, std::uint32_t skin_profile,
      std::uint32_t instance_id, M2DrawEncoder draw = {}) const;

 private:
  M2GpuResources& gpu_resources_;
};

}

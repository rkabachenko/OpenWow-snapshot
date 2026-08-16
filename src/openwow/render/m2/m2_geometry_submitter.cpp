#include "openwow/render/m2/m2_geometry_submitter.h"

#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_gpu_resources.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/render/m2/m2_spatial_queries.h"
#include "openwow/render/m2/m2_submit_trace.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace openwow::render::m2 {
namespace {

constexpr float kRenderableOpacityThreshold = 0.0001f;

[[nodiscard]] std::uint64_t SamplerFlagsForTexture(
    const data::model::M2Model& model, const std::size_t texture_index) {
  if (texture_index >= model.textures.size()) {
    return BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  }
  const auto& texture = model.textures[texture_index];
  std::uint64_t flags = 0;
  if ((texture.flags & 0x1u) == 0u) {
    flags |= BGFX_SAMPLER_U_CLAMP;
  }
  if ((texture.flags & 0x2u) == 0u) {
    flags |= BGFX_SAMPLER_V_CLAMP;
  }
  return flags;
}

[[nodiscard]] bool HasUniformLighting(const M2BatchUniforms* uniforms) noexcept {
  if (uniforms == nullptr) {
    return false;
  }
  return uniforms->light_count[0] > 0.0f || uniforms->light_ambient[0] != 0.0f ||
         uniforms->light_ambient[1] != 0.0f || uniforms->light_ambient[2] != 0.0f;
}

struct M2BatchDrawSetup {
  std::uint64_t state = 0;
  M2BatchUniforms uniforms;
};

constexpr M2BatchUniforms kDefaultM2BatchUniforms{};

void SeedM2BatchDrawSetup(const M2BatchUniforms* const base_uniforms,
                          M2BatchDrawSetup* const setup) {
  setup->uniforms =
      base_uniforms != nullptr ? *base_uniforms : kDefaultM2BatchUniforms;
}

[[nodiscard]] bool PrepareM2BatchDrawSetup(
    const detail::M2ModelResource::RenderBatch& batch,
    const M2TextureUnitMaterialSample& material_sample, const bool is_opaque,
    const bool has_active_lighting, const M2BatchUniforms* base_uniforms,
    const M2Animator* animator, const int animation_index,
    const std::uint32_t animation_time_ms, M2BatchDrawSetup* const out_setup,
    std::string* failure_detail) {
  const bool unlit = (batch.material_flags & kM2MaterialFlagUnlit) != 0u;
  const bool unfogged = (batch.material_flags & kM2MaterialFlagUnfogged) != 0u;
  const M2BlendMode state_blend_mode =
      !is_opaque && (batch.blend_mode == M2BlendMode::Opaque ||
                     batch.blend_mode == M2BlendMode::AlphaKey)
          ? M2BlendMode::Alpha
          : batch.blend_mode;
  const auto render_state = ResolveM2RenderState(state_blend_mode, batch.material_flags);
  if (!render_state.has_value()) {
    if (failure_detail != nullptr) {
      *failure_detail =
          "blend=" + std::to_string(static_cast<std::uint32_t>(batch.blend_mode));
    }
    return false;
  }
  const M2BatchUniforms& base =
      base_uniforms != nullptr ? *base_uniforms : kDefaultM2BatchUniforms;
  M2BatchDrawSetup& setup = *out_setup;
  setup.state = *render_state | BGFX_STATE_MSAA;
  setup.uniforms.combiner_mode = {static_cast<float>(batch.shader_op1),
                                  static_cast<float>(batch.shader_op2),
                                  static_cast<float>(batch.texture_count),
                                  static_cast<float>(batch.shader_variant)};
  setup.uniforms.tex_gen_flags[0] = static_cast<float>(batch.tex0_gen);
  setup.uniforms.tex_gen_flags[1] = static_cast<float>(batch.tex1_gen);
  const auto material_slots =
      BuildMaterialColorSlots(batch.blend_mode, unlit, material_sample.color);
  setup.uniforms.material_flags[0] =
      material_slots.uses_lighting && has_active_lighting ? 0.0f : 1.0f;
  const auto fog_mode =
      ResolveM2MaterialFogMode(batch.blend_mode, unfogged, material_sample.color[3]);

  const bool fog_disabled = fog_mode == M2MaterialFogMode::Disabled;
  setup.uniforms.material_flags[1] =
      fog_disabled ? 1.0f : base.material_flags[1];
  setup.uniforms.fog_color =
      fog_disabled ? base.fog_color
                   : ResolveM2MaterialFogColor(fog_mode, base.fog_color);
  const auto alpha_ref =
      ResolveM2AlphaRefThreshold(batch.blend_mode, material_sample.color[3]);
  if (!alpha_ref.has_value()) {
    if (failure_detail != nullptr) {
      *failure_detail = "alpha_ref blend=" +
          std::to_string(static_cast<std::uint32_t>(batch.blend_mode));
    }
    return false;
  }
  setup.uniforms.alpha_ref[0] = *alpha_ref;

  setup.uniforms.uv_transform0 = base.uv_transform0;
  setup.uniforms.uv_transform0_row1 = base.uv_transform0_row1;
  setup.uniforms.uv_transform1 = base.uv_transform1;
  setup.uniforms.uv_transform1_row1 = base.uv_transform1_row1;

  if (animator != nullptr) {
    if (batch.uv_animation0_index >= 0) {
      if (const auto transform = animator->SampleUvTransform(
              batch.uv_animation0_index, animation_index, animation_time_ms)) {
        setup.uniforms.uv_transform0 = transform->row0;
        setup.uniforms.uv_transform0_row1 = transform->row1;
      }
    }
    if (batch.texture_count > 1 && batch.uv_animation1_index >= 0) {
      if (const auto transform = animator->SampleUvTransform(
              batch.uv_animation1_index, animation_index, animation_time_ms)) {
        setup.uniforms.uv_transform1 = transform->row0;
        setup.uniforms.uv_transform1_row1 = transform->row1;
      }
    }
  }
  setup.uniforms.material_color = material_slots.diffuse;
  setup.uniforms.emissive_color = material_slots.emissive;
  return true;
}

class SubmeshPaletteCache {
 public:

  void BeginSubmission() noexcept { used_ = 0; }

  [[nodiscard]] std::optional<std::span<const float>> Get(
      const openwow::data::model::M2Model& model,
      const openwow::data::model::M2Skin& skin, std::size_t submesh_index,
      std::span<const float> global_bone_matrices, std::string* out_error) {
    for (std::size_t slot = 0; slot < used_; ++slot) {
      if (entries_[slot].submesh_index == submesh_index) {
        return entries_[slot].palette.Span();
      }
    }
    if (used_ == entries_.size()) {
      entries_.emplace_back();
    }
    Entry& entry = entries_[used_];
    entry.palette.Clear();
    if (!BuildM2SubmeshBonePalette(model, skin, submesh_index,
                                   global_bone_matrices, &entry.palette,
                                   out_error)) {
      return std::nullopt;
    }
    entry.submesh_index = submesh_index;
    ++used_;
    return entry.palette.Span();
  }

 private:
  struct Entry {
    std::size_t submesh_index = 0;
    M2BonePaletteBuffer palette;
  };

  std::vector<Entry> entries_;
  std::size_t used_ = 0;
};

struct BatchClassification {
  M2TextureUnitMaterialSample material_sample{};

  bool drawable = false;

  bool opaque = false;
};

struct SubmissionScratch {

  std::vector<BatchClassification> classifications;

  std::vector<std::size_t> draw_order;

  std::vector<std::size_t> candidate_batch_indices;
  std::vector<M2TextureUnitSortInput> sort_inputs;

  std::vector<M2TextureUnitSortKey> sort_keys;
};

}

M2GeometrySubmitter::M2GeometrySubmitter(M2GpuResources& gpu_resources) noexcept
    : gpu_resources_(gpu_resources) {}

M2ShadowClassQuery M2GeometrySubmitter::QueryShadowClass(
    const std::uint32_t, const detail::M2Instance&,
    const detail::M2ModelResource& resource) const {
  constexpr std::uint32_t kShadowRoutingBoneFlagsMask = 0x2F8u;
  std::uint32_t count = 0;
  for (const auto& bone : resource.model_data.bones) {
    if ((bone.flags & kShadowRoutingBoneFlagsMask) != 0u) {
      ++count;
    }
  }
  return {.status = M2ResultStatus::kReady,
          .shadow_class = static_cast<std::uint16_t>(
              std::min<std::uint32_t>(count,
                  std::numeric_limits<std::uint16_t>::max()))};
}

M2ShadowDrawCallLists M2GeometrySubmitter::BuildShadowDrawCalls(
    const std::uint32_t instance_id, const detail::M2Instance& instance,
    const detail::M2ModelResource& resource) const {
  M2ShadowDrawCallLists result;
  if (resource.render_batches.empty()) {
    result.status = M2ResultStatus::kUnsupported;
    return result;
  }
  const auto is_visible = [&](const std::size_t submesh_index) {
    return !instance.has_visible_submesh_filter ||
           std::find(instance.visible_submesh_indices.begin(),
                     instance.visible_submesh_indices.end(), submesh_index) !=
               instance.visible_submesh_indices.end();
  };
  const RenderMatrix4x4 model_matrix = ComputeM2ModelMatrix(instance);
  M2Animator animator(&resource.model_data);
  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const auto animation_time_ms =
      static_cast<std::uint32_t>(instance.animation_time * 1000.0f);

  std::vector<M2ShadowDrawCall> candidates;
  std::vector<M2TextureUnitSortInput> inputs;
  for (const auto& batch : resource.render_batches) {
    const auto& texture_unit =
        resource.skin_data.texture_units[batch.texture_unit_index];
    if (!TextureUnitMatchesDrawBatchScope(
            texture_unit, M2DrawBatchScope::kAllTextureUnits) ||
        !is_visible(batch.submesh_index) || batch.index_count == 0u) {
      continue;
    }
    const auto& submesh = resource.skin_data.submeshes[batch.submesh_index];
    const auto material_sample = SampleM2TextureUnitMaterial(
        resource.model_data, texture_unit, &animator, animation_index,
        animation_time_ms, instance.alpha, instance.tint_color);
    if (material_sample.color[3] < kRenderableOpacityThreshold) {
      continue;
    }
    M2ShadowDrawCall draw_call{
        .instance_id = instance_id, .model_id = instance.model_id,
        .skin_profile = resource.selected_skin_profile,
        .material_index = static_cast<std::uint32_t>(batch.texture_unit_index),
        .texture_unit_index = static_cast<std::uint32_t>(batch.texture_unit_index),
        .sort_key = static_cast<std::uint32_t>(batch.texture_unit_index),
        .batch_count = 1u};
    const std::uint32_t blend_mode = static_cast<std::uint32_t>(batch.blend_mode);
    if (BatchBelongsToOpaqueList(blend_mode, material_sample.color[3])) {
      result.opaque.push_back(draw_call);
      continue;
    }
    M2TextureUnitSortInput input{};
    input.texture_unit_index = batch.texture_unit_index;
    input.blend_mode = blend_mode;
    input.priority_plane = texture_unit.priority_plane;
    input.texture_unit_order_key = batch.texture_unit_order_key;
    input.geoset_id = batch.geoset_id;
    input.sort_distance =
        ComputeM2TextureUnitSortDistance(submesh, model_matrix, std::nullopt);
    input.render_pass_order_key = batch.render_pass_order_key;
    input.type = 0;
    inputs.push_back(input);
    candidates.push_back(draw_call);
  }
  auto keys = BuildM2TransparentSortKeys(
      inputs, ResolveM2TransparentSortMode(resource.model_data.header.global_flags));
  if (!keys.has_value()) {
    result.status = M2ResultStatus::kUnsupported;
    result.reason = M2ResultReason::kUnsupportedBlendMode;
    result.detail = "invalid transparent shadow blend mode";
    return result;
  }
  std::stable_sort(keys->begin(), keys->end(), CompareM2TransparentSortKeys);
  result.transparent.reserve(keys->size());
  for (const auto& key : *keys) {
    if (key.input_index < candidates.size()) {
      auto draw_call = candidates[key.input_index];
      draw_call.sort_key = key.pass_priority;
      result.transparent.push_back(draw_call);
    }
  }
  result.status = M2ResultStatus::kReady;
  return result;
}

M2RenderInstanceResult M2GeometrySubmitter::Submit(
    const std::uint16_t view_id, M2SkinnedMesh* skinned,
    const std::vector<bgfx::TextureHandle>& textures,
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    const std::vector<detail::M2ModelResource::RenderBatch>& render_batches,
    const RenderMatrix4x4& model_mtx, const RenderVec4& tint_color,
    const float alpha, const std::vector<std::size_t>* visible_submesh_indices,
    const M2BatchUniforms* base_uniforms, const M2RenderPassScope pass_scope,
    const M2DrawBatchScope batch_scope, const M2Animator* animator,
    const int animation_index, const std::uint32_t animation_time_ms,
    const std::span<const float> bone_matrices,
    const std::optional<RenderMatrix4x4View> view_mtx,
    const std::optional<RenderSubmitTraceBinding>& submit_trace,
    const std::string_view model_path, const std::uint32_t skin_profile,
    const std::uint32_t instance_id, const M2DrawEncoder draw) const {
  (void)gpu_resources_;
  const auto record_status = [&](const std::string_view pass,
                                 const std::string_view pipeline,
                                 const std::string_view skinning_mode,
                                 const M2ResultStatus status,
                                 const M2ResultReason reason, std::string detail) {
    RecordM2StatusTrace(submit_trace, view_id, model_path, skin_profile, instance_id,
                        pass, pipeline, skinning_mode, status, reason,
                        std::move(detail));
  };

  if (render_batches.empty()) {
    if (pass_scope != M2RenderPassScope::kAll || !model.particle_emitters.empty() ||
        !model.ribbon_emitters.empty()) {
      return {.status = M2ResultStatus::kReady,
              .reason = M2ResultReason::kNoDrawableGeometry};
    }
    record_status("m2.reject", "m2.material", "none", M2ResultStatus::kUnsupported,
                  M2ResultReason::kNoDrawableGeometry, std::string(model_path));
    return {.status = M2ResultStatus::kUnsupported,
            .reason = M2ResultReason::kNoDrawableGeometry,
            .detail = std::string(model_path)};
  }

  const bool use_gpu_skinning =
      bone_matrices.size() >= 16u && skinned != nullptr && skinned->skinned_ok();
  if (!use_gpu_skinning) {
    record_status("m2.reject", "m2.geometry", "none", M2ResultStatus::kUnsupported,
                  M2ResultReason::kGpuGeometryNotReady, std::string(model_path));
    return {.status = M2ResultStatus::kUnsupported,
            .reason = M2ResultReason::kGpuGeometryNotReady,
            .detail = std::string(model_path)};
  }

  const auto is_visible = [&](const std::size_t index) {
    return visible_submesh_indices == nullptr ||
           std::find(visible_submesh_indices->begin(), visible_submesh_indices->end(), index) !=
               visible_submesh_indices->end();
  };
  const bool has_active_lighting = HasUniformLighting(base_uniforms);
  std::uint32_t submitted_draw_count = 0;
  bool draw_order_failed = false;

  thread_local SubmeshPaletteCache palette_cache;
  palette_cache.BeginSubmission();
  std::string palette_error;

  M2BatchDrawSetup setup;
  SeedM2BatchDrawSetup(base_uniforms, &setup);

  std::uint32_t model_transform_cache_index = kNoMatrixCacheIndex;

  thread_local SubmissionScratch scratch;
  std::vector<BatchClassification>& classifications = scratch.classifications;
  classifications.assign(render_batches.size(), BatchClassification{});

  thread_local M2SubmissionMaterialCache material_cache;
  material_cache.BeginSubmission(model, animator, animation_index, animation_time_ms,
                                 alpha, tint_color);

  const bool opaque_only_submission =
      !M2RenderPassScopeIncludesTransparent(pass_scope);
  for (std::size_t index = 0; index < render_batches.size(); ++index) {
    const auto& batch = render_batches[index];
    const auto& texture_unit = skin.texture_units[batch.texture_unit_index];
    if (!TextureUnitMatchesDrawBatchScope(texture_unit, batch_scope) ||
        !is_visible(batch.submesh_index) || batch.index_count == 0u) {
      continue;
    }
    if (opaque_only_submission && batch.blend_mode > M2BlendMode::AlphaKey) {
      continue;
    }
    auto& classification = classifications[index];
    classification.material_sample = material_cache.Sample(texture_unit);
    if (classification.material_sample.color[3] < kRenderableOpacityThreshold) {
      continue;
    }
    classification.drawable = true;
    classification.opaque = BatchBelongsToOpaqueList(
        static_cast<std::uint32_t>(batch.blend_mode),
        classification.material_sample.color[3]);
  }

  const auto build_draw_order =
      [&](const bool opaque_pass) -> const std::vector<std::size_t>& {
    std::vector<std::size_t>& order = scratch.draw_order;
    std::vector<std::size_t>& candidate_batch_indices =
        scratch.candidate_batch_indices;
    std::vector<M2TextureUnitSortInput>& inputs = scratch.sort_inputs;
    order.clear();
    candidate_batch_indices.clear();
    inputs.clear();
    for (std::size_t index = 0; index < render_batches.size(); ++index) {
      const auto& classification = classifications[index];
      if (!classification.drawable || classification.opaque != opaque_pass) {
        continue;
      }
      if (opaque_pass) {
        order.push_back(index);
        continue;
      }
      const auto& batch = render_batches[index];
      const auto& texture_unit = skin.texture_units[batch.texture_unit_index];
      const auto& submesh = skin.submeshes[batch.submesh_index];
      M2TextureUnitSortInput input{};
      input.texture_unit_index = batch.texture_unit_index;
      input.blend_mode = static_cast<std::uint32_t>(batch.blend_mode);
      input.priority_plane = texture_unit.priority_plane;
      input.texture_unit_order_key = batch.texture_unit_order_key;
      input.geoset_id = batch.geoset_id;
      input.sort_distance = ComputeM2TextureUnitSortDistance(submesh, model_mtx, view_mtx);
      input.render_pass_order_key = batch.render_pass_order_key;
      input.type = 0;
      inputs.push_back(input);
      candidate_batch_indices.push_back(index);
    }
    if (!opaque_pass) {
      std::vector<M2TextureUnitSortKey>& keys = scratch.sort_keys;
      if (!BuildM2TransparentSortKeysInto(
              inputs, ResolveM2TransparentSortMode(model.header.global_flags),
              &keys)) {
        draw_order_failed = true;
        return order;
      }
      std::stable_sort(keys.begin(), keys.end(), CompareM2TransparentSortKeys);
      order.reserve(keys.size());
      for (const auto& key : keys) {
        if (key.input_index < candidate_batch_indices.size()) {
          order.push_back(candidate_batch_indices[key.input_index]);
        }
      }
    }
    return order;
  };

  for (int pass = 0; pass < 2; ++pass) {
    const bool opaque_pass = pass == 0;
    if ((opaque_pass && !M2RenderPassScopeIncludesOpaque(pass_scope)) ||
        (!opaque_pass && !M2RenderPassScopeIncludesTransparent(pass_scope))) {
      continue;
    }
    draw_order_failed = false;
    const std::vector<std::size_t>& draw_order = build_draw_order(opaque_pass);
    if (draw_order_failed) {
      record_status(opaque_pass ? "m2.opaque" : "m2.transparent", "m2.material",
                    "gpu", M2ResultStatus::kUnsupported,
                    M2ResultReason::kUnsupportedBlendMode,
                    "invalid transparent texture-unit blend mode");
      return {.status = M2ResultStatus::kUnsupported,
              .reason = M2ResultReason::kUnsupportedBlendMode,
              .detail = "invalid transparent texture-unit blend mode",
              .submitted_draw_count = submitted_draw_count};
    }

    for (const std::size_t render_batch_index : draw_order) {
      const auto& batch = render_batches[render_batch_index];
      const auto& texture_unit = skin.texture_units[batch.texture_unit_index];
      const auto& submesh = skin.submeshes[batch.submesh_index];

      const auto& classification = classifications[render_batch_index];
      const M2TextureUnitMaterialSample& material_sample =
          classification.material_sample;
      const bool is_opaque = classification.opaque;
      std::string setup_failure;
      if (!PrepareM2BatchDrawSetup(batch, material_sample, is_opaque,
                                   has_active_lighting, base_uniforms, animator,
                                   animation_index, animation_time_ms, &setup,
                                   &setup_failure)) {
        record_status(opaque_pass ? "m2.opaque" : "m2.transparent", "m2.material",
                      "gpu", M2ResultStatus::kUnsupported,
                      M2ResultReason::kUnsupportedBlendMode, setup_failure);
        return {.status = M2ResultStatus::kUnsupported,
                .reason = M2ResultReason::kUnsupportedBlendMode,
                .detail = setup_failure, .submitted_draw_count = submitted_draw_count};
      }
      const std::uint64_t state = setup.state;
      bgfx::TextureHandle tex0 = BGFX_INVALID_HANDLE;
      bgfx::TextureHandle tex1 = BGFX_INVALID_HANDLE;
      std::size_t tex0_index = model.textures.size();
      std::size_t tex1_index = model.textures.size();
      std::uint64_t sampler0 = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
      std::uint64_t sampler1 = sampler0;

      const auto resolve_texture = [&](const std::uint32_t index,
                                       const char* label,
                                       bgfx::TextureHandle* handle,
                                       std::size_t* resolved_index,
                                       std::uint64_t* sampler) -> std::optional<M2RenderInstanceResult> {
        if (index >= textures.size()) {
          const std::string detail = std::string(label) + "=" + std::to_string(index);
          record_status(opaque_pass ? "m2.opaque" : "m2.transparent", "m2.texture",
                        "gpu", M2ResultStatus::kFailed,
                        M2ResultReason::kMissingTexture, detail);
          return M2RenderInstanceResult{.status = M2ResultStatus::kFailed,
                                        .reason = M2ResultReason::kMissingTexture,
                                        .detail = detail,
                                        .submitted_draw_count = submitted_draw_count};
        }
        *handle = textures[index];
        *resolved_index = index;
        if (!bgfx::isValid(*handle)) {
          const std::string detail = std::string(label) + "=" + std::to_string(index);
          record_status(opaque_pass ? "m2.opaque" : "m2.transparent", "m2.texture",
                        "gpu", M2ResultStatus::kNotReady,
                        M2ResultReason::kTextureNotReady, detail);
          return M2RenderInstanceResult{.status = M2ResultStatus::kNotReady,
                                        .reason = M2ResultReason::kTextureNotReady,
                                        .detail = detail,
                                        .submitted_draw_count = submitted_draw_count};
        }
        *sampler = SamplerFlagsForTexture(model, *resolved_index);
        return std::nullopt;
      };
      if (batch.texture_count > 0) {
        if (auto failure = resolve_texture(batch.texture0_index, "texture0_index", &tex0,
                                           &tex0_index, &sampler0)) {
          return *failure;
        }
        sampler1 = sampler0;
      }
      if (batch.texture_count > 1) {
        if (auto failure = resolve_texture(batch.texture1_index, "texture1_index", &tex1,
                                           &tex1_index, &sampler1)) {
          return *failure;
        }
      }

      const M2BatchUniforms& uniforms = setup.uniforms;

      palette_error.clear();
      const auto section_palette =
          palette_cache.Get(model, skin, batch.submesh_index, bone_matrices,
                            &palette_error);
      if (!section_palette.has_value()) {
        record_status(opaque_pass ? "m2.opaque" : "m2.transparent", "m2.palette",
                      "gpu", M2ResultStatus::kFailed,
                      M2ResultReason::kBonePoseFailed, std::move(palette_error));
        return {.status = M2ResultStatus::kFailed,
                .reason = M2ResultReason::kBonePoseFailed,
                .detail = std::string(model_path),
                .submitted_draw_count = submitted_draw_count};
      }
      const M2ResultStatus submit_status = skinned->SubmitSkinnedBatch(
          view_id, tex0, tex1, model_mtx, batch.start_index, batch.index_count,
          state, uniforms, *section_palette, sampler0, sampler1, draw,
          &model_transform_cache_index, batch.submesh_index);
      if (submit_status != M2ResultStatus::kReady) {
        const auto reason = submit_status == M2ResultStatus::kNotReady
                                ? M2ResultReason::kGpuGeometryNotReady
                                : M2ResultReason::kInvalidMaterial;
        record_status(opaque_pass ? "m2.opaque" : "m2.transparent", "m2.submit",
                      "gpu", submit_status, reason, std::string(model_path));
        return {.status = submit_status, .reason = reason,
                .detail = std::string(model_path),
                .submitted_draw_count = submitted_draw_count};
      }
      RecordM2TextureUnitSubmitTrace(
          submit_trace, view_id, model_path, skin_profile, instance_id, opaque_pass,
          use_gpu_skinning, state, *skinned, batch, model, skin, texture_unit,
          submesh, material_sample, tex0_index, tex1_index);
      ++submitted_draw_count;
    }
  }

  if (submitted_draw_count == 0u) {
    if (pass_scope != M2RenderPassScope::kAll) {
      return {.status = M2ResultStatus::kReady,
              .reason = M2ResultReason::kNoDrawableGeometry};
    }
    record_status("m2.reject", "m2.material", "gpu", M2ResultStatus::kNotReady,
                  M2ResultReason::kNoDrawableGeometry, std::string(model_path));
  }
  return {.status = submitted_draw_count > 0u ? M2ResultStatus::kReady
                                               : M2ResultStatus::kNotReady,
          .reason = submitted_draw_count > 0u ? M2ResultReason::kNone
                                               : M2ResultReason::kNoDrawableGeometry,
          .detail = submitted_draw_count > 0u ? std::string{} : std::string(model_path),
          .submitted_draw_count = submitted_draw_count};
}

M2RenderInstanceResult M2GeometrySubmitter::SubmitInstanced(
    const std::uint16_t view_id, M2SkinnedMesh* const skinned,
    const std::vector<bgfx::TextureHandle>& textures,
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    const std::vector<detail::M2ModelResource::RenderBatch>& render_batches,
    const std::span<const M2InstancedDrawRecord> instance_records,
    const M2BatchUniforms* const base_uniforms, const M2Animator* const animator,
    const int animation_index, const std::uint32_t animation_time_ms,
    const std::span<const float> bone_matrices, const std::string_view model_path,
    const M2DrawEncoder draw) const {
  if (render_batches.empty() || instance_records.empty()) {
    return {.status = M2ResultStatus::kReady,
            .reason = M2ResultReason::kNoDrawableGeometry};
  }
  if (skinned == nullptr || !skinned->skinned_ok() || bone_matrices.size() < 16u) {
    return {.status = M2ResultStatus::kUnsupported,
            .reason = M2ResultReason::kGpuGeometryNotReady,
            .detail = std::string(model_path)};
  }

  const auto requested = static_cast<std::uint32_t>(instance_records.size());
  const std::uint32_t available =
      bgfx::getAvailInstanceDataBuffer(requested, kM2InstanceDataStride);
  if (available == 0u) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kGpuGeometryNotReady,
            .detail = "instance data buffer exhausted"};
  }
  if (available < requested) {

    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_relaxed)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "M2GeometrySubmitter::SubmitInstanced: instance data buffer clamped " +
              std::to_string(requested) + " -> " + std::to_string(available));
    }
  }
  bgfx::InstanceDataBuffer instance_buffer;
  bgfx::allocInstanceDataBuffer(&instance_buffer, available, kM2InstanceDataStride);

  static_assert(kM2InstanceDataStride % 16u == 0u,
                "allocInstanceDataBuffer aligns the stride up to 16; a record "
                "stride that is not already aligned would make the granted "
                "buffer's element pitch differ from M2InstancedDrawRecord's");
  const std::uint32_t granted = instance_buffer.num;
  if (granted == 0u) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kGpuGeometryNotReady,
            .detail = "instance data buffer exhausted"};
  }
  std::memcpy(instance_buffer.data, instance_records.data(),
              static_cast<std::size_t>(granted) * kM2InstanceDataStride);

  const bool has_active_lighting = HasUniformLighting(base_uniforms);
  std::uint32_t submitted_draw_count = 0;

  thread_local SubmeshPaletteCache palette_cache;
  palette_cache.BeginSubmission();
  std::string palette_error;

  M2BatchDrawSetup setup;
  SeedM2BatchDrawSetup(base_uniforms, &setup);

  thread_local M2SubmissionMaterialCache material_cache;
  material_cache.BeginSubmission(model, animator, animation_index, animation_time_ms,
                                 1.0f, std::nullopt);

  for (const auto& batch : render_batches) {
    const auto& texture_unit = skin.texture_units[batch.texture_unit_index];
    if (!TextureUnitMatchesDrawBatchScope(texture_unit,
                                          M2DrawBatchScope::kAllTextureUnits) ||
        batch.index_count == 0u) {
      continue;
    }

    if (batch.blend_mode > M2BlendMode::AlphaKey) {
      continue;
    }

    const auto material_sample = material_cache.Sample(texture_unit);
    if (material_sample.color[3] < kRenderableOpacityThreshold) {
      continue;
    }

    if (!BatchBelongsToOpaqueList(batch.blend_mode, material_sample.color[3])) {
      continue;
    }
    std::string setup_failure;
    if (!PrepareM2BatchDrawSetup(batch, material_sample, true,
                                 has_active_lighting, base_uniforms, animator,
                                 animation_index, animation_time_ms, &setup,
                                 &setup_failure)) {
      return {.status = M2ResultStatus::kUnsupported,
              .reason = M2ResultReason::kUnsupportedBlendMode,
              .detail = setup_failure,
              .submitted_draw_count = submitted_draw_count};
    }

    bgfx::TextureHandle tex0 = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle tex1 = BGFX_INVALID_HANDLE;
    std::uint64_t sampler0 = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    std::uint64_t sampler1 = sampler0;
    const auto resolve_texture =
        [&](const std::uint32_t index, const char* label,
            bgfx::TextureHandle* handle,
            std::uint64_t* sampler) -> std::optional<M2RenderInstanceResult> {
      if (index >= textures.size()) {
        return M2RenderInstanceResult{
            .status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kMissingTexture,
            .detail = std::string(label) + "=" + std::to_string(index),
            .submitted_draw_count = submitted_draw_count};
      }
      *handle = textures[index];
      if (!bgfx::isValid(*handle)) {
        return M2RenderInstanceResult{
            .status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kTextureNotReady,
            .detail = std::string(label) + "=" + std::to_string(index),
            .submitted_draw_count = submitted_draw_count};
      }
      *sampler = SamplerFlagsForTexture(model, index);
      return std::nullopt;
    };
    if (batch.texture_count > 0) {
      if (auto failure =
              resolve_texture(batch.texture0_index, "texture0_index", &tex0, &sampler0)) {
        return *failure;
      }
      sampler1 = sampler0;
    }
    if (batch.texture_count > 1) {
      if (auto failure =
              resolve_texture(batch.texture1_index, "texture1_index", &tex1, &sampler1)) {
        return *failure;
      }
    }

    palette_error.clear();
    const auto section_palette = palette_cache.Get(
        model, skin, batch.submesh_index, bone_matrices, &palette_error);
    if (!section_palette.has_value()) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kBonePoseFailed,
              .detail = std::string(model_path),
              .submitted_draw_count = submitted_draw_count};
    }
    const M2ResultStatus submit_status = skinned->SubmitInstancedBatch(
        view_id, tex0, tex1, instance_buffer, granted, batch.start_index,
        batch.index_count, setup.state, setup.uniforms, *section_palette,
        sampler0, sampler1, draw, batch.submesh_index);
    if (submit_status != M2ResultStatus::kReady) {
      const auto reason = submit_status == M2ResultStatus::kNotReady
                              ? M2ResultReason::kGpuGeometryNotReady
                              : M2ResultReason::kInvalidMaterial;
      return {.status = submit_status, .reason = reason,
              .detail = std::string(model_path),
              .submitted_draw_count = submitted_draw_count};
    }
    ++submitted_draw_count;
  }

  return {.status = submitted_draw_count > 0u ? M2ResultStatus::kReady
                                              : M2ResultStatus::kNotReady,
          .reason = submitted_draw_count > 0u ? M2ResultReason::kNone
                                              : M2ResultReason::kNoDrawableGeometry,
          .detail = submitted_draw_count > 0u ? std::string{}
                                              : std::string(model_path),
          .submitted_draw_count = submitted_draw_count};
}

}

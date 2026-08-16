#include "openwow/render/m2/m2_effect_renderer.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_cvar_callbacks.h"
#include "openwow/render/m2/m2_draw_encoder.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_shaders.h"
#include "openwow/render/m2/m2_submit_trace.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <algorithm>
#include <bx/math.h>
#include <limits>

namespace openwow::render::m2 {
namespace {

constexpr float kRenderableOpacityThreshold = 0.0001f;

[[nodiscard]] const M2RenderInstanceResult* SelectTerminalResult(
    const M2RenderInstanceResult& first,
    const M2RenderInstanceResult& second) noexcept {
  if (M2RenderResultBlocksComposite(first, second)) {
    return &first;
  }
  if (M2RenderResultBlocksComposite(second, first)) {
    return &second;
  }
  return nullptr;
}

[[nodiscard]] M2RenderInstanceResult WithDrawCount(
    M2RenderInstanceResult result, const std::uint32_t draw_count) noexcept {
  result.submitted_draw_count = draw_count;
  return result;
}

[[nodiscard]] std::optional<RenderMatrix4x4View> BoneMatrixAt(
    const std::vector<float>& matrices, const std::size_t bone_index) noexcept {
  constexpr std::size_t kMatrixFloats = 16u;
  if (bone_index > std::numeric_limits<std::size_t>::max() / kMatrixFloats) {
    return std::nullopt;
  }
  const std::size_t offset = bone_index * kMatrixFloats;
  if (offset > matrices.size() || matrices.size() - offset < kMatrixFloats) {
    return std::nullopt;
  }
  return RenderMatrix4x4View{matrices.data() + offset, kMatrixFloats};
}

[[nodiscard]] bx::Vec3 TransformPoint(const RenderMatrix4x4View matrix,
                                      const bx::Vec3& point) {
  return bx::mul(point, matrix.data());
}

[[nodiscard]] RenderMatrix4x4 ComposeBoneWorldMatrix(
    const RenderMatrix4x4View bone, const RenderMatrix4x4& model) noexcept {
  return openwow::render::MultiplyMatrix4x4(bone, RenderMatrix4x4View{model});
}

[[nodiscard]] bx::Vec3 TransformDirection(const RenderMatrix4x4View matrix,
                                          const bx::Vec3& direction) {
  return {matrix[0] * direction.x + matrix[4] * direction.y + matrix[8] * direction.z,
          matrix[1] * direction.x + matrix[5] * direction.y + matrix[9] * direction.z,
          matrix[2] * direction.x + matrix[6] * direction.y + matrix[10] * direction.z};
}

[[nodiscard]] bx::Vec3 NormalizeOr(const bx::Vec3& value,
                                   const bx::Vec3& fallback) {
  const float length = bx::length(value);
  return length <= 1.0e-6f ? fallback : bx::mul(value, 1.0f / length);
}

[[nodiscard]] RenderVec3 ToRenderVec3(const bx::Vec3& value) noexcept {
  return {value.x, value.y, value.z};
}

[[nodiscard]] std::uint32_t PackABGR(const float r, const float g, const float b,
                                     const float a) {
  const auto to_byte = [](const float value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
  };
  return (to_byte(a) << 24u) | (to_byte(b) << 16u) | (to_byte(g) << 8u) |
         to_byte(r);
}

struct RibbonRenderPass {
  std::size_t pass_index = 0;
  std::size_t texture_index = 0;
  std::size_t material_index = 0;
  M2BlendMode blend_mode = M2BlendMode::Alpha;
  std::uint16_t render_flags = 0;
};

[[nodiscard]] std::optional<std::vector<RibbonRenderPass>> ResolveRibbonPasses(
    const data::model::M2Model& model,
    const data::model::M2RibbonEmitter& ribbon) {
  if (ribbon.texture_indices.empty() || ribbon.material_indices.empty()) {
    return std::nullopt;
  }
  std::vector<RibbonRenderPass> passes;
  passes.reserve(ribbon.texture_indices.size());
  for (std::size_t index = 0; index < ribbon.texture_indices.size(); ++index) {
    if (index >= ribbon.material_indices.size()) {
      return std::nullopt;
    }
    const std::size_t texture_index = ribbon.texture_indices[index];
    const std::size_t material_index = ribbon.material_indices[index];
    if (texture_index >= model.textures.size() ||
        material_index >= model.render_flags.size()) {
      return std::nullopt;
    }
    const auto& flags = model.render_flags[material_index];
    if (flags.blend_mode >= static_cast<std::uint16_t>(M2BlendMode::Count)) {
      return std::nullopt;
    }
    passes.push_back({.pass_index = index, .texture_index = texture_index,
                      .material_index = material_index,
                      .blend_mode = static_cast<M2BlendMode>(flags.blend_mode),
                      .render_flags = flags.flags});
  }
  return passes;
}

struct RibbonAtlasRect {
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u_scale = 1.0f;
  float v1 = 1.0f;
};

[[nodiscard]] std::optional<RibbonAtlasRect> ResolveRibbonAtlas(
    const data::model::M2RibbonEmitter& ribbon, const std::uint16_t texture_slot) {
  if (ribbon.texture_rows == 0u || ribbon.texture_cols == 0u) {
    return std::nullopt;
  }
  const std::uint32_t rows = ribbon.texture_rows;
  const std::uint32_t columns = ribbon.texture_cols;
  if (texture_slot >= rows * columns) {
    return std::nullopt;
  }
  const std::uint32_t row = texture_slot / columns;
  const std::uint32_t column = texture_slot % columns;
  const float width = 1.0f / static_cast<float>(columns);
  const float height = 1.0f / static_cast<float>(rows);
  return RibbonAtlasRect{.u0 = column * width, .v0 = row * height,
                         .u_scale = width, .v1 = (row + 1u) * height};
}

[[nodiscard]] std::vector<ParticleVertex> BuildRibbonVertices(
    const std::vector<RibbonSegment>& segments, const std::uint32_t color,
    const RibbonAtlasRect& atlas) {
  std::vector<ParticleVertex> vertices;
  if (segments.size() < 2u) {
    return vertices;
  }
  const auto edge = [&](const RibbonSegment& segment) {
    const float u = atlas.u0 + segment.tex_coord * atlas.u_scale;
    return std::array<ParticleVertex, 2>{
        ParticleVertex{segment.top_position[0], segment.top_position[1],
                       segment.top_position[2], color, u, atlas.v0},
        ParticleVertex{segment.bottom_position[0], segment.bottom_position[1],
                       segment.bottom_position[2], color, u, atlas.v1}};
  };
  vertices.reserve((segments.size() - 1u) * 6u);
  for (std::size_t index = 0; index + 1u < segments.size(); ++index) {
    const auto edge0 = edge(segments[index]);
    const auto edge1 = edge(segments[index + 1u]);
    vertices.insert(vertices.end(), {edge0[0], edge0[1], edge1[0],
                                     edge1[0], edge0[1], edge1[1]});
  }
  return vertices;
}

[[nodiscard]] std::uint64_t SamplerFlagsForTexture(
    const data::model::M2Model& model, const std::size_t texture_index) {
  if (texture_index >= model.textures.size()) {
    return BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  }
  std::uint64_t flags = 0;
  if ((model.textures[texture_index].flags & 0x1u) == 0u) {
    flags |= BGFX_SAMPLER_U_CLAMP;
  }
  if ((model.textures[texture_index].flags & 0x2u) == 0u) {
    flags |= BGFX_SAMPLER_V_CLAMP;
  }
  return flags;
}

}

M2EffectRenderer::~M2EffectRenderer() = default;

void M2EffectRenderer::SetParticleDensity(const float density) noexcept {
  particle_density_ = std::clamp(density, 0.0f, 1.0f);
}

void M2EffectRenderer::Shutdown() {
  if (particle_shader_) {
    DestroyParticleProgram(*particle_shader_);
    particle_shader_.reset();
  }

  particle_shader_once_.emplace();
}

bool M2EffectRenderer::WarmUpParticleProgram() { return EnsureParticleProgram(); }

bool M2EffectRenderer::EnsureParticleProgram() {

  std::call_once(*particle_shader_once_, [this] {
    ParticleVertex::InitLayout();
    particle_shader_ = std::make_unique<ParticleShaderHandles>(LoadParticleProgram());
    if (!bgfx::isValid(particle_shader_->program)) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       "M2System: particle shader creation failed");
    }
  });
  return particle_shader_ && bgfx::isValid(particle_shader_->program);
}

void M2EffectRenderer::BindParticleSystem(detail::M2Instance& instance,
                                          detail::M2ModelResource& resource) {
  if (!instance.particle_system_bound ||
      instance.particle_system_model_key != resource.model_path) {
    instance.particle_system.Init(&resource.model_data);
    instance.particle_system_model_key = resource.model_path;
    instance.particle_system_bound = true;
  }
}

void M2EffectRenderer::BindRibbonSystem(detail::M2Instance& instance,
                                        const detail::M2ModelResource& resource) {
  if (instance.ribbon_system_initialized &&
      instance.ribbon_system_model_key == resource.model_path) {
    return;
  }
  instance.ribbon_system.Clear();
  for (const auto& ribbon : resource.model_data.ribbon_emitters) {
    instance.ribbon_system.AddEmitter(
        {.edge_lifetime_seconds = ribbon.edge_lifetime,
         .edges_per_second = ribbon.edges_per_second,
         .gravity = ribbon.gravity});
  }
  instance.ribbon_system_model_key = resource.model_path;
  instance.ribbon_system_initialized = true;
}

void M2EffectRenderer::AdvanceRibbonEmitters(
    detail::M2Instance& instance, const detail::M2ModelResource& resource,
    M2Animator& animator, const RenderMatrix4x4& model_matrix,
    const int animation_index, const std::uint32_t animation_time_ms,
    const float dt_seconds, const std::vector<float>& bone_matrices,
    const std::size_t bone_count) {
  const auto& ribbons = resource.model_data.ribbon_emitters;
  for (std::size_t index = 0; index < ribbons.size(); ++index) {
    const auto& ribbon = ribbons[index];
    const auto sample =
        animator.SampleRibbonEmitter(index, animation_index, animation_time_ms);
    const bool visible = sample.has_value() && sample->visible;

    if (!instance.ribbon_system.SetEmitterEnabled(
            static_cast<std::uint32_t>(index), visible)) {
      continue;
    }
    if (!visible) {
      continue;
    }
    const std::size_t bone_index = ribbon.bone_index;
    const auto bone_matrix = BoneMatrixAt(bone_matrices, bone_index);
    if (bone_index >= bone_count || !bone_matrix.has_value()) {
      continue;
    }

    const RenderMatrix4x4 bone_world_matrix =
        ComposeBoneWorldMatrix(*bone_matrix, model_matrix);
    const RenderMatrix4x4View bone_world{bone_world_matrix};
    const bx::Vec3 position = TransformPoint(
        bone_world, {ribbon.position[0], ribbon.position[1], ribbon.position[2]});
    const bx::Vec3 width = NormalizeOr(
        TransformDirection(bone_world, {0.0f, 1.0f, 0.0f}), {0.0f, 1.0f, 0.0f});
    const bx::Vec3 tangent = NormalizeOr(
        TransformDirection(bone_world, {0.0f, 0.0f, 1.0f}), {0.0f, 0.0f, 1.0f});
    (void)instance.ribbon_system.UpdateEmitter(
        static_cast<std::uint32_t>(index), dt_seconds, ToRenderVec3(position),
        ToRenderVec3(width), ToRenderVec3(tangent), sample->height_above,
        sample->height_below);
  }
}

void M2EffectRenderer::SimulateEffects(
    detail::M2Instance& instance, detail::M2ModelResource& resource,
    const RenderMatrix4x4& model_matrix,
    const std::optional<RenderMatrix4x4View>& view_matrix,
    const int animation_index, const std::uint32_t animation_time_ms,
    const float frame_delta_seconds, const std::vector<float>& bone_matrices,
    const std::size_t bone_count) {
  if (!instance.effect_emitters_enabled) {
    return;
  }

  M2Animator animator(&resource.model_data);

  if (!resource.model_data.particle_emitters.empty()) {
    BindParticleSystem(instance, resource);

    std::optional<bx::Vec3> camera_world_position;
    if (view_matrix.has_value()) {
      const RenderVec3 eye =
          openwow::render::ExtractCameraPositionFromRetailViewMatrix(*view_matrix);
      camera_world_position = bx::Vec3{eye[0], eye[1], eye[2]};
    }
    instance.particle_system.Update(std::max(0.0f, frame_delta_seconds),
                                    animation_index, animation_time_ms,
                                    bone_matrices, bone_count, animator,
                                    particle_density_, model_matrix,
                                    camera_world_position);
  }

  if (!resource.model_data.ribbon_emitters.empty() && bone_count != 0u) {
    BindRibbonSystem(instance, resource);
    AdvanceRibbonEmitters(instance, resource, animator, model_matrix,
                          animation_index, animation_time_ms,
                          std::max(0.0f, frame_delta_seconds), bone_matrices,
                          bone_count);
  }
}

M2RenderInstanceResult M2EffectRenderer::Submit(
    const std::uint16_t view_id, const std::uint32_t instance_id,
    detail::M2Instance& instance, detail::M2ModelResource& resource,
    const std::vector<bgfx::TextureHandle>& textures,
    const RenderMatrix4x4& model_matrix,
    const std::optional<RenderMatrix4x4View> view_matrix,
    const int animation_index, const std::uint32_t animation_time_ms,
    const std::vector<float>& bone_matrices,
    const std::optional<RenderSubmitTraceBinding>& submit_trace,
    const M2DrawEncoder draw) {
  const std::size_t bone_count = bone_matrices.size() / 16u;

  if (!instance.effect_emitters_enabled) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kNoDrawableGeometry,
            .detail = "effect emitters disabled"};
  }

  if (!resource.HasEffectData()) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kNoDrawableGeometry,
            .detail = resource.model_path};
  }

  M2RenderInstanceResult particle_result = SubmitParticles(
      view_id, instance_id, instance, resource, textures, model_matrix, view_matrix,
      animation_index, animation_time_ms, bone_matrices, bone_count,
      submit_trace, draw);
  M2RenderInstanceResult ribbon_result = SubmitRibbons(
      view_id, instance_id, instance, resource, textures, model_matrix,
      animation_index, animation_time_ms, bone_matrices, bone_count,
      submit_trace, draw);
  const std::uint32_t draw_count = particle_result.submitted_draw_count +
                                   ribbon_result.submitted_draw_count;

  if (draw_count == 0u) {
    if (const auto* terminal = SelectTerminalResult(particle_result, ribbon_result)) {
      return WithDrawCount(*terminal, draw_count);
    }
  }
  if (draw_count > 0u) {
    return {.status = M2ResultStatus::kReady, .submitted_draw_count = draw_count};
  }

  const bool prefer_particle_detail = !particle_result.detail.empty();
  return {.status = M2ResultStatus::kNotReady,
          .reason = particle_result.reason != M2ResultReason::kNone
                        ? particle_result.reason
                        : ribbon_result.reason,
          .detail = prefer_particle_detail
                        ? std::move(particle_result.detail)
                        : std::move(ribbon_result.detail)};
}

M2RenderInstanceResult M2EffectRenderer::SubmitParticles(
    const std::uint16_t view_id, const std::uint32_t instance_id,
    detail::M2Instance& instance, detail::M2ModelResource& resource,
    const std::vector<bgfx::TextureHandle>& textures,
    const RenderMatrix4x4& model_matrix,
    const std::optional<RenderMatrix4x4View> view_matrix,
    const int animation_index, const std::uint32_t animation_time_ms,
    const std::vector<float>& bone_matrices,
    const std::size_t bone_count,
    const std::optional<RenderSubmitTraceBinding>& submit_trace,
    const M2DrawEncoder draw) {

  (void)animation_index;
  (void)animation_time_ms;
  (void)bone_matrices;
  (void)bone_count;
  const auto& emitters = resource.model_data.particle_emitters;
  if (emitters.empty()) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kNoDrawableGeometry,
            .detail = resource.model_path};
  }
  const auto status = [&](const std::string_view pipeline,
                          const M2ResultStatus result_status,
                          const M2ResultReason reason, std::string detail,
                          const std::uint32_t draw_count = 0u) {

    if (submit_trace.has_value()) {
      RecordM2StatusTrace(submit_trace, view_id, resource.model_path,
                          resource.selected_skin_profile, instance_id, "m2.particle",
                          pipeline, "effect", result_status, reason, detail);
    }
    return M2RenderInstanceResult{.status = result_status, .reason = reason,
                                  .detail = std::move(detail),
                                  .submitted_draw_count = draw_count};
  };
  if (!EnsureParticleProgram()) {
    return status("m2.particle.shader", M2ResultStatus::kFailed,
                  M2ResultReason::kShaderNotReady, "particle shader");
  }

  const float model_alpha = instance.alpha * instance.tint_color[3];
  if (model_alpha < kRenderableOpacityThreshold) {
    return status("m2.particle.billboard", M2ResultStatus::kNotReady,
                  M2ResultReason::kNoDrawableGeometry, resource.model_path);
  }

  BindParticleSystem(instance, resource);
  bx::Vec3 camera_right{1.0f, 0.0f, 0.0f};
  bx::Vec3 camera_up{0.0f, 1.0f, 0.0f};
  if (view_matrix.has_value()) {
    camera_right = {(*view_matrix)[0], (*view_matrix)[4], (*view_matrix)[8]};
    camera_up = {(*view_matrix)[1], (*view_matrix)[5], (*view_matrix)[9]};
  }
  const M2BatchUniforms* const instance_uniforms =
      detail::BatchUniformsOf(instance);
  const M2BatchUniforms& uniforms =
      instance_uniforms != nullptr ? *instance_uniforms : detail::DefaultBatchUniforms();

  const M2ParticleLighting particle_lighting{
      .uniforms = &uniforms,
      .world_normal =
          TransformDirection(RenderMatrix4x4View{model_matrix}, {0.0f, 0.0f, 1.0f}),
  };
  const auto& vertices = instance.particle_system.BuildVertices(
      camera_right, camera_up, &particle_lighting);
  if (vertices.empty()) {
    return status("m2.particle.billboard", M2ResultStatus::kNotReady,
                  M2ResultReason::kNoDrawableGeometry, resource.model_path);
  }
  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());

  if (bgfx::getAvailTransientVertexBuffer(vertex_count, ParticleVertex::layout) <
      vertex_count) {
    return status("m2.particle.geometry", M2ResultStatus::kNotReady,
                  M2ResultReason::kGpuGeometryNotReady,
                  "particle transient vertex buffer");
  }
  bgfx::TransientVertexBuffer vertex_buffer;
  bgfx::allocTransientVertexBuffer(&vertex_buffer, vertex_count, ParticleVertex::layout);

  if (vertex_buffer.stride == 0u ||
      vertex_buffer.size / vertex_buffer.stride < vertex_count) {
    return status("m2.particle.geometry", M2ResultStatus::kNotReady,
                  M2ResultReason::kGpuGeometryNotReady,
                  "particle transient vertex buffer");
  }
  std::copy(vertices.begin(), vertices.end(),
            reinterpret_cast<ParticleVertex*>(vertex_buffer.data));

  const std::uint32_t global_flags = openwow::render::GetM2GlobalFlags();
  const bool batch_particles =
      (global_flags & openwow::render::kM2Flag_BatchParticles) != 0u;
  const bool force_additive_sort =
      (global_flags & openwow::render::kM2Flag_ForceAdditiveSort) != 0u;
  std::vector<M2ParticleDrawInput> draw_inputs;
  draw_inputs.reserve(emitters.size());
  std::uint32_t vertex_offset = 0;
  for (std::size_t index = 0; index < emitters.size(); ++index) {
    const auto& emitter = emitters[index];
    const auto count = instance.particle_system.emitter_render_vertex_count(index);
    if (count > 0u &&
        (emitter.texture >= textures.size() || !bgfx::isValid(textures[emitter.texture]))) {
      return status("m2.particle.texture", M2ResultStatus::kNotReady,
                    M2ResultReason::kTextureNotReady,
                    "particle texture=" + std::to_string(emitter.texture));
    }
    draw_inputs.push_back({.emitter_index = static_cast<std::uint32_t>(index),
                           .first_vertex = vertex_offset, .vertex_count = count,
                           .texture_index = emitter.texture,
                           .blending_type = emitter.blending_type,
                           .flags = emitter.flags});
    vertex_offset += count;
  }
  const auto ranges =
      BuildM2ParticleDrawRanges(draw_inputs, batch_particles, force_additive_sort);
  if (!ranges.has_value()) {
    return status("m2.particle.material", M2ResultStatus::kUnsupported,
                  M2ResultReason::kUnsupportedBlendMode, resource.model_path);
  }
  std::uint32_t draw_count = 0;
  for (const auto& range : *ranges) {

    const auto base_state = M2ParticleSystem::BlendStateForType(range.material_blend_index);
    if (!base_state.has_value()) {
      return status("m2.particle.material", M2ResultStatus::kUnsupported,
                    M2ResultReason::kUnsupportedBlendMode,
                    "particle blend=" + std::to_string(range.material_blend_index), draw_count);
    }

    const auto blend_mode_enum = static_cast<M2BlendMode>(range.material_blend_index);

    const bool render_flag_unfogged =
        (range.flag_sort_key & kM2MaterialFlagUnfogged) != 0u;
    const M2MaterialFogMode fog_mode =
        ResolveM2MaterialFogMode(blend_mode_enum, render_flag_unfogged, 1.0f);

    constexpr float kParticleDefaultAlphaRef = 1.0f / 255.0f;
    const float alpha_ref = ResolveM2AlphaRefThreshold(blend_mode_enum, model_alpha)
                                .value_or(kParticleDefaultAlphaRef);

    const RenderVec4 texture_flags{
        1.0f, fog_mode == M2MaterialFogMode::Disabled ? 1.0f : 0.0f, alpha_ref, model_alpha};
    const RenderVec4 particle_fog_color =
        ResolveM2MaterialFogColor(fog_mode, uniforms.fog_color);
    const std::uint64_t state =
        ApplyMaterialRenderFlags(*base_state,
                                 static_cast<std::uint16_t>(range.flag_sort_key));

    draw.setVertexBuffer(0, &vertex_buffer, range.first_vertex, range.vertex_count);
    draw.setTexture(0, particle_shader_->sampler, textures[range.texture_index],
                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    draw.setUniform(particle_shader_->flags, texture_flags.data());
    draw.setUniform(particle_shader_->fog_params, uniforms.fog_params.data());
    draw.setUniform(particle_shader_->fog_color, particle_fog_color.data());
    draw.setState(state);

    if (submit_trace.has_value()) {
      RecordM2EffectSubmitTrace(
          submit_trace, view_id, resource.model_path, resource.selected_skin_profile,
          instance_id, "m2.particle", "m2.particle.billboard", state,

          static_cast<std::int32_t>(((range.sort_group & 0xffffu) << 16u) |
                                    (static_cast<std::uint32_t>(range.render_particle_type) << 8u) |
                                    (range.flag_sort_key & 0xffu)),
          "emitters=" + std::to_string(range.first_emitter) + "+" +
              std::to_string(range.emitter_count) + " vertices=" +
              std::to_string(range.first_vertex) + "+" + std::to_string(range.vertex_count),
          "texture=" + std::to_string(range.texture_index) + ":" +
              M2TraceTextureName(resource.model_data, range.texture_index) + " blend=" +
              std::to_string(range.blending_type) + "/" +
              std::to_string(range.material_blend_index) + " render_type=" +
              std::to_string(range.render_particle_type) + " sort_group=" +
              std::to_string(range.sort_group) + " sort_blend=" +
              std::to_string(range.sort_blend_index) + " flag_key=" +
              std::to_string(range.flag_sort_key));
    }
    draw.submit(static_cast<bgfx::ViewId>(view_id), particle_shader_->program);
    ++draw_count;
  }
  return draw_count > 0u
             ? M2RenderInstanceResult{.status = M2ResultStatus::kReady,
                                      .submitted_draw_count = draw_count}
             : status("m2.particle.billboard", M2ResultStatus::kNotReady,
                      M2ResultReason::kNoDrawableGeometry, resource.model_path);
}

M2RenderInstanceResult M2EffectRenderer::SubmitRibbons(
    const std::uint16_t view_id, const std::uint32_t instance_id,
    detail::M2Instance& instance, const detail::M2ModelResource& resource,
    const std::vector<bgfx::TextureHandle>& textures,
    const RenderMatrix4x4& model_matrix, const int animation_index,
    const std::uint32_t animation_time_ms,
    const std::vector<float>& bone_matrices, const std::size_t bone_count,
    const std::optional<RenderSubmitTraceBinding>& submit_trace,
    const M2DrawEncoder draw) {
  const auto& ribbons = resource.model_data.ribbon_emitters;
  if (ribbons.empty()) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kNoDrawableGeometry,
            .detail = resource.model_path};
  }
  const auto status = [&](const std::string_view pipeline,
                          const M2ResultStatus result_status,
                          const M2ResultReason reason, std::string detail,
                          const std::uint32_t draw_count = 0u) {

    if (submit_trace.has_value()) {
      RecordM2StatusTrace(submit_trace, view_id, resource.model_path,
                          resource.selected_skin_profile, instance_id, "m2.ribbon",
                          pipeline, "effect", result_status, reason, detail);
    }
    return M2RenderInstanceResult{.status = result_status, .reason = reason,
                                  .detail = std::move(detail),
                                  .submitted_draw_count = draw_count};
  };
  if (bone_count == 0u) {
    return status("m2.ribbon.pose", M2ResultStatus::kNotReady,
                  M2ResultReason::kBonePoseFailed, resource.model_path);
  }

  BindRibbonSystem(instance, resource);
  M2Animator animator(&resource.model_data);
  std::vector<M2RibbonEmitterSample> samples;
  samples.reserve(ribbons.size());
  for (std::size_t index = 0; index < ribbons.size(); ++index) {
    auto sample = animator.SampleRibbonEmitter(index, animation_index, animation_time_ms);
    samples.push_back(sample.has_value() ? *sample : M2RibbonEmitterSample{});
  }

  (void)model_matrix;
  (void)bone_matrices;
  if (instance.ribbon_system.GetTotalSegmentCount() == 0u || !EnsureParticleProgram()) {
    const bool empty = instance.ribbon_system.GetTotalSegmentCount() == 0u;
    return status(empty ? "m2.ribbon.trail" : "m2.ribbon.shader",
                  empty ? M2ResultStatus::kNotReady : M2ResultStatus::kFailed,
                  empty ? M2ResultReason::kNoDrawableGeometry
                        : M2ResultReason::kShaderNotReady,
                  resource.model_path);
  }

  const auto& emitters = instance.ribbon_system.GetEmitters();
  const M2BatchUniforms* const instance_uniforms =
      detail::BatchUniformsOf(instance);
  const M2BatchUniforms& uniforms =
      instance_uniforms != nullptr ? *instance_uniforms : detail::DefaultBatchUniforms();
  std::uint32_t draw_count = 0;
  for (std::size_t index = 0; index < emitters.size() && index < ribbons.size(); ++index) {
    const auto& emitter = emitters[index];
    const auto& sample = samples[index];
    if (!emitter.IsEnabled() || emitter.GetSegmentCount() < 2u || !sample.visible ||
        sample.alpha * instance.alpha * instance.tint_color[3] <
            kRenderableOpacityThreshold) {
      continue;
    }
    const auto passes = ResolveRibbonPasses(resource.model_data, ribbons[index]);
    if (!passes.has_value()) {
      return status("m2.ribbon.material", M2ResultStatus::kFailed,
                    M2ResultReason::kInvalidRibbon,
                    "ribbon render passes=" + std::to_string(index), draw_count);
    }
    for (const auto& pass : *passes) {
      if (pass.texture_index >= textures.size() ||
          !bgfx::isValid(textures[pass.texture_index])) {
        return status("m2.ribbon.texture", M2ResultStatus::kNotReady,
                      M2ResultReason::kTextureNotReady,
                      "ribbon texture=" + std::to_string(pass.texture_index), draw_count);
      }
    }
    const auto atlas = ResolveRibbonAtlas(ribbons[index], sample.texture_slot);
    if (!atlas.has_value()) {
      return status("m2.ribbon.material", M2ResultStatus::kFailed,
                    M2ResultReason::kInvalidRibbon,
                    "ribbon atlas slot=" + std::to_string(sample.texture_slot), draw_count);
    }
    const std::uint32_t color = PackABGR(
        sample.color[0] * instance.tint_color[0],
        sample.color[1] * instance.tint_color[1],
        sample.color[2] * instance.tint_color[2],
        sample.alpha * instance.alpha * instance.tint_color[3]);
    const auto vertices =
        BuildRibbonVertices(emitter.GetSegments(), color, *atlas);
    if (vertices.empty()) {
      continue;
    }
    const auto vertex_count = static_cast<std::uint32_t>(vertices.size());

    if (bgfx::getAvailTransientVertexBuffer(vertex_count,
                                            ParticleVertex::layout) <
        vertex_count) {
      return status("m2.ribbon.geometry", M2ResultStatus::kNotReady,
                    M2ResultReason::kGpuGeometryNotReady,
                    "ribbon transient vertex buffer", draw_count);
    }
    bgfx::TransientVertexBuffer vertex_buffer;
    bgfx::allocTransientVertexBuffer(&vertex_buffer, vertex_count, ParticleVertex::layout);

    if (vertex_buffer.stride == 0u ||
        vertex_buffer.size / vertex_buffer.stride < vertex_count) {
      return status("m2.ribbon.geometry", M2ResultStatus::kNotReady,
                    M2ResultReason::kGpuGeometryNotReady,
                    "ribbon transient vertex buffer", draw_count);
    }
    std::copy(vertices.begin(), vertices.end(),
              reinterpret_cast<ParticleVertex*>(vertex_buffer.data));
    for (const auto& pass : *passes) {

      const auto render_state = ResolveM2RenderState(pass.blend_mode, pass.render_flags);
      if (!render_state.has_value()) {
        return status("m2.ribbon.material", M2ResultStatus::kUnsupported,
                      M2ResultReason::kUnsupportedBlendMode,
                      "ribbon blend=" +
                          std::to_string(static_cast<std::uint32_t>(pass.blend_mode)),
                      draw_count);
      }

      const bool ribbon_unfogged = (pass.render_flags & kM2MaterialFlagUnfogged) != 0u;
      const M2MaterialFogMode ribbon_fog_mode =
          ResolveM2MaterialFogMode(pass.blend_mode, ribbon_unfogged, 1.0f);

      constexpr float kRibbonDefaultAlphaRef = 1.0f / 255.0f;
      const float ribbon_element_alpha =
          sample.alpha * instance.alpha * instance.tint_color[3];
      const float ribbon_alpha_ref = ResolveM2AlphaRefThreshold(pass.blend_mode,
                                                                ribbon_element_alpha)
                                         .value_or(kRibbonDefaultAlphaRef);
      const RenderVec4 texture_flags{
          1.0f, ribbon_fog_mode == M2MaterialFogMode::Disabled ? 1.0f : 0.0f,
          ribbon_alpha_ref, 1.0f};
      const RenderVec4 ribbon_fog_color =
          ResolveM2MaterialFogColor(ribbon_fog_mode, uniforms.fog_color);

      draw.setVertexBuffer(0, &vertex_buffer, 0, vertex_count);
      draw.setTexture(0, particle_shader_->sampler, textures[pass.texture_index],
                      SamplerFlagsForTexture(resource.model_data, pass.texture_index));
      draw.setUniform(particle_shader_->flags, texture_flags.data());
      draw.setUniform(particle_shader_->fog_params, uniforms.fog_params.data());
      draw.setUniform(particle_shader_->fog_color, ribbon_fog_color.data());
      const std::uint64_t state = *render_state | BGFX_STATE_MSAA;
      draw.setState(state);

      if (submit_trace.has_value()) {
        RecordM2EffectSubmitTrace(
            submit_trace, view_id, resource.model_path, resource.selected_skin_profile,
            instance_id, "m2.ribbon", "m2.ribbon.trail", state,
            ribbons[index].priority_plane,
            "ribbon=" + std::to_string(index) + " id=" +
                std::to_string(ribbons[index].ribbon_id) + " pass=" +
                std::to_string(pass.pass_index) + " segments=" +
                std::to_string(emitter.GetSegmentCount()) + " vertices=" +
                std::to_string(vertex_count),
            "texture=" + std::to_string(pass.texture_index) + ":" +
                M2TraceTextureName(resource.model_data, pass.texture_index) + " material=" +
                std::to_string(pass.material_index) + " blend=" +
                std::to_string(static_cast<std::uint32_t>(pass.blend_mode)) + " flags=" +
                M2TraceHexValue(pass.render_flags) + " tex_slot=" +
                std::to_string(sample.texture_slot));
      }
      draw.submit(static_cast<bgfx::ViewId>(view_id), particle_shader_->program);
      ++draw_count;
    }
  }
  return draw_count > 0u
             ? M2RenderInstanceResult{.status = M2ResultStatus::kReady,
                                      .submitted_draw_count = draw_count}
             : status("m2.ribbon.trail", M2ResultStatus::kNotReady,
                      M2ResultReason::kNoDrawableGeometry, resource.model_path);
}

}

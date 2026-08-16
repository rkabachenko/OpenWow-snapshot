#pragma once

#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/m2/m2_draw_encoder.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <memory>
#include <mutex>
#include <optional>

namespace openwow::render::m2 {

class M2Animator;

class M2EffectRenderer {
 public:
  M2EffectRenderer() = default;
  ~M2EffectRenderer();
  M2EffectRenderer(const M2EffectRenderer&) = delete;
  M2EffectRenderer& operator=(const M2EffectRenderer&) = delete;

  void SetParticleDensity(float density) noexcept;
  void Shutdown();

  [[nodiscard]] bool WarmUpParticleProgram();

  void SimulateEffects(detail::M2Instance& instance,
                       detail::M2ModelResource& resource,
                       const RenderMatrix4x4& model_matrix,
                       const std::optional<RenderMatrix4x4View>& view_matrix,
                       int animation_index, std::uint32_t animation_time_ms,
                       float frame_delta_seconds,
                       const std::vector<float>& bone_matrices,
                       std::size_t bone_count);

  [[nodiscard]] M2RenderInstanceResult Submit(
      std::uint16_t view_id, std::uint32_t instance_id,
      detail::M2Instance& instance, detail::M2ModelResource& resource,
      const std::vector<bgfx::TextureHandle>& textures,
      const RenderMatrix4x4& model_matrix,
      std::optional<RenderMatrix4x4View> view_matrix, int animation_index,
      std::uint32_t animation_time_ms, const std::vector<float>& bone_matrices,
      const std::optional<RenderSubmitTraceBinding>& submit_trace,
      M2DrawEncoder draw = {});

 private:
  [[nodiscard]] bool EnsureParticleProgram();
  static void BindParticleSystem(detail::M2Instance& instance,
                                 detail::M2ModelResource& resource);
  static void BindRibbonSystem(detail::M2Instance& instance,
                               const detail::M2ModelResource& resource);

  static void AdvanceRibbonEmitters(detail::M2Instance& instance,
                                    const detail::M2ModelResource& resource,
                                    M2Animator& animator,
                                    const RenderMatrix4x4& model_matrix,
                                    int animation_index,
                                    std::uint32_t animation_time_ms,
                                    float dt_seconds,
                                    const std::vector<float>& bone_matrices,
                                    std::size_t bone_count);
  [[nodiscard]] M2RenderInstanceResult SubmitParticles(
      std::uint16_t view_id, std::uint32_t instance_id,
      detail::M2Instance& instance, detail::M2ModelResource& resource,
      const std::vector<bgfx::TextureHandle>& textures,
      const RenderMatrix4x4& model_matrix,
      std::optional<RenderMatrix4x4View> view_matrix, int animation_index,
      std::uint32_t animation_time_ms,
      const std::vector<float>& bone_matrices, std::size_t bone_count,
      const std::optional<RenderSubmitTraceBinding>& submit_trace,
      M2DrawEncoder draw);
  [[nodiscard]] M2RenderInstanceResult SubmitRibbons(
      std::uint16_t view_id, std::uint32_t instance_id,
      detail::M2Instance& instance, const detail::M2ModelResource& resource,
      const std::vector<bgfx::TextureHandle>& textures,
      const RenderMatrix4x4& model_matrix, int animation_index,
      std::uint32_t animation_time_ms,
      const std::vector<float>& bone_matrices, std::size_t bone_count,
      const std::optional<RenderSubmitTraceBinding>& submit_trace,
      M2DrawEncoder draw);

  std::unique_ptr<ParticleShaderHandles> particle_shader_;
  float particle_density_ = 1.0f;

  std::optional<std::once_flag> particle_shader_once_{std::in_place};
};

}

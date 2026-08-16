#pragma once

#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/m2/m2_effect_renderer.h"
#include "openwow/render/m2/m2_geometry_submitter.h"
#include "openwow/render/m2/m2_model_repository.h"

#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace openwow::render {
class TextureManager;
}
namespace openwow::core {
class FrameJobSystem;
}

namespace openwow::render::m2 {

class M2AnimationRuntime;
class M2GpuResources;
class M2InstanceStore;

class M2Renderer {
 public:
  M2Renderer(M2SystemMutex& mutex, M2ModelRepository::ModelMap& models,
             M2InstanceStore& instances, M2AnimationRuntime& animation_runtime,
             M2GpuResources& gpu_resources, TextureManager*& texture_manager);

  void SetParticleDensity(float density) noexcept;
  void Shutdown();

  [[nodiscard]] bool WarmUpParticleProgram() {
    return effect_renderer_.WarmUpParticleProgram();
  }
  void BindSubmitTrace(std::optional<RenderSubmitTraceBinding> binding);

  [[nodiscard]] bool IsSubmitTraceBound() const;

  void UpdateAllEffects(float frame_delta_seconds,
                        std::optional<RenderMatrix4x4View> view_matrix,
                        core::FrameJobSystem* jobs);

  [[nodiscard]] M2RenderInstanceResult Render(
      std::uint16_t view_id, std::uint32_t instance_id,
      std::optional<RenderMatrix4x4View> view_matrix,
      M2RenderPassScope pass_scope, bgfx::Encoder* encoder = nullptr);

  [[nodiscard]] M2RenderInstanceResult RenderUnlocked(
      std::uint16_t view_id, std::uint32_t instance_id,
      std::optional<RenderMatrix4x4View> view_matrix,
      M2RenderPassScope pass_scope, bgfx::Encoder* encoder = nullptr,
      std::optional<std::uint32_t> transparent_draw_ordinal = std::nullopt);

  void RenderBatch(std::uint16_t view_id,
                    std::span<const std::uint32_t> instance_ids,
                    std::optional<RenderMatrix4x4View> view_matrix,
                    M2RenderPassScope pass_scope, core::FrameJobSystem* jobs,
                    double per_instance_microseconds,
                    std::span<M2RenderInstanceResult> out_results,
                    bgfx::Encoder* serial_encoder = nullptr,
                    std::span<const std::uint32_t> transparent_draw_ordinals = {});
  [[nodiscard]] M2ShadowClassQuery QueryShadowClass(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2ShadowDrawCallLists BuildShadowDrawCalls(
      std::uint32_t instance_id) const;

  [[nodiscard]] M2StaticInstancingProfile QueryStaticInstancingProfile(std::uint32_t instance_id);

  [[nodiscard]] bool QueryOpaqueDepthTestGuaranteed(std::uint32_t instance_id);

  [[nodiscard]] M2RenderInstanceResult RenderInstancedGroup(
      std::uint16_t view_id, std::uint32_t exemplar_instance_id,
      std::span<const M2InstancedDrawRecord> records,
      const M2BatchUniforms& base_uniforms, bgfx::Encoder* encoder = nullptr);

  void SetProjectedTexturesEnabled(bool enabled);

  [[nodiscard]] std::vector<M2ProjectedTextureDraw> TakeProjectedTextureDraws();

 private:
  [[nodiscard]] M2RenderInstanceResult RenderLocked(
      std::uint16_t view_id, std::uint32_t instance_id,
      detail::M2Instance& instance,
      std::optional<RenderMatrix4x4View> view_matrix,
      detail::M2ModelResource& resource, M2RenderPassScope pass_scope,
      bgfx::Encoder* encoder,
      std::optional<std::uint32_t> transparent_draw_ordinal);

  struct ProjectedTextureQueueOrder {
    std::uint16_t view_id{0};
    bool has_ordinal{false};
    std::uint32_t ordinal{0};
  };

  void QueueProjectedTextureDraws(
      std::uint32_t instance_id, const detail::M2Instance& instance,
      const detail::M2ModelResource& resource,
      const std::vector<bgfx::TextureHandle>& textures,
      const RenderMatrix4x4& model_matrix, const M2Animator* animator,
      int animation_index, std::uint32_t animation_time_ms,
      std::span<const float> bone_matrices, ProjectedTextureQueueOrder queue_order);

  void SortProjectedTextureQueue();

  struct ResolvedInstanceTextures {
    M2ResultStatus status = M2ResultStatus::kReady;

    const std::vector<bgfx::TextureHandle>* textures = nullptr;
  };

  [[nodiscard]] ResolvedInstanceTextures ResolveInstanceTextures(
      detail::M2Instance& instance, const detail::M2ModelResource& resource,
      std::vector<bgfx::TextureHandle>* override_scratch);

  M2SystemMutex& mutex_;
  M2ModelRepository::ModelMap& models_;
  M2InstanceStore& instances_;
  M2AnimationRuntime& animation_runtime_;
  TextureManager*& texture_manager_;
  M2GeometrySubmitter geometry_submitter_;
  M2EffectRenderer effect_renderer_;
  std::optional<RenderSubmitTraceBinding> submit_trace_;

  std::uint64_t effect_update_frame_{1u};

  struct EffectSimulationTarget {
    detail::M2Instance* instance;
    detail::M2ModelResource* resource;
  };
  std::vector<EffectSimulationTarget> effect_simulation_scratch_;

  bool projected_textures_enabled_{false};

  std::mutex projected_queue_mutex_;
  std::vector<M2ProjectedTextureDraw> pending_projected_texture_draws_;
  std::vector<ProjectedTextureQueueOrder> pending_projected_texture_queue_order_;
};

}

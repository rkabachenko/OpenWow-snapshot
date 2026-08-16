#include "openwow/render/m2/m2_renderer.h"

#include <cassert>
#include <thread>
#include <vector>
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/backend/bgfx/bgfx_encoder_ledger.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_animation_runtime.h"
#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_camera_math.h"
#include "openwow/render/m2/m2_cpu_skinning.h"
#include "openwow/render/m2/m2_gpu_resources.h"
#include "openwow/render/m2/m2_instance_store.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_spatial_queries.h"
#include "openwow/render/m2/m2_submit_trace.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/runtime/scheduling/frame_job_system.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>

namespace openwow::render::m2 {
namespace {

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

[[nodiscard]] M2RenderInstanceResult WithDrawCounts(
    M2RenderInstanceResult result, const std::uint32_t draw_count,
    const std::uint32_t geometry_draw_count) noexcept {
  result.submitted_draw_count = draw_count;
  result.submitted_geometry_draw_count = geometry_draw_count;
  return result;
}

const std::vector<float> kIdentityBoneMatrixPalette(
    kRenderIdentityMatrix4x4.begin(), kRenderIdentityMatrix4x4.end());

[[nodiscard]] bool M2PoseNeedsCameraBasis(
    const detail::M2ModelResource& resource,
    const std::optional<RenderMatrix4x4View>& view_matrix) noexcept {
  return view_matrix.has_value() && resource.has_billboard_bones;
}

}

M2Renderer::M2Renderer(M2SystemMutex& mutex,
                       M2ModelRepository::ModelMap& models,
                       M2InstanceStore& instances,
                       M2AnimationRuntime& animation_runtime,
                       M2GpuResources& gpu_resources,
                       TextureManager*& texture_manager)
    : mutex_(mutex),
      models_(models),
      instances_(instances),
      animation_runtime_(animation_runtime),
      texture_manager_(texture_manager),
      geometry_submitter_(gpu_resources) {}

void M2Renderer::SetParticleDensity(const float density) noexcept {
  effect_renderer_.SetParticleDensity(density);
}

void M2Renderer::Shutdown() { effect_renderer_.Shutdown(); }

void M2Renderer::BindSubmitTrace(
    std::optional<RenderSubmitTraceBinding> binding) {
  std::lock_guard lock(mutex_);
  submit_trace_ = std::move(binding);
}

bool M2Renderer::IsSubmitTraceBound() const {
  std::lock_guard lock(mutex_);
  return submit_trace_.has_value();
}

constexpr double kM2EffectSimulationMicroseconds = 2.0;

void M2Renderer::UpdateAllEffects(
    const float frame_delta_seconds,
    const std::optional<RenderMatrix4x4View> view_matrix,
    core::FrameJobSystem* const jobs) {
  std::lock_guard lock(mutex_);

  ++effect_update_frame_;

  effect_simulation_scratch_.clear();
  for (const auto& [instance_id, instance_ptr] : instances_.effect_carriers()) {
    static_cast<void>(instance_id);
    auto& instance = *instance_ptr;

    if (instance.last_rendered_effect_frame + 1u != effect_update_frame_) {
      continue;
    }
    const auto model_it = models_.find(instance.model_id);
    if (model_it == models_.end() || !model_it->second->loaded) {
      continue;
    }
    auto& resource = *model_it->second;

    if (resource.model_data.particle_emitters.empty() &&
        resource.model_data.ribbon_emitters.empty()) {
      continue;
    }
    effect_simulation_scratch_.push_back(
        EffectSimulationTarget{.instance = &instance, .resource = &resource});
  }

  const auto simulate = [&](const EffectSimulationTarget& target) {
    detail::M2Instance& instance = *target.instance;
    detail::M2ModelResource& resource = *target.resource;

    const RenderMatrix4x4 model_matrix = ComputeM2ModelMatrix(instance);
    const int animation_index = ResolveM2RenderableAnimationIndex(instance);
    const auto animation_time_ms =
        static_cast<std::uint32_t>(instance.animation_time * 1000.0f);

    std::optional<RenderMatrix4x4View> camera_inverse_view;
    RenderMatrix4x4 camera_inverse_model_view_rotation{kRenderIdentityMatrix4x4};
    if (M2PoseNeedsCameraBasis(resource, view_matrix)) {
      camera_inverse_model_view_rotation =
          BuildM2BillboardInverseModelViewRotation(model_matrix, *view_matrix);
      camera_inverse_view.emplace(camera_inverse_model_view_rotation);
    }

    const std::vector<float>* bone_matrix_vector = &kIdentityBoneMatrixPalette;
    if (resource.has_bones) {

      const auto* const sampled =
          SampleM2InstanceBoneMatricesCached(instance, resource, camera_inverse_view);
      if (sampled == nullptr) {
        return;
      }
      bone_matrix_vector = sampled;
    }

    effect_renderer_.SimulateEffects(
        instance, resource, model_matrix, view_matrix, animation_index,
        animation_time_ms, frame_delta_seconds, *bone_matrix_vector,
        bone_matrix_vector->size() / 16u);
  };

  const bool jobs_usable = jobs != nullptr && jobs->WorkerCount() > 0u &&
                           !core::FrameJobSystem::IsCurrentThreadWorker();
  if (!jobs_usable ||
      effect_simulation_scratch_.size() <
          core::ParallelDispatchBreakEven(jobs->WorkerCount() + 1u,
                                          kM2EffectSimulationMicroseconds)) {
    for (const EffectSimulationTarget& target : effect_simulation_scratch_) {
      simulate(target);
    }
    return;
  }

  jobs->ParallelFor(effect_simulation_scratch_.size(),
                    [&](const std::size_t begin, const std::size_t end) {
                      for (std::size_t index = begin; index < end; ++index) {
                        simulate(effect_simulation_scratch_[index]);
                      }
                    });
}

M2ShadowClassQuery M2Renderer::QueryShadowClass(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(instance_id)};
  }
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" +
                      std::to_string(instance_it->second->model_id)};
  }
  if (!model_it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kMissingFile,
            .detail = model_it->second->model_path};
  }
  return geometry_submitter_.QueryShadowClass(
      instance_id, *instance_it->second, *model_it->second);
}

M2ShadowDrawCallLists M2Renderer::BuildShadowDrawCalls(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    return {.status = M2ResultStatus::kFailed};
  }
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end()) {
    return {.status = M2ResultStatus::kFailed};
  }
  if (!model_it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady};
  }
  return geometry_submitter_.BuildShadowDrawCalls(
      instance_id, *instance_it->second, *model_it->second);
}

M2RenderInstanceResult M2Renderer::Render(
    const std::uint16_t view_id, const std::uint32_t instance_id,
    const std::optional<RenderMatrix4x4View> view_matrix,
    const M2RenderPassScope pass_scope, bgfx::Encoder* const encoder) {

  const std::span<const std::uint32_t> bound_ordinals =
      M2TransparentDrawOrdinalScope::Current();
  assert((bound_ordinals.empty() || bound_ordinals.size() == 1u) &&
         "M2Renderer::Render: the bound transparent draw order must hold "
         "exactly one ordinal for a single-instance render");
  const std::optional<std::uint32_t> transparent_draw_ordinal =
      bound_ordinals.size() == 1u ? std::optional<std::uint32_t>{bound_ordinals[0]}
                                  : std::nullopt;

  if (!core::FrameJobSystem::IsCurrentThreadWorker()) {
    animation_runtime_.PumpSequenceLoads();
  }
  std::lock_guard lock(mutex_);
  return RenderUnlocked(view_id, instance_id, view_matrix, pass_scope, encoder,
                        transparent_draw_ordinal);
}

M2RenderInstanceResult M2Renderer::RenderUnlocked(
    const std::uint16_t view_id, const std::uint32_t instance_id,
    const std::optional<RenderMatrix4x4View> view_matrix,
    const M2RenderPassScope pass_scope, bgfx::Encoder* const encoder,
    const std::optional<std::uint32_t> transparent_draw_ordinal) {
  const auto trace_status = [&](const std::string_view model_path,
                                const std::uint32_t skin_profile,
                                const std::string_view pass,
                                const std::string_view pipeline,
                                const M2ResultStatus status,
                                const M2ResultReason reason, std::string detail) {
    RecordM2StatusTrace(submit_trace_, view_id, model_path, skin_profile, instance_id,
                        pass, pipeline, "none", status, reason, std::move(detail));
  };

  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    const std::string detail = "instance_id=" + std::to_string(instance_id);
    trace_status({}, 0u, "m2.reject", "m2.instance", M2ResultStatus::kFailed,
                 M2ResultReason::kInvalidHandle, detail);
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle, .detail = detail};
  }
  auto& instance = *instance_it->second;
  if (!instance.visible) {

    const auto model_it = models_.find(instance.model_id);
    const std::string_view model_path =
        model_it != models_.end()
            ? std::string_view{model_it->second->model_path}
            : std::string_view{};
    const std::uint32_t skin_profile =
        model_it != models_.end() ? model_it->second->selected_skin_profile : 0u;
    const std::string detail = "instance_id=" + std::to_string(instance_id);
    trace_status(model_path, skin_profile, "m2.skip", "m2.visibility",
                 M2ResultStatus::kNotReady, M2ResultReason::kNotVisible, detail);
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kNotVisible, .detail = detail};
  }
  const auto model_it = models_.find(instance.model_id);
  if (model_it == models_.end()) {
    const std::string detail = "model_id=" + std::to_string(instance.model_id);
    trace_status({}, 0u, "m2.reject", "m2.instance", M2ResultStatus::kFailed,
                 M2ResultReason::kInvalidHandle, detail);
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle, .detail = detail};
  }
  auto& resource = *model_it->second;
  if (!resource.loaded) {
    trace_status(resource.model_path, resource.selected_skin_profile, "m2.reject",
                 "m2.resource", M2ResultStatus::kNotReady,
                 M2ResultReason::kMissingFile, resource.model_path);
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kMissingFile, .detail = resource.model_path};
  }
  return RenderLocked(view_id, instance_id, instance, view_matrix, resource,
                      pass_scope, encoder, transparent_draw_ordinal);
}

void M2Renderer::RenderBatch(const std::uint16_t view_id,
                             const std::span<const std::uint32_t> instance_ids,
                             const std::optional<RenderMatrix4x4View> view_matrix,
                             const M2RenderPassScope pass_scope,
                             core::FrameJobSystem* const jobs,
                             const double per_instance_microseconds,
                             const std::span<M2RenderInstanceResult> out_results,
                             bgfx::Encoder* const serial_encoder,
                             const std::span<const std::uint32_t> explicit_draw_ordinals) {

  if (!core::FrameJobSystem::IsCurrentThreadWorker()) {
    animation_runtime_.PumpSequenceLoads();
  }
  const std::size_t n = instance_ids.size();

  const std::span<const std::uint32_t> transparent_draw_ordinals =
      !explicit_draw_ordinals.empty() ? explicit_draw_ordinals
                                      : M2TransparentDrawOrdinalScope::Current();
  assert((transparent_draw_ordinals.empty() || transparent_draw_ordinals.size() == n) &&
         "M2Renderer::RenderBatch: transparent_draw_ordinals must be empty or "
         "parallel to instance_ids");
  const bool has_draw_order =
      !transparent_draw_ordinals.empty() && transparent_draw_ordinals.size() == n;
  const auto ordinal_of = [&](const std::size_t i) -> std::optional<std::uint32_t> {
    if (!has_draw_order) {
      return std::nullopt;
    }
    return transparent_draw_ordinals[i];
  };
  std::lock_guard lock(mutex_);

  const bool trace_bound = submit_trace_.has_value();

  const bool includes_transparent = M2RenderPassScopeIncludesTransparent(pass_scope);
  const bool transparent_without_draw_order = includes_transparent && !has_draw_order;

  const bool on_worker_thread = core::FrameJobSystem::IsCurrentThreadWorker();
  const bool jobs_usable = jobs != nullptr && jobs->WorkerCount() > 0;

  const bool parallel_forbidden =
      trace_bound || transparent_without_draw_order || on_worker_thread || !jobs_usable;

  const std::size_t participants =
      jobs_usable ? static_cast<std::size_t>(jobs->WorkerCount()) + 1u : 0u;
  const bool batch_too_small_for_full_pool =
      n < core::ParallelDispatchBreakEven(static_cast<std::uint32_t>(participants),
                                          per_instance_microseconds);
  const std::uint32_t reserved_encoders =
      parallel_forbidden || batch_too_small_for_full_pool
          ? 0u
          : ReserveBgfxFrameEncoders(static_cast<std::uint32_t>(
                std::min<std::size_t>(n, participants)));
  const std::size_t participant_slots = reserved_encoders;
  const bool batch_too_small =
      batch_too_small_for_full_pool ||
      n < core::ParallelDispatchBreakEven(static_cast<std::uint32_t>(participant_slots),
                                          per_instance_microseconds);

  if (parallel_forbidden || batch_too_small) {

    ReleaseBgfxFrameEncoders(reserved_encoders);
    for (std::size_t i = 0; i < n; ++i) {
      out_results[i] = RenderUnlocked(view_id, instance_ids[i], view_matrix, pass_scope,
                                      serial_encoder, ordinal_of(i));
    }
    return;
  }

  std::mutex unencoded_mutex;
  std::vector<std::pair<std::size_t, std::size_t>> unencoded_ranges;

  std::atomic<std::uint32_t> encoders_spent{0};

  assert(([&] {
    std::vector<std::uint32_t> sorted(instance_ids.begin(), instance_ids.end());
    std::sort(sorted.begin(), sorted.end());
    return std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
  }()) && "M2Renderer::RenderBatch: duplicate instance id in one batch");

  const std::thread::id calling_thread = std::this_thread::get_id();

  constexpr std::size_t kInstanceGrainsPerParticipant = 16;
  const std::size_t instance_grain = std::max<std::size_t>(
      1, n / (participant_slots * kInstanceGrainsPerParticipant));
  std::atomic<std::size_t> instance_cursor{0};

  const auto claim_instances = [&](std::size_t& lo, std::size_t& hi) {
    lo = instance_cursor.fetch_add(instance_grain, std::memory_order_relaxed);
    if (lo >= n) {
      return false;
    }
    hi = std::min(lo + instance_grain, n);
    return true;
  };

  jobs->ParallelFor(participant_slots, [&](const std::size_t, const std::size_t) {

    if (instance_cursor.load(std::memory_order_relaxed) >= n) {
      return;
    }

    const bool on_calling_thread = std::this_thread::get_id() == calling_thread;
    bgfx::Encoder* encoder = nullptr;
    if (!on_calling_thread) {
      encoders_spent.fetch_add(1u, std::memory_order_relaxed);
      encoder = bgfx::begin(true);
    }
    if (on_calling_thread || encoder != nullptr) {
      std::size_t lo = 0;
      std::size_t hi = 0;
      while (claim_instances(lo, hi)) {
        for (std::size_t i = lo; i < hi; ++i) {
          out_results[i] = RenderUnlocked(view_id, instance_ids[i], view_matrix,
                                          pass_scope, encoder, ordinal_of(i));
        }
      }
      if (encoder != nullptr) {
        bgfx::end(encoder);
      }
      return;
    }

    std::lock_guard<std::mutex> unencoded_lock(unencoded_mutex);
    std::size_t lo = 0;
    std::size_t hi = 0;
    while (claim_instances(lo, hi)) {
      unencoded_ranges.emplace_back(lo, hi);
    }
  });

  const std::uint32_t spent = encoders_spent.load(std::memory_order_relaxed);
  ReleaseBgfxFrameEncoders(reserved_encoders > spent ? reserved_encoders - spent : 0u);

  if (!unencoded_ranges.empty()) {

    diagnostics::Log(diagnostics::LogLevel::kDebug,
                     "M2Renderer::RenderBatch: bgfx::begin(true) failed on a "
                     "ledger-reserved slot; replaying " +
                         std::to_string(unencoded_ranges.size()) +
                         " slice(s) serially on the API thread");
    for (const auto& [begin, end] : unencoded_ranges) {
      for (std::size_t i = begin; i < end; ++i) {
        out_results[i] = RenderUnlocked(view_id, instance_ids[i], view_matrix,
                                        pass_scope, nullptr, ordinal_of(i));
      }
    }
  }

}

namespace {

template <typename T>
[[nodiscard]] bool IsM2TrackStaticForInstancing(
    const data::model::M2Track<T>& track) noexcept {
  if (track.SetCount() == 0u) {
    return true;
  }
  return track.SetCount() == 1u && track.SetValues(0u).size() <= 1u;
}

[[nodiscard]] M2StaticInstancingProfile ComputeStaticInstancingProfile(
    const detail::M2ModelResource& resource) {
  if (!(bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)) {
    return {};
  }
  const auto& model = resource.model_data;
  if (model.bones.size() > kM2StaticInstancingMaxBones ||
      resource.has_billboard_bones) {
    return {};
  }

  for (const auto& bone : model.bones) {
    if (!IsM2TrackStaticForInstancing(bone.translation) ||
        !IsM2TrackStaticForInstancing(bone.rotation) ||
        !IsM2TrackStaticForInstancing(bone.scaling)) {
      return {};
    }
  }

  M2StaticInstancingProfile profile{};
  profile.has_transparent_residue = !model.particle_emitters.empty() ||
                                    !model.ribbon_emitters.empty() ||
                                    !resource.projected_batches.empty();
  if (resource.render_batches.empty()) {
    return {};
  }
  const auto track_static_for_batch =
      [&](const detail::M2ModelResource::RenderBatch& batch,
          const data::model::SkinTextureUnit& texture_unit) {
        if (batch.uv_animation0_index >= 0 &&
            static_cast<std::size_t>(batch.uv_animation0_index) <
                model.uv_animations.size()) {
          const auto& uv = model.uv_animations[static_cast<std::size_t>(
              batch.uv_animation0_index)];
          if (!IsM2TrackStaticForInstancing(uv.translation) ||
              !IsM2TrackStaticForInstancing(uv.rotation) ||
              !IsM2TrackStaticForInstancing(uv.scaling)) {
            return false;
          }
        }
        if (batch.uv_animation1_index >= 0 &&
            static_cast<std::size_t>(batch.uv_animation1_index) <
                model.uv_animations.size()) {
          const auto& uv = model.uv_animations[static_cast<std::size_t>(
              batch.uv_animation1_index)];
          if (!IsM2TrackStaticForInstancing(uv.translation) ||
              !IsM2TrackStaticForInstancing(uv.rotation) ||
              !IsM2TrackStaticForInstancing(uv.scaling)) {
            return false;
          }
        }
        if (static_cast<std::size_t>(texture_unit.color_index) <
            model.colors.size()) {
          const auto& color =
              model.colors[static_cast<std::size_t>(texture_unit.color_index)];
          if (!IsM2TrackStaticForInstancing(color.color) ||
              !IsM2TrackStaticForInstancing(color.alpha)) {
            return false;
          }
        }
        std::size_t resolved_transparency =
            static_cast<std::size_t>(texture_unit.transparency_index);
        if (resolved_transparency < model.transparency_lookup.size()) {
          resolved_transparency = static_cast<std::size_t>(
              model.transparency_lookup[resolved_transparency]);
        }
        if (resolved_transparency < model.transparencies.size() &&
            !IsM2TrackStaticForInstancing(
                model.transparencies[resolved_transparency].alpha)) {
          return false;
        }
        return true;
      };

  bool any_opaque = false;
  M2Animator animator(&resource.model_data);
  for (const auto& batch : resource.render_batches) {
    if (batch.texture_unit_index >= resource.skin_data.texture_units.size()) {
      return {};
    }
    const auto& texture_unit =
        resource.skin_data.texture_units[batch.texture_unit_index];

    if (static_cast<std::uint32_t>(batch.blend_mode) >
        static_cast<std::uint32_t>(M2BlendMode::AlphaKey)) {
      profile.has_transparent_residue = true;
      continue;
    }

    if (!track_static_for_batch(batch, texture_unit)) {
      return {};
    }
    const auto sample = SampleM2TextureUnitMaterial(
        resource.model_data, texture_unit, &animator, 0,
        0u, 1.0f, std::nullopt);
    if (BatchBelongsToOpaqueList(batch.blend_mode, sample.color[3])) {
      any_opaque = true;
    } else {
      profile.has_transparent_residue = true;
    }
  }
  profile.opaque_instanceable = any_opaque;
  return profile;
}

[[nodiscard]] const M2StaticInstancingProfile& ResolveStaticInstancingProfile(
    detail::M2ModelResource& resource) {
  if (!resource.static_instancing_cache.has_value()) {
    resource.static_instancing_cache = ComputeStaticInstancingProfile(resource);
  }
  return *resource.static_instancing_cache;
}

[[nodiscard]] bool ComputeOpaqueDepthTestGuaranteed(
    const detail::M2ModelResource& resource) {
  for (const auto& batch : resource.render_batches) {
    if (static_cast<std::uint32_t>(batch.blend_mode) >
        static_cast<std::uint32_t>(M2BlendMode::AlphaKey)) {
      continue;

    }
    const auto state =
        ResolveM2RenderState(batch.blend_mode, batch.material_flags);
    if (!state.has_value()) {
      continue;
    }
    const std::uint64_t depth_test = *state & BGFX_STATE_DEPTH_TEST_MASK;
    if (depth_test != BGFX_STATE_DEPTH_TEST_LESS &&
        depth_test != BGFX_STATE_DEPTH_TEST_LEQUAL) {
      return false;
    }
  }
  return true;
}

}

M2StaticInstancingProfile M2Renderer::QueryStaticInstancingProfile(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    return {};
  }
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {
    return {};
  }
  return ResolveStaticInstancingProfile(*model_it->second);
}

bool M2Renderer::QueryOpaqueDepthTestGuaranteed(const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) {
    return false;
  }
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {

    return false;
  }
  auto& resource = *model_it->second;
  if (!resource.opaque_depth_gate_cache.has_value()) {
    resource.opaque_depth_gate_cache =
        ComputeOpaqueDepthTestGuaranteed(resource);
  }
  return *resource.opaque_depth_gate_cache;
}

M2RenderInstanceResult M2Renderer::RenderInstancedGroup(
    const std::uint16_t view_id, const std::uint32_t exemplar_instance_id,
    const std::span<const M2InstancedDrawRecord> records,
    const M2BatchUniforms& base_uniforms, bgfx::Encoder* const encoder) {
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(exemplar_instance_id);
  if (instance_it == instances_.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "instance_id=" + std::to_string(exemplar_instance_id)};
  }
  auto& instance = *instance_it->second;
  const auto model_it = models_.find(instance.model_id);
  if (model_it == models_.end() || !model_it->second->loaded) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "model_id=" + std::to_string(instance.model_id)};
  }
  auto& resource = *model_it->second;
  if (!ResolveStaticInstancingProfile(resource).opaque_instanceable) {
    return {.status = M2ResultStatus::kUnsupported,
            .reason = M2ResultReason::kNoDrawableGeometry,
            .detail = resource.model_path};
  }

  instance.last_rendered_effect_frame = effect_update_frame_;

  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const auto animation_time_ms =
      static_cast<std::uint32_t>(instance.animation_time * 1000.0f);
  M2Animator animator(&resource.model_data);
  const bool has_animator = !resource.model_data.bones.empty() ||
                            M2ModelNeedsMaterialAnimator(resource.model_data);

  std::span<const float> bone_matrices;
  if (resource.has_bones) {
    const auto* const sampled =
        SampleM2InstanceBoneMatricesCached(instance, resource, std::nullopt);
    if (sampled == nullptr) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kBonePoseFailed,
              .detail = resource.model_path};
    }
    bone_matrices = *sampled;
  } else {
    bone_matrices = kIdentityBoneMatrixPalette;
  }

  return geometry_submitter_.SubmitInstanced(
      view_id, resource.skinned_mesh.get(), resource.gpu_textures,
      resource.model_data, resource.skin_data, resource.render_batches,
      records, &base_uniforms, has_animator ? &animator : nullptr,
      animation_index, animation_time_ms, bone_matrices, resource.model_path,
      M2DrawEncoder{encoder});
}

M2RenderInstanceResult M2Renderer::RenderLocked(
    const std::uint16_t view_id, const std::uint32_t instance_id,
    detail::M2Instance& instance,
    const std::optional<RenderMatrix4x4View> view_matrix,
    detail::M2ModelResource& resource, const M2RenderPassScope pass_scope,
    bgfx::Encoder* const encoder,
    const std::optional<std::uint32_t> transparent_draw_ordinal) {

  instance.last_rendered_effect_frame = effect_update_frame_;

  std::optional<M2InstanceDrawSortDepth> sort_depth;
  if (transparent_draw_ordinal.has_value() &&
      M2RenderPassScopeIncludesTransparent(pass_scope)) {
    sort_depth.emplace(*transparent_draw_ordinal);
  }
  const M2DrawEncoder draw(encoder, sort_depth.has_value() ? &*sort_depth : nullptr);
  const auto trace_status = [&](const std::string_view pipeline,
                                const std::string_view skinning_mode,
                                const M2ResultStatus status,
                                const M2ResultReason reason, std::string detail) {
    RecordM2StatusTrace(submit_trace_, view_id, resource.model_path,
                        resource.selected_skin_profile, instance_id, "m2.reject",
                        pipeline, skinning_mode, status, reason, std::move(detail));
  };

  const RenderMatrix4x4 model_matrix = ComputeM2ModelMatrix(instance);
  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const auto animation_time_ms =
      static_cast<std::uint32_t>(instance.animation_time * 1000.0f);
  M2Animator animator(&resource.model_data);
  const bool has_animator = !resource.model_data.bones.empty() ||
                            M2ModelNeedsMaterialAnimator(resource.model_data);

  RenderMatrix4x4 camera_inverse_model_view_rotation{kRenderIdentityMatrix4x4};
  std::optional<RenderMatrix4x4View> camera_inverse_view;
  if (M2PoseNeedsCameraBasis(resource, view_matrix)) {
    camera_inverse_model_view_rotation =
        BuildM2BillboardInverseModelViewRotation(model_matrix, *view_matrix);
    camera_inverse_view.emplace(camera_inverse_model_view_rotation);
  }

  const std::vector<float>* bone_matrix_vector = nullptr;
  std::span<const float> bone_matrices;
  if (resource.has_bones) {

    if (resource.HasRenderMaterialData() &&
        (!resource.skinned_mesh || !resource.skinned_mesh->skinned_ok())) {
      trace_status("m2.skinning", "gpu", M2ResultStatus::kUnsupported,
                   M2ResultReason::kGpuGeometryNotReady, resource.model_path);
    }
    const auto* const sampled = SampleM2InstanceBoneMatricesCached(
        instance, resource, camera_inverse_view);
    if (sampled == nullptr) {
      trace_status("m2.pose", "gpu", M2ResultStatus::kFailed,
                   M2ResultReason::kBonePoseFailed, resource.model_path);
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kBonePoseFailed,
              .detail = resource.model_path};
    }
    bone_matrix_vector = sampled;
    bone_matrices = *bone_matrix_vector;
  } else {
    bone_matrix_vector = &kIdentityBoneMatrixPalette;
    bone_matrices = *bone_matrix_vector;
  }

  std::vector<bgfx::TextureHandle> override_textures;
  const ResolvedInstanceTextures resolved =
      ResolveInstanceTextures(instance, resource, &override_textures);
  const M2ResultStatus texture_status = resolved.status;
  if (texture_status != M2ResultStatus::kReady) {
    const M2ResultReason reason = texture_status == M2ResultStatus::kNotReady
                                      ? M2ResultReason::kTextureNotReady
                                      : M2ResultReason::kMissingTexture;
    trace_status("m2.texture", "gpu", texture_status, reason, resource.model_path);
    return {.status = texture_status, .reason = reason, .detail = resource.model_path};
  }
  const std::vector<bgfx::TextureHandle>& textures = *resolved.textures;

  const auto* visible_submeshes = instance.has_visible_submesh_filter
                                      ? &instance.visible_submesh_indices
                                      : nullptr;
  M2RenderInstanceResult geometry_result = geometry_submitter_.Submit(
      view_id, resource.skinned_mesh.get(), textures, resource.model_data,
      resource.skin_data, resource.render_batches, model_matrix, instance.tint_color,
      instance.alpha, visible_submeshes,
      detail::BatchUniformsOf(instance), pass_scope,
      M2DrawBatchScope::kAllTextureUnits, has_animator ? &animator : nullptr,
      animation_index, animation_time_ms, bone_matrices, view_matrix, submit_trace_,
      resource.model_path, resource.selected_skin_profile, instance_id, draw);

  M2RenderInstanceResult effect_result{.status = M2ResultStatus::kReady};
  if (M2RenderPassScopeIncludesTransparent(pass_scope)) {
    effect_result = effect_renderer_.Submit(
        view_id, instance_id, instance, resource, textures, model_matrix, view_matrix,
        animation_index, animation_time_ms, *bone_matrix_vector, submit_trace_, draw);
  }

  if (M2RenderPassScopeIncludesTransparent(pass_scope) &&
      projected_textures_enabled_ && !resource.projected_batches.empty()) {
    QueueProjectedTextureDraws(
        instance_id, instance, resource, textures, model_matrix,
        has_animator ? &animator : nullptr, animation_index, animation_time_ms,
        bone_matrices,
        ProjectedTextureQueueOrder{
            .view_id = view_id,
            .has_ordinal = transparent_draw_ordinal.has_value(),
            .ordinal = transparent_draw_ordinal.value_or(0u)});
  }
  const std::uint32_t draw_count = geometry_result.submitted_draw_count +
                                   effect_result.submitted_draw_count;

  const M2RenderInstanceResult* terminal = nullptr;
  if (draw_count == 0u) {
    terminal = SelectTerminalResult(geometry_result, effect_result);
    if (terminal == &geometry_result &&
        geometry_result.status == M2ResultStatus::kUnsupported &&
        resource.HasEffectData()) {
      terminal = M2RenderResultBlocksComposite(effect_result, geometry_result)
                     ? &effect_result
                     : nullptr;
    }
  }
  if (terminal != nullptr) {
    return WithDrawCounts(*terminal, draw_count,
                          geometry_result.submitted_draw_count);
  }
  if (draw_count > 0u) {
    return {.status = M2ResultStatus::kReady,
            .submitted_draw_count = draw_count,
            .submitted_geometry_draw_count = geometry_result.submitted_draw_count};
  }
  if (pass_scope != M2RenderPassScope::kAll &&
      geometry_result.reason == M2ResultReason::kNoDrawableGeometry &&
      (!M2RenderPassScopeIncludesTransparent(pass_scope) ||
       effect_result.reason == M2ResultReason::kNoDrawableGeometry ||
       effect_result.status == M2ResultStatus::kReady)) {
    return {.status = M2ResultStatus::kReady};
  }
  if (geometry_result.status == M2ResultStatus::kUnsupported &&
      effect_result.status == M2ResultStatus::kUnsupported) {
    return {.status = M2ResultStatus::kUnsupported,
            .reason = geometry_result.reason != M2ResultReason::kNone
                          ? geometry_result.reason
                          : effect_result.reason,
            .detail = !geometry_result.detail.empty() ? geometry_result.detail
                                                       : effect_result.detail};
  }
  if (geometry_result.status == M2ResultStatus::kUnsupported &&
      resource.model_data.particle_emitters.empty() &&
      resource.model_data.ribbon_emitters.empty()) {
    return geometry_result;
  }
  return {.status = M2ResultStatus::kNotReady,
          .reason = geometry_result.reason != M2ResultReason::kNone
                        ? geometry_result.reason
                        : effect_result.reason,
          .detail = !geometry_result.detail.empty() ? geometry_result.detail
                                                     : effect_result.detail};
}

M2Renderer::ResolvedInstanceTextures M2Renderer::ResolveInstanceTextures(
    detail::M2Instance& instance, const detail::M2ModelResource& resource,
    std::vector<bgfx::TextureHandle>* const override_scratch) {
  const data::model::M2Model& model = resource.model_data;

  if (instance.texture_overrides.empty() || model.textures.empty()) {
    return {.status = M2ResultStatus::kReady, .textures = &resource.gpu_textures};
  }
  std::vector<bgfx::TextureHandle>& textures = *override_scratch;
  textures = resource.gpu_textures;
  const std::size_t texture_count = std::min(model.textures.size(), textures.size());
  for (auto& override : instance.texture_overrides) {
    bool matched = false;
    for (std::size_t index = 0; index < texture_count; ++index) {
      matched = matched || model.textures[index].type == override.type_id;
    }
    if (!matched) {
      continue;
    }
    bgfx::TextureHandle handle = override.texture;
    if (!override.texture_path.empty() && !override.texture_lease) {
      override.texture_lease = texture_manager_ != nullptr
                                   ? texture_manager_->AcquireCachedTexture(
                                         override.texture_path)
                                   : TextureLease{};
      handle = BgfxTextureLeaseAccess::Get(override.texture_lease);
    }
    if (!bgfx::isValid(handle)) {
      return {.status = M2ResultStatus::kNotReady, .textures = nullptr};
    }
    override.texture = handle;
    for (std::size_t index = 0; index < texture_count; ++index) {
      if (model.textures[index].type == override.type_id) {
        textures[index] = handle;
      }
    }
  }
  return {.status = M2ResultStatus::kReady, .textures = &textures};
}

void M2Renderer::SetProjectedTexturesEnabled(const bool enabled) {
  std::lock_guard lock(mutex_);
  projected_textures_enabled_ = enabled;
  if (!enabled) {
    std::lock_guard<std::mutex> queue_lock(projected_queue_mutex_);
    pending_projected_texture_draws_.clear();
    pending_projected_texture_queue_order_.clear();
  }
}

std::vector<M2ProjectedTextureDraw> M2Renderer::TakeProjectedTextureDraws() {
  std::lock_guard lock(mutex_);
  std::lock_guard<std::mutex> queue_lock(projected_queue_mutex_);
  SortProjectedTextureQueue();
  std::vector<M2ProjectedTextureDraw> taken;
  taken.swap(pending_projected_texture_draws_);
  pending_projected_texture_queue_order_.clear();
  return taken;
}

void M2Renderer::SortProjectedTextureQueue() {
  auto& draws = pending_projected_texture_draws_;
  const auto& order = pending_projected_texture_queue_order_;
  assert(draws.size() == order.size());

  std::size_t run_begin = 0u;
  while (run_begin < draws.size()) {
    if (!order[run_begin].has_ordinal) {
      ++run_begin;
      continue;
    }
    std::size_t run_end = run_begin + 1u;
    while (run_end < draws.size() && order[run_end].has_ordinal &&
           order[run_end].view_id == order[run_begin].view_id) {
      ++run_end;
    }
    const std::size_t count = run_end - run_begin;
    if (count >= 2u) {

      std::vector<std::size_t> permutation(count);
      for (std::size_t i = 0; i < count; ++i) {
        permutation[i] = run_begin + i;
      }
      std::stable_sort(permutation.begin(), permutation.end(),
                       [&order](const std::size_t lhs, const std::size_t rhs) {
                         return order[lhs].ordinal < order[rhs].ordinal;
                       });
      std::vector<M2ProjectedTextureDraw> sorted_draws;
      sorted_draws.reserve(count);
      for (const std::size_t index : permutation) {
        sorted_draws.push_back(std::move(draws[index]));
      }
      std::move(sorted_draws.begin(), sorted_draws.end(), draws.begin() + run_begin);
    }
    run_begin = run_end;
  }
}

void M2Renderer::QueueProjectedTextureDraws(
    const std::uint32_t instance_id, const detail::M2Instance& instance,
    const detail::M2ModelResource& resource,
    const std::vector<bgfx::TextureHandle>& textures,
    const RenderMatrix4x4& model_matrix, const M2Animator* const animator,
    const int animation_index, const std::uint32_t animation_time_ms,
    const std::span<const float> bone_matrices,
    const ProjectedTextureQueueOrder queue_order) {

  constexpr float kProjectedTextureVerticalPad = 2.0f;

  constexpr float kProjectedTextureDegenerateArea = 2.3841858e-07f;

  constexpr float kProjectedTextureMinAlpha = 0.0001f;

  const auto& skin = resource.skin_data;
  const auto& vertices = resource.model_data.vertices;

  for (std::size_t batch_index = 0;
       batch_index < resource.projected_batches.size(); ++batch_index) {
    const auto& batch = resource.projected_batches[batch_index];

    if (batch.index_count < 4u ||
        batch.start_index + batch.index_count > skin.triangles.size()) {
      continue;
    }
    if (batch.texture_unit_index >= skin.texture_units.size()) {
      continue;
    }
    const auto& texture_unit = skin.texture_units[batch.texture_unit_index];
    const auto material_sample = SampleM2TextureUnitMaterial(
        resource.model_data, texture_unit, animator, animation_index,
        animation_time_ms, instance.alpha, instance.tint_color);
    if (material_sample.color[3] < kProjectedTextureMinAlpha) {
      continue;
    }
    const auto render_state =
        ResolveM2RenderState(batch.blend_mode, batch.material_flags);
    if (!render_state.has_value()) {
      continue;
    }
    const bgfx::TextureHandle texture =
        batch.texture0_index < textures.size()
            ? textures[batch.texture0_index]
            : bgfx::TextureHandle{bgfx::kInvalidHandle};
    if (!bgfx::isValid(texture)) {
      continue;
    }

    M2ProjectedTextureDraw draw;
    draw.instance_id = instance_id;
    draw.batch_index = static_cast<std::uint32_t>(batch_index);
    draw.color = {material_sample.color[0], material_sample.color[1],
                  material_sample.color[2], material_sample.color[3]};
    draw.render_state = *render_state;

    if (batch.texture0_index < resource.model_data.textures.size()) {
      const auto& texture_record =
          resource.model_data.textures[batch.texture0_index];
      if ((texture_record.flags & 0x1u) == 0u) {
        draw.sampler_flags |= BGFX_SAMPLER_U_CLAMP;
      }
      if ((texture_record.flags & 0x2u) == 0u) {
        draw.sampler_flags |= BGFX_SAMPLER_V_CLAMP;
      }
    } else {
      draw.sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    }
    draw.texture = texture;

    float min_x = std::numeric_limits<float>::max();
    float min_y = min_x;
    float min_z = min_x;
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = max_x;
    float max_z = max_x;
    std::array<std::array<float, 2>, 3> sample_xy{};
    std::array<std::array<float, 2>, 3> sample_uv{};

    std::vector<data::model::M2Vertex> batch_vertices;
    batch_vertices.reserve(batch.index_count);
    bool vertices_valid = true;
    for (std::uint32_t i = 0; i < batch.index_count; ++i) {
      const std::uint16_t vertex_index =
          skin.triangles[batch.start_index + i];
      if (vertex_index >= vertices.size()) {
        vertices_valid = false;
        break;
      }
      batch_vertices.push_back(vertices[vertex_index]);
    }
    if (!vertices_valid) {
      continue;
    }
    const auto posed =
        ComputeCpuSkinnedVertexStreams(batch_vertices, bone_matrices);
    for (std::uint32_t i = 0; i < batch.index_count; ++i) {
      const auto& local = posed.positions[i];
      const RenderVec3 world = TransformAffinePoint4x4(
          RenderVec3{local.x, local.y, local.z},
          RenderMatrix4x4View{model_matrix});
      min_x = std::min(min_x, world[0]);
      min_y = std::min(min_y, world[1]);
      min_z = std::min(min_z, world[2]);
      max_x = std::max(max_x, world[0]);
      max_y = std::max(max_y, world[1]);
      max_z = std::max(max_z, world[2]);
      if (i < 3u) {
        sample_xy[i] = {world[0], world[1]};
        sample_uv[i] = {batch_vertices[i].texcoord0[0],
                        batch_vertices[i].texcoord0[1]};
      }
    }

    const float edge1_x = sample_xy[1][0] - sample_xy[0][0];
    const float edge1_y = sample_xy[1][1] - sample_xy[0][1];
    const float edge2_x = sample_xy[2][0] - sample_xy[0][0];
    const float edge2_y = sample_xy[2][1] - sample_xy[0][1];
    const float doubled_area = edge1_x * edge2_y - edge1_y * edge2_x;
    if (std::fabs(doubled_area) < kProjectedTextureDegenerateArea) {
      continue;
    }
    const float inverse_area = 1.0f / doubled_area;
    const float du1 = sample_uv[1][0] - sample_uv[0][0];
    const float dv1 = sample_uv[1][1] - sample_uv[0][1];
    const float du2 = sample_uv[2][0] - sample_uv[0][0];
    const float dv2 = sample_uv[2][1] - sample_uv[0][1];
    draw.uv_jacobian = {
        (du1 * edge2_y - du2 * edge1_y) * inverse_area,
        (dv1 * edge2_y - dv2 * edge1_y) * inverse_area,
        (du2 * edge1_x - du1 * edge2_x) * inverse_area,
        (dv2 * edge1_x - dv1 * edge2_x) * inverse_area,
    };
    draw.reference_xy = {sample_xy[0][0], sample_xy[0][1]};
    draw.reference_uv = {sample_uv[0][0], sample_uv[0][1]};
    draw.world_bounds = {min_x, min_y, min_z - kProjectedTextureVerticalPad,
                         max_x, max_y, max_z + kProjectedTextureVerticalPad};

    std::lock_guard<std::mutex> queue_lock(projected_queue_mutex_);
    pending_projected_texture_draws_.push_back(draw);
    pending_projected_texture_queue_order_.push_back(queue_order);
  }
}

}

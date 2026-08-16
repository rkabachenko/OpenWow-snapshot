#include "openwow/render/m2/m2_system_state.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/m2/m2_model_preparation.h"
#include "openwow/render/m2/m2_render_validation.h"
#include "openwow/render/m2/m2_skin_profile.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/runtime/scheduling/frame_job_system.h"

#include <bgfx/bgfx.h>

#include <algorithm>

namespace openwow::render::m2 {
namespace {

void EraseModelPathRowLocked(
    std::unordered_map<std::string, std::uint32_t>& paths,
    const std::uint32_t model_id, const std::string& model_path) {
  const M2ModelIdentity identity = M2ModelRepository::Canonicalize(model_path);
  if (identity.valid) {
    if (const auto row = paths.find(identity.cache_key);
        row != paths.end() && row->second == model_id) {
      paths.erase(row);
      return;
    }
  }
  std::erase_if(paths,
                [model_id](const auto& entry) { return entry.second == model_id; });
}

}

M2SystemState::M2SystemState()
    : mutex(repository.mutex()),
      models(repository.models()),
      model_paths(repository.paths()),
      load_flights(repository.load_flights()),
      instances(mutex, models, texture_manager),
      model_queries(mutex, models, dbc),
      spatial_queries(mutex, models, instances, dbc),
      sequence_streamer(mutex, models, file_loader),
      animation_runtime(mutex, models, instances, dbc, sequence_streamer),
      frame_preparer(animation_runtime, model_queries, spatial_queries),
      renderer(mutex, models, instances, animation_runtime, gpu_resources,
               texture_manager),
      visibility(mutex, models, instances),
      resource_streamer(
          repository,
          {
              .prepare =
                  [this](const std::string& path,
                         const M2StreamFileLoader& loader,
                         const M2StreamTexturePreparer& texture_preparer,
                         const M2ModelEarlyReadyCallback& on_early_ready) {
                    const M2StreamModelPreparer prepare =
                        [this](const std::string& model_path,
                               const M2StreamFileLoader& snapshot,
                               const M2ModelEarlyReadyCallback& early_ready) {
                          return PrepareModel(model_path, true, snapshot,
                                             early_ready);
                        };
                    return PrepareM2ResourceBundle(prepare, path, loader,
                                                   texture_preparer, on_early_ready);
                  },
              .commit_texture = [this](PreparedTextureUpload texture) {
                return texture_manager != nullptr &&
                       bgfx::isValid(
                           texture_manager->CommitPreparedTexture(texture));
              },
              .commit_model = [this](std::unique_ptr<M2PreparedModel> model) {
                return CommitPreparedModel(std::move(model));
              },
              .recycle_model = [this](const std::uint32_t model) {
                return RecycleModelIfUnused(model);
              },
              .create_instance = [this](const std::uint32_t model) {
                return CreateSeatedInstance(model);
              },
          }) {
  instances.SetDestroyCallbackCollector(
      [this](detail::M2Instance& instance,
             const detail::M2ModelResource& resource,
             M2InstanceStore::DeferredCallbacks* callbacks) {
        animation_runtime.CollectTriggeredAnimationEventCallbacksLocked(instance, resource,
                                                         callbacks);
      });
}

M2SystemState::~M2SystemState() { instances.RevokeLeases(); }

bool M2SystemState::Initialize() {
  std::lock_guard lock(mutex);
  if (initialized) return true;
  sequence_streamer.Start();
  initialized = true;

  if (openwow::render::IsRendererContextActive()) {
    static_cast<void>(gpu_resources.EnsureShaders());

    static_cast<void>(renderer.WarmUpParticleProgram());
  }

  diagnostics::Log(diagnostics::LogLevel::kInfo, "M2System: initialized");
  return true;
}

void M2SystemState::Shutdown() {
  {
    std::lock_guard lock(mutex);
    DoReset();
    gpu_resources.Shutdown();
    renderer.Shutdown();
    initialized = false;
  }
  sequence_streamer.Shutdown();
}

void M2SystemState::ReleaseDeviceResources() {

  resource_streamer.ReleaseDeviceResources();
  Shutdown();
}

void M2SystemState::Reset() {
  std::lock_guard lock(mutex);
  DoReset();
}

void M2SystemState::DoReset() {
  sequence_streamer.ClearLocked();

  instances.ResetLocked();
  repository.Clear();
  visibility.Reset();
  skin_profile_quality = kM2SkinProfileFullQuality;
}

M2InstanceCreateResult M2SystemState::CreateSeatedInstance(
    const std::uint32_t model_id) {
  const auto created = instances.Create(model_id);
  if (created.status == M2ResultStatus::kReady && created.instance_id != 0u) {

    constexpr std::uint32_t kDefaultSeatAnimationId = 0u;
    static_cast<void>(animation_runtime.SetAnimation(
        created.instance_id, kDefaultSeatAnimationId, 1.0f));
  }
  return created;
}

M2ModelLoadResult M2SystemState::LoadModel(const std::string& path) {
  return LoadModelInternal(path, true);
}

M2ModelLoadResult M2SystemState::LoadModelForSampling(const std::string& path) {
  return LoadModelInternal(path, false);
}

M2ModelInstanceLoadResult M2SystemState::LoadModelInstance(
    const std::string& path) {
  const auto loaded = LoadModel(path);
  if (loaded.status != M2ResultStatus::kReady || loaded.model_id == 0u) {
    return {.status = loaded.status,
            .reason = loaded.reason != M2ResultReason::kNone
                          ? loaded.reason
                          : M2ResultReason::kInvalidHandle,
            .detail = loaded.detail.empty() ? path : loaded.detail,
            .model_id = loaded.model_id};
  }
  const auto created = CreateSeatedInstance(loaded.model_id);
  if (created.status != M2ResultStatus::kReady || created.instance_id == 0u) {
    return {.status = created.status,
            .reason = created.reason != M2ResultReason::kNone
                          ? created.reason
                          : M2ResultReason::kInvalidHandle,
            .detail = created.detail.empty() ? path : created.detail,
            .model_id = loaded.model_id,
            .instance_id = created.instance_id};
  }
  return {.status = M2ResultStatus::kReady,
          .model_id = loaded.model_id,
          .instance_id = created.instance_id};
}

M2ModelInstanceLoadResult M2SystemState::LoadModelInstanceWithFallback(
    const std::string& path, const std::string_view fallback) {
  auto result = LoadModelInstance(path);
  if (result.status == M2ResultStatus::kReady || fallback.empty()) return result;
  const auto original_status = result.status;
  const auto original_reason = result.reason;
  const std::string original_detail = result.detail.empty() ? path : result.detail;
  result = LoadModelInstance(std::string(fallback));
  if (result.status != M2ResultStatus::kReady) {
    return {.status = result.status,
            .reason = result.reason != M2ResultReason::kNone ? result.reason
                                                             : original_reason,
            .detail = "primary=" + original_detail + "; fallback=" +
                      (result.detail.empty() ? std::string(fallback)
                                             : result.detail),
            .model_id = result.model_id,
            .instance_id = result.instance_id};
  }
  result.used_fallback = true;
  result.detail =
      "primary_status=" + std::string(M2ResultStatusName(original_status)) +
      "; primary=" + original_detail;
  return result;
}

M2ModelInstanceLoadResult M2SystemState::LoadAttachedChildInstance(
    const std::uint32_t parent, const std::string& path,
    const std::int32_t slot, const M2ChildDestroyPolicy policy,
    const std::string_view fallback) {
  {
    std::lock_guard lock(mutex);
    if (parent == 0u || !instances.contains(parent)) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kInvalidHandle,
              .detail = "parent_instance_id=" + std::to_string(parent)};
    }
  }
  auto child = LoadModelInstanceWithFallback(path, fallback);
  if (child.status != M2ResultStatus::kReady) return child;
  const auto status = instances.AttachChild(parent, child.instance_id, slot, policy);
  if (status == M2ResultStatus::kReady) return child;
  (void)DestroyInstance(child.instance_id);
  return {.status = status,
          .reason = M2ResultReason::kInvalidHandle,
          .detail = "attach child_instance_id=" +
                    std::to_string(child.instance_id) +
                    " to parent_instance_id=" + std::to_string(parent),
          .model_id = child.model_id,
          .used_fallback = child.used_fallback};
}

M2ModelPrepareResult M2SystemState::PrepareModelForRender(
    const std::string& path) const {
  return PrepareModel(path, true);
}

M2ModelPrepareResult M2SystemState::PrepareModelForRender(
    const std::string& path, M2StreamFileLoader loader) const {
  return PrepareModel(path, true, std::move(loader));
}

M2ModelPrepareResult M2SystemState::PrepareModel(
    const std::string& path, const bool require_render_data,
    M2StreamFileLoader loader,
    const M2ModelEarlyReadyCallback& on_early_ready) const {
  std::uint32_t quality = kM2SkinProfileFullQuality;
  {
    std::lock_guard lock(mutex);
    if (!loader) loader = file_loader;
    quality = skin_profile_quality;
  }
  return PrepareM2ModelPackage(
      path, require_render_data, quality, loader,
      [](const data::model::M2Model& model,
         const data::model::M2Skin& skin,
         detail::M2ModelResource& resource) {
        const auto result = PrepareM2RenderPackage(model, skin, resource);
        return M2ResourcePreparationResult{result.status, result.reason,
                                           result.detail};
      },
      on_early_ready);
}

M2ModelLoadResult M2SystemState::CommitPreparedModel(
    std::unique_ptr<M2PreparedModel> prepared) {
  if (!prepared || !prepared->Valid()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "invalid prepared M2 package"};
  }
  auto* package = M2PreparedModelAccess::Get(prepared.get());
  if (package == nullptr) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "invalid prepared M2 package"};
  }
  const auto identity = repository.ReserveIdentity(package->resource.model_path);
  if (!identity.valid) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = package->resource.model_path};
  }
  {
    std::lock_guard lock(mutex);
    if (const auto path = model_paths.find(package->cache_key);
        path != model_paths.end()) {
      if (const auto model = models.find(path->second);
          model != models.end() && model->second->IsReadyForRender()) {
        return {.status = M2ResultStatus::kReady, .model_id = model->first};
      }
    }
  }
  if (package->resource.HasRenderMaterialData() ||
      package->resource.HasEffectData()) {
    const auto gpu = gpu_resources.Commit(package->resource);
    if (gpu.status != M2ResultStatus::kReady) {
      return {.status = gpu.status, .reason = gpu.reason, .detail = gpu.detail};
    }
  }
  std::lock_guard lock(mutex);
  const auto path = model_paths.find(package->cache_key);
  if (path == model_paths.end()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = package->cache_key};
  }
  if (auto model = models.find(path->second); model != models.end()) {
    if (model->second->IsReadyForRender()) {
      return {.status = M2ResultStatus::kReady, .model_id = model->first};
    }
    *model->second = std::move(package->resource);
  } else {
    models.emplace(path->second, std::make_unique<detail::M2ModelResource>(
                                     std::move(package->resource)));
  }
  sequence_streamer.RegisterModelLocked(path->second, *models.at(path->second));
  return {.status = M2ResultStatus::kReady, .model_id = path->second};
}

M2ModelLoadResult M2SystemState::LoadModelInternal(
    const std::string& path, const bool require_render_data) {
  if (path.empty()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "empty model path"};
  }
  const auto identity = repository.ReserveIdentity(path);
  if (!identity.valid) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kParseFailed,
            .detail = "invalid model extension: " + path};
  }
  for (;;) {
    std::shared_ptr<M2ModelLoadFlight> flight;
    bool leader = false;
    {
      std::lock_guard lock(mutex);
      if (!file_loader) {
        return {.status = M2ResultStatus::kNotReady,
                .reason = M2ResultReason::kMissingFile,
                .detail = "no M2 file loader"};
      }
      if (const auto cached = model_paths.find(identity.cache_key);
          cached != model_paths.end()) {
        if (const auto model = models.find(cached->second);
            model != models.end() &&
            (!require_render_data || model->second->IsReadyForRender())) {
          return {.status = M2ResultStatus::kReady, .model_id = model->first};
        }
      }
      if (const auto active = load_flights.find(identity.cache_key);
          active != load_flights.end()) {
        flight = active->second;
      } else {
        flight = std::make_shared<M2ModelLoadFlight>();
        load_flights.emplace(identity.cache_key, flight);
        leader = true;
      }
    }
    if (!leader) {
      std::unique_lock lock(flight->mutex);
      flight->completion.wait(lock, [&flight] { return flight->complete; });
      if (flight->result.status != M2ResultStatus::kReady) return flight->result;
      continue;
    }
    M2ModelLoadResult result;
    try {
      auto package = PrepareModel(path, require_render_data);
      result = package.status == M2ResultStatus::kReady && package.prepared
                   ? CommitPreparedModel(std::move(package.prepared))
                   : M2ModelLoadResult{package.status, package.reason,
                                       std::move(package.detail)};
    } catch (const std::exception& exception) {
      result = {.status = M2ResultStatus::kFailed,
                .reason = M2ResultReason::kParseFailed,
                .detail = exception.what()};
    } catch (...) {
      result = {.status = M2ResultStatus::kFailed,
                .reason = M2ResultReason::kParseFailed,
                .detail = "unknown M2 preparation exception"};
    }
    {
      std::lock_guard lock(flight->mutex);
      flight->result = result;
      flight->complete = true;
    }
    {
      std::lock_guard lock(mutex);
      if (const auto active = load_flights.find(identity.cache_key);
          active != load_flights.end() && active->second == flight) {
        load_flights.erase(active);
      }
    }
    flight->completion.notify_all();
    return result;
  }
}

M2ResultStatus M2SystemState::UnloadModel(const std::uint32_t model_id) {
  M2AnimationRuntime::DeferredCallbacks callbacks;
  {
    std::lock_guard lock(mutex);
    if (!models.contains(model_id)) return M2ResultStatus::kFailed;

    instances.DestroyByModelLocked(
        model_id,
        [this](detail::M2Instance& instance,
               const detail::M2ModelResource& resource,
               M2InstanceStore::DeferredCallbacks* pending) {
          animation_runtime.CollectTriggeredAnimationEventCallbacksLocked(instance, resource,
                                                           pending);
        },
        &callbacks);
    sequence_streamer.RemoveModelLocked(model_id);
    const auto model = models.find(model_id);
    const std::string model_path =
        model != models.end() ? model->second->model_path : std::string{};
    if (model != models.end()) models.erase(model);
    EraseModelPathRowLocked(model_paths, model_id, model_path);
  }
  for (auto& callback : callbacks) callback();
  return M2ResultStatus::kReady;
}

M2ResultStatus M2SystemState::RecycleModelIfUnused(
    const std::uint32_t model_id) {
  std::lock_guard lock(mutex);
  const auto model = models.find(model_id);
  if (model == models.end()) return M2ResultStatus::kFailed;

  if (instances.HasInstancesOfModel(model_id)) {
    return M2ResultStatus::kNotReady;
  }
  sequence_streamer.RemoveModelLocked(model_id);
  const std::string model_path = model->second->model_path;
  models.erase(model);
  EraseModelPathRowLocked(model_paths, model_id, model_path);
  return M2ResultStatus::kReady;
}

void M2SystemState::PrepareParticleDrawGeometry(
    const std::span<const std::uint32_t> instance_ids,
    const std::optional<RenderMatrix4x4View> view_matrix) {

  core::FrameJobSystem* const jobs = frame_job_system;
  if (jobs == nullptr || jobs->WorkerCount() == 0u || instance_ids.empty() ||
      core::FrameJobSystem::IsCurrentThreadWorker()) {
    return;
  }

  bx::Vec3 camera_right{1.0f, 0.0f, 0.0f};
  bx::Vec3 camera_up{0.0f, 1.0f, 0.0f};
  if (view_matrix.has_value()) {
    camera_right = {(*view_matrix)[0], (*view_matrix)[4], (*view_matrix)[8]};
    camera_up = {(*view_matrix)[1], (*view_matrix)[5], (*view_matrix)[9]};
  }

  std::lock_guard lock(mutex);
  particle_warm_scratch_.clear();
  const auto& carriers = instances.effect_carriers();
  for (const std::uint32_t instance_id : instance_ids) {

    const auto carrier = carriers.find(instance_id);
    if (carrier == carriers.end()) {
      continue;
    }
    detail::M2Instance& instance = *carrier->second;

    if (!instance.particle_system_bound) {
      continue;
    }

    if (!instance.effect_emitters_enabled) {
      continue;
    }
    particle_warm_scratch_.push_back(&instance);
  }

  std::sort(particle_warm_scratch_.begin(), particle_warm_scratch_.end());
  particle_warm_scratch_.erase(
      std::unique(particle_warm_scratch_.begin(), particle_warm_scratch_.end()),
      particle_warm_scratch_.end());

  constexpr double kM2ParticleVertexBuildMicroseconds = 2.0;
  if (particle_warm_scratch_.size() <
      core::ParallelDispatchBreakEven(jobs->WorkerCount() + 1u,
                                      kM2ParticleVertexBuildMicroseconds)) {
    return;
  }

  jobs->ParallelFor(
      particle_warm_scratch_.size(),
      [this, &camera_right, &camera_up](const std::size_t begin,
                                        const std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
          auto* const instance = particle_warm_scratch_[index];

          const auto* const uniforms = detail::BatchUniformsOf(*instance);
          const auto& m = instance->world_transform;
          const M2ParticleLighting lighting{
              .uniforms = uniforms != nullptr ? uniforms
                                              : &detail::DefaultBatchUniforms(),
              .world_normal = bx::Vec3{m[8], m[9], m[10]},
          };
          static_cast<void>(instance->particle_system.BuildVertices(
              camera_right, camera_up, &lighting));
        }
      });
}

void M2SystemState::SetFileLoader(M2StreamFileLoader loader) {
  std::lock_guard lock(mutex);
  file_loader = std::move(loader);
}

M2ResultStatus M2SystemState::SetSkinProfileQualityFromRetailScalar(
    const std::uint32_t quality) {
  std::lock_guard lock(mutex);
  if (!models.empty() || !instances.empty()) return M2ResultStatus::kFailed;
  skin_profile_quality = DecodeM2SkinProfileQualityFromRetailScalar(quality);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2SystemState::DestroyInstance(
    const std::uint32_t instance_id) {
  return instances.Destroy(
      instance_id,
      [this](detail::M2Instance& instance,
             const detail::M2ModelResource& resource,
             M2InstanceStore::DeferredCallbacks* callbacks) {
        animation_runtime.CollectTriggeredAnimationEventCallbacksLocked(instance, resource,
                                                         callbacks);
      });
}

}

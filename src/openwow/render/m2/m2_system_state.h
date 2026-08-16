#pragma once

#include "openwow/render/m2/m2_animation_runtime.h"
#include "openwow/render/m2/m2_gpu_resources.h"
#include "openwow/render/m2/m2_model_queries.h"
#include "openwow/render/m2/m2_model_repository.h"
#include "openwow/render/m2/m2_renderer.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_skin_profile.h"
#include "openwow/render/m2/m2_spatial_queries.h"
#include "openwow/render/m2/m2_visibility.h"

namespace openwow::render::m2 {

class M2SystemState {
 public:
  M2SystemState();
  ~M2SystemState();

  bool Initialize();
  void Shutdown();
  void Reset();

  void ReleaseDeviceResources();
  [[nodiscard]] M2ModelLoadResult LoadModel(const std::string& path);
  [[nodiscard]] M2ModelLoadResult LoadModelForSampling(const std::string& path);
  [[nodiscard]] M2ModelPrepareResult PrepareModelForRender(const std::string& path) const;
  [[nodiscard]] M2ModelPrepareResult PrepareModelForRender(
      const std::string& path, M2StreamFileLoader loader) const;
  [[nodiscard]] M2ModelPrepareResult PrepareModel(
      const std::string& path, bool require_render_data,
      M2StreamFileLoader loader = {},
      const M2ModelEarlyReadyCallback& on_early_ready = {}) const;
  [[nodiscard]] M2ModelLoadResult CommitPreparedModel(
      std::unique_ptr<M2PreparedModel> prepared);
  [[nodiscard]] M2ModelInstanceLoadResult LoadModelInstance(const std::string& path);
  [[nodiscard]] M2ModelInstanceLoadResult LoadModelInstanceWithFallback(
      const std::string& path, std::string_view fallback);
  [[nodiscard]] M2ModelInstanceLoadResult LoadAttachedChildInstance(
      std::uint32_t parent, const std::string& path, std::int32_t slot,
      M2ChildDestroyPolicy policy, std::string_view fallback);
  [[nodiscard]] M2ResultStatus UnloadModel(std::uint32_t model_id);
  [[nodiscard]] M2ResultStatus RecycleModelIfUnused(std::uint32_t model_id);

  void PrepareParticleDrawGeometry(
      std::span<const std::uint32_t> instance_ids,
      std::optional<RenderMatrix4x4View> view_matrix);
  void SetFileLoader(M2StreamFileLoader loader);
  [[nodiscard]] M2ResultStatus SetSkinProfileQualityFromRetailScalar(
      std::uint32_t quality);
  [[nodiscard]] M2ResultStatus DestroyInstance(std::uint32_t instance_id);

  [[nodiscard]] M2InstanceCreateResult CreateSeatedInstance(std::uint32_t model_id);

  M2ModelRepository repository;
  M2SystemMutex& mutex;
  M2ModelRepository::ModelMap& models;
  std::unordered_map<std::string, std::uint32_t>& model_paths;
  std::unordered_map<std::string, std::shared_ptr<M2ModelLoadFlight>>& load_flights;
  TextureManager* texture_manager = nullptr;
  const data::dbc::DbcLoader* dbc = nullptr;
  M2GpuResources gpu_resources;
  M2StreamFileLoader file_loader;
  M2InstanceStore instances;
  M2ModelQueries model_queries;
  M2SpatialQueries spatial_queries;
  M2SequenceStreamer sequence_streamer;
  M2AnimationRuntime animation_runtime;

  M2InstanceFramePreparer frame_preparer;

  core::FrameJobSystem* frame_job_system = nullptr;
  M2Renderer renderer;
  M2Visibility visibility;
  M2ResourceStreamer resource_streamer;
  std::uint32_t skin_profile_quality = kM2SkinProfileFullQuality;
  bool initialized = false;

 private:
  [[nodiscard]] M2ModelLoadResult LoadModelInternal(const std::string& path,
                                                     bool render_data);
  void DoReset();

  std::vector<detail::M2Instance*> particle_warm_scratch_;
};

}

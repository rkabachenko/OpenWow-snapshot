#pragma once

#include "openwow/render/m2/m2_instance_store.h"
#include "openwow/render/m2/m2_model_queries.h"
#include "openwow/render/m2/m2_sequence_streamer.h"
#include "openwow/render/m2/m2_spatial_queries.h"
#include "openwow/runtime/scheduling/frame_job_system.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::render::m2 {

struct PendingCompletion {
  M2AnimationCompletionCallback callback;
  std::uint32_t animation_id = 0;
};

class M2InstanceFramePreparer;

class M2AnimationRuntime {
public:
  using ModelStore = M2SequenceStreamer::ModelStore;
  using DeferredCallbacks = M2SequenceStreamer::DeferredCallbacks;

  M2AnimationRuntime(M2SystemMutex &mutex, ModelStore &models,
                      M2InstanceStore &instances,
                     const data::dbc::DbcLoader *&dbc,
                     M2SequenceStreamer &sequence_streamer);

  [[nodiscard]] M2ResultStatus SetAnimationChangedCallback(
      std::uint32_t instance_id, M2AnimationChangedCallback callback);
  [[nodiscard]] M2ResultStatus ClearAnimationChangedCallback(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus SetAnimationRequestCallback(
      std::uint32_t instance_id, M2AnimationRequestCallback callback);
  [[nodiscard]] M2ResultStatus ClearAnimationRequestCallback(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus SetAnimationCompletionCallback(
      std::uint32_t instance_id, M2AnimationCompletionCallback callback);
  [[nodiscard]] M2ResultStatus ClearAnimationCompletionCallback(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus SetTriggeredEventCallback(
      std::uint32_t instance_id, M2TriggeredEventCallback callback);
  [[nodiscard]] M2ResultStatus ClearTriggeredEventCallback(
      std::uint32_t instance_id);

  [[nodiscard]] M2ResultStatus SetAnimationRequest(
      std::uint32_t instance_id, const M2AnimationRequest &request);
  [[nodiscard]] M2ResultStatus SetAnimation(std::uint32_t instance_id,
                                            std::uint32_t animation_id,
                                            float speed);

  [[nodiscard]] M2ResultStatus SetAnimationSample(
      std::uint32_t instance_id, std::uint32_t animation_id,
      std::uint32_t time_ms, float speed, bool zero_blend = false);
  [[nodiscard]] M2ResultStatus SetAnimationSequenceSample(
      std::uint32_t instance_id, std::uint16_t sequence_index,
      std::uint32_t time_ms, float speed);
  [[nodiscard]] M2ResultStatus SetAnimationSlotRequest(
      std::uint32_t instance_id, std::uint32_t slot_index,
      const M2AnimationRequest &request);

  [[nodiscard]] M2ResultStatus SetAnimationSlotSample(
      std::uint32_t instance_id, std::uint32_t slot_index,
      std::uint32_t animation_id, std::uint32_t time_ms, float speed,
      bool zero_blend = false);
  [[nodiscard]] M2ResultStatus SetAnimationSlotTimes(
      std::uint32_t instance_id, std::span<const std::uint32_t> slot_indices,
      std::uint32_t time_ms);
  [[nodiscard]] M2ResultStatus ClearAnimationSlot(std::uint32_t instance_id,
                                                  std::uint32_t slot_index);

  [[nodiscard]] M2ResultStatus SetKeyBoneBasisOverride(
      std::uint32_t instance_id, std::uint32_t key_bone_lookup_index,
      const RenderMatrix4x4 &basis);
  [[nodiscard]] M2ResultStatus ClearKeyBoneBasisOverride(
      std::uint32_t instance_id, std::uint32_t key_bone_lookup_index);
  [[nodiscard]] M2AnimationSlotStateQuery QueryAnimationSlotState(
      std::uint32_t instance_id, std::uint32_t slot_index) const;
  [[nodiscard]] M2ResultStatus CopyActiveAnimationState(
      std::uint32_t source_instance_id,
      std::uint32_t destination_instance_id);

  void BindFrameJobSystem(core::FrameJobSystem *jobs) { frame_job_system_ = jobs; }

  [[nodiscard]] M2ResultStatus UpdateAnimation(std::uint32_t instance_id,
                                               float delta_time);

  void UpdateAnimations(std::span<const std::uint32_t> instance_ids,
                        float delta_time,
                        std::vector<std::uint32_t>* missing_instance_ids);
  void UpdateAllAnimations(float delta_time);

  void PumpSequenceLoads();
  void ResumePendingAnimationsLocked(std::uint32_t model_id,
                                     DeferredCallbacks *callbacks);
  void CollectSampledAnimationEventCallbacksLocked(
      detail::M2Instance &instance, const detail::M2ModelResource &resource,
      DeferredCallbacks *callbacks);

  void CollectSlotAnimationEventCallbacksLocked(
      detail::M2Instance &instance, const detail::M2ModelResource &resource,
      std::uint32_t slot_index, DeferredCallbacks *callbacks);
  void CollectTriggeredAnimationEventCallbacksLockedFor(
      detail::M2Instance &instance, DeferredCallbacks *callbacks);
  void CollectTriggeredAnimationEventCallbacksLocked(detail::M2Instance &instance,
                                     const detail::M2ModelResource &resource,
                                     DeferredCallbacks *callbacks);

private:
  friend class M2InstanceFramePreparer;

  [[nodiscard]] static bool RecordSampleOnPendingRequestLocked(
      detail::M2Instance &instance, std::uint32_t animation_id,
      std::uint32_t time_ms, float speed) noexcept;

  [[nodiscard]] static bool SampleNeedsRequestLocked(
      const detail::M2Instance &instance, std::uint32_t animation_id) noexcept;

  static void RecordSampleOnPendingLocked(detail::M2Instance &instance,
                                          std::uint32_t time_ms,
                                          float speed) noexcept;

  void ApplyAnimationSampleLocked(std::uint32_t instance_id,
                                  detail::M2Instance &instance,
                                  std::uint32_t time_ms, float speed,
                                  DeferredCallbacks &sampled_events);

  static void ApplyAnimationSelectionLocked(
      detail::M2Instance &instance, const data::model::M2Model &model,
      const M2AnimationRequest &request,
      const M2AnimationSelectionResult &selection);
  static void ApplySequenceSampleLocked(detail::M2Instance &instance,
                                        const data::model::M2Model &model,
                                        std::uint16_t sequence_index,
                                        std::uint32_t time_ms, float speed);

  bool RerollBaseVariantOnLoopLocked(detail::M2Instance &instance,
                                     const detail::M2ModelResource &resource,
                                     std::uint32_t model_id,
                                     std::uint32_t instance_id);

  bool RerollSlotVariantOnLoopLocked(detail::M2Instance &instance,
                                     const detail::M2ModelResource &resource,
                                     std::uint32_t model_id,
                                     std::uint32_t instance_id,
                                     std::uint32_t slot_index);

  void ProcessLoopBoundariesLocked(detail::M2Instance &instance,
                                    std::uint32_t instance_id);

  M2SystemMutex &mutex_;
  ModelStore &models_;
  M2InstanceStore &instances_;
  const data::dbc::DbcLoader *&dbc_;
  M2SequenceStreamer &sequence_streamer_;

  M2SequenceStreamer::ResumePending resume_pending_sequence_loads_;

  core::FrameJobSystem *frame_job_system_{nullptr};

  std::vector<detail::M2Instance *> parallel_resolved_scratch_;
  std::vector<std::vector<PendingCompletion>> parallel_completions_scratch_;
  std::vector<DeferredCallbacks> parallel_events_scratch_;
};

class M2InstanceFramePreparer {
public:
  M2InstanceFramePreparer(M2AnimationRuntime &runtime,
                          M2ModelQueries &model_queries,
                          M2SpatialQueries &spatial_queries) noexcept;

  M2InstanceFramePreparer(const M2InstanceFramePreparer &) = delete;
  M2InstanceFramePreparer &operator=(const M2InstanceFramePreparer &) = delete;

  void Begin();

  void End();

  void Suspend();
  void Resume();

  [[nodiscard]] M2InstanceFrameSpatialResult PrepareSpatial(
      const M2InstanceFrameSpatialRequest &request);

  [[nodiscard]] bool QueryRenderReady(std::uint32_t instance_id);

  [[nodiscard]] bool SamplePresentation(
      const M2InstanceFramePresentationRequest &request);

  [[nodiscard]] M2ResultStatus SetSharedBatchUniforms(
      std::uint32_t instance_id, M2SharedBatchUniformsHandle handle);

  [[nodiscard]] M2ResultStatus ClearReplaceableTexturePath(
      std::uint32_t instance_id, std::uint32_t texture_type);

private:

  detail::M2Instance *ResolveInstance(std::uint32_t instance_id);

  const detail::M2ModelResource *ResolveInstanceModel();

  const detail::M2ModelResource *ResolveRequestModel(std::uint32_t model_id);
  void DropMemo() noexcept;

  void PumpSequenceLoadsUnlocked();

  [[nodiscard]] M2ResultStatus ApplyAnimationSample(
      std::uint32_t instance_id, std::uint32_t animation_id,
      std::uint32_t time_ms, float speed, bool zero_blend);

  [[nodiscard]] std::uint32_t ResolvePresentationDurationMs(
      const M2InstanceFramePresentationRequest &request);

  M2AnimationRuntime &runtime_;
  M2ModelQueries &model_queries_;
  M2SpatialQueries &spatial_queries_;
  M2SystemMutex &mutex_;
  bool locked_ = false;

  std::uint32_t memo_instance_id_ = 0;
  bool memo_instance_valid_ = false;
  detail::M2Instance *memo_instance_ = nullptr;
  bool memo_instance_model_valid_ = false;
  const detail::M2ModelResource *memo_instance_model_ = nullptr;
  std::uint32_t memo_request_model_id_ = 0;
  bool memo_request_model_valid_ = false;
  const detail::M2ModelResource *memo_request_model_ = nullptr;
};

}

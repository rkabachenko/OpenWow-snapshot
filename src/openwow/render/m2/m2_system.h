#pragma once

#include "openwow/render/api/renderer_context.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/scene/selection_triangle.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace openwow::render {
class TextureManager;
}
namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::core {
class FrameJobSystem;
}
namespace bgfx {
struct Encoder;
}

namespace openwow::render::m2 {

class M2SystemState;

class M2System final : public api::RendererDeviceLifecycleObserver {
 public:
  M2System();
  ~M2System() override;
  M2System(const M2System&) = delete;
  M2System& operator=(const M2System&) = delete;

  void BindRendererContext(api::RendererContext* renderer_context);

  void BindTextureManager(TextureManager* value);
  void BindDbc(const data::dbc::DbcLoader* value);

  void BindFrameJobSystem(core::FrameJobSystem* jobs);

  [[nodiscard]] core::FrameJobSystem* frame_job_system() const;
  void SetParticleDensity(float density);
  [[nodiscard]] TextureManager* texture_manager() const;
  void SetAsyncFileLoader(std::function<std::vector<std::uint8_t>(const std::string&)> loader);

  void SetAsyncPrefixFileLoader(M2StreamPrefixFileLoader loader);
  void StartAsyncLoading(std::uint32_t workers);
  void ShutdownAsyncLoading();
  [[nodiscard]] M2StreamTicket AcquireModelAsync(
      const std::string& path,
      M2StreamPriority priority = M2StreamPriority::kNormal);
  void ReleaseModelAsync(M2StreamTicket ticket);
  [[nodiscard]] M2StreamQuery QueryModelAsync(M2StreamTicket ticket) const;
  [[nodiscard]] M2InstanceCreateResult CreateInstanceAsync(M2StreamTicket ticket) const;
  [[nodiscard]] M2StreamPumpResult PumpAsyncLoading(const M2StreamCommitBudget& budget);
  [[nodiscard]] M2StreamPumpResult PumpAsyncLoading();
  [[nodiscard]] static std::string BuildSkinProfilePath(std::string_view path, std::uint32_t profile);
  [[nodiscard]] static M2CameraBasis BuildCameraBasis(const M2CameraPose& pose);
  [[nodiscard]] static float BuildCameraVerticalFov(float fov, float aspect);
  [[nodiscard]] static M2CameraPose TransformCameraPoseByModelMatrix(const M2CameraPose& pose, RenderMatrix4x4View matrix);
  [[nodiscard]] static RenderMatrix4x4 BuildCameraViewMatrix(const M2CameraPose& pose);

  bool Initialize();
  void Shutdown();
  void Reset();
  void SetFileLoader(std::function<std::vector<std::uint8_t>(const std::string&)> loader);
  [[nodiscard]] M2ResultStatus SetSkinProfileQualityFromRetailScalar(std::uint32_t quality);
  [[nodiscard]] M2ModelLoadResult LoadModel(const std::string& path);
  [[nodiscard]] M2ModelLoadResult LoadModelForSampling(const std::string& path);
  [[nodiscard]] M2ModelPrepareResult PrepareModelForRender(const std::string& path) const;
  [[nodiscard]] M2ModelPrepareResult PrepareModelForRender(const std::string& path, std::function<std::vector<std::uint8_t>(const std::string&)> loader) const;
  [[nodiscard]] M2ModelLoadResult CommitPreparedModel(std::unique_ptr<M2PreparedModel> prepared);
  [[nodiscard]] M2ModelInstanceLoadResult LoadModelInstance(const std::string& path);
  [[nodiscard]] M2ModelInstanceLoadResult LoadModelInstanceWithFallback(const std::string& path, std::string_view fallback = kDefaultM2FallbackModelPath);
  [[nodiscard]] M2ModelInstanceLoadResult LoadAttachedChildInstance(std::uint32_t parent, const std::string& path, std::int32_t slot = kM2NoAttachmentLookupIndex, M2ChildDestroyPolicy policy = M2ChildDestroyPolicy::kDestroyWithParent, std::string_view fallback = kDefaultM2FallbackModelPath);
  [[nodiscard]] M2ResultStatus UnloadModel(std::uint32_t model);
  [[nodiscard]] M2ResultStatus RecycleModelIfUnused(std::uint32_t model);

  void PrepareParticleDrawGeometry(
      std::span<const std::uint32_t> instance_ids,
      std::optional<RenderMatrix4x4View> view_matrix);

  [[nodiscard]] M2ModelReadinessQuery QueryModelReadiness(std::uint32_t model) const;
  [[nodiscard]] M2FirstAnimationDurationQuery QueryFirstAnimationDuration(std::uint32_t model) const;
  [[nodiscard]] M2ModelAnimationSequenceQuery QueryModelAnimationSequence(std::uint32_t model, std::uint32_t animation, std::int32_t sub = 0) const;
  [[nodiscard]] M2ModelCollisionGeometryQuery QueryModelCollisionGeometry(std::uint32_t model) const;
  [[nodiscard]] M2AttachmentInfoQuery QueryModelAttachmentInfo(std::uint32_t model, std::uint32_t lookup) const;
  [[nodiscard]] M2ModelSpatialInfoQuery QueryModelSpatialInfo(std::uint32_t model, std::uint16_t sequence = kInvalidM2AnimationSequenceIndex) const;
  [[nodiscard]] M2ModelGeometryInfoQuery QueryModelGeometryInfo(std::uint32_t model) const;
  [[nodiscard]] M2ModelInfoQuery QueryModelInfo(std::uint32_t model) const;
  [[nodiscard]] M2ModelAnimationListQuery QueryModelAnimationList(std::uint32_t model) const;
  [[nodiscard]] M2ModelTextureDependenciesQuery QueryModelTextureDependencies(std::uint32_t model) const;
  [[nodiscard]] M2ModelSubmeshSectionIdsQuery QueryModelSubmeshSectionIds(std::uint32_t model) const;
  [[nodiscard]] M2ModelLightCountQuery QueryModelLightCount(std::uint32_t model) const;
  [[nodiscard]] bool ModelHasSubmeshId(std::uint32_t model, std::uint16_t submesh) const;
  [[nodiscard]] bool ModelHasAnimation(std::uint32_t model, std::uint32_t animation) const;

  [[nodiscard]] bool ModelContainsAnimation(std::uint32_t model, std::uint32_t animation) const;
  [[nodiscard]] M2SampleBoneMatricesQuery QuerySampleBoneMatrices(std::uint32_t model, int sequence, std::uint32_t time, const std::optional<RenderMatrix4x4View>& inverse = std::nullopt) const;
  [[nodiscard]] M2CameraSampleQuery QueryCameraSample(std::uint32_t model, int camera, int sequence, std::uint32_t time) const;
  [[nodiscard]] M2LightSampleQuery QueryLightSample(std::uint32_t model, int light, int sequence, std::uint32_t time, std::span<const float> bones) const;
  [[nodiscard]] std::vector<M2TriggeredEvent> CollectTriggeredEvents(std::uint32_t model, int sequence, std::uint32_t previous, std::uint32_t current, std::span<const float> bones = {}, const std::optional<RenderMatrix4x4>& matrix = std::nullopt) const;

  [[nodiscard]] M2InstanceCreateResult CreateInstance(std::uint32_t model);
  [[nodiscard]] M2ResultStatus DestroyInstance(std::uint32_t instance);
  [[nodiscard]] M2InstanceInfoQuery QueryInstanceInfo(std::uint32_t instance) const;
  [[nodiscard]] M2VisualTreeRevisionQuery QueryVisualTreeRevision(std::uint32_t instance) const;
  [[nodiscard]] M2InstanceModelQuery QueryInstanceModel(std::uint32_t instance) const;
  [[nodiscard]] M2ResultStatus SetInstanceModel(std::uint32_t instance, std::uint32_t model);
  [[nodiscard]] M2ChildLinksQuery QueryChildLinks(std::uint32_t instance) const;
  [[nodiscard]] M2VisibleSubmeshFilterQuery QueryVisibleSubmeshFilter(std::uint32_t instance) const;
  [[nodiscard]] M2InstanceAttachmentVisualStateQuery QueryInstanceAttachmentVisualState(std::uint32_t instance) const;
  [[nodiscard]] M2ResultStatus SetInstanceAttachmentVisualState(std::uint32_t instance, const M2InstanceAttachmentVisualState& state);
  [[nodiscard]] M2ResultStatus SetInstanceEffectContext(std::uint32_t instance, const M2InstanceEffectContext& context);
  [[nodiscard]] M2ResultStatus SetReplaceableTexturePath(std::uint32_t instance, std::uint32_t type, const std::string& path);
  [[nodiscard]] M2ResultStatus ClearReplaceableTexturePath(std::uint32_t instance, std::uint32_t type);
  [[nodiscard]] M2ResultStatus ClearReplaceableTexturePaths(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetVisible(std::uint32_t instance, bool visible);
  [[nodiscard]] M2ResultStatus SetEffectEmittersEnabled(std::uint32_t instance, bool enabled);
  [[nodiscard]] M2ResultStatus BeginTransientWeaponTrail(std::uint32_t instance, std::uint32_t argb, std::uint32_t duration, std::uint32_t start);
  [[nodiscard]] M2ResultStatus SetTintColor(std::uint32_t instance, const RenderVec4& rgba);
  [[nodiscard]] M2ResultStatus SetAlpha(std::uint32_t instance, float alpha);
  [[nodiscard]] M2ResultStatus SetSelectionGlowColor(std::uint32_t instance, const RenderVec4& rgba);
  [[nodiscard]] M2SelectionGlowColorQuery QuerySelectionGlowColor(std::uint32_t instance) const;
  [[nodiscard]] M2ResultStatus ApplyCreatureDisplayRecordOverrides(std::uint32_t instance, const std::array<std::string, 3>& paths, std::optional<M2ParticleColorRecord> record);
  [[nodiscard]] M2ResultStatus SetParticleColorOverride(std::uint32_t instance, std::uint32_t color, std::uint32_t start, std::uint32_t mid, std::uint32_t end);
  [[nodiscard]] M2ResultStatus ClearParticleColorOverride(std::uint32_t instance, std::uint32_t color);
  [[nodiscard]] M2ResultStatus SetReplacementColors(std::uint32_t instance, const std::array<std::uint32_t, 3>& colors);
  [[nodiscard]] M2ResultStatus ClearReplacementColors(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus ApplyParticleColorRecordSlots11To13(std::uint32_t instance, std::optional<M2ParticleColorRecord> record);
  [[nodiscard]] M2ResultStatus SetWorldTransformMatrix(std::uint32_t instance, const RenderMatrix4x4& matrix);
  [[nodiscard]] M2ResultStatus ClearWorldTransformMatrix(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetTransform(std::uint32_t instance, const std::optional<RenderVec3>& position, const std::optional<RenderVec3>& rotation, float scale);
  [[nodiscard]] M2ResultStatus SetPosition(std::uint32_t instance, const RenderVec3& position);
  [[nodiscard]] M2ResultStatus SetRotationDegrees(std::uint32_t instance, const RenderVec3& rotation);
  [[nodiscard]] M2ResultStatus SetScale(std::uint32_t instance, float scale);
  [[nodiscard]] M2ResultStatus SetVisibleSubmeshIndices(std::uint32_t instance, std::vector<std::size_t> indices);
  [[nodiscard]] M2ResultStatus ClearVisibleSubmeshIndices(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetBatchUniforms(std::uint32_t instance, const M2BatchUniforms& uniforms);

  [[nodiscard]] M2SharedBatchUniformsHandle PublishSharedBatchUniforms(const M2BatchUniforms& uniforms);
  void ReleaseSharedBatchUniforms(M2SharedBatchUniformsHandle handle);
  [[nodiscard]] M2ResultStatus SetSharedBatchUniforms(std::uint32_t instance, M2SharedBatchUniformsHandle handle);
  [[nodiscard]] M2ResultStatus SetDoodadFrameRenderState(std::uint32_t instance, const RenderMatrix4x4& world_transform, const M2BatchUniforms& uniforms, const RenderVec4& tint_rgba, float alpha);

  void SetDoodadFrameRenderStates(std::span<const M2DoodadFrameRenderRequest> requests, std::span<M2ResultStatus> out_statuses);
  [[nodiscard]] M2ResultStatus SetVisualPresentation(std::uint32_t instance, bool visible, const RenderVec4& tint_rgba, float alpha);
  [[nodiscard]] M2TransformVisibilityResult SetTransformAndVisibility(std::uint32_t instance, const RenderMatrix4x4& matrix, bool visible);
  [[nodiscard]] M2ResultStatus SetAttachmentFrameRenderState(std::uint32_t instance, const RenderMatrix4x4& world_transform, const M2BatchUniforms& uniforms, bool visible, const RenderVec4& tint_rgba, float alpha);

  void SetAttachmentFrameRenderStates(std::span<const M2AttachmentFrameRenderRequest> requests, std::span<M2ResultStatus> out_statuses);
  [[nodiscard]] M2ResultStatus ClearBatchUniforms(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus AttachChildInstance(std::uint32_t parent, std::uint32_t child, M2ChildDestroyPolicy policy = M2ChildDestroyPolicy::kDestroyWithParent);
  [[nodiscard]] M2ResultStatus AttachChildInstance(std::uint32_t parent, std::uint32_t child, std::int32_t slot, M2ChildDestroyPolicy policy = M2ChildDestroyPolicy::kDestroyWithParent);
  [[nodiscard]] M2ResultStatus SetAttachedModelVisualSelectorFlag(std::uint32_t instance, bool enabled);
  [[nodiscard]] M2PrimaryVisualStateResult EnablePrimaryVisualState(std::uint32_t instance, float duration);
  [[nodiscard]] M2ResultStatus DisablePrimaryVisualState(std::uint32_t instance, bool reset);
  [[nodiscard]] M2ResultStatus ResetPrimaryVisualStateForLightning(std::uint32_t instance);

  [[nodiscard]] M2ResultStatus SetAnimationChangedCallback(std::uint32_t instance, M2AnimationChangedCallback callback);
  [[nodiscard]] M2ResultStatus ClearAnimationChangedCallback(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetAnimationRequestCallback(std::uint32_t instance, M2AnimationRequestCallback callback);
  [[nodiscard]] M2ResultStatus ClearAnimationRequestCallback(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetAnimationCompletionCallback(std::uint32_t instance, M2AnimationCompletionCallback callback);
  [[nodiscard]] M2ResultStatus ClearAnimationCompletionCallback(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetTriggeredEventCallback(std::uint32_t instance, M2TriggeredEventCallback callback);
  [[nodiscard]] M2ResultStatus ClearTriggeredEventCallback(std::uint32_t instance);
  [[nodiscard]] M2ResultStatus SetAnimationRequest(std::uint32_t instance, const M2AnimationRequest& request);
  [[nodiscard]] M2ResultStatus SetAnimation(std::uint32_t instance, std::uint32_t animation, float speed = 1.0f);

  [[nodiscard]] M2ResultStatus SetAnimationSample(std::uint32_t instance, std::uint32_t animation, std::uint32_t time, float speed = 1.0f, bool zero_blend = false);
  [[nodiscard]] M2ResultStatus SetAnimationSequenceSample(std::uint32_t instance, std::uint16_t sequence, std::uint32_t time, float speed = 1.0f);
  [[nodiscard]] M2ResultStatus SetAnimationSlotRequest(std::uint32_t instance, std::uint32_t slot, const M2AnimationRequest& request);

  [[nodiscard]] M2ResultStatus SetAnimationSlotSample(std::uint32_t instance, std::uint32_t slot, std::uint32_t animation, std::uint32_t time, float speed = 1.0f, bool zero_blend = false);
  [[nodiscard]] M2ResultStatus SetAnimationSlotTimes(std::uint32_t instance, std::span<const std::uint32_t> slots, std::uint32_t time);
  [[nodiscard]] M2ResultStatus ClearAnimationSlot(std::uint32_t instance, std::uint32_t slot);

  [[nodiscard]] M2ResultStatus SetKeyBoneBasisOverride(std::uint32_t instance, std::uint32_t key_bone_lookup, const RenderMatrix4x4& basis);
  [[nodiscard]] M2ResultStatus ClearKeyBoneBasisOverride(std::uint32_t instance, std::uint32_t key_bone_lookup);
  [[nodiscard]] M2AnimationSlotStateQuery QueryAnimationSlotState(std::uint32_t instance, std::uint32_t slot) const;
  [[nodiscard]] M2ResultStatus CopyActiveAnimationState(std::uint32_t source, std::uint32_t destination);

  void BeginInstanceFramePrepare();
  void EndInstanceFramePrepare();
  void SuspendInstanceFramePrepare();
  void ResumeInstanceFramePrepare();
  [[nodiscard]] M2InstanceFrameSpatialResult PrepareInstanceSpatial(const M2InstanceFrameSpatialRequest& request);
  [[nodiscard]] bool QueryPreparedInstanceRenderReady(std::uint32_t instance);
  [[nodiscard]] bool SamplePreparedInstancePresentation(const M2InstanceFramePresentationRequest& request);
  [[nodiscard]] M2ResultStatus SetPreparedInstanceSharedBatchUniforms(std::uint32_t instance, M2SharedBatchUniformsHandle handle);
  [[nodiscard]] M2ResultStatus ClearPreparedInstanceReplaceableTexturePath(std::uint32_t instance, std::uint32_t type);

  [[nodiscard]] M2ResultStatus UpdateAnimation(std::uint32_t instance, float delta);

  void UpdateAnimations(std::span<const std::uint32_t> instances, float delta,
                        std::vector<std::uint32_t>* missing_instances);
  void UpdateAllAnimations(float delta);

  void UpdateAllEffects(float frame_delta_seconds,
                        std::optional<RenderMatrix4x4View> view_matrix);

  [[nodiscard]] M2RenderInstanceResult RenderInstance(std::uint16_t view, std::uint32_t instance, bgfx::Encoder* encoder = nullptr);
  [[nodiscard]] M2RenderInstanceResult RenderInstance(std::uint16_t view, std::uint32_t instance, M2RenderPassScope scope, bgfx::Encoder* encoder = nullptr);
  [[nodiscard]] M2RenderInstanceResult RenderInstance(std::uint16_t view, std::uint32_t instance, RenderMatrix4x4View matrix, bgfx::Encoder* encoder = nullptr);
  [[nodiscard]] M2RenderInstanceResult RenderInstance(std::uint16_t view, std::uint32_t instance, RenderMatrix4x4View matrix, M2RenderPassScope scope, bgfx::Encoder* encoder = nullptr);

  void RenderInstanceBatch(std::uint16_t view, std::span<const std::uint32_t> instance_ids,
                           std::optional<RenderMatrix4x4View> view_matrix,
                           M2RenderPassScope scope, core::FrameJobSystem* jobs,
                           double per_instance_microseconds,
                           std::span<M2RenderInstanceResult> out_results,
                           bgfx::Encoder* serial_encoder = nullptr);
  void BindRenderSubmitTrace(std::optional<RenderSubmitTraceBinding> binding);

  [[nodiscard]] bool IsRenderSubmitTraceBound() const;

  [[nodiscard]] M2StaticInstancingProfile QueryStaticInstancingProfile(std::uint32_t instance);

  [[nodiscard]] bool QueryOpaqueDepthTestGuaranteed(std::uint32_t instance);
  [[nodiscard]] M2RenderInstanceResult RenderInstancedGroup(
      std::uint16_t view, std::uint32_t exemplar_instance,
      std::span<const M2InstancedDrawRecord> records,
      const M2BatchUniforms& base_uniforms, bgfx::Encoder* encoder = nullptr);
  [[nodiscard]] M2ShadowClassQuery QueryShadowClass(std::uint32_t instance) const;
  [[nodiscard]] M2ShadowDrawCallLists BuildShadowDrawCalls(std::uint32_t instance) const;

  void SetProjectedTexturesEnabled(bool enabled);
  [[nodiscard]] std::vector<M2ProjectedTextureDraw> TakeProjectedTextureDraws();

  [[nodiscard]] M2InstanceReadinessQuery QueryInstanceReadiness(std::uint32_t instance) const;
  [[nodiscard]] M2InstanceAnimationInfoQuery QueryInstanceAnimationInfo(std::uint32_t instance) const;
  [[nodiscard]] M2InstanceEventQuery QueryInstanceEvent(std::uint32_t instance, std::uint32_t animation, std::uint32_t event) const;
  [[nodiscard]] M2InstanceSpatialInfoQuery QueryInstanceSpatialInfo(std::uint32_t instance) const;
  [[nodiscard]] M2SegmentIntersectionQuery QueryClosestSegmentIntersection(std::uint32_t instance, const RenderVec3& start, const RenderVec3& end) const;
  [[nodiscard]] bool InstanceModelHasAnimation(std::uint32_t instance, std::uint32_t animation) const;
  [[nodiscard]] M2BoneMatricesQuery QueryBoneMatrices(std::uint32_t instance);
  [[nodiscard]] M2KeyBoneQuery QueryKeyBone(std::uint32_t instance, std::uint32_t lookup) const;
  [[nodiscard]] M2SampleBoneMatricesQuery QueryInstanceSampleBoneMatrices(std::uint32_t instance, const std::optional<RenderMatrix4x4View>& inverse = std::nullopt) const;
  [[nodiscard]] M2CameraSampleQuery QueryInstanceCameraSample(std::uint32_t instance, int camera) const;
  [[nodiscard]] M2CameraSampleQuery QueryInstanceCameraSampleByType(std::uint32_t instance, std::uint32_t type) const;
  [[nodiscard]] M2LightSampleQuery QueryInstanceLightSample(std::uint32_t instance, int light, std::span<const float> bones) const;
  [[nodiscard]] std::vector<M2TriggeredEvent> CollectInstanceTriggeredEvents(std::uint32_t instance, std::uint32_t previous, std::uint32_t current, std::span<const float> bones = {}, const std::optional<RenderMatrix4x4>& matrix = std::nullopt) const;
  [[nodiscard]] M2AttachmentInfoQuery QueryAttachmentInfo(std::uint32_t instance, std::uint32_t lookup) const;
  [[nodiscard]] M2AttachmentPositionQuery QueryAttachmentPosition(std::uint32_t instance, std::uint32_t lookup, const std::optional<RenderVec3>& offset = std::nullopt) const;
  [[nodiscard]] M2AttachmentTransformQuery QueryAttachmentTransformMatrix(std::uint32_t instance, std::uint32_t lookup) const;

  void QueryAttachmentPlacements(std::span<const M2AttachmentPlacementRequest> requests, std::span<M2AttachmentPlacementQuery> out_results) const;
  [[nodiscard]] M2ModelWorldPointQuery QueryModelWorldPoint(std::uint32_t instance) const;
  [[nodiscard]] M2ModelWorldTransformMatrixQuery QueryModelWorldTransformMatrix(std::uint32_t instance) const;
  [[nodiscard]] M2RootBoneWorldMatrixQuery QueryRootBoneWorldMatrix(std::uint32_t instance) const;
  [[nodiscard]] std::optional<SelectionTriangleVertices> BuildSelectionTriangleVerticesForSample(std::uint32_t model, const std::vector<float>& bones, std::size_t count, const RenderMatrix4x4& matrix) const;
  [[nodiscard]] M2WorldBoundingSphereQuery QueryWorldBoundingSphere(std::uint32_t instance) const;
  [[nodiscard]] M2WorldBoundingBoxQuery QueryWorldBoundingBox(std::uint32_t instance) const;
  [[nodiscard]] M2ModelBoundingSphereQuery QueryInstanceModelBoundingSphere(std::uint32_t instance) const;

  [[nodiscard]] M2VisualCloneCreateResult CreateVisualClone(std::uint32_t source, std::uint64_t revision);
  [[nodiscard]] M2RenderInstanceResult RenderVisualClone(std::uint16_t view, const M2VisualCloneLease& lease, const RenderMatrix4x4& root, RenderMatrix4x4View matrix);
  [[nodiscard]] M2ResultStatus SetVisualCloneAnimation(const M2VisualCloneLease& lease, std::uint32_t animation, float speed = 1.0f);
  [[nodiscard]] M2ResultStatus SetVisualCloneAnimationSample(const M2VisualCloneLease& lease, std::uint32_t animation, std::uint32_t time, float speed = 1.0f);
  [[nodiscard]] M2CameraSampleQuery QueryVisualCloneCamera(const M2VisualCloneLease& lease, int camera) const;
  [[nodiscard]] M2CameraSampleQuery QueryVisualCloneCameraByType(const M2VisualCloneLease& lease, std::uint32_t type) const;
  [[nodiscard]] M2InstanceSpatialInfoQuery QueryVisualCloneSpatialInfo(const M2VisualCloneLease& lease) const;
  [[nodiscard]] M2VisualCloneSnapshotQuery QueryVisualCloneSnapshot(const M2VisualCloneLease& lease) const;

  void UpdateVisibility();
  void UpdateVisibility(RenderMatrix4x4View view_projection);
  [[nodiscard]] std::uint32_t GetLoadedModelCount() const;
  [[nodiscard]] std::uint32_t GetInstanceCount() const;
  [[nodiscard]] std::uint32_t GetVisibleInstanceCount() const;

 private:
  void OnRendererDeviceWillReset() override;
  void OnRendererDeviceReady(api::DeviceGeneration generation) override;

  [[nodiscard]] M2InstanceStoreLease AcquireInstanceStoreLease(std::uint32_t root);
  std::unique_ptr<M2SystemState> state_;
  api::RendererContext* renderer_context_ = nullptr;
  bool renderer_observer_registered_ = false;
  bool restore_after_device_reset_ = false;
  api::DeviceGeneration renderer_device_generation_{};
};

class M2InstanceFramePrepareScope {
 public:
  explicit M2InstanceFramePrepareScope(M2System& system) : system_(system) {
    system_.BeginInstanceFramePrepare();
  }
  ~M2InstanceFramePrepareScope() { system_.EndInstanceFramePrepare(); }
  M2InstanceFramePrepareScope(const M2InstanceFramePrepareScope&) = delete;
  M2InstanceFramePrepareScope& operator=(const M2InstanceFramePrepareScope&) = delete;

  void Suspend() { system_.SuspendInstanceFramePrepare(); }
  void Resume() { system_.ResumeInstanceFramePrepare(); }
  [[nodiscard]] M2InstanceFrameSpatialResult PrepareSpatial(const M2InstanceFrameSpatialRequest& request) { return system_.PrepareInstanceSpatial(request); }
  [[nodiscard]] bool QueryRenderReady(const std::uint32_t instance) { return system_.QueryPreparedInstanceRenderReady(instance); }
  [[nodiscard]] bool SamplePresentation(const M2InstanceFramePresentationRequest& request) { return system_.SamplePreparedInstancePresentation(request); }
  [[nodiscard]] M2ResultStatus SetSharedBatchUniforms(const std::uint32_t instance, const M2SharedBatchUniformsHandle handle) { return system_.SetPreparedInstanceSharedBatchUniforms(instance, handle); }
  [[nodiscard]] M2ResultStatus ClearReplaceableTexturePath(const std::uint32_t instance, const std::uint32_t type) { return system_.ClearPreparedInstanceReplaceableTexturePath(instance, type); }

 private:
  M2System& system_;
};

class M2SharedBatchUniformsLease {
 public:
  M2SharedBatchUniformsLease() = default;
  M2SharedBatchUniformsLease(M2System& system, const M2BatchUniforms& uniforms)
      : system_(&system), handle_(system.PublishSharedBatchUniforms(uniforms)) {}
  ~M2SharedBatchUniformsLease() { Release(); }
  M2SharedBatchUniformsLease(M2SharedBatchUniformsLease&& other) noexcept
      : system_(other.system_), handle_(other.handle_) {
    other.system_ = nullptr;
    other.handle_ = {};
  }
  M2SharedBatchUniformsLease& operator=(M2SharedBatchUniformsLease&& other) noexcept {
    if (this != &other) {
      Release();
      system_ = other.system_;
      handle_ = other.handle_;
      other.system_ = nullptr;
      other.handle_ = {};
    }
    return *this;
  }
  M2SharedBatchUniformsLease(const M2SharedBatchUniformsLease&) = delete;
  M2SharedBatchUniformsLease& operator=(const M2SharedBatchUniformsLease&) = delete;

  [[nodiscard]] M2SharedBatchUniformsHandle handle() const noexcept { return handle_; }

 private:
  void Release() noexcept {
    if (system_ != nullptr && handle_) {
      system_->ReleaseSharedBatchUniforms(handle_);
    }
    system_ = nullptr;
    handle_ = {};
  }
  M2System* system_ = nullptr;
  M2SharedBatchUniformsHandle handle_{};
};

}

#include "openwow/render/m2/m2_system.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/m2/m2_camera_math.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_skin_profile.h"
#include "openwow/render/m2/m2_system_state.h"

#include <limits>

namespace openwow::render::m2 {

M2System::M2System() : state_(std::make_unique<M2SystemState>()) {}

M2System::~M2System() { BindRendererContext(nullptr); }

void M2System::BindRendererContext(api::RendererContext* renderer_context) {
  if (renderer_context_ == renderer_context) {
    return;
  }
  if (renderer_context_ != nullptr && renderer_observer_registered_) {
    renderer_context_->RemoveDeviceLifecycleObserver(*this);
  }
  renderer_context_ = renderer_context;
  renderer_observer_registered_ = false;
  if (renderer_context_ != nullptr) {
    renderer_context_->AddDeviceLifecycleObserver(*this);
    renderer_observer_registered_ = true;
    renderer_device_generation_ = renderer_context_->Generation();
  } else {
    renderer_device_generation_ = {};
  }
}

void M2System::OnRendererDeviceWillReset() {

  restore_after_device_reset_ = state_->initialized;
  state_->ReleaseDeviceResources();
}

void M2System::OnRendererDeviceReady(const api::DeviceGeneration generation) {
  renderer_device_generation_ = generation;
  if (!restore_after_device_reset_) {
    return;
  }
  restore_after_device_reset_ = false;
  diagnostics::Log(diagnostics::LogLevel::kInfo,
                   "M2System: model cache released for renderer device "
                   "restart; models re-commit on next resolve");

  static_cast<void>(state_->Initialize());
}

void M2System::BindTextureManager(TextureManager* value) { state_->texture_manager = value; state_->gpu_resources.BindTextureManager(value); }
void M2System::BindDbc(const data::dbc::DbcLoader* value) { state_->dbc = value; }
void M2System::BindFrameJobSystem(core::FrameJobSystem* jobs) {
  state_->frame_job_system = jobs;
  state_->animation_runtime.BindFrameJobSystem(jobs);
}
core::FrameJobSystem* M2System::frame_job_system() const { return state_->frame_job_system; }
void M2System::SetParticleDensity(float value) { state_->renderer.SetParticleDensity(value); }
TextureManager* M2System::texture_manager() const { return state_->texture_manager; }
void M2System::SetAsyncFileLoader(M2StreamFileLoader loader) { state_->resource_streamer.SetFileLoader(std::move(loader)); }
void M2System::SetAsyncPrefixFileLoader(M2StreamPrefixFileLoader loader) { state_->resource_streamer.SetPrefixFileLoader(std::move(loader)); }
void M2System::StartAsyncLoading(std::uint32_t workers) { state_->resource_streamer.Start(workers); }
void M2System::ShutdownAsyncLoading() { state_->resource_streamer.Shutdown(); }
M2StreamTicket M2System::AcquireModelAsync(
    const std::string& path, const M2StreamPriority priority) {
  return state_->resource_streamer.Acquire(path, priority);
}
void M2System::ReleaseModelAsync(M2StreamTicket ticket) { state_->resource_streamer.Release(ticket); }
M2StreamQuery M2System::QueryModelAsync(M2StreamTicket ticket) const { return state_->resource_streamer.Query(ticket); }
M2InstanceCreateResult M2System::CreateInstanceAsync(M2StreamTicket ticket) const { return state_->resource_streamer.CreateInstance(ticket); }
M2StreamPumpResult M2System::PumpAsyncLoading(const M2StreamCommitBudget& budget) { return state_->resource_streamer.Pump(budget); }
M2StreamPumpResult M2System::PumpAsyncLoading() { return state_->resource_streamer.Pump(); }

std::string M2System::BuildSkinProfilePath(std::string_view path, std::uint32_t profile) { return BuildM2SkinProfilePath(path, profile); }
M2CameraBasis M2System::BuildCameraBasis(const M2CameraPose& pose) { return BuildM2CameraBasis(pose); }
float M2System::BuildCameraVerticalFov(float fov, float aspect) { return BuildM2CameraVerticalFov(fov, aspect); }
M2CameraPose M2System::TransformCameraPoseByModelMatrix(const M2CameraPose& pose, RenderMatrix4x4View matrix) { return TransformM2CameraPoseByModelMatrix(pose, matrix); }
RenderMatrix4x4 M2System::BuildCameraViewMatrix(const M2CameraPose& pose) { return BuildM2CameraViewMatrix(pose); }

bool M2System::Initialize() { return state_->Initialize(); }
void M2System::Shutdown() { state_->resource_streamer.Shutdown(); state_->Shutdown(); }
void M2System::Reset() { state_->resource_streamer.Shutdown(); state_->Reset(); }
void M2System::SetFileLoader(M2StreamFileLoader loader) { state_->SetFileLoader(std::move(loader)); }
M2ResultStatus M2System::SetSkinProfileQualityFromRetailScalar(std::uint32_t quality) { return state_->SetSkinProfileQualityFromRetailScalar(quality); }
M2ModelLoadResult M2System::LoadModel(const std::string& path) { return state_->LoadModel(path); }
M2ModelLoadResult M2System::LoadModelForSampling(const std::string& path) { return state_->LoadModelForSampling(path); }
M2ModelPrepareResult M2System::PrepareModelForRender(const std::string& path) const { return state_->PrepareModelForRender(path); }
M2ModelPrepareResult M2System::PrepareModelForRender(const std::string& path, M2StreamFileLoader loader) const { return state_->PrepareModelForRender(path, std::move(loader)); }
M2ModelLoadResult M2System::CommitPreparedModel(std::unique_ptr<M2PreparedModel> prepared) { return state_->CommitPreparedModel(std::move(prepared)); }
M2ModelInstanceLoadResult M2System::LoadModelInstance(const std::string& path) { return state_->LoadModelInstance(path); }
M2ModelInstanceLoadResult M2System::LoadModelInstanceWithFallback(const std::string& path, std::string_view fallback) { return state_->LoadModelInstanceWithFallback(path, fallback); }
M2ModelInstanceLoadResult M2System::LoadAttachedChildInstance(std::uint32_t parent, const std::string& path, std::int32_t slot, M2ChildDestroyPolicy policy, std::string_view fallback) { return state_->LoadAttachedChildInstance(parent, path, slot, policy, fallback); }
M2ResultStatus M2System::UnloadModel(std::uint32_t model) { return state_->UnloadModel(model); }
M2ResultStatus M2System::RecycleModelIfUnused(std::uint32_t model) { return state_->RecycleModelIfUnused(model); }
void M2System::PrepareParticleDrawGeometry(std::span<const std::uint32_t> instance_ids, std::optional<RenderMatrix4x4View> view_matrix) { state_->PrepareParticleDrawGeometry(instance_ids, view_matrix); }

M2ModelReadinessQuery M2System::QueryModelReadiness(std::uint32_t id) const { return state_->model_queries.QueryReadiness(id); }
M2FirstAnimationDurationQuery M2System::QueryFirstAnimationDuration(std::uint32_t id) const { return state_->model_queries.QueryFirstAnimationDuration(id); }
M2ModelAnimationSequenceQuery M2System::QueryModelAnimationSequence(std::uint32_t id, std::uint32_t animation, std::int32_t sub) const { return state_->model_queries.QueryAnimationSequence(id, animation, sub); }
M2ModelCollisionGeometryQuery M2System::QueryModelCollisionGeometry(std::uint32_t id) const { return state_->model_queries.QueryCollisionGeometry(id); }
M2AttachmentInfoQuery M2System::QueryModelAttachmentInfo(std::uint32_t id, std::uint32_t lookup) const { return state_->model_queries.QueryAttachmentInfo(id, lookup); }
M2ModelSpatialInfoQuery M2System::QueryModelSpatialInfo(std::uint32_t id, std::uint16_t sequence) const { return state_->model_queries.QuerySpatialInfo(id, sequence); }
M2ModelGeometryInfoQuery M2System::QueryModelGeometryInfo(std::uint32_t id) const { return state_->model_queries.QueryGeometryInfo(id); }
M2ModelInfoQuery M2System::QueryModelInfo(std::uint32_t id) const { return state_->model_queries.QueryInfo(id); }
M2ModelAnimationListQuery M2System::QueryModelAnimationList(std::uint32_t id) const { return state_->model_queries.QueryAnimationList(id); }
M2ModelTextureDependenciesQuery M2System::QueryModelTextureDependencies(std::uint32_t id) const { return state_->model_queries.QueryTextureDependencies(id); }
M2ModelSubmeshSectionIdsQuery M2System::QueryModelSubmeshSectionIds(std::uint32_t id) const { return state_->model_queries.QuerySubmeshSectionIds(id); }
M2ModelLightCountQuery M2System::QueryModelLightCount(std::uint32_t id) const { return state_->model_queries.QueryLightCount(id); }
bool M2System::ModelHasSubmeshId(std::uint32_t id, std::uint16_t submesh) const { return state_->model_queries.HasSubmeshId(id, submesh); }
bool M2System::ModelHasAnimation(std::uint32_t id, std::uint32_t animation) const { return state_->model_queries.HasAnimation(id, animation); }
bool M2System::ModelContainsAnimation(std::uint32_t id, std::uint32_t animation) const { return state_->model_queries.ContainsAnimation(id, animation); }
M2SampleBoneMatricesQuery M2System::QuerySampleBoneMatrices(std::uint32_t id, int sequence, std::uint32_t time, const std::optional<RenderMatrix4x4View>& inverse) const { return state_->model_queries.QuerySampleBoneMatrices(id, sequence, time, inverse); }
M2CameraSampleQuery M2System::QueryCameraSample(std::uint32_t id, int camera, int sequence, std::uint32_t time) const { return state_->model_queries.QueryCameraSample(id, camera, sequence, time); }
M2LightSampleQuery M2System::QueryLightSample(std::uint32_t id, int light, int sequence, std::uint32_t time, std::span<const float> bones) const { return state_->model_queries.QueryLightSample(id, light, sequence, time, bones); }
std::vector<M2TriggeredEvent> M2System::CollectTriggeredEvents(std::uint32_t id, int sequence, std::uint32_t previous, std::uint32_t current, std::span<const float> bones, const std::optional<RenderMatrix4x4>& matrix) const { return state_->model_queries.CollectTriggeredEvents(id, sequence, previous, current, bones, matrix); }

M2InstanceCreateResult M2System::CreateInstance(std::uint32_t model) { return state_->CreateSeatedInstance(model); }
M2ResultStatus M2System::DestroyInstance(std::uint32_t instance) { return state_->DestroyInstance(instance); }
M2InstanceInfoQuery M2System::QueryInstanceInfo(std::uint32_t id) const { return state_->instances.QueryInfo(id); }
M2VisualTreeRevisionQuery M2System::QueryVisualTreeRevision(std::uint32_t id) const { return state_->instances.QueryVisualTreeRevision(id); }
M2InstanceStoreLease M2System::AcquireInstanceStoreLease(std::uint32_t id) { return state_->instances.AcquireLease(id); }
M2InstanceModelQuery M2System::QueryInstanceModel(std::uint32_t id) const { return state_->instances.QueryModel(id); }
M2ResultStatus M2System::SetInstanceModel(std::uint32_t id, std::uint32_t model) { return state_->instances.SetModel(id, model); }
M2ChildLinksQuery M2System::QueryChildLinks(std::uint32_t id) const { return state_->instances.QueryChildLinks(id); }
M2VisibleSubmeshFilterQuery M2System::QueryVisibleSubmeshFilter(std::uint32_t id) const { return state_->instances.QueryVisibleSubmeshFilter(id); }
M2InstanceAttachmentVisualStateQuery M2System::QueryInstanceAttachmentVisualState(std::uint32_t id) const { return state_->instances.QueryAttachmentVisualState(id); }
M2ResultStatus M2System::SetInstanceAttachmentVisualState(std::uint32_t id, const M2InstanceAttachmentVisualState& value) { return state_->instances.SetAttachmentVisualState(id, value); }
M2ResultStatus M2System::SetInstanceEffectContext(std::uint32_t id, const M2InstanceEffectContext& value) { return state_->instances.SetEffectContext(id, value); }
M2ResultStatus M2System::SetReplaceableTexturePath(std::uint32_t id, std::uint32_t type, const std::string& path) { return state_->instances.SetReplaceableTexturePath(id, type, path); }
M2ResultStatus M2System::ClearReplaceableTexturePath(std::uint32_t id, std::uint32_t type) { return state_->instances.ClearReplaceableTexturePath(id, type); }
M2ResultStatus M2System::ClearReplaceableTexturePaths(std::uint32_t id) { return state_->instances.ClearReplaceableTexturePaths(id); }
M2ResultStatus M2System::SetVisible(std::uint32_t id, bool value) { return state_->instances.SetVisible(id, value); }
M2ResultStatus M2System::SetEffectEmittersEnabled(std::uint32_t id, bool value) { return state_->instances.SetEffectEmittersEnabled(id, value); }
M2ResultStatus M2System::BeginTransientWeaponTrail(std::uint32_t id, std::uint32_t argb, std::uint32_t duration, std::uint32_t start) { return state_->instances.BeginTransientWeaponTrail(id, argb, duration, start); }
M2ResultStatus M2System::SetTintColor(std::uint32_t id, const RenderVec4& value) { return state_->instances.SetTintColor(id, value); }
M2ResultStatus M2System::SetAlpha(std::uint32_t id, float value) { return state_->instances.SetAlpha(id, value); }
M2ResultStatus M2System::SetSelectionGlowColor(std::uint32_t id, const RenderVec4& value) { return state_->instances.SetSelectionGlowColor(id, value); }
M2SelectionGlowColorQuery M2System::QuerySelectionGlowColor(std::uint32_t id) const { return state_->instances.QuerySelectionGlowColor(id); }
M2ResultStatus M2System::ApplyCreatureDisplayRecordOverrides(std::uint32_t id, const std::array<std::string, 3>& paths, std::optional<M2ParticleColorRecord> record) { return state_->instances.ApplyCreatureDisplayRecordOverrides(id, paths, std::move(record)); }
M2ResultStatus M2System::SetParticleColorOverride(std::uint32_t id, std::uint32_t color, std::uint32_t start, std::uint32_t mid, std::uint32_t end) { return state_->instances.SetParticleColorOverride(id, color, start, mid, end); }
M2ResultStatus M2System::ClearParticleColorOverride(std::uint32_t id, std::uint32_t color) { return state_->instances.ClearParticleColorOverride(id, color); }
M2ResultStatus M2System::SetReplacementColors(std::uint32_t id, const std::array<std::uint32_t, 3>& colors) { return state_->instances.SetReplacementColors(id, colors); }
M2ResultStatus M2System::ClearReplacementColors(std::uint32_t id) { return state_->instances.ClearReplacementColors(id); }
M2ResultStatus M2System::ApplyParticleColorRecordSlots11To13(std::uint32_t id, std::optional<M2ParticleColorRecord> record) { return state_->instances.ApplyParticleColorRecordSlots11To13(id, std::move(record)); }
M2ResultStatus M2System::SetWorldTransformMatrix(std::uint32_t id, const RenderMatrix4x4& value) { return state_->instances.SetWorldTransformMatrix(id, value); }
M2ResultStatus M2System::ClearWorldTransformMatrix(std::uint32_t id) { return state_->instances.ClearWorldTransformMatrix(id); }
M2ResultStatus M2System::SetTransform(std::uint32_t id, const std::optional<RenderVec3>& position, const std::optional<RenderVec3>& rotation, float scale) { return state_->instances.SetTransform(id, position, rotation, scale); }
M2ResultStatus M2System::SetPosition(std::uint32_t id, const RenderVec3& value) { return state_->instances.SetPosition(id, value); }
M2ResultStatus M2System::SetRotationDegrees(std::uint32_t id, const RenderVec3& value) { return state_->instances.SetRotationDegrees(id, value); }
M2ResultStatus M2System::SetScale(std::uint32_t id, float value) { return state_->instances.SetScale(id, value); }
M2ResultStatus M2System::SetVisibleSubmeshIndices(std::uint32_t id, std::vector<std::size_t> value) { return state_->instances.SetVisibleSubmeshIndices(id, std::move(value)); }
M2ResultStatus M2System::ClearVisibleSubmeshIndices(std::uint32_t id) { return state_->instances.ClearVisibleSubmeshIndices(id); }
M2ResultStatus M2System::SetBatchUniforms(std::uint32_t id, const M2BatchUniforms& value) { return state_->instances.SetBatchUniforms(id, value); }
M2SharedBatchUniformsHandle M2System::PublishSharedBatchUniforms(const M2BatchUniforms& value) { return state_->instances.PublishSharedBatchUniforms(value); }
void M2System::ReleaseSharedBatchUniforms(M2SharedBatchUniformsHandle handle) { state_->instances.ReleaseSharedBatchUniforms(handle); }
M2ResultStatus M2System::SetSharedBatchUniforms(std::uint32_t id, M2SharedBatchUniformsHandle handle) { return state_->instances.SetSharedBatchUniforms(id, handle); }
M2ResultStatus M2System::SetDoodadFrameRenderState(std::uint32_t id, const RenderMatrix4x4& world_transform, const M2BatchUniforms& uniforms, const RenderVec4& tint_rgba, float alpha) { return state_->instances.SetDoodadFrameRenderState(id, world_transform, uniforms, tint_rgba, alpha); }
void M2System::SetDoodadFrameRenderStates(std::span<const M2DoodadFrameRenderRequest> requests, std::span<M2ResultStatus> out_statuses) { state_->instances.SetDoodadFrameRenderStates(requests, out_statuses); }
M2ResultStatus M2System::SetVisualPresentation(std::uint32_t id, bool visible, const RenderVec4& tint_rgba, float alpha) { return state_->instances.SetVisualPresentation(id, visible, tint_rgba, alpha); }
M2TransformVisibilityResult M2System::SetTransformAndVisibility(std::uint32_t id, const RenderMatrix4x4& matrix, bool visible) { return state_->instances.SetTransformAndVisibility(id, matrix, visible); }
M2ResultStatus M2System::SetAttachmentFrameRenderState(std::uint32_t id, const RenderMatrix4x4& world_transform, const M2BatchUniforms& uniforms, bool visible, const RenderVec4& tint_rgba, float alpha) { return state_->instances.SetAttachmentFrameRenderState(id, world_transform, uniforms, visible, tint_rgba, alpha); }
void M2System::SetAttachmentFrameRenderStates(std::span<const M2AttachmentFrameRenderRequest> requests, std::span<M2ResultStatus> out_statuses) { state_->instances.SetAttachmentFrameRenderStates(requests, out_statuses); }
M2ResultStatus M2System::ClearBatchUniforms(std::uint32_t id) { return state_->instances.ClearBatchUniforms(id); }
M2ResultStatus M2System::AttachChildInstance(std::uint32_t parent, std::uint32_t child, M2ChildDestroyPolicy policy) { return state_->instances.AttachChild(parent, child, kM2NoAttachmentLookupIndex, policy); }
M2ResultStatus M2System::AttachChildInstance(std::uint32_t parent, std::uint32_t child, std::int32_t slot, M2ChildDestroyPolicy policy) { return state_->instances.AttachChild(parent, child, slot, policy); }
M2ResultStatus M2System::SetAttachedModelVisualSelectorFlag(std::uint32_t id, bool value) { return state_->instances.SetAttachedModelVisualSelectorFlag(id, value); }
M2PrimaryVisualStateResult M2System::EnablePrimaryVisualState(std::uint32_t id, float duration) { return state_->instances.EnablePrimaryVisualState(id, duration); }
M2ResultStatus M2System::DisablePrimaryVisualState(std::uint32_t id, bool reset) { return state_->instances.DisablePrimaryVisualState(id, reset); }
M2ResultStatus M2System::ResetPrimaryVisualStateForLightning(std::uint32_t id) { return state_->instances.ResetPrimaryVisualStateForLightning(id); }

M2ResultStatus M2System::SetAnimationChangedCallback(std::uint32_t id, M2AnimationChangedCallback callback) { return state_->animation_runtime.SetAnimationChangedCallback(id, std::move(callback)); }
M2ResultStatus M2System::ClearAnimationChangedCallback(std::uint32_t id) { return state_->animation_runtime.ClearAnimationChangedCallback(id); }
M2ResultStatus M2System::SetAnimationRequestCallback(std::uint32_t id, M2AnimationRequestCallback callback) { return state_->animation_runtime.SetAnimationRequestCallback(id, std::move(callback)); }
M2ResultStatus M2System::ClearAnimationRequestCallback(std::uint32_t id) { return state_->animation_runtime.ClearAnimationRequestCallback(id); }
M2ResultStatus M2System::SetAnimationCompletionCallback(std::uint32_t id, M2AnimationCompletionCallback callback) { return state_->animation_runtime.SetAnimationCompletionCallback(id, std::move(callback)); }
M2ResultStatus M2System::ClearAnimationCompletionCallback(std::uint32_t id) { return state_->animation_runtime.ClearAnimationCompletionCallback(id); }
M2ResultStatus M2System::SetTriggeredEventCallback(std::uint32_t id, M2TriggeredEventCallback callback) { return state_->animation_runtime.SetTriggeredEventCallback(id, std::move(callback)); }
M2ResultStatus M2System::ClearTriggeredEventCallback(std::uint32_t id) { return state_->animation_runtime.ClearTriggeredEventCallback(id); }
M2ResultStatus M2System::SetAnimationRequest(std::uint32_t id, const M2AnimationRequest& request) { return state_->animation_runtime.SetAnimationRequest(id, request); }
M2ResultStatus M2System::SetAnimation(std::uint32_t id, std::uint32_t animation, float speed) { return state_->animation_runtime.SetAnimation(id, animation, speed); }
M2ResultStatus M2System::SetAnimationSample(std::uint32_t id, std::uint32_t animation, std::uint32_t time, float speed, bool zero_blend) { return state_->animation_runtime.SetAnimationSample(id, animation, time, speed, zero_blend); }
M2ResultStatus M2System::SetAnimationSequenceSample(std::uint32_t id, std::uint16_t sequence, std::uint32_t time, float speed) { return state_->animation_runtime.SetAnimationSequenceSample(id, sequence, time, speed); }
M2ResultStatus M2System::SetAnimationSlotRequest(std::uint32_t id, std::uint32_t slot, const M2AnimationRequest& request) { return state_->animation_runtime.SetAnimationSlotRequest(id, slot, request); }
M2ResultStatus M2System::SetAnimationSlotSample(std::uint32_t id, std::uint32_t slot, std::uint32_t animation, std::uint32_t time, float speed, bool zero_blend) { return state_->animation_runtime.SetAnimationSlotSample(id, slot, animation, time, speed, zero_blend); }
M2ResultStatus M2System::SetAnimationSlotTimes(std::uint32_t id, std::span<const std::uint32_t> slots, std::uint32_t time) { return state_->animation_runtime.SetAnimationSlotTimes(id, slots, time); }
M2ResultStatus M2System::ClearAnimationSlot(std::uint32_t id, std::uint32_t slot) { return state_->animation_runtime.ClearAnimationSlot(id, slot); }
M2ResultStatus M2System::SetKeyBoneBasisOverride(std::uint32_t id, std::uint32_t key_bone_lookup, const RenderMatrix4x4& basis) { return state_->animation_runtime.SetKeyBoneBasisOverride(id, key_bone_lookup, basis); }
M2ResultStatus M2System::ClearKeyBoneBasisOverride(std::uint32_t id, std::uint32_t key_bone_lookup) { return state_->animation_runtime.ClearKeyBoneBasisOverride(id, key_bone_lookup); }
M2AnimationSlotStateQuery M2System::QueryAnimationSlotState(std::uint32_t id, std::uint32_t slot) const { return state_->animation_runtime.QueryAnimationSlotState(id, slot); }
M2ResultStatus M2System::CopyActiveAnimationState(std::uint32_t source, std::uint32_t destination) { return state_->animation_runtime.CopyActiveAnimationState(source, destination); }
void M2System::BeginInstanceFramePrepare() { state_->frame_preparer.Begin(); }
void M2System::EndInstanceFramePrepare() { state_->frame_preparer.End(); }
void M2System::SuspendInstanceFramePrepare() { state_->frame_preparer.Suspend(); }
void M2System::ResumeInstanceFramePrepare() { state_->frame_preparer.Resume(); }
M2InstanceFrameSpatialResult M2System::PrepareInstanceSpatial(const M2InstanceFrameSpatialRequest& request) { return state_->frame_preparer.PrepareSpatial(request); }
bool M2System::QueryPreparedInstanceRenderReady(std::uint32_t id) { return state_->frame_preparer.QueryRenderReady(id); }
bool M2System::SamplePreparedInstancePresentation(const M2InstanceFramePresentationRequest& request) { return state_->frame_preparer.SamplePresentation(request); }
M2ResultStatus M2System::SetPreparedInstanceSharedBatchUniforms(std::uint32_t id, M2SharedBatchUniformsHandle handle) { return state_->frame_preparer.SetSharedBatchUniforms(id, handle); }
M2ResultStatus M2System::ClearPreparedInstanceReplaceableTexturePath(std::uint32_t id, std::uint32_t type) { return state_->frame_preparer.ClearReplaceableTexturePath(id, type); }
M2ResultStatus M2System::UpdateAnimation(std::uint32_t id, float delta) { return state_->animation_runtime.UpdateAnimation(id, delta); }
void M2System::UpdateAnimations(std::span<const std::uint32_t> instances, float delta,
                                std::vector<std::uint32_t>* missing_instances) {
  state_->animation_runtime.UpdateAnimations(instances, delta, missing_instances);
}
void M2System::UpdateAllAnimations(float delta) { state_->animation_runtime.UpdateAllAnimations(delta); }

void M2System::UpdateAllEffects(float frame_delta_seconds, std::optional<RenderMatrix4x4View> view_matrix) { state_->renderer.UpdateAllEffects(frame_delta_seconds, view_matrix, state_->frame_job_system); }

M2RenderInstanceResult M2System::RenderInstance(std::uint16_t view, std::uint32_t id, bgfx::Encoder* encoder) { return state_->renderer.Render(view, id, std::nullopt, M2RenderPassScope::kAll, encoder); }
M2RenderInstanceResult M2System::RenderInstance(std::uint16_t view, std::uint32_t id, M2RenderPassScope scope, bgfx::Encoder* encoder) { return state_->renderer.Render(view, id, std::nullopt, scope, encoder); }
M2RenderInstanceResult M2System::RenderInstance(std::uint16_t view, std::uint32_t id, RenderMatrix4x4View matrix, bgfx::Encoder* encoder) { return state_->renderer.Render(view, id, matrix, M2RenderPassScope::kAll, encoder); }
M2RenderInstanceResult M2System::RenderInstance(std::uint16_t view, std::uint32_t id, RenderMatrix4x4View matrix, M2RenderPassScope scope, bgfx::Encoder* encoder) { return state_->renderer.Render(view, id, matrix, scope, encoder); }
void M2System::RenderInstanceBatch(std::uint16_t view, std::span<const std::uint32_t> instance_ids, std::optional<RenderMatrix4x4View> view_matrix, M2RenderPassScope scope, core::FrameJobSystem* jobs, double per_instance_microseconds, std::span<M2RenderInstanceResult> out_results, bgfx::Encoder* serial_encoder) { state_->renderer.RenderBatch(view, instance_ids, view_matrix, scope, jobs, per_instance_microseconds, out_results, serial_encoder); }
void M2System::BindRenderSubmitTrace(std::optional<RenderSubmitTraceBinding> binding) { state_->renderer.BindSubmitTrace(std::move(binding)); }
bool M2System::IsRenderSubmitTraceBound() const { return state_->renderer.IsSubmitTraceBound(); }
M2StaticInstancingProfile M2System::QueryStaticInstancingProfile(std::uint32_t id) { return state_->renderer.QueryStaticInstancingProfile(id); }
bool M2System::QueryOpaqueDepthTestGuaranteed(std::uint32_t id) { return state_->renderer.QueryOpaqueDepthTestGuaranteed(id); }
M2RenderInstanceResult M2System::RenderInstancedGroup(std::uint16_t view, std::uint32_t exemplar_instance, std::span<const M2InstancedDrawRecord> records, const M2BatchUniforms& base_uniforms, bgfx::Encoder* encoder) { return state_->renderer.RenderInstancedGroup(view, exemplar_instance, records, base_uniforms, encoder); }
M2ShadowClassQuery M2System::QueryShadowClass(std::uint32_t id) const { return state_->renderer.QueryShadowClass(id); }
M2ShadowDrawCallLists M2System::BuildShadowDrawCalls(std::uint32_t id) const { return state_->renderer.BuildShadowDrawCalls(id); }
void M2System::SetProjectedTexturesEnabled(bool enabled) { state_->renderer.SetProjectedTexturesEnabled(enabled); }
std::vector<M2ProjectedTextureDraw> M2System::TakeProjectedTextureDraws() { return state_->renderer.TakeProjectedTextureDraws(); }

M2InstanceReadinessQuery M2System::QueryInstanceReadiness(std::uint32_t id) const { return state_->spatial_queries.QueryReadiness(id); }
M2InstanceAnimationInfoQuery M2System::QueryInstanceAnimationInfo(std::uint32_t id) const { return state_->spatial_queries.QueryAnimationInfo(id); }
M2InstanceEventQuery M2System::QueryInstanceEvent(std::uint32_t id, std::uint32_t animation, std::uint32_t event) const { return state_->spatial_queries.QueryEvent(id, animation, event); }
M2InstanceSpatialInfoQuery M2System::QueryInstanceSpatialInfo(std::uint32_t id) const { return state_->spatial_queries.QuerySpatialInfo(id); }
M2SegmentIntersectionQuery M2System::QueryClosestSegmentIntersection(std::uint32_t id, const RenderVec3& start, const RenderVec3& end) const { return state_->spatial_queries.QueryClosestSegmentIntersection(id, start, end); }
bool M2System::InstanceModelHasAnimation(std::uint32_t id, std::uint32_t animation) const { return state_->spatial_queries.ModelHasAnimation(id, animation); }
M2BoneMatricesQuery M2System::QueryBoneMatrices(std::uint32_t id) { return state_->spatial_queries.QueryBoneMatrices(id); }
M2KeyBoneQuery M2System::QueryKeyBone(std::uint32_t id, std::uint32_t lookup) const { return state_->spatial_queries.QueryKeyBone(id, lookup); }
M2SampleBoneMatricesQuery M2System::QueryInstanceSampleBoneMatrices(std::uint32_t id, const std::optional<RenderMatrix4x4View>& inverse) const { return state_->spatial_queries.QuerySampleBoneMatrices(id, inverse); }
M2CameraSampleQuery M2System::QueryInstanceCameraSample(std::uint32_t id, int camera) const { return state_->spatial_queries.QueryCameraSample(id, camera, M2CameraLookupKind::kIndex); }
M2CameraSampleQuery M2System::QueryInstanceCameraSampleByType(std::uint32_t id, std::uint32_t type) const { if (type > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) return {.status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kMissingCamera, .detail = "camera_type=" + std::to_string(type)}; return state_->spatial_queries.QueryCameraSample(id, static_cast<int>(type), M2CameraLookupKind::kType); }
M2LightSampleQuery M2System::QueryInstanceLightSample(std::uint32_t id, int light, std::span<const float> bones) const { return state_->spatial_queries.QueryLightSample(id, light, bones); }
std::vector<M2TriggeredEvent> M2System::CollectInstanceTriggeredEvents(std::uint32_t id, std::uint32_t previous, std::uint32_t current, std::span<const float> bones, const std::optional<RenderMatrix4x4>& matrix) const { return state_->spatial_queries.CollectTriggeredEvents(id, previous, current, bones, matrix); }
M2AttachmentInfoQuery M2System::QueryAttachmentInfo(std::uint32_t id, std::uint32_t lookup) const { return state_->spatial_queries.QueryAttachmentInfo(id, lookup); }
M2AttachmentPositionQuery M2System::QueryAttachmentPosition(std::uint32_t id, std::uint32_t lookup, const std::optional<RenderVec3>& offset) const { return state_->spatial_queries.QueryAttachmentPosition(id, lookup, offset); }
M2AttachmentTransformQuery M2System::QueryAttachmentTransformMatrix(std::uint32_t id, std::uint32_t lookup) const { return state_->spatial_queries.QueryAttachmentTransformMatrix(id, lookup); }
void M2System::QueryAttachmentPlacements(std::span<const M2AttachmentPlacementRequest> requests, std::span<M2AttachmentPlacementQuery> out_results) const { state_->spatial_queries.QueryAttachmentPlacements(requests, out_results); }
M2ModelWorldPointQuery M2System::QueryModelWorldPoint(std::uint32_t id) const { return state_->spatial_queries.QueryModelWorldPoint(id); }
M2ModelWorldTransformMatrixQuery M2System::QueryModelWorldTransformMatrix(std::uint32_t id) const { return state_->spatial_queries.QueryModelWorldTransformMatrix(id); }
M2RootBoneWorldMatrixQuery M2System::QueryRootBoneWorldMatrix(std::uint32_t id) const { return state_->spatial_queries.QueryRootBoneWorldMatrix(id); }
std::optional<SelectionTriangleVertices> M2System::BuildSelectionTriangleVerticesForSample(std::uint32_t id, const std::vector<float>& bones, std::size_t count, const RenderMatrix4x4& matrix) const { return state_->spatial_queries.BuildSelectionTriangleVerticesForSample(id, bones, count, matrix); }
M2WorldBoundingSphereQuery M2System::QueryWorldBoundingSphere(std::uint32_t id) const { return state_->spatial_queries.QueryWorldBoundingSphere(id); }
M2WorldBoundingBoxQuery M2System::QueryWorldBoundingBox(std::uint32_t id) const { return state_->spatial_queries.QueryWorldBoundingBox(id); }
M2ModelBoundingSphereQuery M2System::QueryInstanceModelBoundingSphere(std::uint32_t id) const { return state_->spatial_queries.QueryModelBoundingSphere(id); }

void M2System::UpdateVisibility() { state_->visibility.Update(); }
void M2System::UpdateVisibility(RenderMatrix4x4View matrix) { state_->visibility.Update(matrix); }
std::uint32_t M2System::GetLoadedModelCount() const { std::lock_guard lock(state_->mutex); return static_cast<std::uint32_t>(state_->models.size()); }
std::uint32_t M2System::GetInstanceCount() const { std::lock_guard lock(state_->mutex); return static_cast<std::uint32_t>(state_->instances.size()); }
std::uint32_t M2System::GetVisibleInstanceCount() const { return state_->visibility.visible_count(); }

}

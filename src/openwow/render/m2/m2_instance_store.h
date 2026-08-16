#pragma once

#include "openwow/render/m2/m2_model_repository.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <functional>
#include <mutex>
#include <span>
#include <unordered_map>

namespace openwow::render::m2 {

inline constexpr std::size_t kM2InstanceBucketFloor = 4096u;

inline constexpr std::size_t kM2EmitterCarrierBucketFloor = 512u;

class M2InstanceStore {
 public:
  using Map =
      std::unordered_map<std::uint32_t, std::unique_ptr<detail::M2Instance>>;
  using ModelStore = std::unordered_map<
      std::uint32_t, std::unique_ptr<detail::M2ModelResource>>;
  using DeferredCallbacks = std::vector<std::function<void()>>;
  using DestroyCallbackCollector = std::function<void(
      detail::M2Instance&, const detail::M2ModelResource&,
      DeferredCallbacks*)>;

  M2InstanceStore(M2SystemMutex& mutex, ModelStore& models,
                  TextureManager*& texture_manager);
  ~M2InstanceStore();

  M2InstanceStore(const M2InstanceStore&) = delete;
  M2InstanceStore& operator=(const M2InstanceStore&) = delete;

  [[nodiscard]] Map& instances() noexcept { return instances_; }
  [[nodiscard]] const Map& instances() const noexcept { return instances_; }
  [[nodiscard]] bool empty() const noexcept { return instances_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return instances_.size(); }
  [[nodiscard]] auto begin() noexcept { return instances_.begin(); }
  [[nodiscard]] auto end() noexcept { return instances_.end(); }
  [[nodiscard]] auto begin() const noexcept { return instances_.begin(); }
  [[nodiscard]] auto end() const noexcept { return instances_.end(); }
  [[nodiscard]] auto find(const std::uint32_t id) { return instances_.find(id); }
  [[nodiscard]] auto find(const std::uint32_t id) const {
    return instances_.find(id);
  }
  [[nodiscard]] bool contains(const std::uint32_t id) const {
    return instances_.contains(id);
  }

  using EffectCarrierMap =
      std::unordered_map<std::uint32_t, detail::M2Instance*>;
  [[nodiscard]] const EffectCarrierMap& effect_carriers() const noexcept {
    return effect_carriers_;
  }

  [[nodiscard]] bool HasInstancesOfModel(std::uint32_t model_id) const noexcept;

  [[nodiscard]] M2InstanceCreateResult Create(std::uint32_t model_id);
  [[nodiscard]] M2ResultStatus Destroy(
      std::uint32_t instance_id,
      const DestroyCallbackCollector& collect_callbacks);
  void DestroyByModelLocked(std::uint32_t model_id,
                            const DestroyCallbackCollector& collect_callbacks,
                            DeferredCallbacks* callbacks);
  void ResetLocked();

  [[nodiscard]] M2InstanceInfoQuery QueryInfo(std::uint32_t instance_id) const;
  [[nodiscard]] M2VisualTreeRevisionQuery QueryVisualTreeRevision(
      std::uint32_t source_instance_id) const;
  [[nodiscard]] M2InstanceModelQuery QueryModel(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2ResultStatus SetModel(std::uint32_t instance_id,
                                        std::uint32_t model_id);
  [[nodiscard]] M2ChildLinksQuery QueryChildLinks(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2VisibleSubmeshFilterQuery QueryVisibleSubmeshFilter(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2InstanceAttachmentVisualStateQuery QueryAttachmentVisualState(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2ResultStatus SetAttachmentVisualState(
      std::uint32_t instance_id,
      const M2InstanceAttachmentVisualState& visual_state);
  [[nodiscard]] M2ResultStatus SetEffectContext(
      std::uint32_t instance_id,
      const M2InstanceEffectContext& effect_context);
  [[nodiscard]] M2ResultStatus AttachChild(
      std::uint32_t parent_instance_id, std::uint32_t child_instance_id,
      std::int32_t attachment_slot, M2ChildDestroyPolicy destroy_policy);

  [[nodiscard]] M2ResultStatus SetWorldTransformMatrix(
      std::uint32_t instance_id, const RenderMatrix4x4& matrix);
  [[nodiscard]] M2ResultStatus ClearWorldTransformMatrix(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus SetTransform(
      std::uint32_t instance_id, const std::optional<RenderVec3>& position,
      const std::optional<RenderVec3>& rotation_degrees, float scale);
  [[nodiscard]] M2ResultStatus SetPosition(std::uint32_t instance_id,
                                            const RenderVec3& position);
  [[nodiscard]] M2ResultStatus SetRotationDegrees(
      std::uint32_t instance_id, const RenderVec3& rotation_degrees);
  [[nodiscard]] M2ResultStatus SetScale(std::uint32_t instance_id, float scale);

  [[nodiscard]] M2ResultStatus SetReplaceableTexturePath(
      std::uint32_t instance_id, std::uint32_t texture_type,
      const std::string& texture_path);
  [[nodiscard]] M2ResultStatus ClearReplaceableTexturePath(
      std::uint32_t instance_id, std::uint32_t texture_type);
  [[nodiscard]] M2ResultStatus ClearReplaceableTexturePaths(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus SetVisible(std::uint32_t instance_id,
                                           bool visible);

  [[nodiscard]] M2ResultStatus SetDoodadFrameRenderState(
      std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
      const M2BatchUniforms& uniforms, const RenderVec4& tint_rgba,
      float alpha);

  void SetDoodadFrameRenderStates(
      std::span<const M2DoodadFrameRenderRequest> requests,
      std::span<M2ResultStatus> out_statuses);

  [[nodiscard]] M2ResultStatus SetVisualPresentation(
      std::uint32_t instance_id, bool visible, const RenderVec4& tint_rgba,
      float alpha);

  [[nodiscard]] M2TransformVisibilityResult SetTransformAndVisibility(
      std::uint32_t instance_id, const RenderMatrix4x4& matrix, bool visible);

  [[nodiscard]] M2ResultStatus SetAttachmentFrameRenderState(
      std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
      const M2BatchUniforms& uniforms, bool visible,
      const RenderVec4& tint_rgba, float alpha);

  void SetAttachmentFrameRenderStates(
      std::span<const M2AttachmentFrameRenderRequest> requests,
      std::span<M2ResultStatus> out_statuses);
  [[nodiscard]] M2ResultStatus SetEffectEmittersEnabled(
      std::uint32_t instance_id, bool enabled);
  [[nodiscard]] M2ResultStatus BeginTransientWeaponTrail(
      std::uint32_t instance_id, std::uint32_t packed_argb,
      std::uint32_t duration_ms, std::uint32_t start_tick_ms);
  [[nodiscard]] M2ResultStatus SetTintColor(std::uint32_t instance_id,
                                             const RenderVec4& rgba);
  [[nodiscard]] M2ResultStatus SetAlpha(std::uint32_t instance_id, float alpha);
  [[nodiscard]] M2ResultStatus SetSelectionGlowColor(
      std::uint32_t instance_id, const RenderVec4& rgba);
  [[nodiscard]] M2SelectionGlowColorQuery QuerySelectionGlowColor(
      std::uint32_t instance_id) const;
  [[nodiscard]] M2ResultStatus ApplyCreatureDisplayRecordOverrides(
      std::uint32_t instance_id,
      const std::array<std::string, 3>& texture_paths,
      std::optional<M2ParticleColorRecord> record);
  [[nodiscard]] M2ResultStatus SetParticleColorOverride(
      std::uint32_t instance_id, std::uint32_t color_index,
      std::uint32_t start_raw, std::uint32_t mid_raw, std::uint32_t end_raw);
  [[nodiscard]] M2ResultStatus ClearParticleColorOverride(
      std::uint32_t instance_id, std::uint32_t color_index);
  [[nodiscard]] M2ResultStatus SetReplacementColors(
      std::uint32_t instance_id,
      const std::array<std::uint32_t, 3>& colors);
  [[nodiscard]] M2ResultStatus ClearReplacementColors(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus ApplyParticleColorRecordSlots11To13(
      std::uint32_t instance_id,
      std::optional<M2ParticleColorRecord> record);
  [[nodiscard]] M2ResultStatus SetVisibleSubmeshIndices(
      std::uint32_t instance_id,
      std::vector<std::size_t> visible_submesh_indices);
  [[nodiscard]] M2ResultStatus ClearVisibleSubmeshIndices(
      std::uint32_t instance_id);
  [[nodiscard]] M2ResultStatus SetBatchUniforms(
      std::uint32_t instance_id, const M2BatchUniforms& uniforms);
  [[nodiscard]] M2ResultStatus ClearBatchUniforms(
      std::uint32_t instance_id);

  [[nodiscard]] M2SharedBatchUniformsHandle PublishSharedBatchUniforms(
      const M2BatchUniforms& uniforms);

  void ReleaseSharedBatchUniforms(M2SharedBatchUniformsHandle handle);

  [[nodiscard]] M2ResultStatus SetSharedBatchUniforms(
      std::uint32_t instance_id, M2SharedBatchUniformsHandle handle);
  [[nodiscard]] M2ResultStatus SetAttachedModelVisualSelectorFlag(
      std::uint32_t instance_id, bool enabled);
  [[nodiscard]] M2PrimaryVisualStateResult EnablePrimaryVisualState(
      std::uint32_t instance_id, float duration_seconds);
  [[nodiscard]] M2ResultStatus DisablePrimaryVisualState(
      std::uint32_t instance_id, bool reset_on_detach);
  [[nodiscard]] M2ResultStatus ResetPrimaryVisualStateForLightning(
      std::uint32_t instance_id);

  [[nodiscard]] M2InstanceStoreLease AcquireLease(
      std::uint32_t root_instance_id);
  void SetDestroyCallbackCollector(DestroyCallbackCollector collector);
  void RevokeLeases() noexcept;

  [[nodiscard]] static M2ResultStatus ApplyWorldTransformMatrixLocked(
      detail::M2Instance& instance, const RenderMatrix4x4& matrix) noexcept;

  [[nodiscard]] static M2ResultStatus ApplyVisualPresentationLocked(
      detail::M2Instance& instance, bool visible, const RenderVec4& tint_rgba,
      float alpha);

  [[nodiscard]] M2ResultStatus ApplySharedBatchUniformsLocked(
      detail::M2Instance& instance, M2SharedBatchUniformsHandle handle);

  static void ApplyClearReplaceableTexturePathLocked(detail::M2Instance& instance,
                                                     std::uint32_t texture_type);

 private:
  void DestroyRecursiveLocked(std::uint32_t instance_id,
                              const DestroyCallbackCollector& collect_callbacks,
                              DeferredCallbacks* callbacks);
  void EraseLocked(std::uint32_t instance_id);

  void SyncEffectCarrierLocked(std::uint32_t instance_id,
                               detail::M2Instance& instance,
                               std::uint32_t model_id);

  [[nodiscard]] M2ResultStatus ApplyDoodadFrameRenderStateLocked(
      std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
      std::uint64_t world_transform_revision, const M2BatchUniforms* uniforms,
      M2SharedBatchUniformsHandle shared_uniforms, const RenderVec4& tint_rgba,
      float alpha);

  [[nodiscard]] M2ResultStatus ApplyAttachmentFrameRenderStateLocked(
      std::uint32_t instance_id, const RenderMatrix4x4& world_transform,
      const M2BatchUniforms* uniforms,
      M2SharedBatchUniformsHandle shared_uniforms, bool visible,
      const RenderVec4& tint_rgba, float alpha);

  static void StoreExplicitWorldTransformLocked(
      detail::M2Instance& instance, const RenderMatrix4x4& world_transform,
      const std::uint64_t world_transform_revision = 0u) noexcept {
    instance.world_transform = world_transform;
    instance.has_explicit_world_transform = true;
    instance.explicit_world_transform_revision = world_transform_revision;
    instance.dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  }

  void StoreBatchUniformsLocked(detail::M2Instance& instance,
                                const M2BatchUniforms& uniforms);

  void AdoptSharedBatchUniformsLocked(detail::M2Instance& instance,
                                      detail::M2SharedBatchUniforms* block);

  void ReleaseInstanceBatchUniformsLocked(detail::M2Instance& instance);

  [[nodiscard]] detail::M2SharedBatchUniforms*
  AllocateSharedBatchUniformsLocked();

  void ReleaseSharedBatchUniformsLocked(detail::M2SharedBatchUniforms* block);
  static void BumpVisualRevision(detail::M2Instance& instance) noexcept;
  static void ResetModelDependentState(detail::M2Instance& instance);

  void RetainModelInstanceLocked(std::uint32_t model_id);
  void ReleaseModelInstanceLocked(std::uint32_t model_id) noexcept;

  M2SystemMutex& mutex_;
  ModelStore& models_;
  TextureManager*& texture_manager_;
  Map instances_;
  EffectCarrierMap effect_carriers_;

  std::vector<std::unique_ptr<detail::M2SharedBatchUniforms>>
      shared_batch_uniforms_pool_;
  std::vector<detail::M2SharedBatchUniforms*> shared_batch_uniforms_free_;

  std::unordered_map<std::uint32_t, std::uint32_t> instance_count_by_model_;
  std::uint32_t next_instance_id_ = 1;
  std::shared_ptr<std::function<void(std::uint32_t)>> lease_control_;
  DestroyCallbackCollector destroy_callback_collector_;
};

}

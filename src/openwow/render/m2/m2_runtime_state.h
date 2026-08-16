#pragma once

#include "openwow/core/client_crt_random.h"
#include "openwow/data/model/m2_model.h"
#include "openwow/render/m2/m2_particle_system.h"
#include "openwow/render/m2/m2_shaders.h"
#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/render/m2/m2_skinned_mesh.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/m2/m2_ribbon_emitter.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render::m2::detail {

struct M2SharedBatchUniforms {
  M2BatchUniforms uniforms{};
  std::uint32_t ref_count = 0;
};

struct M2PoseCache {
  bool valid = false;

  bool pose_time_invariant = false;
  std::uint16_t blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint32_t model_id = 0;
  int animation_index = 0;
  std::uint32_t animation_time_ms = 0;
  std::uint32_t blend_source_time_ms = 0;
  float blend_factor = 1.0f;

  std::uint64_t animation_state_generation = 0;
  std::vector<float> bone_matrices;
  std::optional<RenderMatrix4x4> camera_inverse_view;
};

struct M2Instance {
  static constexpr std::uint32_t kDirtyWorldTransform = kM2InstanceDirtyWorldTransform;
  static constexpr std::uint32_t kDirtyVisibleSubmeshes = kM2InstanceDirtyVisibleSubmeshes;
  using AnimationChangedCallback = M2AnimationChangedCallback;
  using AnimationRequestCallback = M2AnimationRequestCallback;
  using AnimationCompletionCallback = M2AnimationCompletionCallback;
  using TriggeredEventCallback = M2TriggeredEventCallback;
  using ParticleColorRecord = M2ParticleColorRecord;

  struct ParticleColorOverride {
    std::uint32_t color_index = 0;
    std::uint32_t start_raw = 0;
    std::uint32_t mid_raw = 0;
    std::uint32_t end_raw = 0;
  };

  static constexpr std::int32_t kNoAttachmentSlot = kM2NoAttachmentLookupIndex;

  struct ChildLink {
    std::uint32_t instance_id = 0;
    M2ChildDestroyPolicy destroy_policy = M2ChildDestroyPolicy::kDestroyWithParent;
    std::int32_t attachment_slot = kNoAttachmentSlot;
  };

  struct PrimaryVisualEntryState {
    std::uint32_t flags = 0;
  };

  struct PendingBaseAnimation {
    enum class Kind : std::uint8_t {
      kRequest,
      kSequenceSample,
    };

    Kind kind = Kind::kRequest;
    M2AnimationRequest request{};
    M2AnimationSelectionResult selection{};
    std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
    std::uint32_t sample_time_ms = 0;
    float speed = 1.0f;
    bool has_sample_time = false;
  };

  struct PendingSlotAnimation {
    M2AnimationRequest request{};
    M2AnimationSelectionResult selection{};
    std::uint32_t sample_time_ms = 0;
    float speed = 1.0f;
    bool has_sample_time = false;
  };

  struct TextureOverride {
    std::uint32_t type_id = 0;
    std::string texture_path;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    TextureLease texture_lease;
  };

  std::uint32_t model_id = 0;
  std::uint32_t dirty_flags = 0;

  std::uint64_t visual_revision = 1;
  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};

  std::uint64_t explicit_world_transform_revision = 0;

  M2SharedBatchUniforms* batch_uniforms_block = nullptr;

  RenderVec4 tint_color{1.0f, 1.0f, 1.0f, 1.0f};
  float alpha = 1.0f;
  float animation_time = 0.0f;
  float animation_speed = 1.0f;
  bool has_explicit_world_transform = false;
  bool visible = true;
  bool has_visible_submesh_filter = false;

  bool base_clock_sample_driven = false;

  bool current_animation_play_once = false;

  bool pose_playback_bound = false;

  bool animation_sample_clock_rebased = false;
  bool animation_completion_fired = false;
  std::uint32_t animation_id = 0;
  std::uint32_t resolved_animation_id = 0;
  std::uint16_t animation_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint16_t pose_sequence_index = kInvalidM2AnimationSequenceIndex;
  std::uint16_t pose_blend_source_sequence_index = kInvalidM2AnimationSequenceIndex;
  bool pose_blend_source_clamped_at_end = false;

  std::uint32_t active_animation_slot_count = 0;

  std::uint64_t animation_state_generation = 0;
  float pose_blend_remaining = 0.0f;
  float pose_blend_duration = 0.0f;
  float pose_blend_source_time = 0.0f;

  float pose_blend_source_speed = 1.0f;

  std::uint32_t pose_blend_source_duration_ms = 0;
  std::uint32_t current_animation_duration_ms = 0;
  std::uint32_t processed_event_time_ms = 0;

  std::uint32_t base_loop_boundaries_pending = 0;

  std::uint64_t last_rendered_effect_frame = 0;

  std::uint64_t slot_loop_wrapped_mask = 0;
  std::vector<std::size_t> visible_submesh_indices;
  std::vector<ChildLink> child_links;

  mutable M2PoseCache query_pose_cache{};
  mutable M2PoseCache render_pose_cache{};
  AnimationCompletionCallback animation_completion_callback;
  TriggeredEventCallback triggered_event_callback;
  std::optional<PendingBaseAnimation> pending_base_animation;
  std::vector<TextureOverride> texture_overrides;

  core::ClientCrtRandom animation_variant_random{};
  bool animation_variant_random_seeded = false;
  bool animation_used_random_variant = false;
  std::int32_t animation_sub_index = -1;
  std::int32_t animation_lookup_id = -1;
  std::uint16_t animation_lookup_sequence_index = 0;
  std::int32_t animation_loop_count = 0;
  RenderVec3 position{};
  RenderVec3 rotation{};
  float scale = 1.0f;
  M2AnimationSlotStates animation_slots{};

  std::vector<M2BoneBasisOverride> bone_basis_overrides;
  std::array<std::optional<PendingSlotAnimation>, kM2RetailAnimationSlotCount>
      pending_slot_animations{};

  std::array<std::uint32_t, kM2RetailAnimationSlotCount>
      slot_processed_event_time_ms{};
  std::uint32_t parent_instance_id = 0;
  bool ready_for_attachment_visual_updates = true;
  bool attachment_visual_data_initialized = true;
  std::uint32_t attachment_visual_flags = 0;
  std::uint32_t primary_visual_count = 1;
  std::uint32_t active_primary_visual_count = 0;
  std::uint32_t active_primary_visual_duration_ms = 0;
  float primary_visual_weight = 1.0f;
  std::vector<PrimaryVisualEntryState> primary_visual_entries;
  AnimationChangedCallback animation_changed_callback;
  AnimationRequestCallback animation_request_callback;
  bool destroy_in_progress = false;
  std::vector<ParticleColorOverride> particle_color_overrides;

  bool has_replacement_colors = false;
  std::array<std::uint32_t, 3> replacement_colors{};
  std::array<float, 4> selection_glow_color = {0.0f, 0.0f, 0.0f, 1.0f};

  M2InstanceEffectContext effect_context{};

  bool effect_emitters_enabled = true;
  std::optional<M2TransientWeaponTrailState> transient_weapon_trail;
  M2ParticleSystem particle_system{};
  std::string particle_system_model_key;
  bool particle_system_bound = false;
  M2RibbonEmitterSystem ribbon_system{};
  std::string ribbon_system_model_key;
  bool ribbon_system_initialized = false;

  [[nodiscard]] std::uint16_t GetBlendSourceSequenceIndex() const noexcept {
    return pose_blend_remaining > 0.0f ? pose_blend_source_sequence_index
                                       : kInvalidM2AnimationSequenceIndex;
  }

  [[nodiscard]] bool IsPoseBlending() const noexcept {
    return pose_blend_source_sequence_index != kInvalidM2AnimationSequenceIndex &&
           pose_blend_remaining > 0.0f;
  }

  [[nodiscard]] float PoseBlendFactor() const noexcept {
    if (!IsPoseBlending() || pose_blend_duration <= 0.0f) {
      return 1.0f;
    }
    const float elapsed_fraction = std::clamp(
        1.0f - pose_blend_remaining / pose_blend_duration, 0.0f, 1.0f);
    return elapsed_fraction * elapsed_fraction *
           (elapsed_fraction * -2.0f + 3.0f);
  }
};

[[nodiscard]] inline bool ComputeM2ModelHasBillboardBones(
    const openwow::data::model::M2Model& model_data) noexcept {
  for (const auto& bone : model_data.bones) {
    if ((bone.flags & openwow::data::model::kM2BoneFlagBillboardMask) != 0u) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool ComputeM2ModelHasLiveGlobalSequence(
    const openwow::data::model::M2Model& model_data) noexcept {
  for (const auto duration_ms : model_data.global_sequences_ms) {
    if (duration_ms > 0u) {
      return true;
    }
  }
  return false;
}

struct M2ModelResource {
  struct RenderBatch {
    std::size_t texture_unit_index = 0;
    std::uint16_t submesh_index = 0;
    std::uint32_t start_index = 0;
    std::uint32_t index_count = 0;
    std::uint32_t geoset_id = 0;
    std::uint32_t texture_unit_order_key = 0;
    std::uint32_t render_pass_order_key = 0;
    std::uint16_t material_flags = 0;
    int shader_op1 = 0;
    int shader_op2 = 0;
    int texture_count = 1;
    int shader_variant = 0;
    std::uint16_t shader_id = 0;
    M2BlendMode blend_mode = M2BlendMode::Opaque;
    M2TexGen tex0_gen = M2TexGen::TexCoord0;
    M2TexGen tex1_gen = M2TexGen::TexCoord1;
    std::uint32_t texture0_index = 0;
    std::uint32_t texture1_index = 0;
    int uv_animation0_index = -1;
    int uv_animation1_index = -1;
  };
  std::vector<RenderBatch> render_batches;

  std::vector<RenderBatch> projected_batches;

  RenderVec3 bounds_min{};
  RenderVec3 bounds_max{};
  float bounds_radius = 0.0f;

  static constexpr float kSequenceBoundingRadiusEpsilon = 2.3841858e-07f;

  [[nodiscard]] bool HasAnimationSequenceBounds(const std::uint16_t sequence_index) const noexcept {
    if (sequence_index == kInvalidM2AnimationSequenceIndex ||
        sequence_index >= model_data.animation_sequences.size()) {
      return false;
    }

    const auto &sequence = model_data.animation_sequences[sequence_index];
    return sequence.bounding_box_min[0] <= sequence.bounding_box_max[0] ||
           sequence.bounding_box_min[1] <= sequence.bounding_box_max[1] ||
           sequence.bounding_box_min[2] <= sequence.bounding_box_max[2];
  }

  [[nodiscard]] RenderAabb GetBoundingBox(
      const std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex) const noexcept {
    if (HasAnimationSequenceBounds(sequence_index)) {
      const auto &sequence = model_data.animation_sequences[sequence_index];
      return {
          sequence.bounding_box_min[0], sequence.bounding_box_min[1], sequence.bounding_box_min[2],
          sequence.bounding_box_max[0], sequence.bounding_box_max[1], sequence.bounding_box_max[2],
      };
    }

    return {
        bounds_min[0], bounds_min[1], bounds_min[2], bounds_max[0], bounds_max[1], bounds_max[2],
    };
  }

  [[nodiscard]] RenderAabb
  GetTransitionBoundingBox(const std::uint16_t current_sequence_index,
                           const std::uint16_t source_sequence_index) const noexcept {
    if (source_sequence_index == kInvalidM2AnimationSequenceIndex ||
        source_sequence_index == current_sequence_index ||
        source_sequence_index >= model_data.animation_sequences.size()) {
      return GetBoundingBox(current_sequence_index);
    }

    const auto current_bounds = GetBoundingBox(current_sequence_index);
    const auto source_bounds = GetBoundingBox(source_sequence_index);
    return {
        std::min(current_bounds[0], source_bounds[0]),
        std::min(current_bounds[1], source_bounds[1]),
        std::min(current_bounds[2], source_bounds[2]),
        std::max(current_bounds[3], source_bounds[3]),
        std::max(current_bounds[4], source_bounds[4]),
        std::max(current_bounds[5], source_bounds[5]),
    };
  }

  [[nodiscard]] std::array<float, 4> GetBoundingSphere() const noexcept {
    return GetBoundingSphere(kInvalidM2AnimationSequenceIndex);
  }

  [[nodiscard]] std::array<float, 4>
  GetTransitionBoundingSphere(const std::uint16_t current_sequence_index,
                              const std::uint16_t source_sequence_index) const noexcept {
    if (source_sequence_index == kInvalidM2AnimationSequenceIndex ||
        source_sequence_index == current_sequence_index) {
      return GetBoundingSphere(current_sequence_index);
    }

    const auto bounds = GetTransitionBoundingBox(current_sequence_index, source_sequence_index);
    const float min_x = bounds[0];
    const float min_y = bounds[1];
    const float min_z = bounds[2];
    const float max_x = bounds[3];
    const float max_y = bounds[4];
    const float max_z = bounds[5];
    const float dx = max_x - min_x;
    const float dy = max_y - min_y;
    const float dz = max_z - min_z;

    return {
        (min_x + max_x) * 0.5f,
        (min_y + max_y) * 0.5f,
        (min_z + max_z) * 0.5f,
        std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f,
    };
  }

  [[nodiscard]] std::array<float, 4>
  GetBoundingSphere(const std::uint16_t sequence_index) const noexcept {
    if (sequence_index != kInvalidM2AnimationSequenceIndex &&
        sequence_index < model_data.animation_sequences.size()) {
      const auto &sequence = model_data.animation_sequences[sequence_index];
      if (std::abs(sequence.bounding_radius) < kSequenceBoundingRadiusEpsilon) {
        return {0.0f, 0.0f, 0.0f, sequence.bounding_radius};
      }

      return {
          (sequence.bounding_box_min[0] + sequence.bounding_box_max[0]) * 0.5f,
          (sequence.bounding_box_min[1] + sequence.bounding_box_max[1]) * 0.5f,
          (sequence.bounding_box_min[2] + sequence.bounding_box_max[2]) * 0.5f,
          sequence.bounding_radius,
      };
    }

    return {
        (bounds_min[0] + bounds_max[0]) * 0.5f,
        (bounds_min[1] + bounds_max[1]) * 0.5f,
        (bounds_min[2] + bounds_max[2]) * 0.5f,
        bounds_radius,
    };
  }

  std::unique_ptr<M2SkinnedMesh> skinned_mesh;
  std::vector<TextureLease> gpu_texture_leases;
  std::vector<bgfx::TextureHandle> gpu_textures;

  std::string model_path;
  data::model::M2Model model_data;
  std::shared_ptr<const M2ModelCollisionGeometry> collision_geometry;
  std::shared_ptr<const std::vector<std::uint8_t>> source_model_bytes;
  data::model::M2Skin skin_data;
  M2SkinGeometry skin_geometry;
  std::uint32_t selected_skin_profile = 0;

  bool has_bones = false;

  bool has_billboard_bones = false;

  bool has_live_global_sequence = false;
  bool loaded = false;

  std::optional<M2StaticInstancingProfile> static_instancing_cache;

  std::optional<bool> opaque_depth_gate_cache;

  [[nodiscard]] bool HasRenderMaterialData() const noexcept {

    return !render_batches.empty() || !projected_batches.empty();
  }

  [[nodiscard]] bool HasEffectData() const noexcept {
    return !model_data.particle_emitters.empty() ||
           !model_data.ribbon_emitters.empty();
  }

  [[nodiscard]] static bool DeviceOwnsGpuResources() noexcept {
    return openwow::render::IsRendererContextActive() ||
           openwow::render::IsRendererDeviceRestarting();
  }

  [[nodiscard]] bool HasReadyGpuGeometry() const noexcept {
    if (!DeviceOwnsGpuResources()) {
      return true;
    }
    return skinned_mesh != nullptr && skinned_mesh->skinned_ok();
  }

  [[nodiscard]] bool HasReadyEffectTextures() const noexcept {
    if (!DeviceOwnsGpuResources()) {
      return true;
    }
    if (gpu_textures.size() != model_data.textures.size()) {
      return false;
    }
    for (std::size_t index = 0; index < model_data.textures.size(); ++index) {
      const auto& texture = model_data.textures[index];
      if (texture.type == 0u &&
          !bgfx::isValid(gpu_textures[index])) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool IsReadyForRender() const noexcept {
    if (!loaded) {
      return false;
    }
    if (HasRenderMaterialData()) {
      return HasReadyGpuGeometry();
    }
    return HasEffectData() && HasReadyEffectTextures();
  }
};

[[nodiscard]] inline const M2BatchUniforms* BatchUniformsOf(
    const M2Instance& instance) noexcept {
  return instance.batch_uniforms_block != nullptr
             ? &instance.batch_uniforms_block->uniforms
             : nullptr;
}

}

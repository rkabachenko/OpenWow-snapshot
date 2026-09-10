#pragma once

#include "openwow/game/character_animation.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/render/models/animation/animation_state.h"
#include "openwow/render/models/display_info_resolver.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/world/environment/world_model_lighting.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::render {

struct MountM2PresentationEvent {
  game::ObjectHandle owner;
  m2::M2TriggeredEvent event;
};

struct MountSpatialState {
  RenderMatrix4x4 world_transform{kRenderIdentityMatrix4x4};
  float mount_height{0.0f};
};

struct MountInstance {
  game::ObjectHandle rider;
  game::ObjectGuid rider_guid;
  std::uint32_t mount_display_id{0};
  std::string mount_model_path;

  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};

  std::uint32_t render_ready_latched_instance_id{0};

  AnimationState animation;
  std::uint64_t animation_request_serial{0u};
  std::uint64_t completed_animation_request_serial{0u};
  float animation_playback_rate{1.0f};
  std::uint32_t movement_flags{0u};
  float locomotion_speed{0.0f};

  RenderMatrix4x4 rider_attachment_transform{};
  RenderMatrix4x4 mount_world_transform{kRenderIdentityMatrix4x4};
  bool rider_attachment_transform_valid{false};
  bool rider_attachment_failure_reported{false};
  bool mount_world_transform_valid{false};
  bool event_callback_installed{false};

  float mount_scale{1.0f};
  float mount_opacity{1.0f};
  float mount_height{0.0f};

  std::array<std::string, 3> display_texture_paths{};
  std::optional<CreatureDisplayParticleColors> display_particle_colors;
  std::uint32_t display_geoset_data{0u};

  bool mount_loaded{false};

  bool needs_resolve{true};
  bool display_overrides_applied{false};
  bool visible_submeshes_applied{false};
};

class MountRenderer {
 public:
  explicit MountRenderer(m2::M2System& m2_system)
      : m2_system_(m2_system) {}
  ~MountRenderer();

  MountRenderer(const MountRenderer&) = delete;
  MountRenderer& operator=(const MountRenderer&) = delete;

  bool Initialize();
  void Clear();
  void Shutdown();

  void BindDisplayInfo(DisplayInfoResolver* display_info);

  void SetFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&)> loader);

  void SetWorldM2SceneState(const WorldM2SceneState& scene_state) {
    world_m2_scene_state_ = scene_state;
  }

  void SetCameraPosition(float x, float y, float z) {
    camera_position_ = {x, y, z};
  }

  void BindM2EventSink(
      std::function<void(const MountM2PresentationEvent&)> sink) {
    m2_event_sink_ = std::move(sink);
  }
  void BindAnimationCompletionSink(
      std::function<void(const game::UnitAnimationCompletionEvent&)> sink) {
    animation_completion_sink_ = std::move(sink);
  }

  void SetMount(game::ObjectGuid guid, std::uint32_t mount_display_id);

  void ClearMount(game::ObjectGuid guid);

  [[nodiscard]] bool HasMount(game::ObjectGuid guid) const;

  void Update(float dt, int max_loads_per_frame = 2);

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx,
              const game::ObjectPresentationSnapshot& objects,
              m2::M2TransparentDrawOrder& transparent_draw_order);

  void RenderShadowCasters(
      std::uint8_t view_id, const float* view_mtx, const float* proj_mtx,
      const game::ObjectPresentationSnapshot& objects,
      std::span<const std::uint64_t> rider_entity_ids);

  void PrepareRiderAttachments(
      std::span<const game::ObjectPresentationRecord> objects);

  [[nodiscard]] bool GetRiderWorldTransform(
      game::ObjectGuid guid, float rider_scale,
      RenderMatrix4x4& out_transform) const;

  [[nodiscard]] bool HasRiderAttachmentTransform(
      game::ObjectGuid guid) const;

  [[nodiscard]] bool QueryMountSpatialState(
      game::ObjectGuid guid, MountSpatialState& out) const;

  [[nodiscard]] std::uint32_t QueryMountM2InstanceId(
      game::ObjectHandle rider) const noexcept;

  void SyncFromSnapshot(const game::ObjectPresentationSnapshot& objects);

 private:

  void ClearM2Binding(MountInstance& inst);
  void LoadModelForMount(MountInstance& inst);
  void ApplyDisplayOverrides(MountInstance& inst);
  void ApplyVisibleSubmeshes(MountInstance& inst);
  void ApplyM2EventCallback(MountInstance& inst);

  [[nodiscard]] bool PrepareMountPose(
      MountInstance& inst, const game::ObjectPresentationRecord& unit);
  void RefreshRiderAttachmentTransform(MountInstance& inst);

  [[nodiscard]] bool PrepareMountInstance(MountInstance& inst,
                                          const game::ObjectPresentationRecord& unit,
                                          const m2::M2BatchUniforms& world_uniforms);

  std::vector<MountInstance*> render_batch_mounts_scratch_;
  std::vector<std::uint32_t> render_batch_ids_scratch_;
  std::vector<std::uint32_t> render_batch_draw_ordinals_scratch_;
  std::vector<m2::M2RenderInstanceResult> render_batch_results_scratch_;

  static game::CharacterLocomotionAnimation SelectMountLocomotionAnimation(
      const game::ObjectPresentationRecord& unit);

  static constexpr std::uint32_t kRiderAttachmentLookupIndex = 0u;

  DisplayInfoResolver* display_info_{nullptr};
  m2::M2System& m2_system_;
  bool initialized_{false};

  std::unordered_map<game::ObjectGuid, MountInstance, game::ObjectGuid::Hash>
      mounts_;

  std::function<std::vector<std::uint8_t>(const std::string&)> file_loader_;
  std::function<void(const MountM2PresentationEvent&)> m2_event_sink_;
  std::function<void(const game::UnitAnimationCompletionEvent&)>
      animation_completion_sink_;

  WorldM2SceneState world_m2_scene_state_{};
  RenderVec3 camera_position_{};

  static constexpr int kMaxLoadsPerFrame = 2;
};

}

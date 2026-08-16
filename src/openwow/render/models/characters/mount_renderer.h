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
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {

struct MountInstance {
  game::ObjectHandle rider;
  game::ObjectGuid rider_guid;
  std::uint32_t mount_display_id{0};
  std::string mount_model_path;

  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};

  std::uint32_t render_ready_latched_instance_id{0};

  AnimationState animation;

  float rider_world_offset[3]{0.0f, 0.0f, 0.0f};
  float rider_world_pos[3]{0.0f, 0.0f, 0.0f};
  bool rider_world_pos_valid{false};

  float mount_scale{1.0f};

  bool mount_loaded{false};

  bool needs_resolve{true};
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

  void SetMount(game::ObjectGuid guid, std::uint32_t mount_display_id);

  void ClearMount(game::ObjectGuid guid);

  [[nodiscard]] bool HasMount(game::ObjectGuid guid) const;

  void Update(float dt, int max_loads_per_frame = 2);

  void Render(std::uint8_t view_id, const float* view_mtx,
              const float* proj_mtx,
              const game::ObjectPresentationSnapshot& objects,
              m2::M2TransparentDrawOrder& transparent_draw_order);

  bool GetRiderOffset(game::ObjectGuid guid,
                      float& ox, float& oy, float& oz) const;

  bool GetRiderWorldPos(game::ObjectGuid guid,
                         float& ox, float& oy, float& oz) const;

  [[nodiscard]] float GetMountScale(game::ObjectGuid guid) const;

  void SyncFromSnapshot(const game::ObjectPresentationSnapshot& objects);

 private:

  std::string ResolveMountModel(std::uint32_t display_id);
  void ClearM2Binding(MountInstance& inst);
  void LoadModelForMount(MountInstance& inst);

  void UpdateRiderAttachmentFromM2System(MountInstance& inst);

  [[nodiscard]] bool PrepareMountInstance(MountInstance& inst,
                                          const game::ObjectPresentationRecord& unit,
                                          const m2::M2BatchUniforms& world_uniforms);

  std::vector<MountInstance*> render_batch_mounts_scratch_;
  std::vector<std::uint32_t> render_batch_ids_scratch_;
  std::vector<std::uint32_t> render_batch_draw_ordinals_scratch_;
  std::vector<m2::M2RenderInstanceResult> render_batch_results_scratch_;

  static game::CharacterLocomotionAnimation SelectMountAnimation(
      const game::ObjectPresentationRecord& unit);

  static constexpr std::uint32_t kRiderAttachmentLookupIndex = 0u;

  DisplayInfoResolver* display_info_{nullptr};
  m2::M2System& m2_system_;
  bool initialized_{false};

  std::unordered_map<game::ObjectGuid, MountInstance, game::ObjectGuid::Hash>
      mounts_;

  std::function<std::vector<std::uint8_t>(const std::string&)> file_loader_;

  WorldM2SceneState world_m2_scene_state_{};
  RenderVec3 camera_position_{};

  static constexpr int kMaxLoadsPerFrame = 2;
};

}

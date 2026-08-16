#pragma once

#include "openwow/render/models/characters/equipment_item_visual.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/game/inventory/equipment_presentation.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/render/world/environment/fog_controller.h"
#include "openwow/render/api/renderer_context.h"
#include "openwow/render/api/world_render_views.h"
#include "openwow/render/scene/world_overlay_metrics.h"
#include "openwow/render/effects/spell_visuals/spell_visual_renderer.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/world/environment/sky_settings.h"
#include "openwow/world/collision/collision.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/world/presentation/world_presentation_snapshot.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::audio { class SoundRuntime; }

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::world {
class WorldMap;
}

namespace openwow::render {
class BlobShadowRenderer;
class M2ProjectedTextureDecalRenderer;
class BowstringRenderer;
class ChatBubblePresenter;
struct DoodadCollisionTriangle;
struct GameObjectM2PresentationEvent;
class MissileTrajectoryRenderer;
class NameplateRenderer;
class ObjectRenderer;
class ParticleSystem;
class SelectionCircle;
struct SelectionDecal;
class SpellVisualEffects;
class SpellVisualRenderer;
class UnitNameRenderer;
class TextureManager;
class WaterParticulateSystem;
class WorldFrame;
class WorldPresentationScene;
namespace m2 {
class M2System;
}
namespace api {
class RendererContext;
}
}

namespace openwow::game {

class ObjectManager;
class WorldEnvironmentState;
class WorldSession;

struct WorldSceneRenderViews {
  render::WorldRenderViews world;
  std::uint8_t blob_shadows = 0;
  std::uint8_t objects = 0;
  std::uint8_t mounts = 0;
  std::uint8_t particles = 0;
  std::uint8_t selection_circle = 0;
  std::uint8_t water_particulates = 0;

  std::uint8_t unit_names = 0;
  std::uint8_t nameplates = 0;
};

struct WorldSceneRenderCamera {
  std::array<float, 3> position{};
  std::array<float, 3> forward{1.0f, 0.0f, 0.0f};
  float far_clip{350.0f};
};

[[nodiscard]] WorldSceneRenderCamera BuildWorldSceneRenderCamera(
    const float* row_major_view_matrix,
    float camera_x, float camera_y, float camera_z);

[[nodiscard]] float NormalizeWorldHourOfDay(float hour_of_day);
[[nodiscard]] float WorldHourOfDayToNormalizedTime(float hour_of_day);

class WorldScene final
    : public render::api::RendererDeviceLifecycleObserver {
 public:
  WorldScene(render::TextureManager& texture_manager,
             render::m2::M2System& m2_system,
             render::WorldFrame& world_frame,
              WorldEnvironmentState& world_environment,
              render::SkySettingsProvider sky_settings,
              openwow::audio::SoundRuntime& sound_runtime);
  ~WorldScene();

  WorldScene(const WorldScene&) = delete;
  WorldScene& operator=(const WorldScene&) = delete;

  bool Initialize();
  void BindRendererContext(render::api::RendererContext* renderer_context);

  void Shutdown();

  void UnloadMap();

  void RetireDestroyedObjectGeneration();

  bool SetScreenSize(std::uint32_t width, std::uint32_t height);

  void Update(float dt, float cam_x, float cam_y, float cam_z,
              float environment_detail, float weather_particle_density,
              bool use_weather_shaders);

  void Render(const render::api::RendererContext* renderer_context,
              const WorldSceneRenderViews& views,
              const WorldSceneRenderCamera& camera,
              const float* view_mtx, const float* proj_mtx,
              const render::WorldOverlayMetrics& overlay_metrics,
              float world_capture_width, float world_capture_height,
              render::m2::M2TransparentDrawOrder& alpha_view_draw_order,
              bool force_transparency_pass = false);

  void PrepareFrame(const render::api::RendererContext* renderer_context,
                    const WorldSceneRenderCamera& camera,
                    const float* view_mtx, const float* proj_mtx);

  [[nodiscard]] std::uint32_t scene_clear_argb() const noexcept {
    return presentation_snapshot_.environment.scene_clear_argb;
  }

  void LoadMap(std::uint32_t map_id, const std::string& map_name);

  void UpdatePlayerPosition(float x, float y, float z);

  void SetTimeOfDay(float hour_of_day);

  void SetScreenEffectLightParamSlotOverride(
      std::optional<std::uint8_t> light_param_slot_override);
  void SetScreenEffectFogOverride(
      std::optional<render::FogBandOverride> fog_override);
  void SetShadowPresentationSettings(
      world::ShadowPresentationSettings settings) {
    shadow_settings_ = settings;
  }
  void SetSpecularEnabled(bool enabled);
  void SetSuppressLocalLighting(bool suppress_local_lighting);

  void SetWeather(std::uint32_t type, float intensity, bool smooth);

  void PublishObjectPresentation(ObjectManager& obj_mgr,
                                 WorldSession& world_session);

  void SynchronizeObjectModelBindings(ObjectManager& obj_mgr,
                                      WorldSession& world_session);

  void SetObjectRendererFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&)> loader);

  void SetPrefixFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&, std::size_t)>
          loader);
  void ApplyEquipmentPresentation(
      const EquipmentPresentation& presentation);

  void ConsumeSpellVisualEvents();

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);

  void InitializeSpellVisuals();

  void UpdateNameplates(const ObjectManager& obj_mgr,
                        WorldSession& world_session, uint64_t target_guid,
                        uint64_t mouseover_guid, uint64_t plate_hover_guid,
                        bool show_world_nameplates,
                        float player_x, float player_y, float player_z);

  void BeginSelectionDecals();

  void SubmitSelectionDecal(const render::SelectionDecal& decal);

  [[nodiscard]] render::NameplateRenderer& nameplate_renderer() {
    return nameplate_renderer_;
  }
  [[nodiscard]] const ObjectPresentationSnapshot&
  object_presentation() const {
    return object_presentation_snapshot_;
  }
  [[nodiscard]] render::ChatBubblePresenter& chat_bubble_presenter() {
    return chat_bubble_presenter_;
  }
  [[nodiscard]] render::SelectionCircle& selection_circle() {
    return selection_circle_;
  }

  [[nodiscard]] world::WorldMap& world_map() { return world_map_; }
  [[nodiscard]] const world::WorldMap& world_map() const { return world_map_; }

  [[nodiscard]] world::WorldCamera& camera() { return camera_; }
  [[nodiscard]] const world::WorldCamera& camera() const { return camera_; }

  [[nodiscard]] world::CollisionManager& collision() { return collision_; }
  [[nodiscard]] const world::CollisionManager& collision() const { return collision_; }

  void VisitDoodadCollisionTriangles(
      const std::array<float, 6>& world_bounds,
      const std::function<void(const render::DoodadCollisionTriangle&)>& visitor,
      bool include_object_owned = true) const;
  [[nodiscard]] std::uint64_t DoodadCollisionRevision() const noexcept;

  [[nodiscard]] bool IsDoodadWorldEntryLoadDrained() const;

  [[nodiscard]] render::ParticleSystem& particles() { return particles_; }
  [[nodiscard]] const render::ParticleSystem& particles() const { return particles_; }

  [[nodiscard]] render::SpellVisualEffects& spell_visuals() { return spell_visuals_; }
  [[nodiscard]] const render::SpellVisualEffects& spell_visuals() const { return spell_visuals_; }

  [[nodiscard]] render::SpellVisualRenderer& spell_visual_renderer() { return spell_visual_renderer_; }
  [[nodiscard]] const render::SpellVisualRenderer& spell_visual_renderer() const { return spell_visual_renderer_; }

  [[nodiscard]] render::BlobShadowRenderer& blob_shadows() { return blob_shadows_; }
  [[nodiscard]] const render::BlobShadowRenderer& blob_shadows() const { return blob_shadows_; }

  [[nodiscard]] render::ObjectRenderer& object_renderer();
  [[nodiscard]] const render::ObjectRenderer& object_renderer() const;

  [[nodiscard]] bool initialized() const { return initialized_; }

  void RenderWaterParticulates(const render::api::RendererContext* renderer_context,
                               std::uint8_t view_id, const float* view_mtx, const float* proj_mtx,
                               float screen_w, float screen_h);

  void RenderMissileTrajectory(
      std::uint8_t view_id, const float* view_mtx, const float* proj_mtx,
      const MissileArcRenderSnapshot& snapshot,
      render::m2::M2TransparentDrawOrder& alpha_view_draw_order);

 private:
  struct RenderResources;
  void ClearObjectPresentation();

  void ClearObjectOwnedSpellVisualState();
  void OnRendererDeviceWillReset() override;
  void OnRendererDeviceReady(render::api::DeviceGeneration generation) override;
  void ReleaseRenderDeviceResources();
  [[nodiscard]] bool RestoreRenderDeviceResources();
  void ConsumeWorldPresentationCommands();

  struct ObjectWmoBinding {
    std::array<std::uint64_t, 4> owners{};
    std::uint64_t rebuild_effect_owner{0};

    std::uint8_t prior_doodad_transfer_source_state{0xFFu};

    struct DestructibleTransition {

      std::uint32_t observed_transition_serial{0};
      std::uint32_t start_time_ms{0};
      std::uint32_t duration_ms{0};
      float destination_height{0.0f};
      float source_height{0.0f};
      std::uint8_t source_state{0};
      std::uint8_t destination_state{0};
      std::uint32_t mode{4};
      bool doodad_transfer_pending{false};
      bool waiting_for_models{false};
      bool active{false};
    } destructible_transition;
  };

  static constexpr std::uint64_t kFirstObjectWmoOwner = 1ull << 40u;

  std::unique_ptr<world::WorldMap> world_map_owner_;
  world::WorldMap& world_map_;
  openwow::audio::SoundRuntime& sound_runtime_;
  std::uint32_t area_model_sound_throttle_{0};
  render::TextureManager& texture_manager_;
  render::m2::M2System& m2_system_;

  float last_frame_delta_seconds_{0.0f};
  render::WorldFrame& world_frame_;
  WorldEnvironmentState& world_environment_;
  std::unique_ptr<RenderResources> render_resources_;
  render::WorldPresentationScene& world_presentation_scene_;
  world::WorldCamera camera_;
  world::CollisionManager collision_;
  render::ParticleSystem& particles_;
  render::SpellVisualEffects& spell_visuals_;
  render::SpellVisualRenderer& spell_visual_renderer_;
  world::WorldPresentationSnapshot presentation_snapshot_;
  ObjectPresentationSnapshot object_presentation_snapshot_;

  std::vector<std::uint64_t> visible_entity_ids_;

  std::vector<std::array<float, 4>> visible_entity_bounding_spheres_;

  std::vector<std::uint64_t> opaque_pass_entity_ids_;

  bool frame_prepared_{false};
  std::vector<UnitAnimationCompletionEvent> unit_animation_completions_;
  std::vector<render::GameObjectM2PresentationEvent> game_object_m2_events_;
  std::deque<render::SpellVisualM2PresentationEvent> spell_visual_m2_events_;
  std::deque<render::SpellVisualDeferredImpactCommand>
      spell_visual_deferred_impacts_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
      spell_visual_m2_sound_handles_;
  bool spell_visual_m2_event_overflow_reported_{false};
  bool spell_visual_impact_overflow_reported_{false};
  std::unordered_map<ObjectHandle, ObjectWmoBinding, ObjectHandle::Hash>
      object_wmo_bindings_;
  std::uint64_t next_object_wmo_owner_{kFirstObjectWmoOwner};
  world::ShadowPresentationSettings shadow_settings_{};

  const openwow::data::dbc::DbcLoader* dbc_{nullptr};
  std::unique_ptr<render::ObjectRenderer>& object_renderer_;
  render::NameplateRenderer& nameplate_renderer_;
  render::UnitNameRenderer& unit_name_renderer_;
  render::ChatBubblePresenter& chat_bubble_presenter_;
  render::SelectionCircle& selection_circle_;
  render::BowstringRenderer& bowstring_renderer_;
  render::BlobShadowRenderer& blob_shadows_;
  render::M2ProjectedTextureDecalRenderer& m2_projected_texture_decals_;
  render::MissileTrajectoryRenderer& missile_trajectory_renderer_;
  render::WaterParticulateSystem& water_particulates_;
  std::function<void(std::uint32_t, float, float)>
      world_audio_position_sink_;
  render::api::RendererContext* renderer_context_{nullptr};
  render::api::DeviceGeneration renderer_device_generation_{};
  bool renderer_observer_registered_{false};
  bool render_device_resources_ready_{false};
  bool initialized_{false};
  float time_of_day_{0.5f};
};

}

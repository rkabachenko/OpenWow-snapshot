#pragma once

#include "openwow/world/presentation/world_presentation_commands.h"
#include "openwow/world/presentation/world_presentation_snapshot.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/api/world_render_views.h"
#include "openwow/render/world/environment/sky_settings.h"
#include "openwow/render/world/doodads/doodad_renderer.h"
#include "openwow/render/scene/occlusion/occlusion_depth_buffer.h"

#include <functional>
#include <optional>
#include <chrono>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}
namespace openwow::render {

class DistantTerrainRenderer;
class ShadowPresentationRuntime;
class SkyRenderer;
class TerrainRenderer;
class WaterRenderer;
class WeatherRenderer;
class WmoRenderer;
class ViewProjection;
class TextureManager;
namespace m2 {
class M2System;
}

inline constexpr bool kParallelWorldEncode = true;

class WorldPresentationScene final {
 public:
  using LoadFileCallback =
      std::function<std::vector<std::uint8_t>(const std::string&)>;

  WorldPresentationScene(TextureManager& texture_manager,
                         m2::M2System& m2_system,
                         SkySettingsProvider sky_settings);
  ~WorldPresentationScene();
  bool Initialize();
  void Shutdown();
  [[nodiscard]] world::WorldPresentationAcknowledgment Consume(
      world::WorldPresentationCommandBatch batch);
  void Update(float dt, const RenderVec3& camera_position,
              float environment_detail, float weather_particle_density,
              bool use_weather_shaders, bool indoors);

  void Render(const world::WorldPresentationSnapshot& snapshot,
              const WorldRenderViews& views,
              const ViewProjection& matrices,
              std::uint16_t screen_width, std::uint16_t screen_height,
              m2::M2TransparentDrawOrder& alpha_draw_order);

  void SetWeatherGroundHeightSampler(
      std::function<std::optional<float>(float x, float y, float z)> sampler);
  void SetFileLoader(LoadFileCallback callback);

  void SetPrefixFileLoader(m2::M2StreamPrefixFileLoader callback);
  void BindDbc(const data::dbc::DbcLoader* dbc);
  void SetEnvironmentDetail(float scale);
  void SetSpecularEnabled(bool enabled);
  void BindWmoDoodadM2EventSink(
      std::function<void(const WmoDoodadM2PresentationEvent&)> sink);

  void VisitDoodadCollisionTriangles(
      const std::array<float, 6>& world_bounds,
      const std::function<void(const DoodadCollisionTriangle&)>& visitor,
      bool include_object_owned = true) const;
  [[nodiscard]] std::uint64_t DoodadCollisionRevision() const noexcept;

  [[nodiscard]] bool IsDoodadWorldEntryLoadDrained() const;

  [[nodiscard]] const occlusion::OcclusionDepthBuffer& occlusion_buffer()
      const noexcept {
    return occlusion_buffer_;
  }

 private:
  struct ModelResource;
  struct PendingWmoGroup;
  struct ModelInstance {
    std::string resource_key;
    std::uint64_t doodad_owner{};
    RenderMatrix4x4 transform{};
    std::uint16_t doodad_set{};
    std::array<std::uint16_t, 3> additional_doodad_sets{};
    std::array<world::WmoDoodadAnimationControl, 2> doodad_animation_controls{};
    bool visible{true};
    std::vector<std::shared_ptr<const world::WaterHeightfield>> liquids;
  };

  struct ResolvedWmoPlacement {
    WmoRenderer* renderer{nullptr};
    const world::WorldPresentationItem* item{nullptr};
    const ModelInstance* instance{nullptr};
  };

  void ResetMap();
  void QueueWmoGroupPreparation(
      const world::PublishWorldModelGroupCommand& command);
  void StartQueuedWmoGroupPreparations();
  void PumpPreparedWmoGroups(
      world::WorldPresentationAcknowledgment& acknowledgment);

  TextureManager& texture_manager_;
  m2::M2System& m2_system_;
  SkySettingsProvider sky_settings_;
  std::unique_ptr<TerrainRenderer> terrain_;
  std::unique_ptr<ShadowPresentationRuntime> shadows_;
  std::unique_ptr<SkyRenderer> sky_;
  std::unique_ptr<WeatherRenderer> weather_renderer_;
  std::unique_ptr<WaterRenderer> water_;
  std::unique_ptr<DoodadRenderer> doodads_;
  std::unique_ptr<DistantTerrainRenderer> distant_;

  std::unique_ptr<WmoRenderer> wmo_shader_warm_up_;
  world::WeatherState weather_;
  std::unordered_map<std::string, std::unique_ptr<ModelResource>> models_;
  std::unordered_map<std::uint64_t, ModelInstance> instances_;
  std::map<std::pair<std::string, std::uint32_t>,
           std::unique_ptr<PendingWmoGroup>> pending_wmo_groups_;
  LoadFileCallback load_file_;
  m2::M2StreamPrefixFileLoader load_file_prefix_;
  const data::dbc::DbcLoader* dbc_{nullptr};
  std::function<void(const WmoDoodadM2PresentationEvent&)> wmo_doodad_m2_event_sink_;
  world::MapGeneration generation_{};
  std::uint32_t weather_clock_{};

  std::vector<std::shared_ptr<const world::WaterHeightfield>>
      visible_wmo_liquids_scratch_;

  std::vector<ResolvedWmoPlacement> wmo_placement_scratch_;

  occlusion::OcclusionDepthBuffer occlusion_buffer_;
  bool initialized_{false};
};

}

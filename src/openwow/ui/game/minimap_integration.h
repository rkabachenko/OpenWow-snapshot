#pragma once

#include "openwow/ui/game/minimap.h"
#include "openwow/game/minimap_terrain.h"
#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {
class MinimapSystem;
class ObjectManager;
class WorldSession;
class WorldSceneState;
}

namespace openwow::render {
class TextureManager;
}

namespace openwow::world {
class WorldMap;
}

namespace openwow::ui {
class MinimapSystem;
}

namespace openwow::ui::game {

class MinimapIntegration {
 public:
  using FileLoader =
      std::function<std::vector<std::uint8_t>(const std::string&)>;
  MinimapIntegration(openwow::render::TextureManager& texture_manager,
                     openwow::ui::MinimapSystem& minimap_state,
                     openwow::game::MinimapSystem& minimap_content);
  ~MinimapIntegration();

  MinimapIntegration(const MinimapIntegration&) = delete;
  MinimapIntegration& operator=(const MinimapIntegration&) = delete;

  bool Initialize(float screen_w);

  void SetFileLoader(FileLoader loader);
  void BindWorldSceneState(const openwow::game::WorldSceneState* scene_state) {
    world_scene_state_ = scene_state;
  }
  void BindWorldMap(openwow::world::WorldMap* world_map) {
    world_map_ = world_map;
  }

  void Shutdown();
  void ReleaseRendererDeviceResources();
  bool RestoreRendererDeviceResources();

  void Update(float player_x, float player_y, float player_z, float facing,
              openwow::game::WorldSession& session,
              const openwow::game::ObjectManager* obj_mgr,
              openwow::game::ObjectGuid local_guid);

  void Render(std::uint8_t view_id, float screen_w, float screen_h);
  void RenderToTexture(std::uint8_t view_id);
  [[nodiscard]] MinimapSurfaceSubmitter surface_submitter() const;

  void OnMapChanged(std::uint32_t map_id, const std::string& map_name);

  void SetZoom(float zoom);
  [[nodiscard]] float GetZoom() const;

  void OnScreenResize(float screen_w);

  void SetFrameRect(float x, float y, float width, float height);

  void SetUiUnitScale(float scale);

  void PrepareMarkerPresentation();

  bool HandleClick(float screen_x, float screen_y,
                   float& out_world_x, float& out_world_y);

  [[nodiscard]] bool initialized() const { return initialized_; }
  [[nodiscard]] bool terrain_translations_loaded() const {
    return terrain_translations_loaded_;
  }
  [[nodiscard]] std::size_t resident_terrain_tile_count() const {
    return minimap_.background_tile_count();
  }
  [[nodiscard]] std::size_t last_render_terrain_submission_count() const {
    return minimap_.last_render_terrain_submission_count();
  }

 private:
  struct MarkerLabel {
    std::string text;
    bool flight_master{false};
  };

  struct PublishedWmoTile {
    std::string texture_path;
    openwow::render::TextureLease texture_lease;
    std::array<std::array<float, 3>, 4> world_vertices{};
    std::array<std::array<float, 2>, 4> texture_coords{};
  };

  void LoadTerrainTranslations();

  void PresentPublishedWmoTiles(std::uint64_t placement_stable_id,
                                std::uint32_t terrain_tint);
  void ClearPublishedWmoTiles();

  void UpdateVisibleTerrainTiles(float player_x, float player_y,
                                 float player_z);

  void RebuildMinimapContent(openwow::game::WorldSession& session,
                             const openwow::game::ObjectManager* obj_mgr,
                             openwow::game::ObjectGuid local_guid,
                             float player_x, float player_y, float player_z,
                             float facing, float visible_radius);
  void RefreshVisibleObjectCandidates(
      const openwow::game::ObjectManager& obj_mgr,
      openwow::game::ObjectGuid local_guid, float player_x, float player_y,
      float visible_radius);
  void RebuildMarkerLabels(openwow::game::WorldSession& session,
                           const openwow::game::ObjectManager* obj_mgr);

  openwow::render::TextureManager& texture_manager_;
  openwow::ui::MinimapSystem& minimap_state_;
  openwow::game::MinimapSystem& minimap_content_;
  const openwow::game::WorldSceneState* world_scene_state_{nullptr};
  openwow::world::WorldMap* world_map_{nullptr};
  Minimap minimap_;
  bool initialized_{false};

  std::uint32_t current_map_id_{0};
  std::string current_map_name_;
  std::string terrain_chunk_window_map_name_;
  std::array<openwow::game::MinimapChunkWindowSlot, 4> terrain_chunk_window_{};
  std::array<openwow::render::TextureLease, 4> terrain_chunk_leases_{};
  std::array<std::string, 4> terrain_chunk_lease_paths_{};
  std::unordered_map<std::string, openwow::render::TextureLease>
      wmo_tile_leases_;
  std::vector<PublishedWmoTile> published_wmo_tiles_;
  std::uint64_t published_wmo_placement_stable_id_{0u};
  bool published_wmo_source_valid_{false};
  std::string last_wmo_diagnostic_;
  bool terrain_translations_loaded_{false};
  FileLoader file_loader_;
  std::vector<openwow::game::ObjectGuid> visible_object_candidates_;
  std::unordered_map<std::uint64_t, MarkerLabel> marker_labels_;
  std::uint32_t next_visible_object_refresh_tick_{0};

  static constexpr float kDefaultRadius = 70.0f;
  static constexpr float kMargin = 10.0f;
};

}

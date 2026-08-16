
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/world_map_transform.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {
class CGPlayer_C;
class ObjectGuid;
class WorldSession;
class WorldStateManager;
}

namespace openwow::ui {

struct MapZoneInfo {
  std::uint32_t zone_id = 0;
  std::string name;
  std::uint32_t parent_map = 0;
  float min_x = 0.0f, min_y = 0.0f;
  float max_x = 0.0f, max_y = 0.0f;
  std::uint32_t num_floors = 0;
  bool explored = false;
};

struct MapPOI {
  float x = 0.0f, y = 0.0f;
  std::uint32_t icon_id = 0;
  std::string name;
  std::string description;
  bool is_quest = false;
  std::uint32_t quest_id = 0;
};

struct MapOverlay {
  std::uint32_t area_id = 0;
  std::string texture_path;
  float x = 0.0f, y = 0.0f;
  float w = 0.0f, h = 0.0f;
  std::uint32_t texture_w = 0, texture_h = 0;
  std::uint32_t offset_x = 0, offset_y = 0;
  std::uint32_t hit_rect_top = 0, hit_rect_left = 0;
  std::uint32_t hit_rect_bottom = 0, hit_rect_right = 0;
  std::uint32_t map_area_id = 0;
  std::array<std::uint32_t, 4> reveal_area_ids{};
  std::string texture_name;
};

struct MapLandmarkInfo {
  std::string name;
  std::string description;
  std::uint32_t texture_index = 0;
  float x = 0.0f, y = 0.0f;
  std::uint32_t map_link_id = 0;
  bool show_in_battle_map = false;
};

struct SpecialLandmarkState {
  std::string name;
  std::string description;
  std::array<std::uint32_t, 9> texture_indices{};
  float world_x = 0.0f;
  float world_y = 0.0f;
  std::int32_t map_id = 0;
  std::uint32_t world_state_id = 0;
  std::uint32_t map_link_id = 0;
  bool show_in_battle_map = false;
};

struct ContinentInfo {
  std::uint32_t map_id = 0;
  std::string name;
  std::uint32_t overview_wma_id = 0;
  std::uint32_t left_boundary = 0;
  std::uint32_t right_boundary = 0;
  std::uint32_t top_boundary = 0;
  std::uint32_t bottom_boundary = 0;
  float continent_offset_x = 0.0f;
  float continent_offset_y = 0.0f;
  float scale = 0.0f;
  bool has_overview_bounds = false;
  std::vector<std::uint32_t> zone_wma_ids;
  std::vector<std::uint32_t> lookup_wma_ids;
};

class WorldMapSystem {
public:
  WorldMapSystem() = default;

  void BindWorldSession(const openwow::game::WorldSession* session) noexcept {
    world_session_ = session;
  }

  void SetCurrentMapId(std::uint32_t map_id);
  [[nodiscard]] std::uint32_t GetCurrentMapId() const;

  void SetCurrentContinent(std::uint32_t continent_id);
  [[nodiscard]] std::uint32_t GetCurrentContinent() const;

  void SetZones(const std::vector<MapZoneInfo> &zones);
  [[nodiscard]] std::size_t GetNumZones() const;
  [[nodiscard]] const MapZoneInfo *GetZone(std::size_t index) const;
  [[nodiscard]] const MapZoneInfo *GetZoneById(std::uint32_t zone_id) const;

  void SetPOIs(const std::vector<MapPOI> &pois);
  void ClearPOIs();
  [[nodiscard]] std::size_t GetNumPOIs() const;
  [[nodiscard]] const MapPOI *GetPOI(std::size_t index) const;

  void SetQuestPOIs(std::uint32_t quest_id, const std::vector<MapPOI> &pois);
  [[nodiscard]] std::vector<MapPOI> GetQuestPOIs(std::uint32_t quest_id) const;

  void SetOverlays(const std::vector<MapOverlay> &overlays);
  [[nodiscard]] std::size_t GetNumOverlays() const;
  [[nodiscard]] const MapOverlay *GetOverlay(std::size_t index) const;
  [[nodiscard]] std::optional<MapOverlay> GetVisibleOverlaySnapshot(std::size_t index) const;

  void SetExplored(std::uint32_t area_id, bool explored);
  [[nodiscard]] bool IsExplored(std::uint32_t area_id) const;

  struct MapCoord {
    float x = 0.0f, y = 0.0f;
    bool valid = false;

    bool suppress_unit_position = false;
  };

  struct SelectionProjection {
    float x = 0.0f;
    float y = 0.0f;
    bool valid = false;
    bool indoors = false;
  };

  struct LegacyWorldToMapCoordsResult {
    float x = 0.0f;
    float y = 0.0f;
    bool success = false;
    bool has_projection = false;
    bool displayable = false;
    bool suppress_unit_position = false;
    bool indoors = false;
  };

  struct QuestPoiSelectionContext {
    bool can_update = false;
    std::int32_t displayed_world_map_area_id = -1;
    std::int32_t selected_dungeon_map_id = -1;
  };
  [[nodiscard]] MapCoord WorldToMap(float world_x, float world_y) const;

  void SetMapLocked(bool locked);
  [[nodiscard]] bool IsMapLocked() const;

  void Reset();

  void InitFromDbc(const openwow::data::dbc::DbcLoader &dbc);
  [[nodiscard]] bool IsDbcInitialized() const;

  [[nodiscard]] std::size_t GetContinentCount() const;
  [[nodiscard]] const ContinentInfo *GetContinentInfo(std::size_t index) const;

  void SetMapView(int continent_1based, int zone_1based, int dungeon_map_id);
  [[nodiscard]] bool CommitSelectionAndRefreshLandmarks(int continent_token,
                                                        int zone_token_or_area_id,
                                                        int dungeon_map_id);
  [[nodiscard]] bool RefreshCurrentSelection();
  [[nodiscard]] bool SetMapByWorldMapAreaId(std::uint32_t world_map_area_id,
                                            int dungeon_map_id = -1);
  [[nodiscard]] bool ClickLandmark(std::uint32_t map_link_id);
  [[nodiscard]] bool UpdatePlayerPosition(const openwow::game::WorldSession &session);

  void SetCurrentAreaId(std::uint32_t area_id);
  [[nodiscard]] std::uint32_t GetCurrentAreaId() const;

  void SetCurrentContinentIndex(int index_1based);
  [[nodiscard]] int GetCurrentContinentIndex() const;

  void SetCurrentZoneIndex(int index_1based);
  [[nodiscard]] int GetCurrentZoneIndex() const;

  void SetCurrentDungeonMapId(int dungeon_map_id);
  [[nodiscard]] int GetCurrentDungeonMapId() const;
  [[nodiscard]] int GetCurrentDungeonFloorIndex() const;
  void SetCurrentWmoContext(std::int32_t map_id, std::int32_t wmo_group_id,
                            std::int32_t dungeon_map_id = -1);
  void SetMapUpdateCallback(std::function<void()> callback);
  void RegisterActivePlayerExplorationRefresh(openwow::game::ObjectGuid guid);
  void UnregisterActivePlayerExplorationRefresh();

  [[nodiscard]] int GetCurrentMapAreaIdForLua() const;
  [[nodiscard]] int GetCurrentMapContinentForLua() const;
  [[nodiscard]] int GetCurrentMapZoneForLua() const;
  [[nodiscard]] int GetCurrentMapDungeonLevelForLua() const;
  [[nodiscard]] bool CurrentSelectionUsesTerrainMap() const;
  [[nodiscard]] bool SetDungeonMapLevel(int level_1based);
  [[nodiscard]] bool IsZoomOutAvailable() const;
  [[nodiscard]] bool ZoomOut();

  void SetLandmarks(const std::vector<MapLandmarkInfo> &landmarks);
  [[nodiscard]] std::size_t GetNumLandmarks() const;
  [[nodiscard]] const MapLandmarkInfo *GetLandmark(std::size_t index) const;
  void SetSpecialLandmark(const SpecialLandmarkState &landmark);
  void ClearSpecialLandmark();

  [[nodiscard]] MapCoord WorldToMapForArea(std::uint32_t wma_id, float world_x,
                                           float world_y) const;
  [[nodiscard]] LegacyWorldToMapCoordsResult LegacyWorldToMapCoords(
      std::uint32_t map_id, float world_x, float world_y, float world_z = 0.0f,
      bool allow_outside_bounds = false, int dungeon_map_id = 0) const;
  [[nodiscard]] MapCoord WorldToMapForCurrentSelection(std::uint32_t map_id, float world_x,
                                                       float world_y, float world_z = 0.0f,
                                                       int dungeon_map_id = -1);
  [[nodiscard]] SelectionProjection ProjectCurrentSelectionWithIndoorFlag(
      std::uint32_t map_id, float world_x, float world_y, float world_z = 0.0f,
      int dungeon_map_id = -1) const;
  [[nodiscard]] QuestPoiSelectionContext GetQuestPoiSelectionContext() const;

  using AppliedMapTransform = openwow::game::AppliedWorldMapTransform;
  using WorldMapTransformState = openwow::game::WorldMapTransformRule;

  [[nodiscard]] AppliedMapTransform ApplyMapTransform(std::uint32_t map_id, float x, float y) const;

  [[nodiscard]] std::int32_t GetContinentChildCount(std::uint32_t continent_token) const;

  [[nodiscard]] std::int32_t GetContinentChildWorldMapAreaId(std::uint32_t continent_token,
                                                             std::uint32_t zone_token) const;

  [[nodiscard]] std::int32_t FindDungeonMapForWmoGroupZ(std::int32_t map_id,
                                                        std::int32_t wmo_group_id, float z);

  [[nodiscard]] std::int32_t HitTestMapZone(float norm_x, float norm_y) const;

  void ProcessMapClick(float norm_x, float norm_y);

  [[nodiscard]] std::int32_t GetNumDungeonMapLevels() const;

  struct LegacyMapInfo {
    bool has_name = false;
    std::string internal_name;
    std::int32_t raw_metric = 0;
    std::int32_t padded_metric = 0;
  };

  struct MapHighlightResult {
    bool highlighted = false;
    std::optional<std::string> display_name;
    std::optional<std::string> file_name;
    double map_width_scale = 0.0;
    double map_height_scale = 0.0;
    double texture_width_scale = 0.0;
    double texture_height_scale = 0.0;
    double texture_offset_x = 0.0;
    double texture_offset_y = 0.0;
  };

  [[nodiscard]] LegacyMapInfo GetLegacyMapInfo() const;

  [[nodiscard]] MapHighlightResult ResolveMapHighlight(float norm_x, float norm_y) const;

  void BuildRuntimeData();

  void TeardownRuntimeData();

  [[nodiscard]] std::size_t GetZoneCountForContinent(int continent_1based) const;

  [[nodiscard]] std::string GetZoneNameForContinent(int continent_1based, int zone_1based) const;

  [[nodiscard]] std::uint32_t GetWmaIdForContinent(int continent_1based, int zone_1based) const;

private:
  struct LandmarkSource {
    std::uint32_t id = 0;
    std::string name;
    std::string description;
    std::array<std::uint32_t, 9> texture_indices{};
    float world_x = 0.0f;
    float world_y = 0.0f;
    float world_z = 0.0f;
    float normalized_x = 0.0f;
    float normalized_y = 0.0f;
    std::int32_t map_id = 0;
    std::uint32_t flags = 0;
    std::uint32_t area_id = 0;
    std::uint32_t world_state_id = 0;
    std::uint32_t map_link_id = 0;
    bool has_normalized_coords = false;
  };

  std::uint32_t current_map_ = 0;
  std::uint32_t current_continent_ = 0;
  std::vector<MapZoneInfo> zones_;
  std::vector<MapPOI> pois_;
  std::unordered_map<std::uint32_t, std::vector<MapPOI>> quest_pois_;
  std::vector<MapOverlay> overlay_sources_;
  std::vector<MapOverlay> overlays_;
  std::unordered_set<std::uint32_t> explored_areas_;
  bool map_locked_ = false;

  bool dbc_initialized_ = false;
  std::vector<ContinentInfo> continents_;
  std::vector<LandmarkSource> landmark_sources_;
  std::optional<LandmarkSource> special_landmark_source_;
  std::vector<MapLandmarkInfo> landmarks_;
  std::uint32_t current_area_id_ = 0;
  int current_continent_token_ = -1;
  int current_zone_token_ = -1;
  int current_world_map_area_token_ = -1;
  int current_dungeon_map_id_ = -1;
  std::int32_t current_wmo_map_id_ = 0;
  std::int32_t current_wmo_group_id_ = -1;
  std::int32_t current_wmo_dungeon_map_id_ = -1;

  struct WmaBounds {
    float loc_left = 0.0f, loc_right = 0.0f;
    float loc_top = 0.0f, loc_bottom = 0.0f;
    std::uint32_t map_id = 0;
    std::uint32_t area_id = 0;
    std::int32_t display_map_id = -1;
    std::int32_t default_dungeon_map_id = -1;
    std::uint32_t parent_world_map_id = 0;
    std::string name;
  };
  std::unordered_map<std::uint32_t, WmaBounds> wma_bounds_;

  std::unordered_map<std::uint32_t, std::uint32_t> area_to_wma_;
  std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> area_to_wmas_;

  struct DungeonMapState {
    std::uint32_t id = 0;
    std::uint32_t map_id = 0;
    std::uint32_t floor_index = 0;
    std::uint32_t parent_world_map_id = 0;
    float loc_left = 0.0f;
    float loc_right = 0.0f;
    float loc_top = 0.0f;
    float loc_bottom = 0.0f;
  };
  std::unordered_map<std::uint32_t, DungeonMapState> dungeon_maps_;
  std::vector<DungeonMapState> dungeon_maps_in_order_;
  std::vector<std::uint32_t> orphan_world_map_area_ids_;

  std::vector<WorldMapTransformState> world_map_transforms_;

  struct DungeonMapChunkState {
    std::int32_t map_id = 0;
    std::int32_t wmo_group_id = 0;
    std::int32_t dungeon_map_id = 0;
    float min_z = 0.0f;
  };
  std::vector<DungeonMapChunkState> dungeon_map_chunks_;
  mutable std::int32_t dungeon_map_chunk_cache_map_id_ = -1;
  mutable std::int32_t dungeon_map_chunk_cache_index_ = 0;
  const openwow::data::dbc::DbcLoader *dbc_loader_ = nullptr;
  const openwow::game::WorldSession* world_session_ = nullptr;
  std::function<void()> map_update_callback_;
  std::uint64_t active_player_exploration_callback_handle_ = 0;

  mutable std::mutex mutex_;

  static constexpr float kContinentLookupScale_ = 0.000029296876f;
  static constexpr std::size_t kContinentLookupCellsPerAxis_ = 128;

  [[nodiscard]] bool CommitSelectionAndRefreshLandmarksNoLock(int continent_token,
                                                              int zone_token_or_area_id,
                                                              int dungeon_map_id);
  [[nodiscard]] bool
  CommitSelectionAndRefreshLandmarksNoLock(int continent_token, int zone_token_or_area_id,
                                           int dungeon_map_id,
                                           const openwow::game::CGPlayer_C *player,
                                           const openwow::game::WorldStateManager *world_states);
  [[nodiscard]] AppliedMapTransform ApplyMapTransformNoLock(std::uint32_t map_id, float x,
                                                            float y) const;
  [[nodiscard]] std::int32_t GetSelectedDungeonMapIdOrMinusOneNoLock() const;
  [[nodiscard]] const DungeonMapState *
  LookupDungeonMapStateNoLock(std::int32_t dungeon_map_id) const;
  [[nodiscard]] const WmaBounds *LookupSelectedWorldMapAreaForDungeonMapsNoLock() const;
  [[nodiscard]] static MapCoord ProjectRectBounds(float loc_left, float loc_right, float loc_top,
                                                  float loc_bottom, float world_x, float world_y,
                                                  bool clamp_to_bounds);
  [[nodiscard]] static SelectionProjection ProjectSelectionRect(float loc_left, float loc_right,
                                                                float loc_top, float loc_bottom,
                                                                float world_x, float world_y,
                                                                bool clamp_to_bounds,
                                                                bool indoors);
  [[nodiscard]] LegacyWorldToMapCoordsResult LegacyWorldToMapCoordsNoLock(
      std::uint32_t map_id, float world_x, float world_y, float world_z,
      bool allow_outside_bounds, int dungeon_map_id, bool resolve_chunk_dungeon_map) const;
  [[nodiscard]] MapCoord WorldToMapForCurrentSelectionNoLock(std::uint32_t map_id, float world_x,
                                                             float world_y, float world_z,
                                                             int dungeon_map_id,
                                                             bool resolve_chunk_dungeon_map);
  [[nodiscard]] SelectionProjection ProjectCurrentSelectionWithIndoorFlagNoLock(
      std::uint32_t map_id, float world_x, float world_y, float world_z, int dungeon_map_id,
      bool resolve_chunk_dungeon_map) const;
  [[nodiscard]] std::int32_t ResolveDisplayedWmaIdNoLock() const;
  [[nodiscard]] std::int32_t FindRuntimeContinentIndexByMapIdNoLock(std::uint32_t map_id) const;
  [[nodiscard]] std::int32_t FindContinentIndexByMapIdNoLock(std::uint32_t map_id) const;
  [[nodiscard]] std::int32_t FindZoneIndexForContinentWmaNoLock(std::size_t continent_index,
                                                                std::uint32_t wma_id) const;
  [[nodiscard]] std::int32_t FindZoneIndexForAreaIdNoLock(std::size_t continent_index,
                                                          std::uint32_t area_id) const;
  [[nodiscard]] std::int32_t FindWorldMapAreaIdForAreaNoLock(std::uint32_t area_id,
                                                             std::int32_t map_id) const;
  [[nodiscard]] std::optional<std::uint32_t>
  FindOrphanWorldMapAreaIdByMapIdNoLock(std::uint32_t map_id) const;
  [[nodiscard]] std::int32_t FindDungeonMapForWmoGroupZNoLock(std::int32_t map_id,
                                                              std::int32_t wmo_group_id, float z) const;
  [[nodiscard]] bool ResolveOrphanSelectionNoLock(
      std::int32_t map_id, std::int32_t transformed_dungeon_map_id,
      const openwow::game::CGPlayer_C *player,
      const openwow::game::WorldStateManager *world_states);
  [[nodiscard]] std::int32_t LookupContinentWorldMapAreaIdNoLock(std::size_t continent_index,
                                                                 float world_x, float world_y,
                                                                 bool *clamped) const;
  [[nodiscard]] static std::size_t ComputeContinentLookupIndex(float norm_x, float norm_y);
  [[nodiscard]] bool
  ResolveContinentSelectionNoLock(std::int32_t continent_index, std::uint32_t current_zone_id,
                                  const openwow::game::CGPlayer_C *player,
                                  const openwow::game::WorldStateManager *world_states);
  [[nodiscard]] std::int32_t FindContainingZoneForPointNoLock(std::size_t continent_index,
                                                              float world_x, float world_y) const;
  [[nodiscard]] std::int32_t HitTestMapZoneNoLock(float norm_x, float norm_y) const;
  [[nodiscard]] MapHighlightResult ResolveMapHighlightNoLock(float norm_x, float norm_y) const;
  [[nodiscard]] std::int32_t ResolveCurrentMapContinentForLuaNoLock() const;
  [[nodiscard]] std::int32_t ResolveCurrentMapZoneForLuaNoLock() const;
  [[nodiscard]] std::int32_t ResolveCurrentMapDungeonLevelForLuaNoLock() const;
  [[nodiscard]] std::int32_t ResolveZoomOutTargetWorldMapAreaIdNoLock() const;
  [[nodiscard]] std::optional<std::string_view>
  LookupWorldMapAreaDisplayNameNoLock(std::uint32_t wma_id) const;
  [[nodiscard]] std::optional<std::string> LookupAreaNameNoLock(std::uint32_t area_id) const;
  [[nodiscard]] std::optional<std::string> LookupMapNameNoLock(std::uint32_t map_id) const;
  [[nodiscard]] bool IsAreaVisibleForPlayerNoLock(std::uint32_t area_id,
                                                  const openwow::game::CGPlayer_C *player) const;
  [[nodiscard]] std::uint32_t
  ResolveLandmarkTextureNoLock(const LandmarkSource &landmark,
                               const openwow::game::WorldStateManager *world_states,
                               bool is_special_landmark) const;
  [[nodiscard]] MapCoord ProjectLandmarkToSelectionNoLock(const LandmarkSource &landmark);
  void RefreshVisibleLandmarksNoLock(const openwow::game::CGPlayer_C *player,
                                     const openwow::game::WorldStateManager *world_states);
  void RefreshVisibleOverlaysNoLock(const openwow::game::CGPlayer_C *player);
  [[nodiscard]] std::string ResolveOverlayTexturePathNoLock(const MapOverlay &overlay) const;
  void BuildRuntimeDataFromDbcNoLock(const openwow::data::dbc::DbcLoader &dbc);
  void SortContinentZoneWorldMapAreaIdsNoLock();
  void SyncDisplayedAreaIdNoLock();
  void NotifyMapUpdated();
  [[nodiscard]] static bool HasVisibleWorldMapBounds(const WmaBounds &bounds);
  friend struct WorldMapSystemTestAccess;
};

}

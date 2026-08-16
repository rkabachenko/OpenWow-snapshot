
#include "openwow/ui/game/world_map_system.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_map_continent_lookup.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/api/game_lua_api_map.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/rect_utils.h"
#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace openwow::ui {

namespace {

constexpr std::size_t kLookupCellCount = 128u * 128u;
constexpr std::uint16_t kPlayerExplorationRelativeOffsetBytes =
    static_cast<std::uint16_t>(
        openwow::game::DescriptorWordDistance(
            openwow::game::PLAYER_EXPLORED_ZONES_1, openwow::game::UNIT_END) *
        sizeof(std::uint32_t));
constexpr std::uint16_t kPlayerExplorationSizeBytes = 128u * sizeof(std::uint32_t);
constexpr float kLegacyFloatZeroEpsilon = 0.00000023841858f;
constexpr float kLegacyOverlayHitRectXScale = 0.00099800399f;
constexpr float kLegacyOverlayHitRectYScale = 0.001497006f;
constexpr double kLegacyOverviewCoordXScale = 0.00002994012;
constexpr double kLegacyOverviewCoordYScale = 0.000044910183;
constexpr double kLegacyOverviewTextureXScale = 0.015968064;
constexpr double kLegacyOverviewTextureYScale = 0.023952097;

std::uint32_t ReadLittleEndianU32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

bool LegacyFloatIsNonZero(const float value) {
  return std::fabs(value) >= kLegacyFloatZeroEpsilon;
}

WorldMapSystem::LegacyWorldToMapCoordsResult ProjectLegacyWorldMapRect(
    const float loc_left, const float loc_right, const float loc_top, const float loc_bottom,
    const float world_x, const float world_y, const bool allow_outside_bounds,
    const bool indoors) {
  WorldMapSystem::LegacyWorldToMapCoordsResult result;
  result.indoors = indoors;

  const float width = loc_left - loc_right;
  const float height = loc_top - loc_bottom;
  if (std::fabs(width) < 0.001f || std::fabs(height) < 0.001f) {
    return result;
  }

  result.x = (loc_left - world_y) / width;
  result.y = (loc_top - world_x) / height;
  result.success = true;
  result.has_projection = true;
  result.displayable = true;
  if (!allow_outside_bounds &&
      (result.x < 0.0f || result.x > 1.0f || result.y < 0.0f || result.y > 1.0f)) {
    result.x = 0.0f;
    result.y = 0.0f;
    result.displayable = false;
  }

  return result;
}

std::int32_t ComputeLegacyMapInfoPaddedMetric(const std::int32_t raw_metric) {
  const auto low_byte =
      static_cast<std::uint32_t>(static_cast<std::uint8_t>(raw_metric));
  std::uint32_t padded_low_byte = low_byte;

  if (((low_byte - 1u) & low_byte) != 0u) {
    int bit = 8;
    while (bit > 0) {
      --bit;
      if ((low_byte & (1u << static_cast<unsigned>(bit))) != 0u) {
        padded_low_byte = 1u << static_cast<unsigned>(bit + 1);
        break;
      }
    }
  }

  return raw_metric - static_cast<std::int32_t>(low_byte) +
         static_cast<std::int32_t>(padded_low_byte);
}

std::size_t ComputeLegacyLookupCell(const float norm_coord) {
  return static_cast<std::size_t>(
      std::clamp(static_cast<int>(norm_coord * 128.0f), 0, 127));
}

}

void WorldMapSystem::SetCurrentMapId(std::uint32_t map_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_map_ = map_id;
}

std::uint32_t WorldMapSystem::GetCurrentMapId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_map_;
}

void WorldMapSystem::SetCurrentContinent(std::uint32_t continent_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_continent_ = continent_id;
}

std::uint32_t WorldMapSystem::GetCurrentContinent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_continent_;
}

void WorldMapSystem::SetZones(const std::vector<MapZoneInfo> &zones) {
  std::lock_guard<std::mutex> lock(mutex_);
  zones_ = zones;
}

std::size_t WorldMapSystem::GetNumZones() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return zones_.size();
}

const MapZoneInfo *WorldMapSystem::GetZone(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= zones_.size())
    return nullptr;
  return &zones_[index];
}

const MapZoneInfo *WorldMapSystem::GetZoneById(std::uint32_t zone_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &z : zones_) {
    if (z.zone_id == zone_id)
      return &z;
  }
  return nullptr;
}

void WorldMapSystem::SetPOIs(const std::vector<MapPOI> &pois) {
  std::lock_guard<std::mutex> lock(mutex_);
  pois_ = pois;
}

void WorldMapSystem::ClearPOIs() {
  std::lock_guard<std::mutex> lock(mutex_);
  pois_.clear();
}

std::size_t WorldMapSystem::GetNumPOIs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pois_.size();
}

const MapPOI *WorldMapSystem::GetPOI(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= pois_.size())
    return nullptr;
  return &pois_[index];
}

void WorldMapSystem::SetQuestPOIs(std::uint32_t quest_id, const std::vector<MapPOI> &pois) {
  std::lock_guard<std::mutex> lock(mutex_);
  quest_pois_[quest_id] = pois;
}

std::vector<MapPOI> WorldMapSystem::GetQuestPOIs(std::uint32_t quest_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = quest_pois_.find(quest_id);
  if (it == quest_pois_.end())
    return {};
  return it->second;
}

void WorldMapSystem::SetOverlays(const std::vector<MapOverlay> &overlays) {
  std::lock_guard<std::mutex> lock(mutex_);
  overlay_sources_ = overlays;
  overlays_ = overlays;
}

std::size_t WorldMapSystem::GetNumOverlays() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return overlays_.size();
}

const MapOverlay *WorldMapSystem::GetOverlay(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= overlays_.size())
    return nullptr;
  return &overlays_[index];
}

std::optional<MapOverlay> WorldMapSystem::GetVisibleOverlaySnapshot(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= overlays_.size()) {
    return std::nullopt;
  }

  MapOverlay overlay = overlays_[index];
  overlay.texture_path = ResolveOverlayTexturePathNoLock(overlay);
  return overlay;
}

void WorldMapSystem::SetExplored(std::uint32_t area_id, bool explored) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (explored) {
    explored_areas_.insert(area_id);
  } else {
    explored_areas_.erase(area_id);
  }
}

bool WorldMapSystem::IsExplored(std::uint32_t area_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return explored_areas_.count(area_id) > 0;
}

WorldMapSystem::MapCoord WorldMapSystem::WorldToMap(float world_x, float world_y) const {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto &zone : zones_) {
    float width = zone.max_x - zone.min_x;
    float height = zone.max_y - zone.min_y;
    if (width <= 0.0f || height <= 0.0f)
      continue;

    if (world_x >= zone.min_x && world_x <= zone.max_x && world_y >= zone.min_y &&
        world_y <= zone.max_y) {
      float map_x = (world_x - zone.min_x) / width;
      float map_y = (world_y - zone.min_y) / height;
      return {map_x, map_y, true};
    }
  }

  return {0.0f, 0.0f, false};
}

void WorldMapSystem::SetMapLocked(bool locked) {
  std::lock_guard<std::mutex> lock(mutex_);
  map_locked_ = locked;
}

bool WorldMapSystem::IsMapLocked() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return map_locked_;
}

void WorldMapSystem::Reset() {
  if (auto* ui = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua(); ui != nullptr) {
    openwow::ui::game::detail::DestroyWorldMapArrowFrames(ui->lua_state());
  }

  UnregisterActivePlayerExplorationRefresh();

  std::lock_guard<std::mutex> lock(mutex_);
  current_map_ = 0;
  current_continent_ = 0;
  zones_.clear();
  pois_.clear();
  quest_pois_.clear();
  overlay_sources_.clear();
  overlays_.clear();
  explored_areas_.clear();
  map_locked_ = false;

  landmark_sources_.clear();
  special_landmark_source_.reset();
  landmarks_.clear();
  current_area_id_ = 0;
  current_continent_token_ = -1;
  current_zone_token_ = -1;
  current_world_map_area_token_ = -1;
  current_dungeon_map_id_ = -1;
  area_to_wmas_.clear();
  current_wmo_map_id_ = 0;
  current_wmo_group_id_ = -1;
  current_wmo_dungeon_map_id_ = -1;
  dungeon_map_chunk_cache_map_id_ = -1;
  dungeon_map_chunk_cache_index_ = 0;
}

void WorldMapSystem::InitFromDbc(const openwow::data::dbc::DbcLoader &dbc) {
  std::lock_guard<std::mutex> lock(mutex_);
  dbc_loader_ = &dbc;
  BuildRuntimeDataFromDbcNoLock(dbc);
}

void WorldMapSystem::BuildRuntimeDataFromDbcNoLock(const openwow::data::dbc::DbcLoader &dbc) {

  wma_bounds_.clear();
  area_to_wma_.clear();
  area_to_wmas_.clear();
  for (const auto &wma : dbc.world_map_area()) {
    WmaBounds bounds;
    bounds.loc_left = wma.loc_left;
    bounds.loc_right = wma.loc_right;
    bounds.loc_top = wma.loc_top;
    bounds.loc_bottom = wma.loc_bottom;
    bounds.map_id = wma.map_id;
    bounds.area_id = wma.area_id;
    bounds.display_map_id = wma.display_map_id;
    bounds.default_dungeon_map_id = wma.default_dungeon_map_id;
    bounds.parent_world_map_id = wma.parent_world_map_id;
    bounds.name = std::string(wma.name);
    wma_bounds_[wma.id] = bounds;

    if (wma.area_id != 0) {
      area_to_wmas_[wma.area_id].push_back(wma.id);
      area_to_wma_.try_emplace(wma.area_id, wma.id);
    }
  }

  std::unordered_map<std::uint32_t, const openwow::data::dbc::WorldMapContinentEntry *>
      continent_bounds_by_map_id;
  for (const auto &wmc : dbc.world_map_continent()) {
    continent_bounds_by_map_id.try_emplace(wmc.map_id, &wmc);
  }

  continents_.clear();
  continents_.reserve(dbc.world_map_area().size());

  for (const auto &wma : dbc.world_map_area()) {
    if (wma.area_id != 0) {
      continue;
    }

    ContinentInfo ci;
    ci.map_id = wma.map_id;
    ci.overview_wma_id = wma.id;
    ci.lookup_wma_ids.assign(kLookupCellCount, 0);

    if (const auto map_name = LookupMapNameNoLock(wma.map_id); map_name.has_value()) {
      ci.name = std::move(*map_name);
    }

    if (const auto bounds_it = continent_bounds_by_map_id.find(wma.map_id);
        bounds_it != continent_bounds_by_map_id.end() && bounds_it->second != nullptr) {
      const auto &continent_entry = *bounds_it->second;
      ci.left_boundary = continent_entry.left_boundary;
      ci.right_boundary = continent_entry.right_boundary;
      ci.top_boundary = continent_entry.top_boundary;
      ci.bottom_boundary = continent_entry.bottom_boundary;
      ci.continent_offset_x = continent_entry.continent_offset_x;
      ci.continent_offset_y = continent_entry.continent_offset_y;
      ci.scale = continent_entry.scale;
      ci.has_overview_bounds = true;
    }

    for (const auto &child_wma : dbc.world_map_area()) {
      if (child_wma.area_id == 0) {
        continue;
      }

      const auto child_map_id = child_wma.display_map_id >= 0
                                    ? static_cast<std::uint32_t>(child_wma.display_map_id)
                                    : child_wma.map_id;
      if (child_map_id == wma.map_id) {
        ci.zone_wma_ids.push_back(child_wma.id);
      }
    }

    continents_.push_back(std::move(ci));
  }

  SortContinentZoneWorldMapAreaIdsNoLock();

  const auto *vfs = dbc.vfs();
  for (auto &continent : continents_) {
    continent.lookup_wma_ids.assign(kLookupCellCount, 0);
    if (vfs == nullptr || continent.overview_wma_id == 0) {
      continue;
    }

    const auto overview_it = wma_bounds_.find(continent.overview_wma_id);
    if (overview_it == wma_bounds_.end() || overview_it->second.name.empty()) {
      continue;
    }

    const auto zmp_path = "Interface\\WorldMap\\" + overview_it->second.name + ".zmp";
    const auto bytes = vfs->ReadFileBytes(zmp_path);
    if (!bytes.has_value() || bytes->size() < kLookupCellCount * sizeof(std::uint32_t)) {
      continue;
    }

    for (std::size_t cell = 0; cell < kLookupCellCount; ++cell) {
      std::uint32_t area_id = ReadLittleEndianU32(*bytes, cell * sizeof(std::uint32_t));
      if (area_id == 0) {
        continue;
      }

      const auto *area = dbc.area_table().LookupEntry(area_id);
      if (area != nullptr && area->parent_area != 0) {
        area_id = area->parent_area;
      }

      for (const auto wma_id : continent.zone_wma_ids) {
        const auto bounds_it = wma_bounds_.find(wma_id);
        if (bounds_it == wma_bounds_.end()) {
          continue;
        }

        const auto child_map_id = bounds_it->second.display_map_id >= 0
                                      ? static_cast<std::uint32_t>(bounds_it->second.display_map_id)
                                      : bounds_it->second.map_id;
        if (child_map_id == continent.map_id && bounds_it->second.area_id == area_id) {
          continent.lookup_wma_ids[cell] = wma_id;
          break;
        }
      }
    }
  }

  dungeon_maps_.clear();
  dungeon_maps_in_order_.clear();
  for (const auto &dungeon_map : dbc.dungeon_map()) {
    DungeonMapState state;
    state.id = dungeon_map.id;
    state.map_id = dungeon_map.map_id;
    state.floor_index = dungeon_map.floor_index;
    state.parent_world_map_id = dungeon_map.parent_world_map_id;
    state.loc_left = dungeon_map.max_x;
    state.loc_right = dungeon_map.min_x;
    state.loc_top = dungeon_map.max_y;
    state.loc_bottom = dungeon_map.min_y;
    dungeon_maps_[state.id] = state;
    dungeon_maps_in_order_.push_back(state);
  }

  world_map_transforms_.clear();
  for (const auto &transform : dbc.world_map_transforms()) {
    WorldMapTransformState state;
    state.map_id = transform.map_id;
    state.region_min_x = transform.region_min_x;
    state.region_min_y = transform.region_min_y;
    state.region_max_x = transform.region_max_x;
    state.region_max_y = transform.region_max_y;
    state.new_map_id = transform.new_map_id;
    state.region_offset_x = transform.region_offset_x;
    state.region_offset_y = transform.region_offset_y;
    state.new_dungeon_map_id = transform.new_dungeon_map_id;
    world_map_transforms_.push_back(state);
  }

  dungeon_map_chunks_.clear();
  for (const auto &chunk : dbc.dungeon_map_chunk()) {
    DungeonMapChunkState state;
    state.map_id = static_cast<std::int32_t>(chunk.map_id);
    state.wmo_group_id = static_cast<std::int32_t>(chunk.wmo_group_id);
    state.dungeon_map_id = static_cast<std::int32_t>(chunk.dungeon_map_id);
    state.min_z = chunk.min_z;
    dungeon_map_chunks_.push_back(state);
  }
  dungeon_map_chunk_cache_map_id_ = -1;
  dungeon_map_chunk_cache_index_ = 0;

  overlay_sources_.clear();
  for (const auto &overlay : dbc.world_map_overlay()) {
    MapOverlay state;
    state.area_id = overlay.area_id[0];
    state.x = static_cast<float>(overlay.map_point_x);
    state.y = static_cast<float>(overlay.map_point_y);
    state.texture_w = overlay.texture_width;
    state.texture_h = overlay.texture_height;
    state.offset_x = overlay.offset_x;
    state.offset_y = overlay.offset_y;
    state.hit_rect_top = overlay.hit_rect_top;
    state.hit_rect_left = overlay.hit_rect_left;
    state.hit_rect_bottom = overlay.hit_rect_bottom;
    state.hit_rect_right = overlay.hit_rect_right;
    state.map_area_id = overlay.map_area_id;
    state.reveal_area_ids = overlay.area_id;
    state.texture_name = std::string(overlay.texture_name);
    overlay_sources_.push_back(std::move(state));
  }

  overlays_.clear();
  if (overlays_.capacity() < 32u) {
    overlays_.reserve(32u);
  }

  landmark_sources_.clear();
  for (const auto &area_poi : dbc.area_poi()) {
    LandmarkSource state;
    state.id = area_poi.id;
    state.name = std::string(area_poi.name);
    state.description = std::string(area_poi.description);
    state.texture_indices = area_poi.texture_indices;
    state.world_x = area_poi.x;
    state.world_y = area_poi.y;
    state.world_z = area_poi.z;
    state.map_id = static_cast<std::int32_t>(area_poi.map_id);
    state.flags = area_poi.flags;
    state.area_id = area_poi.area_id;
    state.world_state_id = area_poi.world_state_id;
    state.map_link_id = area_poi.map_link_id;
    landmark_sources_.push_back(std::move(state));
  }
  landmarks_.clear();
  if (landmarks_.capacity() < 16u) {
    landmarks_.reserve(16u);
  }

  orphan_world_map_area_ids_.clear();
  for (const auto &wma : dbc.world_map_area()) {
    if (wma.area_id == 0) {
      continue;
    }

    const auto bounds_it = wma_bounds_.find(wma.id);
    if (bounds_it == wma_bounds_.end()) {
      continue;
    }

    bool found_continent = false;
    const auto child_map_id = bounds_it->second.display_map_id >= 0
                                  ? static_cast<std::uint32_t>(bounds_it->second.display_map_id)
                                  : bounds_it->second.map_id;
    for (const auto &continent : continents_) {
      if (continent.map_id == child_map_id) {
        found_continent = true;
        break;
      }
    }

    if (!HasVisibleWorldMapBounds(bounds_it->second) || !found_continent) {
      orphan_world_map_area_ids_.push_back(wma.id);
    }
  }

  current_world_map_area_token_ = -1;
  dbc_initialized_ = true;
}

bool WorldMapSystem::IsDbcInitialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dbc_initialized_;
}

std::size_t WorldMapSystem::GetContinentCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return continents_.size();
}

const ContinentInfo *WorldMapSystem::GetContinentInfo(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= continents_.size())
    return nullptr;
  return &continents_[index];
}

void WorldMapSystem::SetMapView(int continent_1based, int zone_1based, int dungeon_map_id) {
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const int continent_token = continent_1based - 1;
    const int zone_token = zone_1based > 0 ? zone_1based - 1 : -1;
    updated = CommitSelectionAndRefreshLandmarksNoLock(continent_token, zone_token, dungeon_map_id);
  }
  if (updated) {
    NotifyMapUpdated();
  }
}

bool WorldMapSystem::CommitSelectionAndRefreshLandmarks(int continent_token,
                                                        int zone_token_or_area_id,
                                                        int dungeon_map_id) {
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    updated = CommitSelectionAndRefreshLandmarksNoLock(continent_token, zone_token_or_area_id,
                                                       dungeon_map_id);
  }
  if (updated) {
    NotifyMapUpdated();
  }
  return updated;
}

bool WorldMapSystem::RefreshCurrentSelection() {

  int continent_token = -1;
  int zone_token_or_area_id = -1;
  int dungeon_map_id = -1;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool has_explicit_world_map_area = current_world_map_area_token_ != -1;

    continent_token = has_explicit_world_map_area && current_continent_token_ < 0
                          ? -2
                          : current_continent_token_;
    zone_token_or_area_id =
        has_explicit_world_map_area ? current_world_map_area_token_ : current_zone_token_;
    dungeon_map_id = current_dungeon_map_id_;
  }
  return CommitSelectionAndRefreshLandmarks(continent_token, zone_token_or_area_id, dungeon_map_id);
}

bool WorldMapSystem::SetMapByWorldMapAreaId(std::uint32_t world_map_area_id, int dungeon_map_id) {
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto wma_it = wma_bounds_.find(world_map_area_id);
    if (wma_it == wma_bounds_.end()) {
      return false;
    }

    const auto continent_index = FindContinentIndexByMapIdNoLock(wma_it->second.map_id);
    if (continent_index < 0) {
      updated = CommitSelectionAndRefreshLandmarksNoLock(-2, static_cast<int>(world_map_area_id),
                                                         dungeon_map_id);
    } else {
      const auto zone_index = FindZoneIndexForContinentWmaNoLock(
          static_cast<std::size_t>(continent_index), world_map_area_id);
      if (zone_index >= 0) {
        updated =
            CommitSelectionAndRefreshLandmarksNoLock(continent_index, zone_index, dungeon_map_id);
      } else {
        updated = CommitSelectionAndRefreshLandmarksNoLock(continent_index, -1, dungeon_map_id);
      }
    }
  }
  if (updated) {
    NotifyMapUpdated();
  }
  return updated;
}

bool WorldMapSystem::ClickLandmark(const std::uint32_t map_link_id) {
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto dungeon_map_it = dungeon_maps_.find(map_link_id);
    if (dungeon_map_it == dungeon_maps_.end()) {
      return false;
    }

    if (const auto orphan_wma_id =
            FindOrphanWorldMapAreaIdByMapIdNoLock(dungeon_map_it->second.map_id)) {
      updated = CommitSelectionAndRefreshLandmarksNoLock(
          -2, static_cast<int>(*orphan_wma_id),
          static_cast<int>(std::bit_cast<std::int32_t>(map_link_id)));
    }
  }

  if (updated) {
    NotifyMapUpdated();
  }
  return updated;
}

bool WorldMapSystem::UpdatePlayerPosition(const openwow::game::WorldSession &session) {
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto *player = session.objects().GetLocalPlayerTyped();
    if (!player) {
      updated = CommitSelectionAndRefreshLandmarksNoLock(-1, -1, -1);
    } else {

      std::int32_t map_id =
          session.has_current_map() ? static_cast<std::int32_t>(session.current_map_id()) : -1;
      if (map_id < 0) {
        updated = CommitSelectionAndRefreshLandmarksNoLock(-1, -1, -1);
      } else {
        const float world_x = player->GetX();
        const float world_y = player->GetY();
        AppliedMapTransform transformed =
            ApplyMapTransformNoLock(static_cast<std::uint32_t>(map_id),
                                    world_x,
                                    world_y);

        const std::uint32_t zone_id =
            static_cast<std::uint32_t>(std::max(session.world_states().zone_id(), 0));
        auto continent_index = FindContinentIndexByMapIdNoLock(transformed.map_id);

        if (continent_index < 0) {
          updated = ResolveOrphanSelectionNoLock(map_id, transformed.dungeon_map_id, player,
                                                 &session.world_states());
        }

        if (!updated && continent_index < 0 && dbc_loader_ != nullptr) {
          const auto *map_entry = dbc_loader_->map().LookupEntry(transformed.map_id);
          if (map_entry != nullptr && (std::fabs(map_entry->entrance_x) >= 0.00000023841858f ||
                                       std::fabs(map_entry->entrance_y) >= 0.00000023841858f)) {

            auto entrance = ApplyMapTransformNoLock(
                static_cast<std::uint32_t>(std::max(map_entry->entrance_map, 0)),
                map_entry->entrance_x, map_entry->entrance_y);
            if (entrance.map_id != 0) {
              transformed = entrance;
              continent_index = FindContinentIndexByMapIdNoLock(transformed.map_id);
            }
          }
        }

        if (!updated && continent_index >= 0) {
          updated = ResolveContinentSelectionNoLock(continent_index, zone_id, player,
                                                    &session.world_states());
        } else if (!updated) {
          updated =
              CommitSelectionAndRefreshLandmarksNoLock(-2, -1, -1, player, &session.world_states());
        }
      }
    }
  }
  if (updated) {
    NotifyMapUpdated();
  }
  return updated;
}

void WorldMapSystem::SetCurrentAreaId(std::uint32_t area_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_continent_token_ = -1;
  current_zone_token_ = -1;
  current_area_id_ = area_id;
  current_world_map_area_token_ = area_id == 0 ? -1 : static_cast<int>(area_id);
  current_dungeon_map_id_ = -1;
}

std::uint32_t WorldMapSystem::GetCurrentAreaId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_area_id_;
}

void WorldMapSystem::SetCurrentContinentIndex(int index_1based) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_continent_token_ = index_1based - 1;
  current_world_map_area_token_ = -1;
  current_dungeon_map_id_ = -1;
  SyncDisplayedAreaIdNoLock();
}

int WorldMapSystem::GetCurrentContinentIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveCurrentMapContinentForLuaNoLock();
}

void WorldMapSystem::SetCurrentZoneIndex(int index_1based) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_zone_token_ = index_1based > 0 ? index_1based - 1 : -1;
  current_world_map_area_token_ = -1;
  current_dungeon_map_id_ = -1;
  SyncDisplayedAreaIdNoLock();
}

int WorldMapSystem::GetCurrentZoneIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveCurrentMapZoneForLuaNoLock();
}

void WorldMapSystem::SetCurrentDungeonMapId(int dungeon_map_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_dungeon_map_id_ = dungeon_map_id;
}

int WorldMapSystem::GetCurrentDungeonMapId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return GetSelectedDungeonMapIdOrMinusOneNoLock();
}

int WorldMapSystem::GetCurrentDungeonFloorIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (current_world_map_area_token_ == -1) {
    return 0;
  }

  const auto dungeon_map_it = dungeon_maps_.find(static_cast<std::uint32_t>(current_dungeon_map_id_));
  if (dungeon_map_it == dungeon_maps_.end()) {
    return 0;
  }

  return static_cast<int>(dungeon_map_it->second.floor_index);
}

void WorldMapSystem::SetCurrentWmoContext(std::int32_t map_id, std::int32_t wmo_group_id,
                                          std::int32_t dungeon_map_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_wmo_map_id_ = map_id;
  current_wmo_group_id_ = wmo_group_id;
  current_wmo_dungeon_map_id_ = dungeon_map_id;
}

void WorldMapSystem::SetMapUpdateCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  map_update_callback_ = std::move(callback);
}

void WorldMapSystem::RegisterActivePlayerExplorationRefresh(openwow::game::ObjectGuid guid) {
  UnregisterActivePlayerExplorationRefresh();
  if (!guid) {
    return;
  }

  const auto handle =
      openwow::game::DescriptorCallbackRegistry::Get().RegisterObjectSectionCallback(
          guid, openwow::game::TypeID::kPlayer,
          kPlayerExplorationRelativeOffsetBytes,
          kPlayerExplorationSizeBytes,
          [this](const openwow::game::DescriptorFieldChangeView&) {
            (void)RefreshCurrentSelection();
          });

  std::lock_guard<std::mutex> lock(mutex_);
  active_player_exploration_callback_handle_ = handle.value;
}

void WorldMapSystem::UnregisterActivePlayerExplorationRefresh() {
  std::uint64_t handle_value = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handle_value = active_player_exploration_callback_handle_;
    active_player_exploration_callback_handle_ = 0;
  }

  if (handle_value != 0) {
    openwow::game::DescriptorCallbackRegistry::Get().Unregister(
        openwow::game::DescriptorCallbackRegistry::Handle{handle_value});
  }
}

int WorldMapSystem::GetCurrentMapAreaIdForLua() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveDisplayedWmaIdNoLock() + 1;
}

int WorldMapSystem::GetCurrentMapContinentForLua() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveCurrentMapContinentForLuaNoLock();
}

int WorldMapSystem::GetCurrentMapZoneForLua() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveCurrentMapZoneForLuaNoLock();
}

int WorldMapSystem::GetCurrentMapDungeonLevelForLua() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveCurrentMapDungeonLevelForLuaNoLock();
}

bool WorldMapSystem::SetDungeonMapLevel(int level_1based) {
  bool restore_explicit_world_map_area = false;
  int selected_world_map_area_token = -1;
  bool refresh_current_selection = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const WmaBounds *selected_wma = LookupSelectedWorldMapAreaForDungeonMapsNoLock();
    if (selected_wma == nullptr) {
      return false;
    }

    int adjusted_level = level_1based;
    if (selected_wma->default_dungeon_map_id == -1) {
      --adjusted_level;
      if (adjusted_level < 0) {
        return false;
      }
    } else if (adjusted_level <= 0) {
      return false;
    }

    if (adjusted_level == 0) {
      restore_explicit_world_map_area = true;
      selected_world_map_area_token = current_world_map_area_token_;
    } else {
      bool saw_map_block = false;
      for (const auto &dungeon_map : dungeon_maps_in_order_) {
        if (dungeon_map.map_id != selected_wma->map_id) {
          if (saw_map_block) {
            return false;
          }
          continue;
        }

        saw_map_block = true;
        if (static_cast<int>(dungeon_map.floor_index) != adjusted_level) {
          continue;
        }

        if (dungeon_map.id == 0) {
          return false;
        }

        current_dungeon_map_id_ = static_cast<int>(dungeon_map.id);
        refresh_current_selection = true;
        break;
      }
    }
  }

  if (restore_explicit_world_map_area) {
    return CommitSelectionAndRefreshLandmarks(-2, selected_world_map_area_token, -1);
  }

  if (refresh_current_selection) {
    return RefreshCurrentSelection();
  }

  return false;
}

bool WorldMapSystem::IsZoomOutAvailable() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (current_world_map_area_token_ != -1 && current_dungeon_map_id_ > 0) {
    return LookupDungeonMapStateNoLock(current_dungeon_map_id_) != nullptr;
  }

  if (current_continent_token_ != -2) {
    return true;
  }

  return ResolveZoomOutTargetWorldMapAreaIdNoLock() > 0;
}

bool WorldMapSystem::CurrentSelectionUsesTerrainMap() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (current_world_map_area_token_ < 0) {
    return false;
  }

  const auto wma_it = wma_bounds_.find(static_cast<std::uint32_t>(current_world_map_area_token_));
  return wma_it != wma_bounds_.end() && wma_it->second.default_dungeon_map_id == -1;
}

bool WorldMapSystem::ZoomOut() {
  std::int32_t target_world_map_area_id = -1;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    target_world_map_area_id = ResolveZoomOutTargetWorldMapAreaIdNoLock();
  }

  if (target_world_map_area_id <= 0) {
    return false;
  }

  return SetMapByWorldMapAreaId(static_cast<std::uint32_t>(target_world_map_area_id), -1);
}

WorldMapSystem::AppliedMapTransform WorldMapSystem::ApplyMapTransform(std::uint32_t map_id, float x,
                                                                      float y) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ApplyMapTransformNoLock(map_id, x, y);
}

WorldMapSystem::AppliedMapTransform
WorldMapSystem::ApplyMapTransformNoLock(std::uint32_t map_id, float x, float y) const {
  return openwow::game::ApplyWorldMapTransform(world_map_transforms_, map_id, x, y);
}

std::int32_t WorldMapSystem::GetSelectedDungeonMapIdOrMinusOneNoLock() const {
  return current_world_map_area_token_ == -1 ? -1 : current_dungeon_map_id_;
}

const WorldMapSystem::DungeonMapState *
WorldMapSystem::LookupDungeonMapStateNoLock(std::int32_t dungeon_map_id) const {
  if (dungeon_map_id <= 0) {
    return nullptr;
  }

  const auto dungeon_map_it = dungeon_maps_.find(static_cast<std::uint32_t>(dungeon_map_id));
  if (dungeon_map_it == dungeon_maps_.end()) {
    return nullptr;
  }

  return &dungeon_map_it->second;
}

const WorldMapSystem::WmaBounds *WorldMapSystem::LookupSelectedWorldMapAreaForDungeonMapsNoLock()
    const {
  if (current_world_map_area_token_ < 0) {
    return nullptr;
  }

  if (current_continent_token_ >= 0 &&
      ResolveDisplayedWmaIdNoLock() != current_world_map_area_token_) {
    return nullptr;
  }

  const auto wma_it = wma_bounds_.find(static_cast<std::uint32_t>(current_world_map_area_token_));
  if (wma_it == wma_bounds_.end()) {
    return nullptr;
  }

  return &wma_it->second;
}

WorldMapSystem::MapCoord WorldMapSystem::ProjectRectBounds(float loc_left, float loc_right,
                                                           float loc_top, float loc_bottom,
                                                           float world_x, float world_y,
                                                           bool clamp_to_bounds) {
  const auto projection = ProjectSelectionRect(loc_left, loc_right, loc_top, loc_bottom, world_x,
                                               world_y, clamp_to_bounds, false);
  return {projection.x, projection.y, projection.valid};
}

WorldMapSystem::SelectionProjection WorldMapSystem::ProjectSelectionRect(
    float loc_left, float loc_right, float loc_top, float loc_bottom, float world_x,
    float world_y, bool clamp_to_bounds, const bool indoors) {
  const float width = loc_left - loc_right;
  const float height = loc_top - loc_bottom;
  if (std::fabs(width) < 0.001f || std::fabs(height) < 0.001f) {
    return {0.0f, 0.0f, false, indoors};
  }

  float map_x = (loc_left - world_y) / width;
  float map_y = (loc_top - world_x) / height;
  if (!clamp_to_bounds && (map_x < 0.0f || map_x > 1.0f || map_y < 0.0f || map_y > 1.0f)) {
    return {0.0f, 0.0f, false, indoors};
  }

  if (clamp_to_bounds) {
    map_x = std::clamp(map_x, 0.0f, 1.0f);
    map_y = std::clamp(map_y, 0.0f, 1.0f);
  }

  return {map_x, map_y, true, indoors};
}

WorldMapSystem::MapCoord WorldMapSystem::WorldToMapForCurrentSelection(std::uint32_t map_id,
                                                                       float world_x, float world_y,
                                                                       float world_z,
                                                                       int dungeon_map_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto legacy = LegacyWorldToMapCoordsNoLock(map_id, world_x, world_y, world_z, false,
                                                   dungeon_map_id, true);
  return {.x = legacy.x,
          .y = legacy.y,
          .valid = legacy.displayable,
          .suppress_unit_position = legacy.suppress_unit_position};
}

WorldMapSystem::SelectionProjection WorldMapSystem::ProjectCurrentSelectionWithIndoorFlag(
    std::uint32_t map_id, float world_x, float world_y, float world_z, int dungeon_map_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ProjectCurrentSelectionWithIndoorFlagNoLock(map_id, world_x, world_y, world_z,
                                                     dungeon_map_id, true);
}

WorldMapSystem::MapCoord WorldMapSystem::WorldToMapForCurrentSelectionNoLock(
    std::uint32_t map_id, float world_x, float world_y, float world_z, int dungeon_map_id,
    bool resolve_chunk_dungeon_map) {
  const auto legacy = LegacyWorldToMapCoordsNoLock(map_id, world_x, world_y, world_z, false,
                                                   dungeon_map_id, resolve_chunk_dungeon_map);
  return {legacy.x, legacy.y, legacy.displayable};
}

WorldMapSystem::LegacyWorldToMapCoordsResult WorldMapSystem::LegacyWorldToMapCoordsNoLock(
    const std::uint32_t map_id, const float world_x, const float world_y, const float world_z,
    const bool allow_outside_bounds, const int dungeon_map_id,
    const bool resolve_chunk_dungeon_map) const {
  LegacyWorldToMapCoordsResult result;
  const auto transformed = ApplyMapTransformNoLock(map_id, world_x, world_y);
  if (current_continent_token_ == -1) {
    const auto continent_index = FindContinentIndexByMapIdNoLock(transformed.map_id);
    if (continent_index >= 0) {
      const auto& continent = continents_[static_cast<std::size_t>(continent_index)];
      const auto scale = static_cast<double>(continent.scale);
      result.x = static_cast<float>(static_cast<double>(continent.continent_offset_x) *
                                        kLegacyOverviewTextureXScale +
                                    0.5 -
                                    static_cast<double>(transformed.y) *
                                        kLegacyOverviewCoordXScale * scale);
      result.y = static_cast<float>(static_cast<double>(continent.continent_offset_y) *
                                        kLegacyOverviewTextureYScale +
                                    0.5 -
                                    static_cast<double>(transformed.x) *
                                        kLegacyOverviewCoordYScale * scale);
      result.has_projection = true;
      result.displayable = true;
      result.suppress_unit_position = transformed.map_id == 530u;
      return result;
    }
  }

  const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  if (displayed_wma_id <= 0) {
    return result;
  }

  const auto bounds_it = wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id));
  if (bounds_it == wma_bounds_.end()) {
    return result;
  }

  if (bounds_it->second.map_id != map_id && bounds_it->second.map_id != transformed.map_id) {
    return result;
  }

  float projected_world_x = transformed.x;
  float projected_world_y = transformed.y;
  if (current_zone_token_ >= 0) {
    projected_world_x = world_x;
    projected_world_y = world_y;
  }

  int resolved_dungeon_map_id = dungeon_map_id > 0 ? dungeon_map_id : transformed.dungeon_map_id;
  if (resolved_dungeon_map_id <= 0 && resolve_chunk_dungeon_map && current_wmo_group_id_ > 0) {
    resolved_dungeon_map_id =
        current_wmo_dungeon_map_id_ > 0
            ? current_wmo_dungeon_map_id_
            : FindDungeonMapForWmoGroupZNoLock(static_cast<std::int32_t>(map_id),
                                               current_wmo_group_id_, world_z);
  }

  const int selected_dungeon_map_id = GetSelectedDungeonMapIdOrMinusOneNoLock();
  if (resolved_dungeon_map_id == selected_dungeon_map_id) {
    if (const auto *dungeon_map = LookupDungeonMapStateNoLock(resolved_dungeon_map_id)) {
      return ProjectLegacyWorldMapRect(dungeon_map->loc_left, dungeon_map->loc_right,
                                       dungeon_map->loc_top, dungeon_map->loc_bottom,
                                       projected_world_x, projected_world_y,
                                       allow_outside_bounds, false);
    }
  } else {
    if (transformed.dungeon_map_id == selected_dungeon_map_id) {
      if (const auto *dungeon_map = LookupDungeonMapStateNoLock(transformed.dungeon_map_id)) {
        return ProjectLegacyWorldMapRect(dungeon_map->loc_left, dungeon_map->loc_right,
                                         dungeon_map->loc_top, dungeon_map->loc_bottom,
                                         projected_world_x, projected_world_y,
                                         allow_outside_bounds, false);
      }
    }

    if (selected_dungeon_map_id > 0) {
      return result;
    }
  }

  return ProjectLegacyWorldMapRect(bounds_it->second.loc_left, bounds_it->second.loc_right,
                                   bounds_it->second.loc_top, bounds_it->second.loc_bottom,
                                   projected_world_x, projected_world_y, allow_outside_bounds,
                                   resolved_dungeon_map_id > 0);
}

WorldMapSystem::SelectionProjection WorldMapSystem::ProjectCurrentSelectionWithIndoorFlagNoLock(
    std::uint32_t map_id, float world_x, float world_y, float world_z, int dungeon_map_id,
    bool resolve_chunk_dungeon_map) const {
  const auto legacy = LegacyWorldToMapCoordsNoLock(map_id, world_x, world_y, world_z, false,
                                                   dungeon_map_id, resolve_chunk_dungeon_map);
  return {legacy.x, legacy.y, legacy.displayable, legacy.indoors};
}

std::int32_t WorldMapSystem::GetContinentChildCount(std::uint32_t continent_token) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (continent_token >= continents_.size()) {
    return 0;
  }
  return static_cast<std::int32_t>(continents_[continent_token].zone_wma_ids.size());
}

std::int32_t WorldMapSystem::GetContinentChildWorldMapAreaId(std::uint32_t continent_token,
                                                             std::uint32_t zone_token) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (continent_token >= continents_.size()) {
    return -1;
  }

  const auto &zones = continents_[continent_token].zone_wma_ids;
  if (zone_token >= zones.size()) {
    return -1;
  }

  return static_cast<std::int32_t>(zones[zone_token]);
}

std::int32_t WorldMapSystem::FindDungeonMapForWmoGroupZ(std::int32_t map_id,
                                                        std::int32_t wmo_group_id, float z) {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindDungeonMapForWmoGroupZNoLock(map_id, wmo_group_id, z);
}

std::int32_t WorldMapSystem::FindDungeonMapForWmoGroupZNoLock(std::int32_t map_id,
                                                              std::int32_t wmo_group_id, float z) const {
  if (map_id <= 0 || wmo_group_id <= 0) {
    return -1;
  }

  std::int32_t start_index = 0;
  if (dungeon_map_chunk_cache_map_id_ == map_id) {
    start_index = dungeon_map_chunk_cache_index_;
  } else {
    dungeon_map_chunk_cache_map_id_ = map_id;
    dungeon_map_chunk_cache_index_ = 0;
  }

  bool saw_map_block = false;
  bool saw_group_block = false;
  std::int32_t result = -1;
  for (std::int32_t i = start_index; i < static_cast<std::int32_t>(dungeon_map_chunks_.size());
       ++i) {
    const auto &chunk = dungeon_map_chunks_[static_cast<std::size_t>(i)];
    if (chunk.map_id == map_id) {
      if (!saw_map_block) {
        saw_map_block = true;
        dungeon_map_chunk_cache_index_ = i;
      }

      if (chunk.wmo_group_id == wmo_group_id) {
        saw_group_block = true;
        if (z >= chunk.min_z) {
          result = chunk.dungeon_map_id;
        }
      } else if (saw_group_block) {
        return result;
      }
    } else if (saw_map_block) {
      return result;
    }
  }

  return result;
}

bool WorldMapSystem::ResolveOrphanSelectionNoLock(
    std::int32_t map_id, std::int32_t transformed_dungeon_map_id,
    const openwow::game::CGPlayer_C *player, const openwow::game::WorldStateManager *world_states) {
  if (map_id <= 0) {
    return false;
  }

  const auto orphan_wma_id =
      FindOrphanWorldMapAreaIdByMapIdNoLock(static_cast<std::uint32_t>(map_id));
  if (!orphan_wma_id) {
    return false;
  }

  const auto &bounds = wma_bounds_.at(*orphan_wma_id);
  int resolved_dungeon_map_id = -1;
  if (current_wmo_group_id_ > 0) {
    resolved_dungeon_map_id =
        current_wmo_dungeon_map_id_ > 0
            ? current_wmo_dungeon_map_id_
            : FindDungeonMapForWmoGroupZNoLock(static_cast<std::int32_t>(bounds.map_id),
                                               current_wmo_group_id_,
                                               player != nullptr ? player->GetZ() : 0.0f);
  }
  if (resolved_dungeon_map_id <= 0 && transformed_dungeon_map_id > 0) {
    resolved_dungeon_map_id = transformed_dungeon_map_id;
  }

  return CommitSelectionAndRefreshLandmarksNoLock(
      -2, static_cast<int>(*orphan_wma_id),
      resolved_dungeon_map_id > 0 ? resolved_dungeon_map_id : -1, player, world_states);
}

std::int32_t WorldMapSystem::LookupContinentWorldMapAreaIdNoLock(std::size_t continent_index,
                                                                 float world_x, float world_y,
                                                                 bool *clamped) const {
  if (clamped != nullptr) {
    *clamped = false;
  }
  if (continent_index >= continents_.size()) {
    return -1;
  }

  const float norm_x = 0.5f - world_x * kContinentLookupScale_;
  const float norm_y = 0.5f - world_y * kContinentLookupScale_;
  if (norm_x < 0.0f || norm_x >= 1.0f || norm_y < 0.0f || norm_y >= 1.0f) {
    if (clamped != nullptr) {
      *clamped = true;
    }
    return -1;
  }

  const auto &lookup_wma_ids = continents_[continent_index].lookup_wma_ids;
  const auto lookup_index = ComputeContinentLookupIndex(norm_x, norm_y);
  if (lookup_index < lookup_wma_ids.size() && lookup_wma_ids[lookup_index] != 0) {
    return static_cast<std::int32_t>(lookup_wma_ids[lookup_index]);
  }

  return -1;
}

bool WorldMapSystem::ResolveContinentSelectionNoLock(
    std::int32_t continent_index, std::uint32_t current_zone_id,
    const openwow::game::CGPlayer_C *player, const openwow::game::WorldStateManager *world_states) {
  if (continent_index < 0 || static_cast<std::size_t>(continent_index) >= continents_.size()) {
    return false;
  }

  int fallback_zone_index = -1;
  const auto &zone_wma_ids = continents_[static_cast<std::size_t>(continent_index)].zone_wma_ids;
  for (std::size_t i = 0; i < zone_wma_ids.size(); ++i) {
    const auto bounds_it = wma_bounds_.find(zone_wma_ids[i]);
    if (bounds_it == wma_bounds_.end() || bounds_it->second.area_id != current_zone_id) {
      continue;
    }

    if (HasVisibleWorldMapBounds(bounds_it->second)) {
      fallback_zone_index = static_cast<int>(i);
      break;
    }

    if (current_wmo_group_id_ > 0) {
      const auto dungeon_map_id =
          current_wmo_dungeon_map_id_ > 0
              ? current_wmo_dungeon_map_id_
              : FindDungeonMapForWmoGroupZNoLock(
                    static_cast<std::int32_t>(bounds_it->second.map_id), current_wmo_group_id_,
                    player != nullptr ? player->GetZ() : 0.0f);
      if (dungeon_map_id >= 0) {
        return CommitSelectionAndRefreshLandmarksNoLock(-2, static_cast<int>(zone_wma_ids[i]),
                                                        dungeon_map_id, player, world_states);
      }
    }

    if (bounds_it->second.parent_world_map_id != 0) {
      const auto parent_zone_index = FindZoneIndexForContinentWmaNoLock(
          static_cast<std::size_t>(continent_index), bounds_it->second.parent_world_map_id);
      if (parent_zone_index >= 0) {
        fallback_zone_index = parent_zone_index;
      }
    }
  }

  return CommitSelectionAndRefreshLandmarksNoLock(continent_index, fallback_zone_index, -1, player,
                                                  world_states);
}

void WorldMapSystem::SetLandmarks(const std::vector<MapLandmarkInfo> &landmarks) {
  std::lock_guard<std::mutex> lock(mutex_);
  landmark_sources_.clear();
  landmark_sources_.reserve(landmarks.size());
  for (const auto &landmark : landmarks) {
    LandmarkSource source;
    source.id = static_cast<std::uint32_t>(landmark_sources_.size() + 1);
    source.name = landmark.name;
    source.description = landmark.description;
    source.texture_indices[0] = landmark.texture_index;
    source.normalized_x = landmark.x;
    source.normalized_y = landmark.y;
    source.map_link_id = landmark.map_link_id;
    source.flags = landmark.show_in_battle_map ? 0x200u : 0u;
    source.has_normalized_coords = true;
    landmark_sources_.push_back(std::move(source));
  }
  landmarks_ = landmarks;
}

void WorldMapSystem::SetSpecialLandmark(const SpecialLandmarkState &landmark) {
  std::lock_guard<std::mutex> lock(mutex_);

  LandmarkSource source;
  source.name = landmark.name;
  source.description = landmark.description;
  source.texture_indices = landmark.texture_indices;
  source.world_x = landmark.world_x;
  source.world_y = landmark.world_y;
  source.map_id = landmark.map_id;
  source.world_state_id = landmark.world_state_id;
  source.map_link_id = landmark.map_link_id;
  source.flags = landmark.show_in_battle_map ? 0x200u : 0u;
  special_landmark_source_ = std::move(source);
}

void WorldMapSystem::ClearSpecialLandmark() {
  std::lock_guard<std::mutex> lock(mutex_);
  special_landmark_source_.reset();
}

std::size_t WorldMapSystem::GetNumLandmarks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return landmarks_.size();
}

const MapLandmarkInfo *WorldMapSystem::GetLandmark(std::size_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index >= landmarks_.size())
    return nullptr;
  return &landmarks_[index];
}

WorldMapSystem::MapCoord WorldMapSystem::WorldToMapForArea(std::uint32_t wma_id, float world_x,
                                                           float world_y) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = wma_bounds_.find(wma_id);
  if (it == wma_bounds_.end())
    return {0.0f, 0.0f, false};
  return ProjectRectBounds(it->second.loc_left, it->second.loc_right, it->second.loc_top,
                           it->second.loc_bottom, world_x, world_y, true);
}

WorldMapSystem::LegacyWorldToMapCoordsResult WorldMapSystem::LegacyWorldToMapCoords(
    const std::uint32_t map_id, const float world_x, const float world_y, const float world_z,
    const bool allow_outside_bounds, const int dungeon_map_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return LegacyWorldToMapCoordsNoLock(map_id, world_x, world_y, world_z, allow_outside_bounds,
                                      dungeon_map_id, true);
}

std::size_t WorldMapSystem::GetZoneCountForContinent(int continent_1based) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (continent_1based <= 0 || static_cast<std::size_t>(continent_1based) > continents_.size()) {
    return 0;
  }
  return continents_[continent_1based - 1].zone_wma_ids.size();
}

std::string WorldMapSystem::GetZoneNameForContinent(int continent_1based, int zone_1based) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (continent_1based <= 0 || static_cast<std::size_t>(continent_1based) > continents_.size()) {
    return {};
  }
  const auto &ci = continents_[continent_1based - 1];
  if (zone_1based <= 0 || static_cast<std::size_t>(zone_1based) > ci.zone_wma_ids.size()) {
    return {};
  }

  const auto name = LookupWorldMapAreaDisplayNameNoLock(ci.zone_wma_ids[zone_1based - 1]);
  if (!name.has_value()) {
    return {};
  }

  return std::string(*name);
}

std::uint32_t WorldMapSystem::GetWmaIdForContinent(int continent_1based, int zone_1based) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (continent_1based <= 0 || static_cast<std::size_t>(continent_1based) > continents_.size()) {
    return 0;
  }
  const auto &ci = continents_[continent_1based - 1];
  if (zone_1based <= 0 || static_cast<std::size_t>(zone_1based) > ci.zone_wma_ids.size()) {
    return 0;
  }
  return ci.zone_wma_ids[zone_1based - 1];
}

bool WorldMapSystem::CommitSelectionAndRefreshLandmarksNoLock(int continent_token,
                                                              int zone_token_or_area_id,
                                                              int dungeon_map_id) {
  const auto* const player = world_session_ != nullptr
                                 ? world_session_->objects().GetActivePlayer()
                                 : nullptr;
  return CommitSelectionAndRefreshLandmarksNoLock(
      continent_token, zone_token_or_area_id, dungeon_map_id,
      player, nullptr);
}

bool WorldMapSystem::CommitSelectionAndRefreshLandmarksNoLock(
    int continent_token, int zone_token_or_area_id, int dungeon_map_id,
    const openwow::game::CGPlayer_C *player, const openwow::game::WorldStateManager *world_states) {
  current_continent_token_ = continent_token;
  current_zone_token_ = -1;
  current_world_map_area_token_ = -1;
  current_dungeon_map_id_ = -1;

  if (continent_token >= 0 && static_cast<std::size_t>(continent_token) < continents_.size()) {
    const auto &continent = continents_[static_cast<std::size_t>(continent_token)];
    current_map_ = continent.map_id;

    if (zone_token_or_area_id >= 0 &&
        static_cast<std::size_t>(zone_token_or_area_id) < continent.zone_wma_ids.size()) {
      current_zone_token_ = zone_token_or_area_id;
    }

    const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
    auto wma_it = displayed_wma_id > 0
                      ? wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id))
                      : wma_bounds_.end();
    if (wma_it != wma_bounds_.end() && wma_it->second.default_dungeon_map_id > 0) {

      current_continent_token_ = -2;
      current_zone_token_ = -1;
      current_world_map_area_token_ = displayed_wma_id;
      current_dungeon_map_id_ = wma_it->second.default_dungeon_map_id;
      current_map_ = wma_it->second.map_id;
    }
  } else {
    current_map_ = 0;
    current_zone_token_ = -1;
    current_world_map_area_token_ = continent_token == -2 ? zone_token_or_area_id : -1;
    current_dungeon_map_id_ = dungeon_map_id;

    if (continent_token == -2 && zone_token_or_area_id > 0) {
      auto wma_it = wma_bounds_.find(static_cast<std::uint32_t>(zone_token_or_area_id));
      current_map_ = wma_it != wma_bounds_.end() ? wma_it->second.map_id : 0;
    } else if (continent_token == -1) {
      current_map_ = 0;
    }
  }

  SyncDisplayedAreaIdNoLock();
  const auto resolved_continent = ResolveCurrentMapContinentForLuaNoLock();
  current_continent_ = resolved_continent > 0 ? static_cast<std::uint32_t>(resolved_continent) : 0;
  RefreshVisibleLandmarksNoLock(player, world_states);
  RefreshVisibleOverlaysNoLock(player);
  return true;
}

bool WorldMapSystem::IsAreaVisibleForPlayerNoLock(std::uint32_t area_id,
                                                  const openwow::game::CGPlayer_C *player) const {
  if (area_id == 0 || player == nullptr || dbc_loader_ == nullptr) {
    return false;
  }

  const auto *area = dbc_loader_->area_table().LookupEntry(area_id);
  if (area == nullptr) {
    return false;
  }

  if (static_cast<std::int32_t>(area->area_bit) < 0) {
    return true;
  }

  return player->HasExploredZone(area->area_bit);
}

std::uint32_t WorldMapSystem::ResolveLandmarkTextureNoLock(
    const LandmarkSource &landmark, const openwow::game::WorldStateManager *world_states,
    const bool is_special_landmark) const {
  const bool can_resolve_texture =
      is_special_landmark || (landmark.flags & 0x80u) != 0u ||
      (current_zone_token_ == -1 && current_continent_token_ != -2);
  if (!can_resolve_texture) {
    return 0;
  }

  std::size_t texture_index = 0;
  if (landmark.world_state_id != 0 && world_states != nullptr) {
    const auto value =
        world_states->GetWorldState(static_cast<std::int32_t>(landmark.world_state_id));
    if (value > 0 && value <= static_cast<std::int32_t>(landmark.texture_indices.size())) {
      texture_index = static_cast<std::size_t>(value - 1);
    }
  }

  return landmark.texture_indices[texture_index];
}

WorldMapSystem::MapCoord
WorldMapSystem::ProjectLandmarkToSelectionNoLock(const LandmarkSource &landmark) {
  if (landmark.has_normalized_coords) {
    return {landmark.normalized_x, landmark.normalized_y, true};
  }
  if (landmark.map_id <= 0) {
    return {0.0f, 0.0f, false};
  }

  return WorldToMapForCurrentSelectionNoLock(static_cast<std::uint32_t>(landmark.map_id),
                                             landmark.world_x, landmark.world_y, 0.0f, -1, false);
}

void WorldMapSystem::RefreshVisibleLandmarksNoLock(
    const openwow::game::CGPlayer_C *player, const openwow::game::WorldStateManager *world_states) {
  landmarks_.clear();

  const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  auto displayed_wma_it = displayed_wma_id > 0
                              ? wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id))
                              : wma_bounds_.end();

  for (const auto &landmark : landmark_sources_) {
    const bool selection_visible =
        (((current_continent_token_ < 0 || current_zone_token_ < 0) &&
          current_continent_token_ != -2) ||
         (landmark.flags & 0x4u) != 0) &&
        (current_continent_token_ != -1 || (landmark.flags & 0x10u) != 0) &&
        (current_zone_token_ >= 0 || current_world_map_area_token_ >= 0 ||
         (landmark.flags & 0x8u) != 0);
    if (!selection_visible) {
      continue;
    }

    if ((landmark.flags & 0x400u) != 0) {
      if (displayed_wma_it == wma_bounds_.end() ||
          displayed_wma_it->second.map_id != static_cast<std::uint32_t>(landmark.map_id) ||
          displayed_wma_it->second.area_id != landmark.area_id) {
        continue;
      }
    }

    if (landmark.area_id != 0 && !IsAreaVisibleForPlayerNoLock(landmark.area_id, player)) {
      continue;
    }

    if (landmark.world_state_id != 0 &&
        (world_states == nullptr ||
         world_states->GetWorldState(static_cast<std::int32_t>(landmark.world_state_id)) == 0)) {
      continue;
    }

    const auto projected = ProjectLandmarkToSelectionNoLock(landmark);
    if (!projected.valid || (std::fabs(projected.x) < 0.00000023841858f &&
                             std::fabs(projected.y) < 0.00000023841858f)) {
      continue;
    }

    MapLandmarkInfo visible;
    visible.name = landmark.name;
    visible.description = landmark.description;
    visible.texture_index = ResolveLandmarkTextureNoLock(landmark, world_states, false);
    visible.x = projected.x;
    visible.y = projected.y;
    visible.map_link_id = landmark.map_link_id;
    visible.show_in_battle_map = (landmark.flags & 0x200u) != 0;
    landmarks_.push_back(std::move(visible));
  }

  if (special_landmark_source_.has_value()) {
    const auto projected = ProjectLandmarkToSelectionNoLock(*special_landmark_source_);
    if (projected.valid && (std::fabs(projected.x) >= 0.00000023841858f ||
                            std::fabs(projected.y) >= 0.00000023841858f)) {
      MapLandmarkInfo visible;
      visible.name = special_landmark_source_->name;
      visible.description = special_landmark_source_->description;
      visible.texture_index =
          ResolveLandmarkTextureNoLock(*special_landmark_source_, world_states, true);
      visible.x = projected.x;
      visible.y = projected.y;
      visible.map_link_id = special_landmark_source_->map_link_id;
      visible.show_in_battle_map = (special_landmark_source_->flags & 0x200u) != 0;
      landmarks_.push_back(std::move(visible));
    }
  }
}

void WorldMapSystem::RefreshVisibleOverlaysNoLock(const openwow::game::CGPlayer_C *player) {
  overlays_.clear();

  if (player == nullptr || current_continent_token_ == -2 ||
      (current_zone_token_ < 0 && current_world_map_area_token_ <= 0)) {
    return;
  }

  const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  if (displayed_wma_id <= 0) {
    return;
  }

  for (const auto &overlay : overlay_sources_) {
    if (overlay.map_area_id != static_cast<std::uint32_t>(displayed_wma_id)) {
      continue;
    }

    bool visible = false;
    for (const auto area_id : overlay.reveal_area_ids) {
      if (IsAreaVisibleForPlayerNoLock(area_id, player)) {
        visible = true;
        break;
      }
    }
    if (!visible) {
      continue;
    }

    MapOverlay resolved = overlay;
    resolved.texture_path = ResolveOverlayTexturePathNoLock(resolved);
    overlays_.push_back(std::move(resolved));
  }
}

std::string WorldMapSystem::ResolveOverlayTexturePathNoLock(const MapOverlay &overlay) const {
  if (!overlay.texture_path.empty() || overlay.texture_name.empty()) {
    return overlay.texture_path;
  }

  const auto wma_it = wma_bounds_.find(overlay.map_area_id);
  if (wma_it != wma_bounds_.end() && !wma_it->second.name.empty()) {
    return "Interface\\WorldMap\\" + wma_it->second.name + "\\" + overlay.texture_name;
  }

  return "Interface\\WorldMap\\World\\" + overlay.texture_name;
}

std::int32_t WorldMapSystem::ResolveDisplayedWmaIdNoLock() const {
  if (current_continent_token_ < 0) {
    return current_world_map_area_token_;
  }

  if (static_cast<std::size_t>(current_continent_token_) >= continents_.size()) {
    return -1;
  }

  const auto &continent = continents_[static_cast<std::size_t>(current_continent_token_)];
  if (current_zone_token_ < 0) {
    return continent.overview_wma_id == 0 ? -1
                                          : static_cast<std::int32_t>(continent.overview_wma_id);
  }

  if (static_cast<std::size_t>(current_zone_token_) >= continent.zone_wma_ids.size()) {
    return -1;
  }

  return static_cast<std::int32_t>(
      continent.zone_wma_ids[static_cast<std::size_t>(current_zone_token_)]);
}

std::size_t WorldMapSystem::ComputeContinentLookupIndex(float norm_x, float norm_y) {
  const auto cell_x = ComputeLegacyLookupCell(norm_x);
  const auto cell_y = ComputeLegacyLookupCell(norm_y);

  return cell_x * kContinentLookupCellsPerAxis_ + cell_y;
}

std::int32_t WorldMapSystem::FindRuntimeContinentIndexByMapIdNoLock(std::uint32_t map_id) const {
  for (std::size_t index = 0; index < continents_.size(); ++index) {
    if (continents_[index].map_id == map_id) {
      return static_cast<std::int32_t>(index);
    }
  }

  return -1;
}

std::int32_t WorldMapSystem::FindContinentIndexByMapIdNoLock(std::uint32_t map_id) const {
  return openwow::game::FindLastWorldMapContinentIndexByMapId(continents_, map_id);
}

std::int32_t WorldMapSystem::FindZoneIndexForContinentWmaNoLock(std::size_t continent_index,
                                                                std::uint32_t wma_id) const {
  if (continent_index >= continents_.size()) {
    return -1;
  }

  const auto &zones = continents_[continent_index].zone_wma_ids;
  for (std::size_t i = 0; i < zones.size(); ++i) {
    if (zones[i] == wma_id) {
      return static_cast<std::int32_t>(i);
    }
  }
  return -1;
}

std::int32_t WorldMapSystem::FindZoneIndexForAreaIdNoLock(std::size_t continent_index,
                                                          std::uint32_t area_id) const {
  if (area_id == 0 || continent_index >= continents_.size()) {
    return -1;
  }

  const auto &zone_wma_ids = continents_[continent_index].zone_wma_ids;
  for (std::size_t i = 0; i < zone_wma_ids.size(); ++i) {
    auto bounds_it = wma_bounds_.find(zone_wma_ids[i]);
    if (bounds_it == wma_bounds_.end() || bounds_it->second.area_id != area_id ||
        !HasVisibleWorldMapBounds(bounds_it->second)) {
      continue;
    }

    return static_cast<std::int32_t>(i);
  }

  return -1;
}

std::int32_t WorldMapSystem::FindWorldMapAreaIdForAreaNoLock(std::uint32_t area_id,
                                                             std::int32_t map_id) const {
  auto candidates_it = area_to_wmas_.find(area_id);
  if (candidates_it == area_to_wmas_.end()) {
    return -1;
  }

  for (const auto wma_id : candidates_it->second) {
    auto bounds_it = wma_bounds_.find(wma_id);
    if (bounds_it != wma_bounds_.end() &&
        static_cast<std::int32_t>(bounds_it->second.map_id) == map_id) {
      return static_cast<std::int32_t>(wma_id);
    }
  }

  return candidates_it->second.empty() ? -1
                                       : static_cast<std::int32_t>(candidates_it->second.front());
}

std::optional<std::uint32_t> WorldMapSystem::FindOrphanWorldMapAreaIdByMapIdNoLock(
    const std::uint32_t map_id) const {
  for (const auto orphan_wma_id : orphan_world_map_area_ids_) {
    const auto bounds_it = wma_bounds_.find(orphan_wma_id);
    if (bounds_it != wma_bounds_.end() && bounds_it->second.map_id == map_id) {
      return orphan_wma_id;
    }
  }
  return std::nullopt;
}

std::int32_t WorldMapSystem::FindContainingZoneForPointNoLock(std::size_t continent_index,
                                                              float world_x, float world_y) const {
  if (continent_index >= continents_.size()) {
    return -1;
  }

  const auto &zones = continents_[continent_index].zone_wma_ids;
  for (std::size_t i = 0; i < zones.size(); ++i) {
    auto bounds_it = wma_bounds_.find(zones[i]);
    if (bounds_it == wma_bounds_.end()) {
      continue;
    }

    const auto &bounds = bounds_it->second;
    const float dx = bounds.loc_left - bounds.loc_right;
    const float dy = bounds.loc_top - bounds.loc_bottom;
    if (std::fabs(dx) < 0.001f || std::fabs(dy) < 0.001f) {
      continue;
    }

    const float map_x = (bounds.loc_left - world_y) / dx;
    const float map_y = (bounds.loc_top - world_x) / dy;
    if (map_x >= 0.0f && map_x <= 1.0f && map_y >= 0.0f && map_y <= 1.0f) {
      return static_cast<std::int32_t>(i);
    }
  }

  return -1;
}

std::int32_t WorldMapSystem::ResolveCurrentMapContinentForLuaNoLock() const {
  if (current_world_map_area_token_ != -1) {
    auto dungeon_map_it = dungeon_maps_.find(static_cast<std::uint32_t>(current_dungeon_map_id_));
    if (dungeon_map_it != dungeon_maps_.end()) {
      const auto continent_index =
          FindRuntimeContinentIndexByMapIdNoLock(dungeon_map_it->second.map_id);
      if (continent_index >= 0) {
        return continent_index + 1;
      }
    }
  }

  return current_continent_token_ + 1;
}

std::int32_t WorldMapSystem::ResolveCurrentMapZoneForLuaNoLock() const {
  if (current_zone_token_ >= 0) {
    return current_zone_token_ + 1;
  }

  const std::int32_t displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  auto wma_it = displayed_wma_id > 0
                    ? wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id))
                    : wma_bounds_.end();
  if (wma_it == wma_bounds_.end()) {
    return current_zone_token_ + 1;
  }

  const auto continent_index = FindRuntimeContinentIndexByMapIdNoLock(wma_it->second.map_id);
  if (continent_index < 0) {
    return current_zone_token_ + 1;
  }

  const auto zone_index = FindZoneIndexForContinentWmaNoLock(
      static_cast<std::size_t>(continent_index), static_cast<std::uint32_t>(displayed_wma_id));
  if (zone_index >= 0) {
    return zone_index + 1;
  }

  return current_zone_token_ + 1;
}

std::int32_t WorldMapSystem::ResolveCurrentMapDungeonLevelForLuaNoLock() const {
  int level = 0;

  if (current_world_map_area_token_ != -1) {
    auto dungeon_map_it = dungeon_maps_.find(static_cast<std::uint32_t>(current_dungeon_map_id_));
    if (dungeon_map_it != dungeon_maps_.end()) {
      level = static_cast<int>(dungeon_map_it->second.floor_index);
    }
  }

  auto wma_it = current_world_map_area_token_ >= 0
                    ? wma_bounds_.find(static_cast<std::uint32_t>(current_world_map_area_token_))
                    : wma_bounds_.end();
  if (wma_it != wma_bounds_.end() && wma_it->second.default_dungeon_map_id == -1) {
    ++level;
  }

  return level;
}

WorldMapSystem::QuestPoiSelectionContext WorldMapSystem::GetQuestPoiSelectionContext() const {
  std::lock_guard<std::mutex> lock(mutex_);

  QuestPoiSelectionContext context;
  context.displayed_world_map_area_id = ResolveDisplayedWmaIdNoLock();
  if (context.displayed_world_map_area_id > 0 &&
      wma_bounds_.find(static_cast<std::uint32_t>(context.displayed_world_map_area_id)) !=
          wma_bounds_.end() &&
      (current_zone_token_ != -1 || current_continent_token_ == -2)) {
    context.can_update = true;
  }

  const auto selected_dungeon_map_id = GetSelectedDungeonMapIdOrMinusOneNoLock();
  if (const auto *dungeon_map = LookupDungeonMapStateNoLock(selected_dungeon_map_id);
      dungeon_map != nullptr) {
    context.selected_dungeon_map_id = static_cast<std::int32_t>(dungeon_map->id);
    context.can_update = true;
  }

  return context;
}

std::int32_t WorldMapSystem::ResolveZoomOutTargetWorldMapAreaIdNoLock() const {
  if (current_world_map_area_token_ != -1 && current_dungeon_map_id_ > 0) {
    const auto *dungeon_map = LookupDungeonMapStateNoLock(current_dungeon_map_id_);
    if (dungeon_map == nullptr || dungeon_map->parent_world_map_id == 0) {
      return -1;
    }
    return static_cast<std::int32_t>(dungeon_map->parent_world_map_id);
  }

  if (current_continent_token_ != -2) {
    return -1;
  }

  const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  if (displayed_wma_id <= 0) {
    return -1;
  }

  const auto current_wma_it = wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id));
  if (current_wma_it == wma_bounds_.end()) {
    return -1;
  }

  if (current_wma_it->second.parent_world_map_id != 0) {
    const auto parent_wma_it = wma_bounds_.find(current_wma_it->second.parent_world_map_id);
    if (parent_wma_it == wma_bounds_.end()) {
      return -1;
    }
    return static_cast<std::int32_t>(current_wma_it->second.parent_world_map_id);
  }

  const auto continent_index = FindContinentIndexByMapIdNoLock(current_wma_it->second.map_id);
  if (continent_index < 0) {
    return -1;
  }

  const auto overview_wma_id =
      continents_[static_cast<std::size_t>(continent_index)].overview_wma_id;
  return overview_wma_id == 0 ? -1 : static_cast<std::int32_t>(overview_wma_id);
}

std::optional<std::string> WorldMapSystem::LookupAreaNameNoLock(std::uint32_t area_id) const {
  if (area_id == 0 || dbc_loader_ == nullptr) {
    return std::nullopt;
  }

  const auto *area = dbc_loader_->area_table().LookupEntry(area_id);
  if (area == nullptr || area->name.empty()) {
    return std::nullopt;
  }

  return std::string(area->name);
}

std::optional<std::string_view>
WorldMapSystem::LookupWorldMapAreaDisplayNameNoLock(const std::uint32_t wma_id) const {
  if (dbc_loader_ == nullptr) {
    return std::nullopt;
  }

  const auto bounds_it = wma_bounds_.find(wma_id);
  if (bounds_it == wma_bounds_.end() || bounds_it->second.area_id == 0) {
    return std::nullopt;
  }

  const auto *area = dbc_loader_->area_table().LookupEntry(bounds_it->second.area_id);
  if (area == nullptr || area->name.empty()) {
    return std::nullopt;
  }

  return area->name;
}

std::optional<std::string> WorldMapSystem::LookupMapNameNoLock(std::uint32_t map_id) const {
  if (dbc_loader_ == nullptr) {
    return std::nullopt;
  }

  const auto *map = dbc_loader_->map().LookupEntry(map_id);
  if (map == nullptr || map->name.empty()) {
    return std::nullopt;
  }

  return std::string(map->name);
}

void WorldMapSystem::SyncDisplayedAreaIdNoLock() {
  const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  current_area_id_ = displayed_wma_id > 0 ? static_cast<std::uint32_t>(displayed_wma_id) : 0;
}

void WorldMapSystem::NotifyMapUpdated() {
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = map_update_callback_;
  }
  if (callback) {
    callback();
  }
}

void WorldMapSystem::SortContinentZoneWorldMapAreaIdsNoLock() {
  for (auto &continent : continents_) {
    std::stable_sort(
        continent.zone_wma_ids.begin(), continent.zone_wma_ids.end(),
        [this](const std::uint32_t lhs, const std::uint32_t rhs) {
          const auto left_name = LookupWorldMapAreaDisplayNameNoLock(lhs);
          const auto right_name = LookupWorldMapAreaDisplayNameNoLock(rhs);
          if (!left_name.has_value() || !right_name.has_value()) {
            return false;
          }

          return openwow::core::SStrCmpNoCaseCollate(left_name->data(), right_name->data(),
                                                     0x7FFFFFFFu) < 0;
        });
  }
}

bool WorldMapSystem::HasVisibleWorldMapBounds(const WmaBounds &bounds) {
  return std::fabs(bounds.loc_left) >= 0.00000023841858f ||
         std::fabs(bounds.loc_right) >= 0.00000023841858f ||
         std::fabs(bounds.loc_top) >= 0.00000023841858f ||
         std::fabs(bounds.loc_bottom) >= 0.00000023841858f;
}

std::int32_t WorldMapSystem::HitTestMapZone(float norm_x, float norm_y) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return HitTestMapZoneNoLock(norm_x, norm_y);
}

std::int32_t WorldMapSystem::HitTestMapZoneNoLock(float norm_x, float norm_y) const {
  if (current_continent_token_ == -2)
    return 0;

  if (current_continent_token_ != -1) {
    const float clamped_x = std::clamp(norm_x, 0.0f, 1.0f);
    const float clamped_y = std::clamp(norm_y, 0.0f, 1.0f);
    if (static_cast<std::size_t>(current_continent_token_) >= continents_.size()) {
      return 0;
    }

    const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
    if (displayed_wma_id <= 0) {
      return 0;
    }

    const auto bounds_it = wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id));
    if (bounds_it == wma_bounds_.end()) {
      return 0;
    }

    const auto &bounds = bounds_it->second;
    const auto transformed_top_left =
        ApplyMapTransformNoLock(bounds.map_id, bounds.loc_top, bounds.loc_left);
    const auto transformed_bottom_right =
        ApplyMapTransformNoLock(bounds.map_id, bounds.loc_bottom, bounds.loc_right);
    const float transformed_height = transformed_top_left.x - transformed_bottom_right.x;
    const float transformed_width = transformed_top_left.y - transformed_bottom_right.y;
    if (!LegacyFloatIsNonZero(transformed_height) ||
        !LegacyFloatIsNonZero(transformed_width)) {
      return 0;
    }

    const float world_x =
        transformed_bottom_right.x + transformed_height * (1.0f - clamped_y);
    const float world_y =
        transformed_bottom_right.y + transformed_width * (1.0f - clamped_x);
    const float lookup_x = 0.5f - world_x * kContinentLookupScale_;
    const float lookup_y = 0.5f - world_y * kContinentLookupScale_;
    if (lookup_x < 0.0f || lookup_x > 1.0f || lookup_y < 0.0f || lookup_y > 1.0f) {
      return 0;
    }

    const auto &continent = continents_[static_cast<std::size_t>(current_continent_token_)];
    const auto lookup_index = ComputeContinentLookupIndex(lookup_x, lookup_y);
    if (lookup_index >= continent.lookup_wma_ids.size()) {
      return 0;
    }

    const auto hovered_wma_id =
        static_cast<std::int32_t>(continent.lookup_wma_ids[lookup_index]);
    if (hovered_wma_id <= 0) {
      return 0;
    }

    if (current_zone_token_ >= 0 &&
        static_cast<std::size_t>(current_zone_token_) < continent.zone_wma_ids.size() &&
        hovered_wma_id ==
            static_cast<std::int32_t>(continent.zone_wma_ids[static_cast<std::size_t>(
                current_zone_token_)])) {
      return 0;
    }

    return hovered_wma_id;
  }

  for (std::size_t i = 0; i < continents_.size(); ++i) {
    const auto &continent = continents_[i];
    if (!continent.has_overview_bounds || continent.overview_wma_id == 0) {
      continue;
    }

    const float left = 0.5f -
                       static_cast<float>(continent.right_boundary) * kContinentLookupScale_;
    const float right = 0.5f -
                        static_cast<float>(continent.left_boundary) * kContinentLookupScale_;
    const float top = 0.5f -
                      static_cast<float>(continent.bottom_boundary) * kContinentLookupScale_;
    const float bottom = 0.5f -
                         static_cast<float>(continent.top_boundary) * kContinentLookupScale_;
    if (RectContainsPointInclusive(left, top, right, bottom, norm_x, norm_y)) {
      return static_cast<std::int32_t>(continent.overview_wma_id);
    }
  }

  return 0;
}

void WorldMapSystem::ProcessMapClick(float norm_x, float norm_y) {
  bool updated = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (current_continent_token_ == -2) {
      return;
    }

    if (current_continent_token_ == -1) {
      for (std::size_t i = 0; i < continents_.size(); ++i) {
        const auto &continent = continents_[i];
        if (continent.overview_wma_id == 0) {
          continue;
        }

        const float left =
            0.5f - static_cast<float>(continent.right_boundary) * kContinentLookupScale_;
        const float right =
            0.5f - static_cast<float>(continent.left_boundary) * kContinentLookupScale_;
        const float top =
            0.5f - static_cast<float>(continent.bottom_boundary) * kContinentLookupScale_;
        const float bottom =
            0.5f - static_cast<float>(continent.top_boundary) * kContinentLookupScale_;
        if (!RectContainsPointInclusive(left, top, right, bottom, norm_x, norm_y)) {
          continue;
        }

        updated = CommitSelectionAndRefreshLandmarksNoLock(static_cast<int>(i), -1, -1);
        break;
      }
    } else {
      if (static_cast<std::size_t>(current_continent_token_) >= continents_.size()) {
        return;
      }

      const auto &continent = continents_[static_cast<std::size_t>(current_continent_token_)];
      const auto hit_wma_id = static_cast<std::uint32_t>(HitTestMapZoneNoLock(norm_x, norm_y));
      if (hit_wma_id == 0) {
        return;
      }

      for (std::size_t j = 0; j < continent.zone_wma_ids.size(); ++j) {
        if (continent.zone_wma_ids[j] == hit_wma_id) {
          updated = CommitSelectionAndRefreshLandmarksNoLock(current_continent_token_,
                                                             static_cast<int>(j), -1);
          break;
        }
      }
    }
  }

  if (updated) {
    NotifyMapUpdated();
  }
}

std::int32_t WorldMapSystem::GetNumDungeonMapLevels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const WmaBounds *selected_wma = LookupSelectedWorldMapAreaForDungeonMapsNoLock();
  if (selected_wma == nullptr) {
    return 0;
  }

  const auto target_map_id = selected_wma->map_id;
  std::uint32_t seen_mask = 0;
  std::int32_t count = 0;

  for (const auto &dm : dungeon_maps_in_order_) {
    if (dm.map_id == target_map_id) {
      const std::uint32_t bit = std::uint32_t{1} << (dm.floor_index & 31u);
      if ((seen_mask & bit) == 0) {
        seen_mask |= bit;
        ++count;
      }
    }
  }

  if (selected_wma->default_dungeon_map_id == -1) {
    ++count;
  }

  return count;
}

WorldMapSystem::LegacyMapInfo WorldMapSystem::GetLegacyMapInfo() const {
  std::lock_guard<std::mutex> lock(mutex_);

  LegacyMapInfo result;
  const auto displayed_wma_id = ResolveDisplayedWmaIdNoLock();
  if (displayed_wma_id <= 0) {
    return result;
  }

  const auto wma_it = wma_bounds_.find(static_cast<std::uint32_t>(displayed_wma_id));
  if (wma_it == wma_bounds_.end()) {
    return result;
  }

  result.has_name = true;
  result.internal_name = wma_it->second.name;

  double width = 0.0;
  double height = 0.0;
  const auto dungeon_map_id =
      current_world_map_area_token_ == -1 ? -1 : current_dungeon_map_id_;

  if (const auto *dungeon_map = LookupDungeonMapStateNoLock(dungeon_map_id)) {
    width = static_cast<double>(dungeon_map->loc_left - dungeon_map->loc_right);
    height = static_cast<double>(dungeon_map->loc_top - dungeon_map->loc_bottom);
  } else if (current_zone_token_ == -1) {
    const auto continent_index = FindContinentIndexByMapIdNoLock(wma_it->second.map_id);
    if (continent_index < 0) {
      return result;
    }

    const auto &continent = continents_[static_cast<std::size_t>(continent_index)];
    if (!continent.has_overview_bounds) {
      return result;
    }
    width = static_cast<double>(continent.right_boundary) -
            static_cast<double>(continent.left_boundary) + 1.0;
    height = static_cast<double>(continent.bottom_boundary) -
             static_cast<double>(continent.top_boundary) + 1.0;
  } else {
    width = static_cast<double>(wma_it->second.loc_left - wma_it->second.loc_right);
    height = static_cast<double>(wma_it->second.loc_top - wma_it->second.loc_bottom);
  }

  if (width == 0.0 || height == 0.0) {
    return result;
  }

  result.raw_metric = static_cast<std::int32_t>(height * 1002.0 / width);
  result.padded_metric = ComputeLegacyMapInfoPaddedMetric(result.raw_metric);
  return result;
}

WorldMapSystem::MapHighlightResult WorldMapSystem::ResolveMapHighlight(float norm_x,
                                                                       float norm_y) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ResolveMapHighlightNoLock(norm_x, norm_y);
}

WorldMapSystem::MapHighlightResult WorldMapSystem::ResolveMapHighlightNoLock(float norm_x,
                                                                             float norm_y) const {
  MapHighlightResult result;

  if (current_zone_token_ >= 0) {
    for (const auto &overlay : overlays_) {
      const float left =
          static_cast<float>(overlay.hit_rect_left) * kLegacyOverlayHitRectXScale;
      const float right =
          static_cast<float>(overlay.hit_rect_right) * kLegacyOverlayHitRectXScale;
      const float top =
          static_cast<float>(overlay.hit_rect_top) * kLegacyOverlayHitRectYScale;
      const float bottom =
          static_cast<float>(overlay.hit_rect_bottom) * kLegacyOverlayHitRectYScale;
      if (!LegacyFloatIsNonZero(right - left) || !LegacyFloatIsNonZero(bottom - top)) {
        continue;
      }
      if (!RectContainsPointInclusive(left, top, right, bottom, norm_x, norm_y)) {
        continue;
      }

      result.display_name = LookupAreaNameNoLock(overlay.reveal_area_ids[0]);
      if (!result.display_name.has_value()) {
        continue;
      }

      result.highlighted = true;
      return result;
    }
  }

  const auto highlighted_wma_id = HitTestMapZoneNoLock(norm_x, norm_y);
  if (highlighted_wma_id <= 0) {
    return result;
  }

  const auto bounds_it = wma_bounds_.find(static_cast<std::uint32_t>(highlighted_wma_id));
  if (bounds_it == wma_bounds_.end()) {
    return result;
  }

  const auto &bounds = bounds_it->second;
  if (current_zone_token_ >= 0) {
    result.display_name = LookupAreaNameNoLock(bounds.area_id);
    result.highlighted = result.display_name.has_value();
    return result;
  }

  if (!LegacyFloatIsNonZero(bounds.loc_left) && !LegacyFloatIsNonZero(bounds.loc_right) &&
      !LegacyFloatIsNonZero(bounds.loc_top) && !LegacyFloatIsNonZero(bounds.loc_bottom)) {
    return result;
  }

  if (bounds.area_id != 0) {
    result.display_name = LookupAreaNameNoLock(bounds.area_id);
  } else {
    result.display_name = LookupMapNameNoLock(bounds.map_id);
  }
  if (!bounds.name.empty()) {
    result.file_name = bounds.name;
  }

  double width_units = 64.0;
  double height_units = 64.0;
  std::int32_t raw_metric = 128;
  int continent_index = -1;

  if (bounds.area_id == 0) {
    continent_index = FindContinentIndexByMapIdNoLock(bounds.map_id);
    if (continent_index >= 0) {
      const auto &continent = continents_[static_cast<std::size_t>(continent_index)];
      width_units = static_cast<double>(continent.right_boundary) -
                    static_cast<double>(continent.left_boundary) + 1.0;
      height_units = static_cast<double>(continent.bottom_boundary) -
                     static_cast<double>(continent.top_boundary) + 1.0;
      if (width_units != 0.0) {
        raw_metric = static_cast<std::int32_t>(512.0 * height_units / width_units);
      }
    }
  } else {
    width_units = static_cast<double>(bounds.loc_left) - static_cast<double>(bounds.loc_right);
    height_units = static_cast<double>(bounds.loc_top) - static_cast<double>(bounds.loc_bottom);
    if (width_units != 0.0) {
      raw_metric = static_cast<std::int32_t>(128.0 * height_units / width_units);
    }
  }

  const auto padded_metric = ComputeLegacyMapInfoPaddedMetric(raw_metric);
  if (bounds.area_id == 0) {
    if (height_units >= width_units) {
      result.map_width_scale = width_units / height_units;
      result.map_height_scale = 1.0;
    } else {
      result.map_width_scale = 1.0;
      result.map_height_scale = height_units / width_units;
    }

    if (continent_index >= 0) {
      const auto &continent = continents_[static_cast<std::size_t>(continent_index)];
      const double scale = static_cast<double>(continent.scale);
      result.texture_width_scale = scale * width_units * kLegacyOverviewTextureXScale;
      result.texture_height_scale = scale * height_units * kLegacyOverviewTextureYScale;
      result.texture_offset_x =
          (static_cast<double>(continent.left_boundary) * scale + 31.3125 - scale * 32.0 +
           static_cast<double>(continent.continent_offset_x)) *
          kLegacyOverviewTextureXScale;
      result.texture_offset_y =
          (20.875 - scale * 32.0 +
           static_cast<double>(continent.top_boundary) * scale +
           static_cast<double>(continent.continent_offset_y)) *
          kLegacyOverviewTextureYScale;
    }
  } else {
    result.map_width_scale = 1.0;
    if (padded_metric != 0) {
      result.map_height_scale =
          static_cast<double>(raw_metric) / static_cast<double>(padded_metric);
    }

    if (current_continent_token_ >= 0 &&
        static_cast<std::size_t>(current_continent_token_) < continents_.size()) {
      const auto &continent = continents_[static_cast<std::size_t>(current_continent_token_)];
      const auto transformed_top_left =
          ApplyMapTransformNoLock(bounds.map_id, bounds.loc_top, bounds.loc_left);
      const auto transformed_bottom_right =
          ApplyMapTransformNoLock(bounds.map_id, bounds.loc_bottom, bounds.loc_right);

      const double parent_width = static_cast<double>(continent.right_boundary) -
                                  static_cast<double>(continent.left_boundary);
      const double parent_height = static_cast<double>(continent.bottom_boundary) -
                                   static_cast<double>(continent.top_boundary);
      const double child_width =
          static_cast<double>(transformed_top_left.y) -
          static_cast<double>(transformed_bottom_right.y);
      const double child_height =
          static_cast<double>(transformed_top_left.x) -
          static_cast<double>(transformed_bottom_right.x);

      if (parent_width != 0.0 && parent_height != 0.0) {
        result.texture_width_scale = child_width / parent_width;
        result.texture_height_scale = child_height / parent_height;
        result.texture_offset_x =
            (static_cast<double>(continent.right_boundary) -
             static_cast<double>(transformed_top_left.y)) /
            parent_width;
        result.texture_offset_y =
            (static_cast<double>(continent.bottom_boundary) -
             static_cast<double>(transformed_top_left.x)) /
            parent_height;
      }
    }
  }

  result.highlighted = true;
  return result;
}

void WorldMapSystem::BuildRuntimeData() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (dbc_loader_ != nullptr) {
    BuildRuntimeDataFromDbcNoLock(*dbc_loader_);
    return;
  }

  current_world_map_area_token_ = -1;
}

void WorldMapSystem::TeardownRuntimeData() {
  std::lock_guard<std::mutex> lock(mutex_);
  continents_.clear();
  landmarks_.clear();
  landmark_sources_.clear();
  wma_bounds_.clear();
  area_to_wma_.clear();
  area_to_wmas_.clear();
  dungeon_maps_.clear();
  dungeon_maps_in_order_.clear();
  orphan_world_map_area_ids_.clear();
  world_map_transforms_.clear();
  dungeon_map_chunks_.clear();
  zones_.clear();
  pois_.clear();
  quest_pois_.clear();
  overlay_sources_.clear();
  overlays_.clear();
  explored_areas_.clear();
  current_area_id_ = 0;
  current_continent_token_ = -1;
  current_zone_token_ = -1;
  current_world_map_area_token_ = -1;
  current_dungeon_map_id_ = -1;
  dbc_loader_ = nullptr;
  dbc_initialized_ = false;
}

}

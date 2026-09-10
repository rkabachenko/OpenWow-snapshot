
#include "openwow/ui/game/minimap_integration.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/group_system.h"
#include "openwow/game/minimap_system.h"
#include "openwow/game/minimap_terrain.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/party_stats.h"
#include "openwow/game/tracking_system.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/game/world_session.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/framescript/widgets/minimap_texture_uv.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/world/coordinates/world_geometry.h"
#include "openwow/world/streaming/world_map.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <unordered_set>

namespace openwow::ui::game {

using namespace openwow::game;

namespace {

constexpr std::uint32_t kMaxRenderedPoiIconId = 0xC3;

constexpr float kMinimapBlipInRangeRadiusFraction = 0.8f;
constexpr float kMaximumPoiArrowDistance = 694.44446f;
constexpr std::uint32_t kVisibleObjectRefreshIntervalMs = 999;
constexpr std::uint32_t kNpcFlagFlightMaster = 0x00002000u;

struct VisibleMinimapChunkVertex {
  float world_x = 0.0f;
  float world_y = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
};

struct VisibleMinimapChunkQuad {
  std::array<VisibleMinimapChunkVertex, 4> vertices{};
};

std::optional<VisibleMinimapChunkQuad> BuildVisibleMinimapChunkRenderQuad(
    const openwow::game::MinimapChunkWorldBounds& chunk_bounds,
    const float center_x, const float center_y, const float visible_radius,
    const std::uint32_t texture_width, const std::uint32_t texture_height) {
  if (texture_width == 0 || texture_height == 0) {
    return std::nullopt;
  }

  const float view_min_x = center_x - visible_radius;
  const float view_max_x = center_x + visible_radius;
  const float view_min_y = center_y - visible_radius;
  const float view_max_y = center_y + visible_radius;

  const float clipped_min_x = std::max(view_min_x, chunk_bounds.min_x);
  const float clipped_max_x = std::min(view_max_x, chunk_bounds.max_x);
  const float clipped_min_y = std::max(view_min_y, chunk_bounds.min_y);
  const float clipped_max_y = std::min(view_max_y, chunk_bounds.max_y);
  if (clipped_min_x >= clipped_max_x || clipped_min_y >= clipped_max_y) {
    return std::nullopt;
  }

  VisibleMinimapChunkQuad quad;
  quad.vertices = {{
      {clipped_max_x, clipped_max_y},
      {clipped_max_x, clipped_min_y},
      {clipped_min_x, clipped_min_y},
      {clipped_min_x, clipped_max_y},
  }};

  for (auto& vertex : quad.vertices) {
    vertex.u = (chunk_bounds.max_y - vertex.world_y) /
               openwow::game::kMinimapChunkTileSize;
    vertex.v = (chunk_bounds.max_x - vertex.world_x) /
               openwow::game::kMinimapChunkTileSize;
    vertex.u = ContractNormalizedTextureCoordForHalfTexel(vertex.u,
                                                          texture_width);
    vertex.v = ContractNormalizedTextureCoordForHalfTexel(vertex.v,
                                                          texture_height);
  }

  return quad;
}

void AppendPointOfInterestIcons(Minimap& minimap,
                                const openwow::ui::MinimapSystem& minimap_state,
                                const float player_x, const float player_y,
                                const float visible_radius) {
  const auto poi_texture_path = minimap_state.GetPoiIconTexturePath();
  const float visible_distance =
      visible_radius * kMinimapBlipInRangeRadiusFraction;
  const float visible_distance_sq = visible_distance * visible_distance;
  for (const auto& pin : minimap_state.GetPinsSnapshot()) {
    if (!pin.is_poi || pin.icon_id > kMaxRenderedPoiIconId) {
      continue;
    }
    const float dx = pin.x - player_x;
    const float dy = pin.y - player_y;
    if ((dx * dx) + (dy * dy) > visible_distance_sq) {
      continue;
    }

    MinimapIcon icon;
    icon.world_x = pin.x;
    icon.world_y = pin.y;
    icon.color = 0xFFFFFFFFu;
    icon.type = MinimapIconType::kQuest;
    icon.quad_span_pixels =
        openwow::ui::kMinimapBlipQuadSpanUiUnits * minimap.ui_unit_scale();
    icon.texture_kind = MinimapIconTextureKind::kPoiAtlas;
    icon.atlas_icon_index = pin.icon_id;
    icon.texture_path = poi_texture_path;
    icon.tooltip = pin.tooltip;
    minimap.AddIcon(icon);
  }
}

void AppendGuidePointOfInterestArrow(Minimap& minimap,
                                     const openwow::ui::MinimapSystem& minimap_state,
                                     const float player_x, const float player_y,
                                     const float visible_radius) {
  const auto guide_poi = minimap_state.GetGuidePointOfInterest();
  if (!guide_poi.has_value()) {
    return;
  }

  const float dx = guide_poi->x - player_x;
  const float dy = guide_poi->y - player_y;
  const float visible_distance = visible_radius * kMinimapBlipInRangeRadiusFraction;
  const float distance_sq = (dx * dx) + (dy * dy);
  if (distance_sq <= visible_distance * visible_distance) {
    return;
  }

  if (distance_sq > kMaximumPoiArrowDistance * kMaximumPoiArrowDistance) {
    return;
  }

  minimap.AddRotatingArrow({
      .angle_radians = std::atan2(dx, -dy),
      .kind = MinimapRotatingArrowKind::kGuidePoi,
      .visible_in_indoor_minimap = true,
      .tooltip = guide_poi->title,
  });
}

}

MinimapIntegration::MinimapIntegration(
    openwow::render::TextureManager& texture_manager,
    openwow::ui::MinimapSystem& minimap_state,
    openwow::game::MinimapSystem& minimap_content)
    : texture_manager_(texture_manager),
      minimap_state_(minimap_state),
      minimap_content_(minimap_content),
      minimap_(texture_manager, minimap_state, minimap_content) {}
MinimapIntegration::~MinimapIntegration() { Shutdown(); }

void MinimapIntegration::SetFileLoader(FileLoader loader) {
  file_loader_ = std::move(loader);
  if (!initialized_) {
    return;
  }
  minimap_.InvalidateTextureLeases();
  terrain_chunk_leases_.fill({});
  terrain_chunk_lease_paths_.fill({});
  wmo_tile_leases_.clear();
  ClearPublishedWmoTiles();
  last_wmo_diagnostic_.clear();
  terrain_translations_loaded_ = false;
  LoadTerrainTranslations();
}

void MinimapIntegration::LoadTerrainTranslations() {

  terrain_translations_loaded_ = false;
  openwow::game::Minimap_ResetTerrainTextureTranslations();
  if (file_loader_) {
    try {
      const auto bytes = file_loader_("Textures\\Minimap\\md5translate.trs");
      if (!bytes.empty()) {
        terrain_translations_loaded_ =
            openwow::game::Minimap_LoadTerrainTextureTranslationsFromText(
                std::string_view(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size()));
      }
    } catch (const std::exception& exception) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "MinimapIntegration: md5translate loader failed: " +
              std::string(exception.what()));
    } catch (...) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "MinimapIntegration: md5translate loader failed");
    }
  }
  if (!terrain_translations_loaded_) {
    terrain_translations_loaded_ =
        openwow::game::Minimap_LoadTerrainTextureTranslations();
  }
}

bool MinimapIntegration::Initialize(float screen_w) {
  if (initialized_) return true;

  const float center_x = screen_w - kDefaultRadius - kMargin;
  const float center_y = kDefaultRadius + kMargin;

  if (!minimap_.Initialize(center_x, center_y, kDefaultRadius)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "MinimapIntegration: Minimap initialization failed");
    return false;
  }

  LoadTerrainTranslations();
  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "MinimapIntegration: initialized");
  return true;
}

void MinimapIntegration::Shutdown() {
  if (!initialized_) return;
  minimap_.Shutdown();
  initialized_ = false;
  current_map_id_ = 0;
  current_map_name_.clear();
  terrain_chunk_window_map_name_.clear();
  terrain_chunk_window_.fill({});
  terrain_chunk_leases_.fill({});
  terrain_chunk_lease_paths_.fill({});
  wmo_tile_leases_.clear();
  ClearPublishedWmoTiles();
  last_wmo_diagnostic_.clear();
  visible_object_candidates_.clear();
  marker_labels_.clear();
  minimap_state_.ClearMarkerPresentation();
  next_visible_object_refresh_tick_ = 0;
  terrain_translations_loaded_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "MinimapIntegration: shutdown");
}

void MinimapIntegration::ReleaseRendererDeviceResources() {
  if (initialized_) {
    minimap_.ReleaseRendererDeviceResources();
  }
}

bool MinimapIntegration::RestoreRendererDeviceResources() {
  return !initialized_ || minimap_.RestoreRendererDeviceResources();
}

void MinimapIntegration::Update(float player_x, float player_y,
                                float player_z, float facing,
                                WorldSession& session,
                                const ObjectManager* obj_mgr,
                                ObjectGuid local_guid) {
  if (!initialized_) return;

  minimap_.Update(player_x, player_y, player_z, facing);
  const bool rotate_minimap =
      CVarSystem::Instance().GetCVarBool("rotateMinimap");
  minimap_.SetRotating(rotate_minimap);

  auto& minimap_state = minimap_state_;
  const bool has_wmo_minimap_source =
      world_map_ != nullptr &&
      world_map_->UsesWmoMinimapSourceAtPosition(player_x, player_y, player_z);
  minimap_state.SetIndoorMinimapActive(has_wmo_minimap_source);
  const float visible_radius = minimap_state.GetVisibleRadius();
  minimap_state.SetPlayerPosition(player_x, player_y, facing);
  minimap_state.SetMode(rotate_minimap ? openwow::ui::MinimapSystem::Mode::Rotate
                                       : openwow::ui::MinimapSystem::Mode::Fixed);
  minimap_.SetVisibleRadius(visible_radius);
  minimap_.SetIndoorMinimapActive(minimap_state.IsIndoorMinimapActive());
  minimap_.SetMaskTexturePath(minimap_state.GetMaskTexturePath());
  minimap_.SetRotatingArrowTexturePath(
      openwow::ui::MinimapRotatingArrowKind::kStaticPoi,
      minimap_state.GetRotatingArrowTexturePath(
          openwow::ui::MinimapRotatingArrowKind::kStaticPoi));
  minimap_.SetRotatingArrowTexturePath(
      openwow::ui::MinimapRotatingArrowKind::kGuidePoi,
      minimap_state.GetRotatingArrowTexturePath(
          openwow::ui::MinimapRotatingArrowKind::kGuidePoi));
  minimap_.SetRotatingArrowTexturePath(
      openwow::ui::MinimapRotatingArrowKind::kCorpsePoi,
      minimap_state.GetRotatingArrowTexturePath(
          openwow::ui::MinimapRotatingArrowKind::kCorpsePoi));
  minimap_.SetRotatingArrowTexturePath(
      openwow::ui::MinimapRotatingArrowKind::kGroupMember,
      minimap_state.GetRotatingArrowTexturePath(
          openwow::ui::MinimapRotatingArrowKind::kGroupMember));
  minimap_.SetPlayerArrowTexturePath(minimap_state.GetPlayerArrowTexturePath());
  minimap_.SetPlayerArrowSize(minimap_state.GetPlayerArrowWidth(),
                              minimap_state.GetPlayerArrowHeight());
  minimap_.ClearRotatingArrows();
  for (const auto& arrow : minimap_state.GetRotatingArrows()) {
    minimap_.AddRotatingArrow(arrow);
  }
  AppendGuidePointOfInterestArrow(minimap_, minimap_state, player_x, player_y,
                                  visible_radius);

  (void)minimap_state.ConsumeExplorationOverlayDirty();
  UpdateVisibleTerrainTiles(player_x, player_y, player_z);

  RebuildMinimapContent(session, obj_mgr, local_guid, player_x, player_y, player_z,
                        facing, visible_radius);
}

void MinimapIntegration::Render(std::uint8_t view_id, float screen_w,
                                float screen_h) {
  if (!initialized_) return;
  minimap_.Render(view_id, screen_w, screen_h);
}

void MinimapIntegration::RenderToTexture(const std::uint8_t view_id) {
  if (initialized_) {
    minimap_.RenderToTexture(view_id);
  }
}

MinimapSurfaceSubmitter MinimapIntegration::surface_submitter() const {
  return minimap_.SurfaceSubmitter();
}

void MinimapIntegration::OnMapChanged(std::uint32_t map_id,
                                      const std::string& map_name) {

  (void)openwow::game::Minimap_InitRenderTarget(minimap_state_,
                                                static_cast<int>(map_id));

  current_map_id_ = map_id;
  current_map_name_ = map_name;
  terrain_chunk_window_map_name_.clear();
  terrain_chunk_window_.fill({});
  terrain_chunk_leases_.fill({});
  terrain_chunk_lease_paths_.fill({});
  wmo_tile_leases_.clear();
  ClearPublishedWmoTiles();
  last_wmo_diagnostic_.clear();
  visible_object_candidates_.clear();
  marker_labels_.clear();
  minimap_state_.ClearMarkerPresentation();
  next_visible_object_refresh_tick_ = 0;
}

void MinimapIntegration::SetZoom(float zoom) {
  minimap_state_.SetZoom(zoom);
}

float MinimapIntegration::GetZoom() const {
  return minimap_state_.GetZoom();
}

void MinimapIntegration::OnScreenResize(float screen_w) {
  if (!initialized_) return;
  const float center_x = screen_w - kDefaultRadius - kMargin;
  const float center_y = kDefaultRadius + kMargin;
  minimap_.SetScreenPosition(center_x, center_y, kDefaultRadius);
}

void MinimapIntegration::SetFrameRect(const float x, const float y,
                                      const float width, const float height) {
  if (!initialized_ || width <= 0.0f || height <= 0.0f) {
    return;
  }
  const float radius = std::min(width, height) * 0.5f;
  minimap_.SetScreenPosition(x + width * 0.5f, y + height * 0.5f, radius);
}

void MinimapIntegration::SetUiUnitScale(const float scale) {
  if (initialized_) {
    minimap_.SetUiUnitScale(scale);
  }
}

void MinimapIntegration::PrepareMarkerPresentation() {
  if (!initialized_) {
    minimap_state_.ClearMarkerPresentation();
    return;
  }

  std::vector<openwow::ui::MinimapMarkerPresentation> markers;
  markers.reserve(128);

  const auto append_slot = [&](const MinimapObjectInfoSlot& slot,
                               const float half_extent) {
    const auto label = marker_labels_.find(slot.GetGuid().GetRawValue());
    if (label == marker_labels_.end() || label->second.text.empty()) {
      return;
    }
    markers.push_back({
        .left = slot.GetProjectedX() - half_extent,
        .top = slot.GetProjectedY() - half_extent,
        .right = slot.GetProjectedX() + half_extent,
        .bottom = slot.GetProjectedY() + half_extent,
        .tooltip = label->second.text,
        .transport_layer_mismatch = slot.HasTransportLayerMismatch(),
        .flight_master = label->second.flight_master,
    });
  };
  const auto append_category = [&](const std::uint32_t category,
                                   const bool transport_layer_mismatch) {
    const float half_extent =
        minimap_content_.GetPresentedObjectInfoHalfExtent(category);
    const std::uint32_t count =
        minimap_content_.GetObjectInfoCategoryCount(category);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto* slot =
          minimap_content_.GetObjectInfoCategorySlot(category, index);
      if (slot != nullptr &&
          slot->HasTransportLayerMismatch() == transport_layer_mismatch) {
        append_slot(*slot, half_extent);
      }
    }
  };

  for (std::uint32_t category = 0; category <= 1u; ++category) {
    append_category(category, false);
  }
  for (std::uint32_t category = 16; category <= 22u; ++category) {
    append_category(category, false);
  }
  for (const bool transport_layer_mismatch : {false, true}) {
    for (std::uint32_t category = 2; category <= 15u; ++category) {
      append_category(category, transport_layer_mismatch);
    }
  }

  const auto arrow_layout = minimap_content_.GetDirectionalArrowLayout();
  for (std::uint32_t index = 0;
       index < openwow::game::MinimapSystem::kVehicleIconSlotCount; ++index) {
    const auto* slot = minimap_content_.GetVehicleIconSlot(index);
    if (slot == nullptr || !slot->IsVisible()) {
      continue;
    }
    const auto label = marker_labels_.find(slot->GetGuid().GetRawValue());
    if (label == marker_labels_.end() || label->second.text.empty()) {
      continue;
    }
    markers.push_back({
        .left = slot->GetProjectedX() - arrow_layout.iconHalfExtent,
        .top = slot->GetProjectedY() - arrow_layout.iconHalfExtent,
        .right = slot->GetProjectedX() + arrow_layout.iconHalfExtent,
        .bottom = slot->GetProjectedY() + arrow_layout.iconHalfExtent,
        .tooltip = label->second.text,
        .flight_master = label->second.flight_master,
    });
  }

  minimap_.AppendMarkerPresentation(markers);
  minimap_state_.PublishMarkerPresentation(std::move(markers));
}

bool MinimapIntegration::HandleClick(float screen_x, float screen_y,
                                     float& out_world_x, float& out_world_y) {
  if (!initialized_) return false;
  return minimap_.HandleClick(screen_x, screen_y, out_world_x, out_world_y);
}

void MinimapIntegration::PresentPublishedWmoTiles(
    const std::uint64_t placement_stable_id,
    const std::uint32_t terrain_tint) {
  if (!published_wmo_source_valid_ ||
      published_wmo_placement_stable_id_ != placement_stable_id) {
    return;
  }
  for (const auto& published : published_wmo_tiles_) {
    MinimapBackgroundTile tile;
    tile.texture_path = published.texture_path;
    tile.texture_lease = published.texture_lease;
    tile.color = terrain_tint;
    for (std::size_t index = 0u; index < published.world_vertices.size();
         ++index) {
      minimap_.ProjectWorldToScreen(published.world_vertices[index][0],
                                    published.world_vertices[index][1],
                                    tile.vertices[index].x,
                                    tile.vertices[index].y);
      tile.vertices[index].u = published.texture_coords[index][0];
      tile.vertices[index].v = published.texture_coords[index][1];
    }
    minimap_.AddBackgroundTile(std::move(tile));
  }
}

void MinimapIntegration::ClearPublishedWmoTiles() {
  published_wmo_tiles_.clear();
  published_wmo_placement_stable_id_ = 0u;
  published_wmo_source_valid_ = false;
}

void MinimapIntegration::UpdateVisibleTerrainTiles(const float player_x,
                                                   const float player_y,
                                                   const float player_z) {
  minimap_.ClearBackgroundTiles();
  minimap_.SetWmoMinimapActive(false);
  if (current_map_name_.empty()) {
    ClearPublishedWmoTiles();
    return;
  }

  if (!terrain_translations_loaded_) {
    ClearPublishedWmoTiles();
    return;
  }

  auto& tex_mgr = texture_manager_;
  const std::uint32_t terrain_tint = openwow::game::BuildMinimapTerrainTint(
      world_scene_state_ != nullptr
          ? world_scene_state_->GetPackedFogColorArgb()
          : 0xFF99B3D9u);
  if (world_map_ != nullptr) {
    const openwow::world::WmoMinimapSource wmo_source =
        world_map_->PrepareWmoMinimapSource(
            player_x, player_y, player_z, minimap_state_.GetVisibleRadius());
    if (wmo_source.status !=
        openwow::world::WmoMinimapSourceStatus::kOutdoor) {
      minimap_.SetWmoMinimapActive(true);
      terrain_chunk_window_map_name_.clear();
      terrain_chunk_window_.fill({});
      terrain_chunk_leases_.fill({});
      terrain_chunk_lease_paths_.fill({});

      const auto report_diagnostic = [this](const std::string& message,
                                            const bool warning) {
        if (message == last_wmo_diagnostic_) {
          return;
        }
        last_wmo_diagnostic_ = message;
        openwow::diagnostics::Log(
            warning ? openwow::diagnostics::LogLevel::kWarn
                    : openwow::diagnostics::LogLevel::kInfo,
            "MinimapIntegration: " + message);
      };
      if (wmo_source.status !=
          openwow::world::WmoMinimapSourceStatus::kReady) {
        PresentPublishedWmoTiles(wmo_source.placement_stable_id, terrain_tint);
        report_diagnostic(
            "WMO minimap unavailable root=" + wmo_source.root_path +
                " group=" + std::to_string(wmo_source.active_group_index) +
                " reason=" + wmo_source.detail +
                " retained=" +
                std::to_string(minimap_.background_tile_count()),
            wmo_source.status ==
                openwow::world::WmoMinimapSourceStatus::kFailed);
        return;
      }

      std::unordered_set<std::string> visible_paths;
      std::vector<std::uint32_t> submitted_groups;
      struct PreparedWmoTile {
        const openwow::world::WmoMinimapTileRecord* record{nullptr};
        std::string texture_path;
        std::uint32_t texture_width{0u};
        std::uint32_t texture_height{0u};
      };
      std::vector<PreparedWmoTile> prepared_tiles;
      prepared_tiles.reserve(wmo_source.tiles.size());
      std::size_t unresolved_path_count = 0u;
      std::size_t pending_texture_count = 0u;
      std::string first_unresolved_tile;
      for (const auto& record : wmo_source.tiles) {
        const int tile_key[3] = {
            static_cast<int>(record.group_index),
            static_cast<int>(record.tile_x),
            static_cast<int>(record.tile_y),
        };
        char resolved_path[260]{};
        if (!openwow::game::Minimap_ResolveTerrainTexturePath(
                tile_key, wmo_source.root_path.c_str(), resolved_path,
                std::size(resolved_path))) {
          ++unresolved_path_count;
          if (first_unresolved_tile.empty()) {
            first_unresolved_tile =
                std::to_string(record.group_index) + '/' +
                std::to_string(record.tile_x) + '/' +
                std::to_string(record.tile_y);
          }
          continue;
        }

        const std::string texture_path = resolved_path;
        visible_paths.insert(texture_path);
        auto& texture_lease = wmo_tile_leases_[texture_path];
        if (!texture_lease.valid()) {
          texture_lease = texture_manager_.AcquireTextureAsync(
              texture_path,
              openwow::render::TextureLoadFailurePolicy::kStrict,
              openwow::render::TextureLoadPriority::kDemand);
        }
        if (!texture_lease.valid()) {
          ++pending_texture_count;
          continue;
        }
        const auto [texture_width, texture_height] =
            texture_manager_.GetTextureDimensions(texture_path);
        if (texture_width == 0u || texture_height == 0u) {
          ++pending_texture_count;
          continue;
        }

        prepared_tiles.push_back({
            .record = &record,
            .texture_path = texture_path,
            .texture_width = texture_width,
            .texture_height = texture_height,
        });
      }

      if (pending_texture_count != 0u) {
        PresentPublishedWmoTiles(wmo_source.placement_stable_id, terrain_tint);
        report_diagnostic(
            "WMO minimap textures pending root=" + wmo_source.root_path +
                " group=" + std::to_string(wmo_source.active_group_index) +
                " pending=" + std::to_string(pending_texture_count) +
                " unresolved=" + std::to_string(unresolved_path_count) +
                " records=" + std::to_string(wmo_source.tiles.size()) +
                " retained=" +
                std::to_string(minimap_.background_tile_count()),
            false);
        return;
      }

      std::vector<PublishedWmoTile> next_published_tiles;
      next_published_tiles.reserve(prepared_tiles.size());
      for (const auto& prepared : prepared_tiles) {
        const auto& record = *prepared.record;

        const float min_x = record.local_bounds[0];
        const float min_y = record.local_bounds[1];
        const float z = record.local_bounds[2];
        const float max_x = record.local_bounds[3];
        const float max_y = record.local_bounds[4];
        const std::array<openwow::world::Vec3, 4> local_vertices{{
            {min_x, min_y, z},
            {max_x, min_y, z},
            {max_x, max_y, z},
            {min_x, max_y, z},
        }};
        const std::array<std::array<float, 2>, 4> texture_coords{{
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f},
        }};

        PublishedWmoTile tile;
        tile.texture_path = prepared.texture_path;
        tile.texture_lease = wmo_tile_leases_.at(prepared.texture_path);
        for (std::size_t index = 0u; index < local_vertices.size(); ++index) {
          const openwow::world::Vec3 world_vertex =
              openwow::world::TransformPoint(local_vertices[index],
                                             wmo_source.model_matrix);
          tile.world_vertices[index] = world_vertex;
          tile.texture_coords[index][0] =
              ContractNormalizedTextureCoordForHalfTexel(
                  texture_coords[index][0], prepared.texture_width);
          tile.texture_coords[index][1] =
              ContractNormalizedTextureCoordForHalfTexel(
                  texture_coords[index][1], prepared.texture_height);
        }
        next_published_tiles.push_back(std::move(tile));
        if (std::find(submitted_groups.begin(), submitted_groups.end(),
                      record.group_index) == submitted_groups.end()) {
          submitted_groups.push_back(record.group_index);
        }
      }

      published_wmo_tiles_ = std::move(next_published_tiles);
      published_wmo_placement_stable_id_ = wmo_source.placement_stable_id;
      published_wmo_source_valid_ = true;
      for (auto lease = wmo_tile_leases_.begin();
           lease != wmo_tile_leases_.end();) {
        if (!visible_paths.contains(lease->first)) {
          lease = wmo_tile_leases_.erase(lease);
        } else {
          ++lease;
        }
      }
      PresentPublishedWmoTiles(wmo_source.placement_stable_id, terrain_tint);

      std::string group_summary;
      constexpr std::size_t kDiagnosticGroupLimit = 16u;
      for (std::size_t index = 0u;
           index < std::min(submitted_groups.size(), kDiagnosticGroupLimit);
           ++index) {
        if (!group_summary.empty()) {
          group_summary += ',';
        }
        group_summary += std::to_string(submitted_groups[index]);
      }
      if (submitted_groups.size() > kDiagnosticGroupLimit) {
        group_summary += ",...";
      }
      report_diagnostic(
          "WMO minimap ready root=" + wmo_source.root_path +
              " group=" + std::to_string(wmo_source.active_group_index) +
              " radius=" + std::to_string(minimap_state_.GetVisibleRadius()) +
              " tile_groups=[" + group_summary + "]" +
              " records=" + std::to_string(wmo_source.tiles.size()) +
              " mapped=" + std::to_string(visible_paths.size()) +
              " submitted=" + std::to_string(prepared_tiles.size()) +
              " unresolved=" + std::to_string(unresolved_path_count) +
              (first_unresolved_tile.empty()
                   ? std::string()
                   : " first_unresolved=" + first_unresolved_tile),
          unresolved_path_count != 0u);
      return;
    }
  }

  wmo_tile_leases_.clear();
  ClearPublishedWmoTiles();
  last_wmo_diagnostic_.clear();
  const bool continent_changed =
      terrain_chunk_window_map_name_ != current_map_name_;
  terrain_chunk_window_map_name_ = current_map_name_;
  const auto origin =
      openwow::game::Minimap_ComputeChunkWindowOrigin(player_x, player_y);
  openwow::game::Minimap_UpdateChunkTextureWindow(
      origin, continent_changed, current_map_name_.c_str(),
      terrain_chunk_window_);

  const float visible_radius =
      minimap_state_.GetVisibleRadius();
  for (std::size_t slot_index = 0;
       slot_index < terrain_chunk_window_.size(); ++slot_index) {
    const auto& slot = terrain_chunk_window_[slot_index];
    if (slot.texture_path.empty()) {
      terrain_chunk_leases_[slot_index] = {};
      terrain_chunk_lease_paths_[slot_index].clear();
      continue;
    }

    if (terrain_chunk_lease_paths_[slot_index] != slot.texture_path) {
      terrain_chunk_leases_[slot_index] = {};
      terrain_chunk_lease_paths_[slot_index] = slot.texture_path;
    }
    if (!terrain_chunk_leases_[slot_index].valid()) {
      terrain_chunk_leases_[slot_index] = tex_mgr.AcquireTextureAsync(
          slot.texture_path,
          openwow::render::TextureLoadFailurePolicy::kStrict,
          openwow::render::TextureLoadPriority::kDemand);
    }
    if (!terrain_chunk_leases_[slot_index].valid()) {
      continue;
    }

    const auto [texture_width, texture_height] =
        tex_mgr.GetTextureDimensions(slot.texture_path);
    const auto clipped_quad = BuildVisibleMinimapChunkRenderQuad(
        openwow::game::Minimap_ChunkCoordsToWorldBounds(slot.row, slot.column),
        player_x, player_y, visible_radius, texture_width, texture_height);
    if (!clipped_quad) {
      continue;
    }

    MinimapBackgroundTile tile;
    tile.texture_path = slot.texture_path;
    tile.texture_lease = terrain_chunk_leases_[slot_index];
    tile.color = terrain_tint;
    for (std::size_t i = 0; i < clipped_quad->vertices.size(); ++i) {
      float screen_x = 0.0f;
      float screen_y = 0.0f;
      const auto& vertex = clipped_quad->vertices[i];
      minimap_.ProjectWorldToScreen(vertex.world_x, vertex.world_y, screen_x,
                                    screen_y);
      tile.vertices[i].x = screen_x;
      tile.vertices[i].y = screen_y;
      tile.vertices[i].u = vertex.u;
      tile.vertices[i].v = vertex.v;
    }

    minimap_.AddBackgroundTile(std::move(tile));
  }
}

void MinimapIntegration::RefreshVisibleObjectCandidates(
    const ObjectManager& obj_mgr, const ObjectGuid local_guid,
    const float player_x, const float player_y, const float visible_radius) {
  visible_object_candidates_.clear();
  const float candidate_radius = visible_radius * kMinimapBlipInRangeRadiusFraction;
  const float candidate_radius_sq = candidate_radius * candidate_radius;

  obj_mgr.ForEach([&](const WorldObject& object) {
    if (object.GetGuid() == local_guid ||
        (!object.IsUnit() && !object.IsGameObject())) {
      return;
    }
    const float dx = object.GetX() - player_x;
    const float dy = object.GetY() - player_y;
    if ((dx * dx) + (dy * dy) <= candidate_radius_sq) {
      visible_object_candidates_.push_back(object.GetGuid());
    }
  });
}

void MinimapIntegration::RebuildMinimapContent(
    WorldSession& session, const ObjectManager* obj_mgr,
    const ObjectGuid local_guid,
    const float player_x, const float player_y, const float player_z,
    const float facing, const float visible_radius) {
  (void)player_z;
  minimap_.ClearIcons();
  marker_labels_.clear();

  const auto& minimap_state = minimap_state_;
  openwow::game::Minimap_TickPOIDirections(
      minimap_state, static_cast<std::int32_t>(session.objects().GetMapId()),
      player_x, player_y);
  AppendPointOfInterestIcons(minimap_, minimap_state, player_x, player_y,
                             visible_radius);

  auto& content = minimap_content_;
  content.ClearObjectInfoCategories();
  content.ClearDirectionalIconSlots();
  content.SetPlayerPosition(player_x, player_y, facing);
  content.SetRotating(minimap_.IsRotating());

  float minimap_center_x = 0.0f;
  float minimap_center_y = 0.0f;
  minimap_.ProjectWorldToScreenUnrotated(player_x, player_y, minimap_center_x,
                                         minimap_center_y);

  const float ui_unit_scale = minimap_.ui_unit_scale();
  content.SetDirectionalArrowLayout(
      minimap_center_x, minimap_center_y,
      openwow::ui::kMinimapRotatingArrowRingRadiusUiUnits * ui_unit_scale,
      openwow::ui::kMinimapRotatingArrowQuadSpanUiUnits * 0.5f * ui_unit_scale);
  content.SetBlipHalfExtents(
      openwow::ui::kMinimapBlipQuadSpanUiUnits * 0.5f * ui_unit_scale,
      openwow::ui::kMinimapVehicleStateBlipQuadSpanUiUnits * 0.5f *
          ui_unit_scale);

  if (obj_mgr == nullptr || visible_radius <= 0.0f) {
    content.RenderMinimapContent();
    RebuildMarkerLabels(session, obj_mgr);
    return;
  }
  const auto* player = obj_mgr->GetActivePlayer();
  if (player == nullptr) {
    content.RenderMinimapContent();
    RebuildMarkerLabels(session, obj_mgr);
    return;
  }

  const auto to_transport_key = [](const ObjectGuid guid) {
    const std::uint64_t raw = guid.GetRawValue();
    return MinimapTransportKey{
        static_cast<std::uint32_t>(raw),
        static_cast<std::uint32_t>(raw >> 32),
    };
  };
  const auto project_unrotated = [&](const float world_x, const float world_y,
                                     float& minimap_x, float& minimap_y) {
    minimap_.ProjectWorldToScreenUnrotated(world_x, world_y, minimap_x,
                                           minimap_y);
  };
  const auto pvp_faction_selector = [](const std::uint8_t race) {
    switch (race) {
      case 1:
      case 3:
      case 4:
      case 7:
      case 11:
        return 0;
      case 2:
      case 5:
      case 6:
      case 8:
      case 10:
        return 1;
      default:
        return -1;
    }
  };

  MinimapVisibleObjectContext context;
  context.activePlayerGuid = local_guid;
  context.centerX = player_x;
  context.centerY = player_y;
  context.visibleRange = visible_radius;
  context.viewerTransport = to_transport_key(player->GetTransportGUID());
  context.showTrivialQuestMarkers =
      TrackingSystem::Get().IsTrivialQuestTrackingActive();
  if (const auto tracked =
          TrackingSystem::Get().GetActiveLuaTrackingSelection();
      tracked.has_value() && tracked->type >= 1 && tracked->type <= 3) {
    context.trackedInfo = MinimapTrackedInfoSelection{
        static_cast<MinimapTrackedInfoType>(tracked->type), tracked->value};
  }

  const std::uint32_t now_tick = core::GameClock::GetTickCount32();
  if (next_visible_object_refresh_tick_ == 0 ||
      static_cast<std::int32_t>(now_tick - next_visible_object_refresh_tick_) >=
          0) {
    RefreshVisibleObjectCandidates(*obj_mgr, local_guid, player_x, player_y,
                                   visible_radius);
    next_visible_object_refresh_tick_ =
        now_tick + kVisibleObjectRefreshIntervalMs;
  }

  const bool viewer_is_dead_or_ghost = player->State().IsDeadOrGhost();
  const std::uint32_t viewer_track_creature_mask =
      player->State().GetActivePlayerTrackCreatureMask();
  const bool viewer_sees_creeps =
      (player->GetPlayerFlags() & kMinimapCreepViewPlayerFlag) != 0u;
  const int viewer_pvp_rank_faction_selector =
      pvp_faction_selector(player->State().GetRace());

  auto& battlefield = BattlefieldInfo::Get();
  const auto is_battlefield_vehicle = [&](const ObjectGuid guid) {
    for (std::uint32_t i = 0; i < battlefield.GetBattlefieldVehicleCount();
         ++i) {
      if (battlefield.GetBattlefieldVehicleGuid(i) == guid) {
        return true;
      }
    }
    return false;
  };

  for (const ObjectGuid guid : visible_object_candidates_) {
    const auto* object = obj_mgr->Get(guid);
    if (object == nullptr) {
      continue;
    }

    MinimapVisibleObjectSnapshot snapshot;
    snapshot.guid = guid;
    snapshot.worldX = object->GetX();
    snapshot.worldY = object->GetY();
    project_unrotated(snapshot.worldX, snapshot.worldY, snapshot.minimapX,
                      snapshot.minimapY);
    snapshot.transport = to_transport_key(object->GetTransportGUID());
    snapshot.questMarkerState =
        static_cast<std::uint32_t>(object->GetOverlayDisplayType());

    if (object->IsUnit()) {
      const auto& unit = static_cast<const CGUnit_C&>(*object);
      if (is_battlefield_vehicle(guid) ||
          battlefield.IsControllerRepresentedByBattlefieldVehicle(*obj_mgr,
                                                                   guid)) {
        continue;
      }

      snapshot.objectType = object->IsPlayer()
                                ? MinimapVisibleObjectType::kPlayer
                                : MinimapVisibleObjectType::kUnit;
      snapshot.controllingGuid = unit.Interaction().GetControllingPlayerGuid();

      snapshot.isActivePlayerOrPartyOrRaidMember =
          unit.Interaction().ResolveGroupRelation(*player).same_raid;
      snapshot.passesInteractionGate =
          unit.Interaction().IsSelectableOrOwnedPet(session);
      snapshot.npcTrackingFlags = unit.State().GetNpcFlags();

      UnitInteractionRuntime::ReactionMemo reaction;
      snapshot.reactionLevel = static_cast<std::int32_t>(
          player->Interaction().GetReaction(unit, reaction));
      snapshot.passesTrackedInfoTargetFilter =
          snapshot.reactionLevel >=
          static_cast<std::int32_t>(ReactionType::kNeutral);

      snapshot.minimapTrackInfo = MinimapTrackInfo{
          viewer_is_dead_or_ghost,
          unit.State().GetVisFlags(),
          unit.State().GetDynamicFlags(),
          static_cast<std::uint32_t>(unit.State().GetCreatureType()),
          viewer_track_creature_mask,
          viewer_sees_creeps,
      };
      snapshot.isPlayerControlledTarget = unit.Interaction().IsPlayerControlled();
      snapshot.canAttackSpellTarget =
          player->Interaction().CanAttackSpellTarget(unit, reaction);

      const auto* controlling_player =
          snapshot.controllingGuid == guid
              ? static_cast<const CGPlayer_C*>(object)
              : obj_mgr->GetPlayer(snapshot.controllingGuid);
      snapshot.pvpRankFactionSelector = pvp_faction_selector(
          controlling_player != nullptr ? controlling_player->State().GetRace()
                                        : unit.State().GetRace());
      snapshot.activePlayerPvpRankFactionSelector =
          viewer_pvp_rank_faction_selector;
    } else if (object->IsGameObject()) {
      const auto& game_object = static_cast<const CGGameObject_C&>(*object);
      snapshot.objectType = MinimapVisibleObjectType::kGameObject;
      snapshot.gameObjectTypeId =
          static_cast<std::uint32_t>(game_object.GetGoType());
      snapshot.passesTrackedInfoTargetFilter =
          player->Interaction().IsNeutralGameObjectTarget(game_object);
      snapshot.passesGameObjectMinimapFilter =
          game_object.GetLootArtVisualControlState().tracked_resource_match;
    } else {
      continue;
    }

    (void)content.AppendVisibleObjectBlip(context, snapshot);
  }

  const float in_range = visible_radius * kMinimapBlipInRangeRadiusFraction;
  const float in_range_sq = in_range * in_range;
  const bool raid = GroupSystem::Get().IsInRaid();
  for (const auto& member : GroupSystem::Get().GetMembers()) {
    if (member.guid.IsEmpty() || member.guid == local_guid) {
      continue;
    }

    float member_x = 0.0f;
    float member_y = 0.0f;
    std::uint8_t class_id = member.classId;
    bool online = member.online;
    if (const auto* live = obj_mgr->GetUnit(member.guid); live != nullptr) {
      member_x = live->GetX();
      member_y = live->GetY();
      class_id = live->State().GetClass();
      online = true;
    } else {
      const auto cached =
          session.party_stats().GetCachedMember(member.guid.GetRawValue());
      if (!cached.has_value() ||
          (cached->available_mask & GroupUpdateFlag::kPosition) == 0) {
        continue;
      }
      member_x = static_cast<float>(cached->stats.pos_x);
      member_y = static_cast<float>(cached->stats.pos_y);
      if ((cached->available_mask & GroupUpdateFlag::kStatus) != 0) {
        online = (cached->stats.status & GroupMemberStatus::kOnline) != 0;
      }
    }
    if (!online) {
      continue;
    }

    const float dx = member_x - player_x;
    const float dy = member_y - player_y;
    if ((dx * dx) + (dy * dy) <= in_range_sq) {
      auto* slot = content.AllocateObjectInfoSlot(raid ? 0u : 1u);
      if (slot != nullptr) {
        float minimap_x = 0.0f;
        float minimap_y = 0.0f;
        project_unrotated(member_x, member_y, minimap_x, minimap_y);
        slot->SetGuid(member.guid);
        slot->SetProjectedCoords(minimap_x, minimap_y);
        slot->SetTransportLayerMismatch(false);
        slot->SetBlipTextureCellPlusOne(
            std::clamp<std::uint32_t>(class_id, 1u, 16u));
      }
    } else {

      minimap_.AddRotatingArrow({
          .angle_radians = std::atan2(dx, -dy),
          .kind = MinimapRotatingArrowKind::kGroupMember,
          .visible_in_indoor_minimap = true,
          .tooltip = member.name,
      });
    }
  }

  std::uint32_t vehicle_arrow_index = 0;
  for (std::uint32_t i = 0; i < battlefield.GetBattlefieldVehicleCount();
       ++i) {
    const auto vehicle_guid = battlefield.GetBattlefieldVehicleGuid(i);
    const auto* vehicle = obj_mgr->GetUnit(vehicle_guid);
    if (vehicle == nullptr) {
      continue;
    }
    const float dx = vehicle->GetX() - player_x;
    const float dy = vehicle->GetY() - player_y;
    if ((dx * dx) + (dy * dy) <= in_range_sq) {
      const bool occupied =
          !vehicle->Interaction().GetControllingPlayerGuid().IsEmpty();
      const std::uint32_t category =
          16u + (vehicle->Movement().IsFlying() ? 2u : 0u) +
          (occupied ? 1u : 0u);
      if (auto* slot = content.AllocateObjectInfoSlot(category);
          slot != nullptr) {
        float minimap_x = 0.0f;
        float minimap_y = 0.0f;
        project_unrotated(vehicle->GetX(), vehicle->GetY(), minimap_x,
                          minimap_y);
        slot->SetGuid(vehicle_guid);
        slot->SetProjectedCoords(minimap_x, minimap_y);
        slot->SetTransportLayerMismatch(false);
        slot->SetFacingRadians(vehicle->GetFacing() -
                               (minimap_.IsRotating() ? facing : 0.0f));
      }
    } else if (vehicle_arrow_index <
               openwow::game::MinimapSystem::kVehicleIconSlotCount) {
      if (auto* slot = content.GetVehicleIconSlot(vehicle_arrow_index++);
          slot != nullptr) {
        slot->SetGuid(vehicle_guid);
        slot->SetVisible(true);
        slot->SetFacingRadians(std::atan2(dy, dx));
      }
    }
  }

  for (std::uint32_t poi_index = 0;
       poi_index < openwow::game::Minimap_GetNearestPOICount(); ++poi_index) {
    const auto* const poi =
        openwow::game::Minimap_GetNearestPOIRecord(poi_index);
    if (poi == nullptr) {
      continue;
    }
    const auto kind =
        poi == &openwow::game::Minimap_GetSpecialLandmark(
                    openwow::game::kCorpseSpecialLandmarkIndex)
            ? MinimapRotatingArrowKind::kCorpsePoi
            : MinimapRotatingArrowKind::kStaticPoi;
    const float dx = poi->worldX - player_x;
    const float dy = poi->worldY - player_y;

    minimap_.AddRotatingArrow({
        .angle_radians = std::atan2(dx, -dy),
        .kind = kind,
        .visible_in_indoor_minimap = true,
        .tooltip = poi->name != nullptr ? poi->name : "",
    });
  }

  content.RenderMinimapContent();
  RebuildMarkerLabels(session, obj_mgr);
}

void MinimapIntegration::RebuildMarkerLabels(WorldSession& session,
                                             const ObjectManager* obj_mgr) {
  marker_labels_.clear();

  const auto group_members = GroupSystem::Get().GetMembers();
  const auto resolve_group_name = [&](const ObjectGuid guid) {
    const auto member = std::find_if(
        group_members.begin(), group_members.end(),
        [guid](const auto& candidate) { return candidate.guid == guid; });
    return member != group_members.end() ? member->name : std::string{};
  };

  const auto retain_label = [&](const ObjectGuid guid,
                                std::string preferred_text) {
    if (guid.IsEmpty()) {
      return;
    }

    bool flight_master = false;
    if (obj_mgr != nullptr) {
      if (const auto* object = obj_mgr->Get(guid); object != nullptr) {
        if (preferred_text.empty()) {
          preferred_text = object->GetName();
        }
        if (preferred_text.empty() && object->IsPlayer()) {
          if (const auto* player_name =
                  session.query_cache().GetOrRequestPlayerName(
                      guid.GetRawValue());
              player_name != nullptr) {
            preferred_text = player_name->name;
          }
        }
        if (object->IsUnit()) {
          const auto& unit = static_cast<const CGUnit_C&>(*object);
          flight_master =
              (unit.State().GetNpcFlags() & kNpcFlagFlightMaster) != 0u;
          if (preferred_text.empty()) {
            if (const auto* creature_template =
                    session.query_cache().GetOrRequestCreatureTemplate(
                        object->GetEntry(), guid.GetRawValue());
                creature_template != nullptr) {
              preferred_text = creature_template->name;
            }
          }
        } else if (object->IsGameObject() && preferred_text.empty()) {
          if (const auto* gameobject_template =
                  session.query_cache().GetOrRequestGameObjectTemplate(
                      object->GetEntry(), guid.GetRawValue());
              gameobject_template != nullptr) {
            preferred_text = gameobject_template->name;
          }
        }
      }
    }

    if (!preferred_text.empty()) {
      marker_labels_[guid.GetRawValue()] = {
          .text = std::move(preferred_text),
          .flight_master = flight_master,
      };
    }
  };

  for (std::uint32_t category = 0;
       category < openwow::game::MinimapSystem::kObjectInfoCategoryCount;
       ++category) {
    const std::uint32_t count =
        minimap_content_.GetObjectInfoCategoryCount(category);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto* slot =
          minimap_content_.GetObjectInfoCategorySlot(category, index);
      if (slot == nullptr) {
        continue;
      }
      retain_label(slot->GetGuid(),
                   category <= 1u ? resolve_group_name(slot->GetGuid())
                                  : std::string{});
    }
  }

  for (std::uint32_t index = 0;
       index < openwow::game::MinimapSystem::kVehicleIconSlotCount; ++index) {
    const auto* slot = minimap_content_.GetVehicleIconSlot(index);
    if (slot != nullptr && slot->IsVisible()) {
      retain_label(slot->GetGuid(), {});
    }
  }
}

}

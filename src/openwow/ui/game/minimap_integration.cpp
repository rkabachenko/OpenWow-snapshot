
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
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/game/world_session.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/framescript/widgets/minimap_texture_uv.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>

namespace openwow::ui::game {

using namespace openwow::game;

namespace {

constexpr std::uint32_t kMaxRenderedPoiIconId = 0xC3;

constexpr float kMinimapBlipInRangeRadiusFraction = 0.8f;
constexpr float kMaximumPoiArrowDistance = 694.44446f;
constexpr std::uint32_t kVisibleObjectRefreshIntervalMs = 999;

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
    minimap.AddIcon(icon);
  }
}

void AppendGuidePointOfInterestIcon(Minimap& minimap,
                                    const openwow::ui::MinimapSystem& minimap_state,
                                    const float player_x, const float player_y,
                                    const float visible_radius) {
  const auto guide_poi = minimap_state.GetGuidePointOfInterest();
  if (!guide_poi.has_value() || guide_poi->icon_id > kMaxRenderedPoiIconId) {
    return;
  }

  const float dx = guide_poi->x - player_x;
  const float dy = guide_poi->y - player_y;
  const float visible_distance = visible_radius * kMinimapBlipInRangeRadiusFraction;
  if ((dx * dx) + (dy * dy) > visible_distance * visible_distance) {
    return;
  }

  MinimapIcon icon;
  icon.world_x = guide_poi->x;
  icon.world_y = guide_poi->y;
  icon.color = 0xFFFFFFFFu;
  icon.type = MinimapIconType::kQuest;
  icon.quad_span_pixels =
      openwow::ui::kMinimapBlipQuadSpanUiUnits * minimap.ui_unit_scale();
  icon.texture_kind = MinimapIconTextureKind::kPoiAtlas;
  icon.atlas_icon_index = guide_poi->icon_id;
  icon.texture_path = minimap_state.GetPoiIconTexturePath();
  minimap.AddIcon(icon);
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

  minimap.AddRotatingArrow(
      {std::atan2(dx, -dy), MinimapRotatingArrowKind::kGuidePoi, true});
}

}

MinimapIntegration::MinimapIntegration(
    openwow::render::TextureManager& texture_manager,
    openwow::ui::MinimapSystem& minimap_state,
    openwow::game::MinimapSystem& minimap_content,
    openwow::game::WorldEnvironmentState& world_environment)
    : texture_manager_(texture_manager),
      minimap_state_(minimap_state),
      minimap_content_(minimap_content),
      world_environment_(world_environment),
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
  visible_object_candidates_.clear();
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
                                const WorldSession& session,
                                const ObjectManager* obj_mgr,
                                ObjectGuid local_guid) {
  if (!initialized_) return;

  minimap_.Update(player_x, player_y, player_z, facing);
  const bool rotate_minimap =
      CVarSystem::Instance().GetCVarBool("rotateMinimap");
  minimap_.SetRotating(rotate_minimap);

  auto& minimap_state = minimap_state_;
  minimap_state.SetIndoorMinimapActive(
      world_environment_.IsIndoors());
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
  UpdateVisibleTerrainTiles(player_x, player_y);

  RebuildMinimapContent(session, obj_mgr, local_guid, player_x, player_y, player_z,
                        facing, visible_radius);
}

void MinimapIntegration::Render(std::uint8_t view_id, float screen_w,
                                float screen_h) {
  if (!initialized_) return;
  minimap_.Render(view_id, screen_w, screen_h);
}

void MinimapIntegration::RenderToTexture(const std::uint8_t view_id) {
  if (initialized_) minimap_.RenderToTexture(view_id);
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
  visible_object_candidates_.clear();
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

bool MinimapIntegration::HandleClick(float screen_x, float screen_y,
                                     float& out_world_x, float& out_world_y) {
  if (!initialized_) return false;
  return minimap_.HandleClick(screen_x, screen_y, out_world_x, out_world_y);
}

void MinimapIntegration::UpdateVisibleTerrainTiles(float player_x,
                                                   float player_y) {
  minimap_.ClearBackgroundTiles();
  if (current_map_name_.empty()) {
    return;
  }

  if (!terrain_translations_loaded_) {
    return;
  }

  auto& tex_mgr = texture_manager_;
  const std::uint32_t terrain_tint = openwow::game::BuildMinimapTerrainTint(
      world_scene_state_ != nullptr
          ? world_scene_state_->GetPackedFogColorArgb()
          : 0xFF99B3D9u);
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
    const WorldSession& session, const ObjectManager* obj_mgr,
    const ObjectGuid local_guid,
    const float player_x, const float player_y, const float player_z,
    const float facing, const float visible_radius) {
  (void)player_z;
  minimap_.ClearIcons();

  const auto& minimap_state = minimap_state_;
  openwow::game::Minimap_TickPOIDirections(
      minimap_state, static_cast<std::int32_t>(session.objects().GetMapId()),
      player_x, player_y);
  AppendPointOfInterestIcons(minimap_, minimap_state, player_x, player_y,
                             visible_radius);
  AppendGuidePointOfInterestIcon(
      minimap_, minimap_state, player_x, player_y, visible_radius);

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
    return;
  }
  const auto* player = obj_mgr->GetActivePlayer();
  if (player == nullptr) {
    content.RenderMinimapContent();
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

      minimap_.AddRotatingArrow(
          {std::atan2(dx, -dy), MinimapRotatingArrowKind::kGroupMember,
           true});
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

    minimap_.AddRotatingArrow({std::atan2(dx, -dy), kind, true});
  }

  content.RenderMinimapContent();
}

}

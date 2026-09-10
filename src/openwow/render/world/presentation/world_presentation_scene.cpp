#include "openwow/render/world/presentation/world_presentation_scene.h"

#include "openwow/core/platform_runtime_policy.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/render/backend/bgfx/bgfx_encoder_ledger.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/world/terrain/distant_terrain.h"
#include "openwow/render/world/doodads/doodad_renderer.h"
#include "openwow/render/world/detail_doodads/detail_doodad_renderer.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/render/scene/shadow_presentation_runtime.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/render/world/environment/sky_renderer.h"
#include "openwow/render/world/terrain/terrain_renderer.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/world/water/water_renderer.h"
#include "openwow/render/world/environment/weather_renderer.h"
#include "openwow/render/world/wmo/wmo_renderer.h"
#include "openwow/render/world/wmo/wmo_portal_fill_renderer.h"
#include "openwow/world/liquid/wmo_liquid_surface.h"
#include "openwow/world/world_render_pipeline.h"
#include "openwow/render/api/math/view_projection.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/diagnostics/performance_logging.h"
#include "openwow/runtime/scheduling/frame_job_system.h"

#include <algorithm>
#include <bgfx/bgfx.h>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace openwow::render {
namespace {

template <typename>
inline constexpr bool kUnhandledWorldPresentationCommand = false;

constexpr float kRetailLowDetailNearOverlap = 50.0f;

void LogWmoGroupFailure(const std::string& path, const std::uint32_t group,
                         const char* phase, const char* reason) {
  diagnostics::Log(diagnostics::LogLevel::kWarn,
      "WorldPresentationScene: WMO group failed phase=" + std::string(phase) +
          " path=" + path + " group=" + std::to_string(group) +
          " reason=" + reason);
}

}

struct WorldPresentationScene::ModelResource {
  struct DoodadGroup {
    std::vector<std::uint16_t> refs;
    std::uint32_t flags{};
  };

  std::unique_ptr<WmoRenderer> renderer;
  std::shared_ptr<const data::wmo::WmoRoot> root;
  std::size_t group_count{};
  std::unordered_map<std::uint32_t, DoodadGroup> groups;
};

struct WorldPresentationScene::PendingTerrainTile {
  struct Prepared {
    PreparedTerrainTile terrain;
    PreparedTerrainMaterialTextures materials;
    std::chrono::milliseconds preparation_time{};
    std::int32_t tile_x{};
    std::int32_t tile_y{};
  };

  std::future<Prepared> future;
  world::PublishTerrainTileCommand command;
  bool started{false};
};

struct WorldPresentationScene::PendingWmoGroup {
  struct Prepared {
    world::PublishWorldModelGroupCommand command;
    WmoGroupMesh mesh;
    PreparedWmoMaterialTextures textures;
    std::uint64_t publication_bytes{};
  };
  std::future<Prepared> future;
  std::optional<Prepared> prepared;
  world::PublishWorldModelGroupCommand command;
  bool started{false};
};

WorldPresentationScene::WorldPresentationScene(
    TextureManager& texture_manager, m2::M2System& m2_system,
    SkySettingsProvider sky_settings)
    : texture_manager_(texture_manager),
      m2_system_(m2_system),
      sky_settings_(std::move(sky_settings)) {}
WorldPresentationScene::~WorldPresentationScene() { Shutdown(); }

bool WorldPresentationScene::Initialize() {
  if (initialized_) return true;
  terrain_ = std::make_unique<TerrainRenderer>(texture_manager_);
  shadows_ = std::make_unique<ShadowPresentationRuntime>(m2_system_);
  sky_ = std::make_unique<SkyRenderer>(
      texture_manager_, m2_system_, sky_settings_);
  weather_renderer_ = std::make_unique<WeatherRenderer>(texture_manager_);
  water_ = std::make_unique<WaterRenderer>(texture_manager_);
  doodads_ = std::make_unique<DoodadRenderer>(m2_system_);
  detail_doodads_ = std::make_unique<DetailDoodadRenderer>(texture_manager_);
  doodads_->BindWmoDoodadM2EventSink(wmo_doodad_m2_event_sink_);
  distant_ = std::make_unique<DistantTerrainRenderer>();
  portal_fills_ = std::make_unique<WmoPortalFillRenderer>();

  static_cast<void>(portal_fills_->Initialize());
  initialized_ = terrain_->Initialize() && shadows_->Initialize() &&
                 sky_->Initialize() &&
                 weather_renderer_->Initialize() && water_->Initialize() &&
                 doodads_->Initialize() && detail_doodads_->Initialize();
  static_cast<void>(distant_->Initialize());

  if (openwow::render::IsRendererContextActive()) {
    wmo_shader_warm_up_ = std::make_unique<WmoRenderer>(texture_manager_);
    if (!wmo_shader_warm_up_->Initialize()) {
      wmo_shader_warm_up_.reset();
    }
  }

  SetFileLoader(load_file_);
  SetPrefixFileLoader(load_file_prefix_);
  BindDbc(dbc_);
  return initialized_;
}

void WorldPresentationScene::VisitDoodadCollisionTriangles(
    const std::array<float, 6>& world_bounds,
    const std::function<void(const DoodadCollisionTriangle&)>& visitor,
    const bool include_object_owned) const {
  if (doodads_) {
    doodads_->VisitCollisionTriangles(world_bounds, visitor,
                                      include_object_owned);
  }
}

std::uint64_t WorldPresentationScene::DoodadCollisionRevision() const noexcept {
  return doodads_ ? doodads_->CollisionRevision() : 0u;
}

bool WorldPresentationScene::IsDoodadWorldEntryLoadDrained() const {
  return !doodads_ || doodads_->IsWorldEntryLoadDrained();
}

bool WorldPresentationScene::IsTerrainWorldEntryLoadDrained() const {
  return pending_terrain_tiles_.empty();
}

void WorldPresentationScene::BindWmoDoodadM2EventSink(
    std::function<void(const WmoDoodadM2PresentationEvent&)> sink) {
  wmo_doodad_m2_event_sink_ = std::move(sink);
  if (doodads_) {
    doodads_->BindWmoDoodadM2EventSink(wmo_doodad_m2_event_sink_);
  }
}

void WorldPresentationScene::ResetMap() {
  for (auto& pending : pending_terrain_tiles_) {
    if (pending && pending->started) {
      retired_terrain_tiles_.push_back(std::move(pending));
    }
  }
  pending_terrain_tiles_.clear();
  pending_wmo_groups_.clear();
  for (auto& [key, model] : models_) {
    (void)key;
    if (model->renderer) model->renderer->Shutdown();
  }
  models_.clear();
  instances_.clear();
  if (doodads_) doodads_->Clear();
  if (detail_doodads_) detail_doodads_->Clear();
  if (terrain_) terrain_->ClearTerrain();
  if (shadows_) shadows_->ResetMap();
  if (water_) water_->ClearSurfaces();
  if (distant_) distant_->Clear();
  if (sky_) sky_->Reset();
  if (weather_renderer_) weather_renderer_->Reset();
  world::ResetWeather(weather_);
}

void WorldPresentationScene::QueueTerrainTilePreparation(
    const world::PublishTerrainTileCommand& command) {
  if (!command.adt) {
    return;
  }

  RetireTerrainTilePreparation(command.tile_x, command.tile_y);
  auto pending = std::make_unique<PendingTerrainTile>();
  pending->command = command;
  pending_terrain_tiles_.push_back(std::move(pending));
}

void WorldPresentationScene::RetireTerrainTilePreparation(
    const std::int32_t tile_x, const std::int32_t tile_y) {
  auto pending = std::find_if(
      pending_terrain_tiles_.begin(), pending_terrain_tiles_.end(),
      [tile_x, tile_y](const auto& entry) {
        return entry && entry->command.tile_x == tile_x &&
               entry->command.tile_y == tile_y;
      });
  if (pending == pending_terrain_tiles_.end()) {
    return;
  }
  if ((*pending)->started) {
    retired_terrain_tiles_.push_back(std::move(*pending));
  }
  pending_terrain_tiles_.erase(pending);
}

void WorldPresentationScene::DrainRetiredTerrainTilePreparations(
    const bool wait) {
  auto retired = retired_terrain_tiles_.begin();
  while (retired != retired_terrain_tiles_.end()) {
    PendingTerrainTile& pending = **retired;
    if (!pending.future.valid()) {
      retired = retired_terrain_tiles_.erase(retired);
      continue;
    }
    if (!wait && pending.future.wait_for(std::chrono::seconds(0)) !=
                     std::future_status::ready) {
      ++retired;
      continue;
    }
    try {
      static_cast<void>(pending.future.get());
    } catch (const std::exception& exception) {
      diagnostics::Log(
          diagnostics::LogLevel::kWarn,
          "WorldPresentationScene: retired terrain preparation failed tile=(" +
              std::to_string(pending.command.tile_x) + "," +
              std::to_string(pending.command.tile_y) + ") reason=" +
              exception.what());
    } catch (...) {
      diagnostics::Log(
          diagnostics::LogLevel::kWarn,
          "WorldPresentationScene: retired terrain preparation failed tile=(" +
              std::to_string(pending.command.tile_x) + "," +
              std::to_string(pending.command.tile_y) +
              ") reason=unknown exception");
    }
    retired = retired_terrain_tiles_.erase(retired);
  }
}

void WorldPresentationScene::StartQueuedTerrainTilePreparation() {
  if (std::ranges::any_of(pending_terrain_tiles_, [](const auto& pending) {
        return pending && pending->started;
      }) ||
      !retired_terrain_tiles_.empty()) {
    return;
  }

  const auto pending = std::find_if(
      pending_terrain_tiles_.begin(), pending_terrain_tiles_.end(),
      [](const auto& entry) { return entry && !entry->started; });
  if (pending == pending_terrain_tiles_.end()) {
    return;
  }

  PendingTerrainTile& entry = **pending;
  const auto adt = entry.command.adt;
  const auto tile_x = entry.command.tile_x;
  const auto tile_y = entry.command.tile_y;
  const auto big_alpha = entry.command.big_alpha;
  const auto loader = load_file_;
  try {
    entry.future = std::async(
        std::launch::async,
        [adt, tile_x, tile_y, big_alpha, loader]() mutable {
          const auto started = std::chrono::steady_clock::now();
          PendingTerrainTile::Prepared prepared{
              .tile_x = tile_x,
              .tile_y = tile_y,
          };
          prepared.terrain = PrepareAdtTerrainTile(
              *adt, static_cast<std::uint32_t>(tile_x),
              static_cast<std::uint32_t>(tile_y), big_alpha,
              openwow::core::GetPlatformRuntimePolicy()
                  .terrain_alpha_map_dimension);
          prepared.materials =
              PrepareTerrainMaterialTextures(prepared.terrain, loader);
          prepared.preparation_time =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started);
          return prepared;
        });
    entry.started = true;
  } catch (const std::exception& exception) {
    diagnostics::Log(
        diagnostics::LogLevel::kWarn,
        "WorldPresentationScene: terrain preparation scheduling failed tile=(" +
            std::to_string(tile_x) + "," + std::to_string(tile_y) +
            ") reason=" + exception.what());
  } catch (...) {
    diagnostics::Log(
        diagnostics::LogLevel::kWarn,
        "WorldPresentationScene: terrain preparation scheduling failed tile=(" +
            std::to_string(tile_x) + "," + std::to_string(tile_y) +
            ") reason=unknown exception");
  }
}

void WorldPresentationScene::PumpPreparedTerrainTiles() {
  auto pending = std::find_if(
      pending_terrain_tiles_.begin(), pending_terrain_tiles_.end(),
      [](const auto& entry) { return entry && entry->started; });
  if (pending == pending_terrain_tiles_.end() ||
      !(*pending)->future.valid() ||
      (*pending)->future.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready) {
    return;
  }

  const auto tile_x = (*pending)->command.tile_x;
  const auto tile_y = (*pending)->command.tile_y;
  try {
    auto prepared = (*pending)->future.get();
    const auto commit_started = std::chrono::steady_clock::now();
    if (terrain_) {
      terrain_->UploadPreparedAdt(prepared.terrain, prepared.materials,
                                  prepared.tile_x, prepared.tile_y);
    }
    const auto commit_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - commit_started);
    if (prepared.preparation_time >= std::chrono::milliseconds(100) ||
        commit_time >= std::chrono::milliseconds(16)) {
      diagnostics::Log(
          diagnostics::LogLevel::kWarn,
          "WorldPresentationScene: slow terrain pipeline tile=(" +
              std::to_string(prepared.tile_x) + "," +
              std::to_string(prepared.tile_y) + ") preparation_ms=" +
              std::to_string(prepared.preparation_time.count()) +
              " commit_ms=" + std::to_string(commit_time.count()));
    }
  } catch (const std::exception& exception) {
    diagnostics::Log(
        diagnostics::LogLevel::kWarn,
        "WorldPresentationScene: terrain preparation failed tile=(" +
            std::to_string(tile_x) + "," + std::to_string(tile_y) +
            ") reason=" + exception.what());
  } catch (...) {
    diagnostics::Log(
        diagnostics::LogLevel::kWarn,
        "WorldPresentationScene: terrain preparation failed tile=(" +
            std::to_string(tile_x) + "," + std::to_string(tile_y) +
            ") reason=unknown exception");
  }
  pending_terrain_tiles_.erase(pending);
}

void WorldPresentationScene::QueueWmoGroupPreparation(
    const world::PublishWorldModelGroupCommand& command) {
  const auto key = std::pair{command.resource_key, command.group_index};
  if (!command.group || pending_wmo_groups_.contains(key)) {
    return;
  }
  auto pending = std::make_unique<PendingWmoGroup>();
  pending->command = command;
  pending_wmo_groups_.emplace(key, std::move(pending));
}

void WorldPresentationScene::StartQueuedWmoGroupPreparations() {
  constexpr std::size_t kMaxConcurrentWmoGroupPreparations =
#if defined(OPENWOW_PLATFORM_IOS)
      1u;
#else
      4u;
#endif
  std::size_t active = 0u;
  std::unordered_set<std::string> renderer_attempted;
  for (const auto& [_, pending] : pending_wmo_groups_) {
    active += pending->started ? 1u : 0u;
  }
  for (auto& [key, pending] : pending_wmo_groups_) {
    if (active >= kMaxConcurrentWmoGroupPreparations || pending->started) {
      continue;
    }
    const auto model = models_.find(key.first);
    if (model == models_.end() || !model->second->root) {
      continue;
    }
    if (!model->second->renderer && renderer_attempted.insert(key.first).second) {
      auto renderer = std::make_unique<WmoRenderer>(texture_manager_);
      if (renderer->Initialize()) {
        renderer->BeginStreaming(*model->second->root,
                                 model->second->group_count);
        model->second->renderer = std::move(renderer);
      }
    }
    const auto command = pending->command;
    const auto root = model->second->root;
    const auto loader = load_file_;
    // Groups in the same WMO commonly share materials. Their GPU leases are
    // already owned by the renderer; decoding them again for every group
    // wastes worker time and temporary memory without changing publication.
    std::vector<std::uint32_t> missing_materials;
    for (const auto material : command.material_indices) {
      if (!model->second->renderer ||
          !model->second->renderer->IsMaterialResident(material)) {
        missing_materials.push_back(material);
      }
    }
    try {
      pending->future = std::async(
          std::launch::async,
          [command, root, loader,
           missing_materials = std::move(missing_materials)]() mutable {
          const openwow::diagnostics::PerformanceTimer performance;
          PendingWmoGroup::Prepared prepared{.command = std::move(command)};
          prepared.mesh = GenerateWmoGroupMesh(
              *root, *prepared.command.group);
          prepared.textures = WmoRenderer::PrepareMaterialTextures(
              *root, missing_materials, loader);
          prepared.publication_bytes =
              prepared.mesh.vertices.size() * sizeof(WmoVertex) +
              prepared.mesh.composite_vertices.size() *
                  sizeof(WmoCompositeVertex) +
              prepared.mesh.indices.size() * sizeof(std::uint16_t);
          for (const auto& upload : prepared.textures.uploads) {
            if (upload) {
              prepared.publication_bytes += upload->upload_size;
            }
          }
          if (performance.enabled() && performance.ElapsedMs() >= 20.0) {
            static openwow::diagnostics::PerformanceLogSite performance_site;
            openwow::diagnostics::LogPerformanceDuration(
                performance_site, "world.wmo_prepare", performance,
                "path=" + prepared.command.resource_key + " group=" +
                    std::to_string(prepared.command.group_index) +
                    " materials=" +
                    std::to_string(prepared.command.material_indices.size()) +
                    " missing_materials=" + std::to_string(missing_materials.size()) +
                    " upload_bytes=" + std::to_string(prepared.publication_bytes));
          }
          return prepared;
        });
      pending->started = true;
      ++active;
    } catch (const std::exception& exception) {
      LogWmoGroupFailure(key.first, key.second, "schedule", exception.what());
      pending->started = false;
    } catch (...) {
      LogWmoGroupFailure(key.first, key.second, "schedule", "unknown exception");
      pending->started = false;
    }
  }
}

void WorldPresentationScene::PumpPreparedWmoGroups(
    world::WorldPresentationAcknowledgment& acknowledgment) {
  constexpr std::size_t kGroupBudget = 8u;
  constexpr std::uint64_t kByteBudget = 8u * 1024u * 1024u;
  constexpr auto kTimeBudget = std::chrono::milliseconds(2);
  const auto deadline = std::chrono::steady_clock::now() + kTimeBudget;
  std::size_t published = 0u;
  std::uint64_t remaining_bytes = kByteBudget;
  for (auto it = pending_wmo_groups_.begin();
       it != pending_wmo_groups_.end();) {

    const bool is_transport_group =
        it->first.first.find("transports/") != std::string::npos ||
        it->first.first.find("Transports/") != std::string::npos;
    if (!is_transport_group && published >= kGroupBudget) {
      break;
    }
    if (!is_transport_group && std::chrono::steady_clock::now() >= deadline) {

      ++it;
      continue;
    }
    PendingWmoGroup& pending = *it->second;
    if (!pending.started) {
      ++it;
      continue;
    }
    if (!pending.prepared.has_value()) {
      if (!pending.future.valid() ||
          pending.future.wait_for(std::chrono::seconds(0)) !=
              std::future_status::ready) {
        ++it;
        continue;
      }
      try {
        pending.prepared = pending.future.get();
      } catch (const std::exception& exception) {
        LogWmoGroupFailure(it->first.first, it->first.second,
                           "prepare", exception.what());
        acknowledgment.wmo_groups.push_back({
            .resource_key = it->first.first,
            .group_index = it->first.second,
            .status = world::WmoGroupPublicationStatus::kRetryableFailure});
        it = pending_wmo_groups_.erase(it);
        continue;
      } catch (...) {
        LogWmoGroupFailure(it->first.first, it->first.second,
                           "prepare", "unknown exception");
        acknowledgment.wmo_groups.push_back({
            .resource_key = it->first.first,
            .group_index = it->first.second,
            .status = world::WmoGroupPublicationStatus::kRetryableFailure});
        it = pending_wmo_groups_.erase(it);
        continue;
      }
    }
    auto& prepared = *pending.prepared;
    std::uint64_t remaining_after_publication = remaining_bytes;

    if (!is_transport_group &&
        !world::TryConsumePublicationByteBudget(
            prepared.publication_bytes, kByteBudget,
            remaining_after_publication)) {
      ++it;
      continue;
    }
    const auto model = models_.find(prepared.command.resource_key);
    if (model == models_.end() || !model->second->root) {
      LogWmoGroupFailure(it->first.first, it->first.second,
                         "publish", "missing model root");
      acknowledgment.wmo_groups.push_back({
          .resource_key = it->first.first,
          .group_index = it->first.second,
          .status = world::WmoGroupPublicationStatus::kRetryableFailure});
      it = pending_wmo_groups_.erase(it);
      continue;
    }
    ModelResource::DoodadGroup doodad_group{
        .refs = prepared.command.group->doodadRefs,
        .flags = prepared.command.group->header.flags,
    };
    model->second->groups.insert_or_assign(
        prepared.command.group_index, std::move(doodad_group));
    for (auto& [id, instance] : instances_) {
      (void)id;
      if (instance.resource_key == prepared.command.resource_key) {
        doodads_->PublishStreamingWmoGroup(
            instance.doodad_owner, *model->second->root,
            prepared.command.group->doodadRefs,
            prepared.command.group->header.flags,
            static_cast<std::uint16_t>(prepared.command.group_index),
            instance.transform);
      }
    }
    if (!model->second->renderer) {
      LogWmoGroupFailure(it->first.first, it->first.second,
                         "publish", "renderer unavailable");
      acknowledgment.wmo_groups.push_back({
          .resource_key = it->first.first,
          .group_index = it->first.second,
          .status = world::WmoGroupPublicationStatus::kRetryableFailure});
      it = pending_wmo_groups_.erase(it);
      continue;
    }
    world::WmoGroupPublicationStatus publication_status =
        world::WmoGroupPublicationStatus::kRetryableFailure;
    try {
      const auto classification =
          ClassifyWmoGroupPublication(
              *prepared.command.group,
              model->second->root->materials.size());
      if (classification != world::WmoGroupPublicationStatus::kDrawableReady) {
        publication_status = model->second->renderer->UploadWmoGroup(
            prepared.command.group_index, *prepared.command.group,
            prepared.mesh);
      } else {
        for (const auto& upload : prepared.textures.uploads) {
          if (upload) {
            static_cast<void>(texture_manager_.CommitPreparedTexture(*upload));
          }
        }
        const bool materials_ready =
            model->second->renderer->PublishPreparedMaterialSubset(
                *model->second->root, prepared.command.material_indices,
                prepared.textures.failed_paths);
        if (materials_ready || prepared.command.material_indices.empty()) {
          publication_status = model->second->renderer->UploadWmoGroup(
              prepared.command.group_index, *prepared.command.group,
              prepared.mesh);
        }
      }
    } catch (const std::exception& exception) {
      LogWmoGroupFailure(it->first.first, it->first.second,
                         "publish", exception.what());
      publication_status = world::WmoGroupPublicationStatus::kRetryableFailure;
    } catch (...) {
      LogWmoGroupFailure(it->first.first, it->first.second,
                         "publish", "unknown exception");
      publication_status = world::WmoGroupPublicationStatus::kRetryableFailure;
    }
    if (publication_status == world::WmoGroupPublicationStatus::kFailed ||
        publication_status ==
            world::WmoGroupPublicationStatus::kRetryableFailure) {
      acknowledgment.wmo_groups.push_back({
          .resource_key = it->first.first,
          .group_index = it->first.second,
          .status = publication_status});
      it = pending_wmo_groups_.erase(it);
      continue;
    }
    acknowledgment.wmo_groups.push_back({
        .resource_key = it->first.first,
        .group_index = it->first.second,
        .status = publication_status});

    if (!is_transport_group) {
      remaining_bytes = remaining_after_publication;
      ++published;
    }
    it = pending_wmo_groups_.erase(it);
  }
}

void WorldPresentationScene::Shutdown() {
  if (!terrain_) return;
  ResetMap();
  DrainRetiredTerrainTilePreparations(true);

  if (wmo_shader_warm_up_) {
    wmo_shader_warm_up_->Shutdown();
    wmo_shader_warm_up_.reset();
  }
  InvalidateSharedWmoShaderResources();
  distant_->Shutdown();
  if (portal_fills_) portal_fills_->Shutdown();
  doodads_->Shutdown();
  detail_doodads_->Shutdown();
  water_->Shutdown();
  weather_renderer_->Shutdown();
  sky_->Shutdown();
  shadows_->Shutdown();
  terrain_->Shutdown();
  distant_.reset(); detail_doodads_.reset(); doodads_.reset(); water_.reset();
  weather_renderer_.reset(); sky_.reset(); shadows_.reset(); terrain_.reset();
  portal_fills_.reset();
  initialized_ = false;
}

void WorldPresentationScene::SetFileLoader(LoadFileCallback callback) {
  load_file_ = std::move(callback);
  if (sky_) sky_->SetFileLoader(load_file_);
  if (doodads_) doodads_->SetFileLoader(load_file_);
  if (detail_doodads_) detail_doodads_->SetFileLoader(load_file_);
}

void WorldPresentationScene::SetPrefixFileLoader(
    m2::M2StreamPrefixFileLoader callback) {
  load_file_prefix_ = std::move(callback);

  if (doodads_) doodads_->SetPrefixFileLoader(load_file_prefix_);
}

void WorldPresentationScene::BindDbc(const data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
  if (detail_doodads_) {
    detail_doodads_->BindDbc(dbc_);
  }
  if (water_) {
    water_->SetLiquidStores(
        [this](const std::uint32_t id) {
          return dbc_ ? dbc_->liquid_type().LookupEntry(id) : nullptr;
        },
        [this](const std::uint32_t id) {
          return dbc_ ? dbc_->liquid_material().LookupEntry(id) : nullptr;
        });
  }
}

void WorldPresentationScene::SetEnvironmentDetail(const float scale) {
  if (doodads_) doodads_->SetEnvironmentDetail(scale);
}

void WorldPresentationScene::SetSpecularEnabled(const bool enabled) {
  if (water_) {
    water_->SetSpecularEnabled(enabled);
  }
  for (auto& [_, resource] : models_) {
    if (resource && resource->renderer) {
      resource->renderer->SetSpecularEnabled(enabled);
    }
  }
}

world::WorldPresentationAcknowledgment WorldPresentationScene::Consume(
    world::WorldPresentationCommandBatch batch) {
  world::WorldPresentationAcknowledgment acknowledgment{
      .generation = batch.generation};
  if (!batch.generation.IsValid()) return acknowledgment;

  const auto acknowledge_dropped_pending_groups = [this, &acknowledgment]() {
    for (const auto& [key, pending] : pending_wmo_groups_) {
      (void)pending;
      acknowledgment.wmo_groups.push_back({
          .resource_key = key.first,
          .group_index = key.second,
          .status = world::WmoGroupPublicationStatus::kRetryableFailure});
    }
  };
  if (generation_.IsValid() && generation_ != batch.generation) {
    acknowledge_dropped_pending_groups();
    ResetMap();
  }
  generation_ = batch.generation;
  DrainRetiredTerrainTilePreparations(false);
  PumpPreparedTerrainTiles();
  PumpPreparedWmoGroups(acknowledgment);
  for (auto& command : batch.commands) {
    std::visit([this, &acknowledgment,
                &acknowledge_dropped_pending_groups](auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, world::ResetPresentationCommand>) {
        acknowledge_dropped_pending_groups();
        ResetMap();
      } else if constexpr (std::is_same_v<T, world::PublishDistantTerrainCommand>) {
        if (distant_ && value.wdl) distant_->LoadWdl(*value.wdl);
      } else if constexpr (std::is_same_v<T, world::PublishTerrainTileCommand>) {
        if (terrain_ && value.adt) {
          if (openwow::core::GetPlatformRuntimePolicy()
                  .constrained_mobile_runtime) {
            QueueTerrainTilePreparation(value);
          } else {
            const auto prepared = PrepareAdtTerrainTile(
                *value.adt, static_cast<std::uint32_t>(value.tile_x),
                static_cast<std::uint32_t>(value.tile_y), value.big_alpha,
                openwow::core::GetPlatformRuntimePolicy()
                    .terrain_alpha_map_dimension);
            const auto materials =
                PrepareTerrainMaterialTextures(prepared, load_file_);
            terrain_->UploadPreparedAdt(prepared, materials, value.tile_x,
                                        value.tile_y);
          }
        }
        if (doodads_ && value.adt)
          doodads_->LoadFromAdt(*value.adt, value.tile_x, value.tile_y);
        if (detail_doodads_ && value.adt)
          detail_doodads_->LoadFromAdt(value.adt, value.tile_x, value.tile_y);
        if (water_ && value.liquids)
          water_->ReplaceOwnedWaterHeightfields(value.owner, *value.liquids);
        if (distant_) distant_->SetDetailedTile(value.tile_x, value.tile_y, true);
      } else if constexpr (std::is_same_v<T, world::RemoveTerrainTileCommand>) {
        RetireTerrainTilePreparation(value.tile_x, value.tile_y);
        if (terrain_) terrain_->RemoveAdt(value.tile_x, value.tile_y);
        if (doodads_) doodads_->UnloadTile(value.tile_x, value.tile_y);
        if (detail_doodads_)
          detail_doodads_->UnloadTile(value.tile_x, value.tile_y);
        if (water_) water_->RemoveOwnedWaterHeightfields(value.owner);
        if (distant_) distant_->SetDetailedTile(value.tile_x, value.tile_y, false);
      } else if constexpr (std::is_same_v<T, world::BeginWorldModelCommand>) {
        auto model = std::make_unique<ModelResource>();
        model->root = value.root;
        model->group_count = value.group_count;
        model->renderer = std::make_unique<WmoRenderer>(texture_manager_);
        if (model->renderer->Initialize() && value.root) {
          model->renderer->BeginStreaming(*value.root, value.group_count);
          models_.insert_or_assign(value.resource_key, std::move(model));
        } else if (!models_.contains(value.resource_key)) {
          model->renderer.reset();
          models_.emplace(value.resource_key, std::move(model));
        }
      } else if constexpr (std::is_same_v<T, world::PublishWorldModelGroupCommand>) {
        QueueWmoGroupPreparation(value);
      } else if constexpr (std::is_same_v<T, world::RemoveWorldModelCommand>) {
        std::erase_if(pending_wmo_groups_, [&](const auto& entry) {
          if (entry.first.first != value.resource_key) {
            return false;
          }

          acknowledgment.wmo_groups.push_back({
              .resource_key = entry.first.first,
              .group_index = entry.first.second,
              .status = world::WmoGroupPublicationStatus::kRetryableFailure});
          return true;
        });
        if (auto found = models_.find(value.resource_key); found != models_.end()) {
          found->second->renderer->Shutdown();
          models_.erase(found);
        }
      } else if constexpr (std::is_same_v<T, world::PublishWorldModelInstanceCommand>) {
        ModelInstance instance{.resource_key = value.resource_key,
          .doodad_owner = value.doodad_owner, .transform = value.transform,
          .doodad_set = value.doodad_set,
          .additional_doodad_sets = value.additional_doodad_sets,
          .doodad_animation_controls = value.doodad_animation_controls,
          .visible = value.visible};
        instance.liquids.resize(value.group_count);
        doodads_->BeginStreamingWmoInstance(
            value.doodad_owner, value.group_count, value.doodad_set,
            value.additional_doodad_sets, value.object_guid);
        doodads_->SetWmoInstanceEnabled(value.doodad_owner, value.visible);
        doodads_->SetWmoInstanceDoodadAnimations(
            value.doodad_owner, value.doodad_animation_controls);
        auto [published, inserted] = instances_.insert_or_assign(
            value.stable_id, std::move(instance));
        (void)inserted;
        if (auto model = models_.find(value.resource_key);
            model != models_.end() && model->second->root) {
          doodads_->SetWmoInstanceTransferDestinationGroups(
              published->second.doodad_owner, *model->second->root);
          for (const auto& [group_index, group] : model->second->groups) {
            if (group_index <= std::numeric_limits<std::uint16_t>::max()) {
              doodads_->PublishStreamingWmoGroup(
                  published->second.doodad_owner, *model->second->root,
                  group.refs, group.flags,
                  static_cast<std::uint16_t>(group_index),
                  published->second.transform);
            }
          }
        }
      } else if constexpr (std::is_same_v<T, world::RemoveWorldModelInstanceCommand>) {
        doodads_->UnloadWmoInstance(value.doodad_owner);
        instances_.erase(value.stable_id);
      } else if constexpr (std::is_same_v<T, world::UpdateWorldModelInstanceCommand>) {
        const auto instance = instances_.find(value.stable_id);
        if (instance == instances_.end() || instance->second.doodad_owner != value.doodad_owner) {
          return;
        }
        instance->second.transform = value.transform;
        instance->second.doodad_set = value.doodad_set;
        instance->second.additional_doodad_sets = value.additional_doodad_sets;
        instance->second.doodad_animation_controls = value.doodad_animation_controls;
        instance->second.visible = value.visible;
        doodads_->SetWmoInstanceTransform(value.doodad_owner, value.transform);
        doodads_->SetWmoInstanceDoodadSets(
            value.doodad_owner, value.doodad_set, value.additional_doodad_sets);
        doodads_->SetWmoInstanceEnabled(value.doodad_owner, value.visible);
        doodads_->SetWmoInstanceDoodadAnimations(
            value.doodad_owner, value.doodad_animation_controls);
      } else if constexpr (std::is_same_v<T,
                                          world::TransferWorldModelDoodadSetCommand>) {
        doodads_->TransferWmoDoodadSet(
            value.source_doodad_owner, value.destination_doodad_owner,
            value.source_doodad_set, value.destination_doodad_set);
      } else if constexpr (std::is_same_v<T, world::PublishWorldModelLiquidCommand>) {
        if (auto found = instances_.find(value.stable_id); found != instances_.end()) {
          if (found->second.liquids.size() <= value.group_index)
            found->second.liquids.resize(value.group_index + 1u);
          found->second.liquids[value.group_index] = value.liquid;
        }
      } else if constexpr (std::is_same_v<T, world::SetWeatherPresentationCommand>) {

        world::SetWeather(weather_, value.type, value.density,
                          value.row ? &*value.row : nullptr, value.smooth,
                          value.row ? value.row->transition_value : 1.0f,
                          weather_clock_);
      } else if constexpr (std::is_same_v<T, world::SpawnWaterRippleCommand>) {
        if (water_) water_->SpawnWaterRipple(value);
      } else {
        static_assert(kUnhandledWorldPresentationCommand<T>,
                      "World presentation command visitor is not exhaustive");
      }
    }, command);
  }
  StartQueuedTerrainTilePreparation();
  StartQueuedWmoGroupPreparations();
  return acknowledgment;
}

void WorldPresentationScene::SetWeatherGroundHeightSampler(
    std::function<std::optional<float>(float x, float y, float z)> sampler) {
  if (weather_renderer_) {
    weather_renderer_->SetGroundHeightSampler(std::move(sampler));
  }
}

void WorldPresentationScene::SetWeatherCollisionSampler(
    WeatherRenderer::CollisionSampler sampler) {
  if (weather_renderer_) {
    weather_renderer_->SetCollisionSampler(std::move(sampler));
  }
}

void WorldPresentationScene::Update(const float dt,
                                    const RenderVec3& position,
                                    const float environment_detail,
                                    const float weather_particle_density,
                                    const bool use_weather_shaders,
                                    const bool indoors,
                                    const float facing) {
  if (!initialized_) return;
  SetEnvironmentDetail(environment_detail);
  weather_clock_ += static_cast<std::uint32_t>(std::max(0.0f, dt) * 1000.0f);
  world::UpdateWeather(
      weather_, {.now = weather_clock_,
                  .position = position,
                  .facing = facing,
                  .movement_speed = 0.0f,
                  .indoors = indoors});
  weather_renderer_->Update(dt, position, weather_,
                            weather_particle_density, use_weather_shaders);
  water_->Update(dt);
  doodads_->SetLoadingFocus(position[0], position[1], position[2]);

  doodads_->UpdateLoading();
  doodads_->Update(dt);
  detail_doodads_->Update(position);
}

void WorldPresentationScene::Render(
    const world::WorldPresentationSnapshot& snapshot,
    const WorldRenderViews& views,
    const ViewProjection& matrices, std::uint16_t screen_width,
    std::uint16_t screen_height, m2::M2TransparentDrawOrder& alpha_draw_order) {

  occlusion_buffer_.BeginFrame(occlusion::ResolveProjectionNearDepth(
      matrices.bgfx_projection(), matrices.homogeneous_depth()));
  if (!initialized_ || snapshot.map_generation != generation_) return;
  const auto gpu = matrices.AsBgfxColumnMajor();
  world::Frustum frustum{};
  for (std::size_t p = 0; p < frustum.planes.size(); ++p)
    std::copy_n(snapshot.camera.frustum_planes.begin() + p * 4u, 4u,
                frustum.planes[p].begin());
  WorldEnvironmentSnapshot env{};
  env.generation = snapshot.map_generation.value;
  env.fog.params = {snapshot.environment.fog_start,
                    snapshot.environment.fog_end,
                    snapshot.environment.fog_density, 0.0f};
  env.fog.color = snapshot.environment.fog_color;
  env.models.light_direction = snapshot.environment.model_light_direction;
  env.models.ambient_color = snapshot.environment.model_ambient;
  env.models.diffuse_color = snapshot.environment.model_diffuse;
  env.models.fog = env.fog;
  env.models.receives_world_shadows =
      snapshot.shadows.enabled && snapshot.shadows.quality >= 3u;
  env.surface_to_light = snapshot.environment.light_direction;
  env.ambient = snapshot.environment.ambient;
  env.diffuse = snapshot.environment.diffuse;
  env.specular = snapshot.environment.diffuse;
  env.wmo.outdoor_ambient = snapshot.environment.wmo_outdoor_ambient;
  env.wmo.outdoor_diffuse = snapshot.environment.wmo_outdoor_diffuse;
  env.wmo.window_ambient = snapshot.environment.window_ambient;
  env.wmo.window_diffuse = snapshot.environment.window_diffuse;

  env.point_lights = doodads_->scene_point_lights();
  world::SkyColors sky_colors{};
  sky_colors.colors = snapshot.sky.colors;
  sky_colors.fog_distance = snapshot.sky.fog_distance;
  sky_colors.fog_multiplier = snapshot.sky.fog_multiplier;
  sky_colors.highlight_sky = snapshot.sky.highlight;
  sky_colors.scene_visibility = snapshot.sky.scene_visibility;
  sky_colors.water_shallow_alpha = snapshot.sky.water_shallow_alpha;
  sky_colors.water_deep_alpha = snapshot.sky.water_deep_alpha;
  sky_colors.ocean_shallow_alpha = snapshot.sky.ocean_shallow_alpha;
  sky_colors.ocean_deep_alpha = snapshot.sky.ocean_deep_alpha;
  env.sky = sky_colors;
  const auto& pos = snapshot.camera.position;

  const float low_detail_near_clip =
      snapshot.camera.far_clip - kRetailLowDetailNearOverlap;
  RenderMatrix4x4 low_detail_projection = snapshot.camera.projection;
  const float low_detail_depth_range =
      snapshot.camera.low_detail_far_clip - low_detail_near_clip;
  low_detail_projection[10] =
      (low_detail_near_clip + snapshot.camera.low_detail_far_clip) /
      low_detail_depth_range;
  low_detail_projection[14] =
      (-2.0f * low_detail_near_clip * snapshot.camera.low_detail_far_clip) /
      low_detail_depth_range;
  const ViewProjection low_detail_matrices = ViewProjection::CopyOf(
      snapshot.camera.view.data(), low_detail_projection.data(),
      matrices.homogeneous_depth());
  const auto low_detail_gpu = low_detail_matrices.AsBgfxColumnMajor();

  bgfx::setViewMode(views.scene, bgfx::ViewMode::Default);

  bgfx::setViewTransform(views.scene, gpu.view.data(), gpu.projection.data());

  bgfx::setViewMode(views.wmo, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(views.wmo, 0, 0, screen_width, screen_height);
  bgfx::setViewTransform(views.wmo, matrices.bgfx_view().data(),
                         matrices.bgfx_projection().data());
  bgfx::setViewClear(views.wmo, BGFX_CLEAR_NONE);

  bgfx::setViewMode(views.low_detail, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(views.low_detail, 0, 0, screen_width, screen_height);
  bgfx::setViewTransform(views.low_detail, low_detail_gpu.view.data(),
                         low_detail_gpu.projection.data());
  // bgfx does not expose the viewport minZ/maxZ interval used by the retail
  // client. Preserve the real low-detail projection so clipping remains
  // correct, then isolate its depth from the foreground with depth-only view
  // boundaries. Color is retained, so the low-detail world remains behind the
  // normal world without admitting geometry outside either projection.
  bgfx::setViewClear(views.low_detail, BGFX_CLEAR_DEPTH, 0x00000000u,
                     1.0f, 0u);
  bgfx::touch(views.low_detail);

  bgfx::setViewMode(views.foreground_depth_reset,
                    bgfx::ViewMode::Sequential);
  bgfx::setViewRect(views.foreground_depth_reset, 0, 0, screen_width,
                    screen_height);
  bgfx::setViewTransform(views.foreground_depth_reset, gpu.view.data(),
                         gpu.projection.data());
  bgfx::setViewClear(views.foreground_depth_reset, BGFX_CLEAR_DEPTH,
                     0x00000000u, 1.0f, 0u);
  bgfx::touch(views.foreground_depth_reset);

  bgfx::setViewMode(views.doodads, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(views.doodads, 0, 0, screen_width, screen_height);
  bgfx::setViewTransform(views.doodads, matrices.bgfx_view().data(),
                         matrices.bgfx_projection().data());
  bgfx::setViewClear(views.doodads, BGFX_CLEAR_NONE);

  bgfx::setViewMode(views.alpha, bgfx::ViewMode::DepthAscending);
  bgfx::setViewRect(views.alpha, 0, 0, screen_width, screen_height);
  bgfx::setViewTransform(views.alpha, matrices.bgfx_view().data(),
                         matrices.bgfx_projection().data());
  bgfx::setViewClear(views.alpha, BGFX_CLEAR_NONE);

  bgfx::setViewMode(views.detail_doodads, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(views.detail_doodads, 0, 0, screen_width, screen_height);
  bgfx::setViewTransform(views.detail_doodads, matrices.bgfx_view().data(),
                         matrices.bgfx_projection().data());
  bgfx::setViewClear(views.detail_doodads, BGFX_CLEAR_NONE);

  const auto& sky_rect = snapshot.environment.sky_clip_rect;
  const float capture_w = static_cast<float>(screen_width);
  const float capture_h = static_cast<float>(screen_height);
  const float sky_px_x0 = std::clamp((sky_rect.min_x + 1.0f) * 0.5f * capture_w,
                                     0.0f, capture_w);
  const float sky_px_x1 = std::clamp((sky_rect.max_x + 1.0f) * 0.5f * capture_w,
                                     0.0f, capture_w);
  const float sky_px_y0 = std::clamp((1.0f - sky_rect.max_y) * 0.5f * capture_h,
                                     0.0f, capture_h);
  const float sky_px_y1 = std::clamp((1.0f - sky_rect.min_y) * 0.5f * capture_h,
                                     0.0f, capture_h);
  const auto sky_scissor_x = static_cast<std::uint16_t>(std::floor(sky_px_x0));
  const auto sky_scissor_y = static_cast<std::uint16_t>(std::floor(sky_px_y0));
  const auto sky_scissor_right = static_cast<std::uint16_t>(std::ceil(sky_px_x1));
  const auto sky_scissor_bottom = static_cast<std::uint16_t>(std::ceil(sky_px_y1));
  const bool sky_scissor_empty =
      sky_scissor_right <= sky_scissor_x || sky_scissor_bottom <= sky_scissor_y;
  const bool sky_scissor_covers_viewport =
      sky_scissor_x == 0u && sky_scissor_y == 0u &&
      sky_scissor_right >= screen_width && sky_scissor_bottom >= screen_height;
  if (sky_scissor_covers_viewport || sky_scissor_empty) {
    bgfx::setViewScissor(views.sky, 0, 0, 0, 0);
  } else {
    bgfx::setViewScissor(
        views.sky, sky_scissor_x, sky_scissor_y,
        static_cast<std::uint16_t>(sky_scissor_right - sky_scissor_x),
        static_cast<std::uint16_t>(sky_scissor_bottom - sky_scissor_y));
  }

  const DrawSortDepth terrain_sort_depth{
      .camera_position = snapshot.camera.position,
      .far_clip = snapshot.camera.far_clip};
  const DrawSortDepth low_detail_sort_depth{
      .camera_position = snapshot.camera.position,
      .far_clip = snapshot.camera.low_detail_far_clip};
  sky_->Update(0.0f, snapshot.time_of_day_hours);
  sky_->SetTimeOfDay(snapshot.time_of_day_hours);
  sky_->SetColors(sky_colors);
  sky_->SetZoneSkybox(snapshot.primary_skybox);
  sky_->SetDomeRuntimeState({.camera_forward = snapshot.camera.forward,
    .spell_visual_tint_argb = snapshot.spell_visual_tint_argb,
    .spell_visual_tint_blend = static_cast<std::uint8_t>(
        std::clamp(snapshot.spell_visual_tint_blend, 0.0f, 255.0f))});

  const bool render_low_detail_world =
      openwow::world::CWorld_HasRenderFlag(
          openwow::world::WorldRenderFlag::kTerrainLowDetail);
  const bool render_distant_terrain =
      snapshot.distant_terrain_enabled && render_low_detail_world;

  const bool render_terrain = snapshot.environment.terrain_visible;
  world::Frustum terrain_frustum = frustum;
  world::Frustum low_detail_frustum{};
  world::Frustum low_detail_terrain_frustum{};
  const world::Matrix4 low_detail_view_projection = world::Multiply(
      std::span<const float, 16>{snapshot.camera.view},
      std::span<const float, 16>{low_detail_projection});
  low_detail_frustum.ExtractFromViewProj(
      std::span<const float, 16>{low_detail_view_projection});
  low_detail_terrain_frustum = low_detail_frustum;
  if (render_terrain) {
    const auto& terrain_rect = snapshot.environment.terrain_clip_rect;
    const bool full_window = terrain_rect.min_x <= -1.0f &&
                             terrain_rect.min_y <= -1.0f &&
                             terrain_rect.max_x >= 1.0f &&
                             terrain_rect.max_y >= 1.0f;
    if (!full_window) {
      const world::Matrix4 view_projection = world::Multiply(
          std::span<const float, 16>{snapshot.camera.view},
          std::span<const float, 16>{snapshot.camera.projection});

      terrain_frustum.ExtractFromViewProjWindow(
          std::span<const float, 16>{view_projection}, terrain_rect.min_x,
          terrain_rect.min_y, terrain_rect.max_x, terrain_rect.max_y);
      low_detail_terrain_frustum.ExtractFromViewProjWindow(
          std::span<const float, 16>{low_detail_view_projection},
          terrain_rect.min_x, terrain_rect.min_y, terrain_rect.max_x,
          terrain_rect.max_y);
    }
  }
  if (render_distant_terrain) {
    distant_->SetFrustum(&low_detail_terrain_frustum);
  }
  terrain_->SetFrustum(&terrain_frustum);

  const RenderMatrix4x4 world_view_projection = MultiplyMatrix4x4(
      RenderMatrix4x4View{gpu.view}, RenderMatrix4x4View{gpu.projection});

  const auto render_sky = [&] {

    if (portal_fills_ && portal_fills_->IsValid() &&
        snapshot.environment.sky_visible) {
      portal_fills_->Render(views.sky, snapshot.environment.portal_fills,
                            snapshot.environment.portal_fill_argb,
                            static_cast<std::uint16_t>(screen_width),
                            static_cast<std::uint16_t>(screen_height));
    }

    if (!snapshot.environment.sky_pass_enabled || sky_scissor_empty) {
      return;
    }

    sky_->Render(views.sky, gpu.view.data(), gpu.projection.data(),
                 pos[0], pos[1], pos[2]);

    sky_->RenderZoneSkybox(views.sky, gpu.view.data(),
                           gpu.projection.data(),
                           pos[0], pos[1], pos[2]);
  };

  const auto encode_distant_terrain = [&](bgfx::Encoder* const encoder) {
    if (render_terrain && render_distant_terrain) {
      distant_->Render(views.low_detail, env, low_detail_sort_depth, encoder);
    }
  };
  const auto encode_detailed_terrain = [&](bgfx::Encoder* const encoder) {
    if (!render_terrain) {
      return;
    }

    terrain_->Render(views.scene, env, terrain_sort_depth, encoder);
  };

  const auto resolve_wmo_placements = [&] {
    wmo_placement_scratch_.clear();
    wmo_placement_scratch_.reserve(snapshot.world_models.size());
    for (const auto& item : snapshot.world_models) {
      auto resource = models_.find(item.resource_key);
      auto instance = instances_.find(item.stable_id);
      if (resource == models_.end() || instance == instances_.end() ||
          !resource->second->renderer) {
        continue;
      }
      if (!item.visible || !instance->second.visible) {
        continue;
      }
      wmo_placement_scratch_.push_back(
          ResolvedWmoPlacement{.renderer = resource->second->renderer.get(),
                               .item = &item,
                               .instance = &instance->second});
    }
  };

  const auto encode_wmo = [&](bgfx::Encoder* const encoder) {
    for (const ResolvedWmoPlacement& placement : wmo_placement_scratch_) {
      const auto& item = *placement.item;
      auto& renderer = *placement.renderer;
      renderer.SetFogParams(env.fog);
      renderer.SetSunDirection(env.surface_to_light[0], env.surface_to_light[1],
                               env.surface_to_light[2]);
      renderer.SetLightingPalette(env.wmo);
      renderer.SetNightGlowIntensity(snapshot.wmo_night_glow);
      renderer.SetFrustum(&frustum);
      static_cast<void>(renderer.Render(
          views.wmo, gpu.view.data(), gpu.projection.data(), item.transform,
          &item.visible_subresources, item.wmo_visible_group_paths, screen_width,
          screen_height, &occlusion_buffer_, encoder));
      const auto count = std::min(item.visible_subresources.size(),
                                  placement.instance->liquids.size());
      for (std::size_t group = 0; group < count; ++group) {
        if (group > std::numeric_limits<std::uint16_t>::max() ||
            !item.visible_subresources[group] ||
            !placement.instance->liquids[group]) {
          continue;
        }
        const auto& liquid = placement.instance->liquids[group];
        if (world::IsWmoLiquidVisibleThroughGroupPaths(
                *liquid, static_cast<std::uint16_t>(group),
                item.wmo_visible_group_paths, world_view_projection)) {
          visible_wmo_liquids_scratch_.push_back(liquid);
        }
      }
    }
  };

  const auto encode_low_detail_world = [&](bgfx::Encoder* const encoder) {
    if (!render_low_detail_world) {
      return;
    }
    encode_distant_terrain(encoder);
    for (const ResolvedWmoPlacement& placement : wmo_placement_scratch_) {
      const auto& item = *placement.item;
      auto& renderer = *placement.renderer;
      renderer.SetFogParams(env.fog);
      renderer.SetSunDirection(env.surface_to_light[0], env.surface_to_light[1],
                               env.surface_to_light[2]);
      renderer.SetLightingPalette(env.wmo);
      renderer.SetNightGlowIntensity(snapshot.wmo_night_glow);
      renderer.SetFrustum(&low_detail_frustum);
      static_cast<void>(renderer.Render(
          views.low_detail, low_detail_gpu.view.data(),
          low_detail_gpu.projection.data(), item.transform, nullptr, {},
          screen_width, screen_height, nullptr, encoder, false,
          WmoGroupRenderFilter::kExteriorOnly));
    }
  };

  const auto encode_doodad_opaque = [&] {
    doodads_->SetWorldM2SceneState(env.models);
    doodads_->Render(views.doodads, gpu.view.data(), gpu.projection.data(), &frustum,
      pos[0], pos[1], pos[2], snapshot.camera.forward,
      m2::M2RenderPassScope::kOpaqueOnly);
  };

  const auto encode_doodad_alpha = [&] {
    doodads_->Render(views.alpha, gpu.view.data(), gpu.projection.data(), &frustum,
                     pos[0], pos[1], pos[2], snapshot.camera.forward,
                     m2::M2RenderPassScope::kTransparentOnly, &alpha_draw_order);
  };

  const auto encode_detail_doodads = [&] {
    if (render_terrain) {
      detail_doodads_->Render(
          views.detail_doodads, gpu.view.data(), gpu.projection.data(),
          &terrain_frustum, pos, env.models);
    }
  };

  const auto encode_water_and_weather = [&] {

    water_->SetTransientWaterHeightfields(visible_wmo_liquids_scratch_);

    water_->Render(views.water, matrices.bgfx_view(), matrices.bgfx_projection(),
                   pos, env);

    weather_renderer_->Render(views.weather, matrices.bgfx_view(),
      matrices.bgfx_projection(), {pos[0], pos[1], pos[2]}, env.fog.color);
  };

  visible_wmo_liquids_scratch_.clear();

  if constexpr (!kParallelWorldEncode) {

    render_sky();
    encode_detailed_terrain(nullptr);
    resolve_wmo_placements();
    encode_wmo(nullptr);
    encode_low_detail_world(nullptr);
    encode_doodad_opaque();
    encode_detail_doodads();
    encode_doodad_alpha();
    encode_water_and_weather();
    return;
  }

  resolve_wmo_placements();

  core::FrameJobSystem* const jobs = m2_system_.frame_job_system();
  const bool jobs_usable = jobs != nullptr && jobs->WorkerCount() > 0;

  constexpr std::uint32_t kFrameJobWorkersReservedForNestedBatches = 1u;
  const std::uint32_t dedicated_encode_budget =
      jobs_usable && jobs->WorkerCount() > kFrameJobWorkersReservedForNestedBatches
          ? jobs->WorkerCount() - kFrameJobWorkersReservedForNestedBatches
          : 0u;

  constexpr std::uint32_t kEncodersPerDedicatedJob = 1u;
  const bool dispatch_wmo_job =
      dedicated_encode_budget >= 1u &&
      ReserveBgfxFrameEncoders(kEncodersPerDedicatedJob) == kEncodersPerDedicatedJob;
  const bool dispatch_terrain_job =
      dedicated_encode_budget >= 2u &&
      ReserveBgfxFrameEncoders(kEncodersPerDedicatedJob) == kEncodersPerDedicatedJob;

  bool wmo_encoder_exhausted = false;
  bool terrain_encoder_exhausted = false;
  std::shared_ptr<core::FrameWaitGroup> wmo_wait_group;
  std::shared_ptr<core::FrameWaitGroup> terrain_wait_group;

  if (dispatch_wmo_job) {
    wmo_wait_group = jobs->SubmitRange(
        1, [&](std::size_t, std::size_t) {
          bgfx::Encoder* const encoder = bgfx::begin(true);
          if (encoder == nullptr) {
            wmo_encoder_exhausted = true;
            return;
          }
          encode_wmo(encoder);
          bgfx::end(encoder);
        });
  }
  if (dispatch_terrain_job) {
    terrain_wait_group = jobs->SubmitRange(
        1, [&](std::size_t, std::size_t) {
          bgfx::Encoder* const encoder = bgfx::begin(true);
          if (encoder == nullptr) {
            terrain_encoder_exhausted = true;
            return;
          }
          encode_detailed_terrain(encoder);
          bgfx::end(encoder);
        });
  }

  if (!dispatch_terrain_job) {
    encode_detailed_terrain(nullptr);
  }
  render_sky();
  encode_doodad_opaque();

  encode_detail_doodads();

  encode_doodad_alpha();

  if (wmo_wait_group) {
    wmo_wait_group->Wait();
    if (wmo_encoder_exhausted) {
      encode_wmo(nullptr);
    }
  } else {
    encode_wmo(nullptr);
  }

  if (terrain_wait_group) {
    terrain_wait_group->Wait();
    if (terrain_encoder_exhausted) {
      encode_detailed_terrain(nullptr);
    }
  }

  encode_low_detail_world(nullptr);
  encode_water_and_weather();
}

void WorldPresentationScene::RenderShadows(
    const world::WorldPresentationSnapshot& snapshot,
    const std::uint8_t first_shadow_view, ObjectRenderer& objects,
    const game::ObjectPresentationSnapshot& object_snapshot) {
  if (!initialized_ || snapshot.map_generation != generation_) {
    return;
  }

  WorldM2SceneState model_state{};
  model_state.light_direction = snapshot.environment.model_light_direction;
  model_state.ambient_color = snapshot.environment.model_ambient;
  model_state.diffuse_color = snapshot.environment.model_diffuse;
  model_state.fog.params = {snapshot.environment.fog_start,
                            snapshot.environment.fog_end,
                            snapshot.environment.fog_density, 0.0f};
  model_state.fog.color = snapshot.environment.fog_color;
  model_state.receives_world_shadows =
      snapshot.shadows.enabled && snapshot.shadows.quality >= 3u;
  doodads_->SetWorldM2SceneState(model_state);

  wmo_placement_scratch_.clear();
  wmo_placement_scratch_.reserve(snapshot.world_models.size());
  for (const auto& item : snapshot.world_models) {
    const auto resource = models_.find(item.resource_key);
    const auto instance = instances_.find(item.stable_id);
    if (resource == models_.end() || instance == instances_.end() ||
        !resource->second->renderer || !item.visible ||
        !instance->second.visible) {
      continue;
    }
    wmo_placement_scratch_.push_back(
        ResolvedWmoPlacement{.renderer = resource->second->renderer.get(),
                             .item = &item,
                             .instance = &instance->second});
  }

  shadows_->Render(
      snapshot, first_shadow_view, *doodads_, *terrain_, objects,
      object_snapshot,
      [&](const std::uint8_t view_id, const float* const view_mtx,
          const float* const projection_mtx,
          const world::Frustum& frustum, const RenderVec3& target,
          const std::uint16_t resolution) {
        for (const ResolvedWmoPlacement& placement :
             wmo_placement_scratch_) {
          auto& renderer = *placement.renderer;
          renderer.SetFrustum(&frustum);
          static_cast<void>(renderer.Render(
              view_id, view_mtx, projection_mtx, placement.item->transform,
              nullptr, {}, resolution, resolution, nullptr, nullptr, true));
        }
        detail_doodads_->Render(view_id, view_mtx, projection_mtx, &frustum,
                                target, model_state, nullptr, true);
      });
}

}

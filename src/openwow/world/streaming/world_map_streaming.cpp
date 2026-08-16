#include "openwow/world/streaming/world_map.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/world/coordinates/map_placement.h"
#include "openwow/world/liquid/wmo_liquid_surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::world {
namespace {

[[nodiscard]] Matrix4 BuildInverseUniformScaleTransform(const Matrix4 &model_matrix,
                                                        const float uniform_scale) {
  Matrix4 inverse{};
  math::row_major_mat4x4::BuildInverseRigidTransform4x4(inverse.data(), model_matrix.data());
  const float inverse_scale_squared = 1.0f / (uniform_scale * uniform_scale);
  for (std::size_t row = 0u; row < 3u; ++row) {
    for (std::size_t column = 0u; column < 3u; ++column) {
      inverse[row * 4u + column] *= inverse_scale_squared;
    }
  }
  const float tx = -model_matrix[12];
  const float ty = -model_matrix[13];
  const float tz = -model_matrix[14];
  inverse[12] = inverse[0] * tx + inverse[4] * ty + inverse[8] * tz;
  inverse[13] = inverse[1] * tx + inverse[5] * ty + inverse[9] * tz;
  inverse[14] = inverse[2] * tx + inverse[6] * ty + inverse[10] * tz;
  return inverse;
}

[[nodiscard]] bool IsRetailDestructibleWmoPlacement(const data::terrain::WmoPlacement &placement) {
  return (placement.flags & 0x1u) != 0u;
}

}

std::uint64_t WorldMap::TileOwnerId(const TileCoord &coord) noexcept {
  return 1u + static_cast<std::uint64_t>(coord.y * 64 + coord.x);
}

void WorldMap::RegisterWmoPlacement(PendingWmoPlacement placement,
                                    const float publication_priority) {
  if (placement.path.empty()) {
    return;
  }
  const WmoPlacementKey key = placement.key;
  const std::string path = placement.path;
  const auto registration = streaming_ownership_.Register(std::move(placement));
  if (!registration) {
    return;
  }
  if (registration.displaced.has_value()) {

    if (TryTransferWmoPlacementInstance(*registration.displaced, key)) {

      (void)streaming_ownership_.TakePending(key);
      return;
    }

  }

  if (wmo_cache_.contains(path)) {
    if (auto pending = streaming_ownership_.TakePending(key)) {
      MaterializeWmoPlacement(*pending);
    }
    streaming_ownership_.PrunePath(path);
    QueueInitialWmoGroups(path);
    return;
  }
  QueueWmoLoad(path, publication_priority);
}

void WorldMap::SynchronizeObjectWmoPlacement(
    const std::uint64_t owner, const ObjectWmoPlacement &placement) {
  if (owner == 0u || placement.path.empty() || !std::isfinite(placement.uniform_scale) ||
      placement.uniform_scale <= 0.0f) {
    RemoveObjectWmoPlacement(owner);
    return;
  }

  const auto existing = object_wmo_placements_.find(owner);
  if (existing != object_wmo_placements_.end() && existing->second == placement) {
    return;
  }

  const WmoPlacementKey key{.owner = owner, .ordinal = 0u};

  if (existing != object_wmo_placements_.end() && existing->second.path == placement.path &&
      wmo_instances_.contains(key)) {
    existing->second = placement;
    UpdateResidentObjectWmoPresentation(owner, placement);
    return;
  }

  if (existing != object_wmo_placements_.end()) {
    RemoveWmoPlacementOwner(owner);
  }
  object_wmo_placements_.insert_or_assign(owner, placement);
  RegisterWmoPlacement(
      PendingWmoPlacement{
          .key = {.owner = owner, .ordinal = 0u},
          .path = placement.path,
          .model_matrix = placement.model_matrix,
          .world_bounds = placement.world_bounds,
          .uniform_scale = placement.uniform_scale,
          .stable_id = owner,
          .additional_doodad_sets = placement.additional_doodad_sets,
          .doodad_animation_controls = placement.doodad_animation_controls,
          .visible = placement.visible,
          .object_guid = placement.object_guid,
      },
      0.0f);
}

void WorldMap::TransferObjectWmoDoodadSet(
    const std::uint64_t source_owner, const std::uint64_t destination_owner,
    const std::uint16_t source_doodad_set,
    const std::uint16_t destination_doodad_set) {

  if (source_owner == 0u || destination_owner == 0u ||
      source_owner == destination_owner || source_doodad_set == 0u ||
      destination_doodad_set == 0u) {
    return;
  }
  presentation_commands_.emplace_back(TransferWorldModelDoodadSetCommand{
      .source_doodad_owner = source_owner,
      .destination_doodad_owner = destination_owner,
      .source_doodad_set = source_doodad_set,
      .destination_doodad_set = destination_doodad_set,
  });
}

void WorldMap::UpdateResidentObjectWmoPresentation(
    const std::uint64_t owner, const ObjectWmoPlacement& placement) {
  const WmoPlacementKey key{.owner = owner, .ordinal = 0u};
  const auto active = wmo_instances_.find(key);
  if (active == wmo_instances_.end()) {
    return;
  }
  const auto cached = wmo_cache_.find(active->second.wmo_path);
  if (cached == wmo_cache_.end()) {
    return;
  }

  WmoInstance& instance = active->second;
  const std::array<float, 6> previous_world_bounds =
      instance.placement_world_bounds;
  const bool previously_visible = instance.visible;
  instance.model_matrix = placement.model_matrix;
  instance.inverse_model_matrix =
      BuildInverseUniformScaleTransform(instance.model_matrix, placement.uniform_scale);
  instance.placement_world_bounds = placement.world_bounds;
  instance.uniform_scale = placement.uniform_scale;
  instance.additional_active_wmo_doodad_sets = placement.additional_doodad_sets;
  instance.doodad_animation_controls = placement.doodad_animation_controls;
  instance.visible = placement.visible;
  instance.object_guid = placement.object_guid;

  const std::size_t group_count = cached->second.root.groupInfos.size();
  bool index_inputs_changed = instance.visible != previously_visible ||
                              instance.group_world_bounds.size() != group_count;
  instance.group_world_bounds.resize(group_count);
  const auto cell_rectangle = [](const std::array<float, 6>& world_bounds) {
    return std::pair{
        WmoAreaSpatialIndex::CellFor(world_bounds[0], world_bounds[1]),
        WmoAreaSpatialIndex::CellFor(world_bounds[3], world_bounds[4])};
  };
  for (std::size_t group_index = 0u; group_index < group_count; ++group_index) {
    const data::wmo::WmoGroupInfo& group =
        cached->second.root.groupInfos[group_index];
    const std::array<float, 6> local_bounds{
        group.boundingBox1[0], group.boundingBox1[1], group.boundingBox1[2],
        group.boundingBox2[0], group.boundingBox2[1], group.boundingBox2[2],
    };
    std::array<float, 6> world_bounds{};
    math::row_major_mat4x4::TransformAABBByRowMajorAffine4x4(
        world_bounds.data(), local_bounds.data(), instance.model_matrix.data());
    std::array<float, 6>& stored_bounds = instance.group_world_bounds[group_index];
    if (stored_bounds != world_bounds) {
      if (cell_rectangle(stored_bounds) != cell_rectangle(world_bounds)) {
        index_inputs_changed = true;
      }
      stored_bounds = world_bounds;
    }
  }

  if (index_inputs_changed) {
    wmo_area_spatial_index_.RemovePlacement(key);
    if (instance.visible) {
      for (std::size_t group_index = 0u; group_index < group_count;
           ++group_index) {
        wmo_area_spatial_index_.Add(
            WmoAreaGroupRef{key, static_cast<std::uint32_t>(group_index)},
            instance.group_world_bounds[group_index]);
      }
    }
  }

  const std::array<float, 6> moved_bounds{
      std::min(previous_world_bounds[0], placement.world_bounds[0]),
      std::min(previous_world_bounds[1], placement.world_bounds[1]),
      std::min(previous_world_bounds[2], placement.world_bounds[2]),
      std::max(previous_world_bounds[3], placement.world_bounds[3]),
      std::max(previous_world_bounds[4], placement.world_bounds[4]),
      std::max(previous_world_bounds[5], placement.world_bounds[5]),
  };
  InvalidateAreaEnvironmentCacheInBounds(moved_bounds);
  presentation_commands_.emplace_back(UpdateWorldModelInstanceCommand{
      .stable_id = instance.placement_stable_id,
      .doodad_owner = instance.doodad_owner_id,
      .transform = instance.model_matrix,
      .doodad_set = instance.active_wmo_doodad_set_id,
      .additional_doodad_sets = instance.additional_active_wmo_doodad_sets,
      .doodad_animation_controls = instance.doodad_animation_controls,
      .visible = instance.visible,
  });
}

void WorldMap::RemoveObjectWmoPlacement(const std::uint64_t owner) {
  if (owner == 0u) {
    return;
  }
  object_wmo_placements_.erase(owner);
  RemoveWmoPlacementOwner(owner);
}

std::optional<Bounds> WorldMap::EnsureObjectWmoLocalBounds(
    const std::uint64_t owner) {
  const auto placement = object_wmo_placements_.find(owner);
  if (placement == object_wmo_placements_.end()) {
    return std::nullopt;
  }

  const std::string &path = placement->second.path;
  if (!wmo_cache_.contains(path)) {
    if (!load_file_) {
      return std::nullopt;
    }

    if (auto request = pending_wmo_loads_.find(path);
        request != pending_wmo_loads_.end()) {
      if (request->second.task_id != 0u) {
        static_cast<void>(world_staging_workers_.CancelTask(request->second.task_id));
      }
      ready_wmo_loads_.erase(path);
      pending_wmo_loads_.erase(request);
    }

    std::string error;
    std::unique_ptr<CachedWmo> cached =
        PrepareWmoCpuBundle(load_file_, path, &error);
    if (!cached) {
      wmo_cpu_retry_[path].RecordFailure(world_staging_pump_sequence_, 0.0f);
      if (!error.empty()) {
        diagnostics::Log(diagnostics::LogLevel::kWarn,
                         "WorldMap: WMO " + path + " — " + error);
      }
      return std::nullopt;
    }

    wmo_cpu_retry_.erase(path);
    auto [published, inserted] = wmo_cache_.insert_or_assign(path, std::move(*cached));
    (void)inserted;

    InvalidateAreaEnvironmentCache();
    presentation_commands_.emplace_back(BeginWorldModelCommand{
        .resource_key = path,
        .root = std::make_shared<const data::wmo::WmoRoot>(published->second.root),
        .group_count = static_cast<std::uint32_t>(published->second.groups.size()),
    });
    PublishWmoPlacementsForPath(path);
    QueueInitialWmoGroups(path);
  }

  return QueryObjectWmoLocalBounds(owner);
}

std::optional<Bounds> WorldMap::QueryObjectWmoLocalBounds(
    const std::uint64_t owner) const {
  const auto placement = object_wmo_placements_.find(owner);
  if (placement == object_wmo_placements_.end()) {
    return std::nullopt;
  }
  const auto cached = wmo_cache_.find(placement->second.path);
  if (cached == wmo_cache_.end()) {
    return std::nullopt;
  }
  const Bounds bounds{
      cached->second.root.header.boundingBox1[0],
      cached->second.root.header.boundingBox1[1],
      cached->second.root.header.boundingBox1[2],
      cached->second.root.header.boundingBox2[0],
      cached->second.root.header.boundingBox2[1],
      cached->second.root.header.boundingBox2[2],
  };
  if (!std::all_of(bounds.begin(), bounds.end(),
                   [](const float value) { return std::isfinite(value); })) {
    return std::nullopt;
  }
  return bounds;
}

bool WorldMap::IsObjectWmoPlacementRenderReady(const std::uint64_t owner) const {
  const auto placement = object_wmo_placements_.find(owner);
  if (placement == object_wmo_placements_.end()) {
    return false;
  }
  const auto cached = wmo_cache_.find(placement->second.path);
  if (cached == wmo_cache_.end()) {

    return false;
  }
  for (std::size_t group_index = 0u;
       group_index < cached->second.group_residency.size(); ++group_index) {

    if (cached->second.group_residency[group_index] ==
        WmoGroupResidency::kUnrequested) {
      continue;
    }
    if (cached->second.group_residency[group_index] !=
            WmoGroupResidency::kResident ||
        group_index >= cached->second.group_gpu_publication.size() ||
        !IsWmoGroupPublicationComplete(
            cached->second.group_gpu_publication[group_index])) {
      return false;
    }
  }
  return true;
}

std::optional<std::vector<std::array<float, 4>>>
WorldMap::QueryObjectWmoConvexVolumePlanes(const std::uint64_t owner) const {
  const auto placement = object_wmo_placements_.find(owner);
  if (placement == object_wmo_placements_.end()) {
    return std::nullopt;
  }
  const auto cached = wmo_cache_.find(placement->second.path);
  if (cached == wmo_cache_.end()) {
    return std::nullopt;
  }

  const float uniform_scale = placement->second.uniform_scale;
  if (!std::isfinite(uniform_scale) || uniform_scale <= 0.0f) {
    return std::nullopt;
  }

  std::vector<std::array<float, 4>> planes;
  planes.reserve(cached->second.root.convexVolumePlanes.size());
  for (const data::wmo::WmoConvexVolumePlane &plane :
       cached->second.root.convexVolumePlanes) {
    const std::array<float, 4> entry{plane.normal[0], plane.normal[1],
                                     plane.normal[2],
                                     plane.distance * uniform_scale};
    if (!std::all_of(entry.begin(), entry.end(),
                     [](const float value) { return std::isfinite(value); })) {
      return std::nullopt;
    }
    planes.push_back(entry);
  }
  return planes;
}

void WorldMap::RegisterMapWmoPlacementOwners() {
  constexpr std::uint64_t kGlobalWdtOwner = 0u;
  constexpr std::uint64_t kFirstWdlOwner = 4097u;

  if (!has_player_streaming_focus_ && wdt_loaded_ && wdt_.has_global_wmo &&
      !wdt_.global_wmo_path.empty()) {
    const auto &placement = wdt_.global_wmo_placement;
    RegisterWmoPlacement(
        PendingWmoPlacement{
            .key = {.owner = kGlobalWdtOwner, .ordinal = 0u},
            .path = wdt_.global_wmo_path,
            .model_matrix = BuildWmoModelMatrix(placement),
            .world_bounds = BuildWmoWorldBounds(placement),
            .uniform_scale = data::terrain::DecodeWmoPlacementScale(placement.scale),
            .unique_id = placement.unique_id,
            .flags = placement.flags,
            .doodad_set = placement.doodad_set,
            .name_set = placement.name_set,
        },
        0.0f);
  }

  if (wdl_loaded_ && wdl_ != nullptr && has_player_streaming_focus_) {
    const data::terrain::WdlFile &wdl = *wdl_;
    const float stream_radius = static_cast<float>(std::max(1, view_distance_ + 1)) * kTileSize;
    const float stream_radius_squared = stream_radius * stream_radius;
    std::unordered_set<std::uint64_t> desired_owners;
    for (std::size_t ordinal = 0; ordinal < wdl.wmo_placements.size(); ++ordinal) {
      const auto &placement = wdl.wmo_placements[ordinal];
      if (placement.name_id >= wdl.wmos.size()) {
        continue;
      }
      const auto matrix = BuildWmoModelMatrix(placement);
      const auto world_bounds = BuildWmoWorldBounds(placement);
      const float dx = player_x_ < world_bounds[0]
                           ? world_bounds[0] - player_x_
                           : (player_x_ > world_bounds[3] ? player_x_ - world_bounds[3] : 0.0f);
      const float dy = player_y_ < world_bounds[1]
                           ? world_bounds[1] - player_y_
                           : (player_y_ > world_bounds[4] ? player_y_ - world_bounds[4] : 0.0f);
      const float distance_squared = dx * dx + dy * dy;
      if (distance_squared > stream_radius_squared) {
        continue;
      }
      const std::uint64_t owner = kFirstWdlOwner + ordinal;
      desired_owners.insert(owner);
      if (active_wdl_wmo_owners_.contains(owner)) {
        continue;
      }
      RegisterWmoPlacement(
          PendingWmoPlacement{
              .key = {.owner = owner, .ordinal = 0u},
              .path = wdl.wmos[placement.name_id],
              .model_matrix = matrix,
              .world_bounds = world_bounds,
              .uniform_scale = data::terrain::DecodeWmoPlacementScale(placement.scale),
              .unique_id = placement.unique_id,
              .flags = placement.flags,
              .doodad_set = placement.doodad_set,
              .name_set = placement.name_set,
          },
          distance_squared);
      active_wdl_wmo_owners_.insert(owner);
    }

    std::vector<std::uint64_t> owners_to_remove;
    for (const std::uint64_t owner : active_wdl_wmo_owners_) {
      if (!desired_owners.contains(owner)) {
        owners_to_remove.push_back(owner);
      }
    }
    for (const std::uint64_t owner : owners_to_remove) {
      RemoveWmoPlacementOwner(owner);
      active_wdl_wmo_owners_.erase(owner);
    }
  }
}

void WorldMap::RegisterTileWmoPlacementOwner(const LoadedTile &tile) {
  const std::uint64_t owner = TileOwnerId(tile.coord);
  RemoveWmoPlacementOwner(owner);
  for (const std::uint32_t placement_index : tile.adt.referenced_wmo_placement_indices) {
    if (placement_index >= tile.adt.wmo_placements.size()) {
      continue;
    }
    const auto &placement = tile.adt.wmo_placements[placement_index];
    if (placement.name_id >= tile.adt.wmos.size()) {
      continue;
    }
    const std::string &path = tile.adt.wmos[placement.name_id];
    if (IsRetailDestructibleWmoPlacement(placement)) {
      continue;
    }
    const auto world_bounds = BuildWmoWorldBounds(placement);
    const float dx = player_x_ < world_bounds[0]
                         ? world_bounds[0] - player_x_
                         : (player_x_ > world_bounds[3] ? player_x_ - world_bounds[3] : 0.0f);
    const float dy = player_y_ < world_bounds[1]
                         ? world_bounds[1] - player_y_
                         : (player_y_ > world_bounds[4] ? player_y_ - world_bounds[4] : 0.0f);
    RegisterWmoPlacement(
        PendingWmoPlacement{
            .key = {.owner = owner, .ordinal = placement_index},
            .path = path,
            .model_matrix = BuildWmoModelMatrix(placement),
            .world_bounds = world_bounds,
            .uniform_scale = data::terrain::DecodeWmoPlacementScale(placement.scale),
            .unique_id = placement.unique_id,
            .flags = placement.flags,
            .doodad_set = placement.doodad_set,
            .name_set = placement.name_set,
        },
        dx * dx + dy * dy);
  }
}

void WorldMap::MaterializeWmoPlacement(const PendingWmoPlacement &placement) {
  const auto cached = wmo_cache_.find(placement.path);
  if (cached == wmo_cache_.end() || wmo_instances_.contains(placement.key) ||
      placement.uniform_scale <= 0.0f) {
    return;
  }

  WmoInstance instance;
  instance.wmo_path = placement.path;
  instance.model_matrix = placement.model_matrix;
  instance.inverse_model_matrix =
      BuildInverseUniformScaleTransform(instance.model_matrix, placement.uniform_scale);
  instance.placement_world_bounds = placement.world_bounds;
  instance.uniform_scale = placement.uniform_scale;
  instance.active_wmo_doodad_set_id = placement.doodad_set;
  instance.additional_active_wmo_doodad_sets = placement.additional_doodad_sets;
  instance.doodad_animation_controls = placement.doodad_animation_controls;
  instance.visible = placement.visible;
  instance.name_set = placement.name_set;
  instance.placement_stable_id =
      placement.stable_id != 0u ? placement.stable_id : placement.unique_id;
  instance.placement_unique_id = placement.unique_id;
  instance.placement_flags = placement.flags;
  instance.object_guid = placement.object_guid;
  instance.collision_owner_id = 0x0200000000000000ull | next_wmo_collision_owner_id_++;
  instance.doodad_owner_id = 0x8000000000000000ull | (placement.key.owner << 20u) |
                             (static_cast<std::uint64_t>(placement.key.ordinal) + 1u);

  instance.group_world_bounds.reserve(cached->second.root.groupInfos.size());
  for (const data::wmo::WmoGroupInfo &group : cached->second.root.groupInfos) {
    const std::array<float, 6> local_bounds{
        group.boundingBox1[0], group.boundingBox1[1], group.boundingBox1[2],
        group.boundingBox2[0], group.boundingBox2[1], group.boundingBox2[2],
    };
    std::array<float, 6> world_bounds{};
    math::row_major_mat4x4::TransformAABBByRowMajorAffine4x4(
        world_bounds.data(), local_bounds.data(), instance.model_matrix.data());
    instance.group_world_bounds.push_back(world_bounds);
  }
  instance.group_liquid_surfaces.resize(cached->second.groups.size());
  instance.critical_spawn_groups.assign(cached->second.groups.size(), 0u);
  auto [published, inserted] = wmo_instances_.emplace(placement.key, std::move(instance));
  if (!inserted) {
    return;
  }
  presentation_commands_.emplace_back(PublishWorldModelInstanceCommand{
      .stable_id = published->second.placement_stable_id,
      .doodad_owner = published->second.doodad_owner_id,
      .object_guid = published->second.object_guid,
      .resource_key = published->second.wmo_path,
      .transform = published->second.model_matrix,
      .doodad_set = published->second.active_wmo_doodad_set_id,
      .additional_doodad_sets = published->second.additional_active_wmo_doodad_sets,
      .doodad_animation_controls = published->second.doodad_animation_controls,
      .visible = published->second.visible,
      .group_count = static_cast<std::uint32_t>(cached->second.groups.size())});
  if (published->second.visible) {
    for (std::size_t group_index = 0u; group_index < published->second.group_world_bounds.size();
         ++group_index) {
      AddWmoAreaGroupToIndex(placement.key, published->second, group_index);
    }
  }
  streaming_ownership_.AddActivePath(placement.path);
}

bool WorldMap::TryTransferWmoPlacementInstance(const WmoPlacementKey &from,
                                               const WmoPlacementKey &to) {
  auto node = wmo_instances_.extract(from);
  if (node.empty()) {
    return false;
  }
  wmo_area_spatial_index_.RemovePlacement(from);
  node.key() = to;
  auto inserted = wmo_instances_.insert(std::move(node));
  if (!inserted.inserted) {

    inserted.node.key() = from;
    auto restored = wmo_instances_.insert(std::move(inserted.node));
    if (restored.inserted && restored.position->second.visible) {
      for (std::size_t group_index = 0u;
           group_index < restored.position->second.group_world_bounds.size();
           ++group_index) {
        AddWmoAreaGroupToIndex(from, restored.position->second, group_index);
      }
    }
    return false;
  }
  WmoInstance &instance = inserted.position->second;
  if (instance.visible) {
    for (std::size_t group_index = 0u;
         group_index < instance.group_world_bounds.size(); ++group_index) {
      AddWmoAreaGroupToIndex(to, instance, group_index);
    }
  }

  InvalidateAreaEnvironmentCache();
  return true;
}

void WorldMap::PublishWmoPlacementsForPath(const std::string &path) {
  for (const PendingWmoPlacement &placement : streaming_ownership_.TakePendingForPath(path)) {
    MaterializeWmoPlacement(placement);
  }
}

void WorldMap::RemoveWmoPlacementOwner(const std::uint64_t owner) {
  std::unordered_set<std::string> paths_to_release;
  const auto removed_owner = streaming_ownership_.RemoveOwner(owner);
  if (removed_owner.existed) {

    std::unordered_map<std::uint32_t, const PendingWmoPlacement *> promoted_by_unique_id;
    for (const PendingWmoPlacement &promoted : removed_owner.promoted) {
      if (promoted.unique_id != 0u) {
        promoted_by_unique_id.emplace(promoted.unique_id, &promoted);
      }
    }
    std::unordered_set<const PendingWmoPlacement *> adopted;
    for (const WmoPlacementKey &key : removed_owner.keys) {
      if (auto active = wmo_instances_.find(key); active != wmo_instances_.end()) {
        if (active->second.placement_unique_id != 0u) {
          const auto handoff =
              promoted_by_unique_id.find(active->second.placement_unique_id);
          if (handoff != promoted_by_unique_id.end() &&
              TryTransferWmoPlacementInstance(key, handoff->second->key)) {

            (void)streaming_ownership_.TakePending(handoff->second->key);
            adopted.insert(handoff->second);
            continue;
          }
        }
        paths_to_release.insert(active->second.wmo_path);
        presentation_commands_.emplace_back(
            RemoveWorldModelInstanceCommand{.stable_id = active->second.placement_stable_id,
                                            .doodad_owner = active->second.doodad_owner_id});
        streaming_ownership_.RemoveActivePath(active->second.wmo_path);
        wmo_area_spatial_index_.RemovePlacement(key);
        InvalidateAreaEnvironmentCache();
        wmo_instances_.erase(active);
      }
    }
    paths_to_release.insert(removed_owner.pending_paths.begin(), removed_owner.pending_paths.end());
    for (const PendingWmoPlacement &promoted : removed_owner.promoted) {
      if (adopted.contains(&promoted)) {
        continue;
      }
      if (wmo_cache_.contains(promoted.path)) {
        if (auto pending = streaming_ownership_.TakePending(promoted.key)) {
          MaterializeWmoPlacement(*pending);
        }
        streaming_ownership_.PrunePath(promoted.path);
        QueueInitialWmoGroups(promoted.path);
      } else {
        QueueWmoLoad(promoted.path);
      }
    }
  }

  for (const std::string &path : paths_to_release) {
    MaybeReleaseWmoCache(path);
  }
}

void WorldMap::MaybeReleaseWmoCache(const std::string &path) {
  if (streaming_ownership_.HasActivePath(path) || streaming_ownership_.HasPendingForPath(path)) {
    return;
  }
  wmo_cpu_retry_.erase(path);
  streaming_ownership_.PrunePath(path);
  CancelWmoGroupLoadsForPath(path);
  if (auto request = pending_wmo_loads_.find(path); request != pending_wmo_loads_.end()) {
    if (request->second.task_id != 0u) {
      static_cast<void>(world_staging_workers_.CancelTask(request->second.task_id));
    }
    ready_wmo_loads_.erase(path);
    pending_wmo_loads_.erase(request);
  }
  if (auto cached = wmo_cache_.find(path); cached != wmo_cache_.end()) {
    presentation_commands_.emplace_back(RemoveWorldModelCommand{.resource_key = path});
    wmo_cache_.erase(cached);

    InvalidateAreaEnvironmentCache();
  }
}

}

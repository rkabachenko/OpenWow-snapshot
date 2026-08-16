#include "openwow/world/streaming/world_streaming_ownership.h"

#include <algorithm>
#include <utility>

namespace openwow::world {
namespace {

[[nodiscard]] float BoundsDistanceSquared2D(
    const std::array<float, 6>& bounds, const float x, const float y) {
  const float dx = x < bounds[0] ? bounds[0] - x
                                 : (x > bounds[3] ? x - bounds[3] : 0.0f);
  const float dy = y < bounds[1] ? bounds[1] - y
                                 : (y > bounds[4] ? y - bounds[4] : 0.0f);
  return dx * dx + dy * dy;
}

}

WorldStreamingOwnership::Registration WorldStreamingOwnership::Register(
    PendingWmoPlacement placement) {
  if (placement.path.empty() || placements_.contains(placement.key)) {
    return {};
  }
  const WmoPlacementKey key = placement.key;
  const std::string path = placement.path;
  owner_keys_[key.owner].push_back(key);
  placements_.emplace(key, placement);
  Registration registration{.canonical = true};
  if (placement.unique_id != 0u) {
    auto canonical = canonical_by_unique_id_.find(placement.unique_id);
    if (canonical != canonical_by_unique_id_.end()) {
      if (!(key < canonical->second)) {
        return {};
      }
      registration.displaced = canonical->second;
      pending_.erase(canonical->second);
      canonical->second = key;
    } else {
      canonical_by_unique_id_.emplace(placement.unique_id, key);
    }
  }
  path_keys_[path].push_back(key);
  pending_.emplace(key, std::move(placement));
  return registration;
}

std::optional<PendingWmoPlacement>
WorldStreamingOwnership::TakePending(const WmoPlacementKey &key) {
  const auto pending = pending_.find(key);
  if (pending == pending_.end()) {
    return std::nullopt;
  }
  PendingWmoPlacement placement = std::move(pending->second);
  pending_.erase(pending);
  return placement;
}

std::vector<PendingWmoPlacement>
WorldStreamingOwnership::TakePendingForPath(const std::string &path) {
  std::vector<PendingWmoPlacement> placements;
  const auto keys = path_keys_.find(path);
  if (keys == path_keys_.end()) {
    return placements;
  }
  placements.reserve(keys->second.size());
  for (const WmoPlacementKey &key : keys->second) {
    const auto pending = pending_.find(key);
    if (pending == pending_.end() || pending->second.path != path) {
      continue;
    }
    placements.push_back(std::move(pending->second));
    pending_.erase(pending);
  }
  path_keys_.erase(keys);
  return placements;
}

WorldStreamingOwnership::RemovedOwner
WorldStreamingOwnership::RemoveOwner(const std::uint64_t owner) {
  RemovedOwner removed;
  const auto keys = owner_keys_.find(owner);
  if (keys == owner_keys_.end()) {
    return removed;
  }
  removed.existed = true;
  removed.keys = std::move(keys->second);
  owner_keys_.erase(keys);
  removed.pending_paths.reserve(removed.keys.size());
  std::unordered_set<std::uint32_t> canonical_ids_to_handoff;
  for (const WmoPlacementKey &key : removed.keys) {
    const auto placement = placements_.find(key);
    if (placement != placements_.end()) {
      const std::uint32_t unique_id = placement->second.unique_id;
      if (unique_id != 0u) {
        const auto canonical = canonical_by_unique_id_.find(unique_id);
        if (canonical != canonical_by_unique_id_.end() &&
            canonical->second == key) {
          canonical_by_unique_id_.erase(canonical);
          canonical_ids_to_handoff.insert(unique_id);
        }
      }
      placements_.erase(placement);
    }
    const auto pending = pending_.find(key);
    if (pending == pending_.end()) {
      continue;
    }
    removed.pending_paths.push_back(pending->second.path);
    pending_.erase(pending);
  }
  for (const std::uint32_t unique_id : canonical_ids_to_handoff) {
    const auto replacement = std::find_if(
        placements_.begin(), placements_.end(),
        [unique_id](const auto &entry) {
          return entry.second.unique_id == unique_id;
        });
    if (replacement == placements_.end()) {
      continue;
    }
    canonical_by_unique_id_.emplace(unique_id, replacement->first);
    pending_.insert_or_assign(replacement->first, replacement->second);
    path_keys_[replacement->second.path].push_back(replacement->first);
    removed.promoted.push_back(replacement->second);
  }
  std::sort(removed.pending_paths.begin(), removed.pending_paths.end());
  removed.pending_paths.erase(
      std::unique(removed.pending_paths.begin(), removed.pending_paths.end()),
      removed.pending_paths.end());
  return removed;
}

bool WorldStreamingOwnership::HasPendingForPath(
    const std::string &path) const {
  const auto keys = path_keys_.find(path);
  if (keys == path_keys_.end()) {
    return false;
  }
  return std::any_of(keys->second.begin(), keys->second.end(),
                     [this, &path](const WmoPlacementKey &key) {
                       const auto pending = pending_.find(key);
                       return pending != pending_.end() &&
                              pending->second.path == path;
                     });
}

std::optional<float>
WorldStreamingOwnership::PendingPriorityForPath(
    const std::string &path, const float focus_x, const float focus_y) const {
  const auto keys = path_keys_.find(path);
  if (keys == path_keys_.end()) {
    return std::nullopt;
  }
  std::optional<float> best;
  for (const WmoPlacementKey &key : keys->second) {
    const auto pending = pending_.find(key);
    if (pending == pending_.end() || pending->second.path != path) {
      continue;
    }

    float distance_squared = 0.0f;
    if (key.owner != 0u) {
      distance_squared = BoundsDistanceSquared2D(
          pending->second.world_bounds, focus_x, focus_y);
    }
    best = best.has_value() ? std::min(*best, distance_squared)
                            : distance_squared;
  }
  return best;
}

bool WorldStreamingOwnership::HasPendingWithin(
    const float focus_x, const float focus_y, const float distance) const {
  const float max_distance_squared = distance * distance;
  return std::any_of(
      pending_.begin(), pending_.end(),
      [&](const auto& entry) {
        return entry.first.owner == 0u ||
               BoundsDistanceSquared2D(entry.second.world_bounds, focus_x,
                                       focus_y) <= max_distance_squared;
      });
}

void WorldStreamingOwnership::PrunePath(const std::string &path) {
  const auto keys = path_keys_.find(path);
  if (keys == path_keys_.end()) {
    return;
  }
  std::erase_if(keys->second, [this, &path](const WmoPlacementKey &key) {
    const auto pending = pending_.find(key);
    return pending == pending_.end() || pending->second.path != path;
  });
  if (keys->second.empty()) {
    path_keys_.erase(keys);
  }
}

void WorldStreamingOwnership::AddActivePath(const std::string &path) {
  ++active_path_refs_[path];
}

void WorldStreamingOwnership::RemoveActivePath(
    const std::string &path) {
  const auto refs = active_path_refs_.find(path);
  if (refs == active_path_refs_.end()) {
    return;
  }
  if (--refs->second == 0u) {
    active_path_refs_.erase(refs);
  }
}

bool WorldStreamingOwnership::HasActivePath(
    const std::string &path) const {
  return active_path_refs_.contains(path);
}

void WorldStreamingOwnership::Clear() {
  pending_.clear();
  placements_.clear();
  canonical_by_unique_id_.clear();
  path_keys_.clear();
  owner_keys_.clear();
  active_path_refs_.clear();
}

}

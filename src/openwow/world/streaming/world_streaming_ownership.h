#pragma once

#include "openwow/world/presentation/world_presentation_commands.h"

#include "openwow/world/coordinates/world_geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::world {

struct WmoPlacementKey {
  std::uint64_t owner{0};
  std::uint32_t ordinal{0};

  [[nodiscard]] friend bool operator<(const WmoPlacementKey &lhs,
                                      const WmoPlacementKey &rhs) noexcept {
    return lhs.owner < rhs.owner ||
           (lhs.owner == rhs.owner && lhs.ordinal < rhs.ordinal);
  }
  [[nodiscard]] friend bool operator==(const WmoPlacementKey &lhs,
                                       const WmoPlacementKey &rhs) noexcept {
    return lhs.owner == rhs.owner && lhs.ordinal == rhs.ordinal;
  }
};

struct PendingWmoPlacement {
  WmoPlacementKey key{};
  std::string path;
  Matrix4 model_matrix{};
  std::array<float, 6> world_bounds{};
  float uniform_scale{1.0f};
  std::uint32_t unique_id{0};

  std::uint64_t stable_id{0};
  std::uint16_t flags{0};
  std::uint16_t doodad_set{0};
  std::array<std::uint16_t, 3> additional_doodad_sets{};
  std::array<WmoDoodadAnimationControl, 2> doodad_animation_controls{};
  bool visible{true};
  std::uint16_t name_set{0};

  std::uint64_t object_guid{0};
};

class WorldStreamingOwnership {
 public:
  struct Registration {
    bool canonical{false};
    std::optional<WmoPlacementKey> displaced;

    [[nodiscard]] operator bool() const noexcept { return canonical; }
  };
  struct RemovedOwner {
    std::vector<WmoPlacementKey> keys;
    std::vector<std::string> pending_paths;
    std::vector<PendingWmoPlacement> promoted;
    bool existed{false};
  };

  [[nodiscard]] Registration Register(PendingWmoPlacement placement);
  [[nodiscard]] std::optional<PendingWmoPlacement>
  TakePending(const WmoPlacementKey &key);
  [[nodiscard]] std::vector<PendingWmoPlacement>
  TakePendingForPath(const std::string &path);
  [[nodiscard]] RemovedOwner RemoveOwner(std::uint64_t owner);

  [[nodiscard]] bool HasPendingForPath(const std::string &path) const;

  [[nodiscard]] bool HasAnyPending() const noexcept { return !pending_.empty(); }
  [[nodiscard]] std::optional<float>
  PendingPriorityForPath(const std::string &path, float focus_x,
                         float focus_y) const;
  [[nodiscard]] bool HasPendingWithin(float focus_x, float focus_y,
                                      float distance) const;
  void PrunePath(const std::string &path);

  void AddActivePath(const std::string &path);
  void RemoveActivePath(const std::string &path);
  [[nodiscard]] bool HasActivePath(const std::string &path) const;

  void Clear();

  [[nodiscard]] std::size_t pending_count() const noexcept {
    return pending_.size();
  }
  [[nodiscard]] std::size_t owner_count() const noexcept {
    return owner_keys_.size();
  }

 private:
  std::map<WmoPlacementKey, PendingWmoPlacement> pending_;
  std::map<WmoPlacementKey, PendingWmoPlacement> placements_;
  std::unordered_map<std::uint32_t, WmoPlacementKey> canonical_by_unique_id_;
  std::unordered_map<std::string, std::vector<WmoPlacementKey>> path_keys_;
  std::unordered_map<std::uint64_t, std::vector<WmoPlacementKey>> owner_keys_;
  std::unordered_map<std::string, std::size_t> active_path_refs_;
};

}

#pragma once

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/coordinates/world_geometry.h"
#include "openwow/world/occlusion/world_occluder_volumes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::world {

struct WmoVisibilityPortal {
  std::uint16_t connected_group{0xFFFFu};
  std::vector<Vec3> vertices;

  Vec4 plane_{};
};

struct WmoVisibilityGroup {
  std::uint32_t flags{0};
  std::array<float, 6> bounds{};
  std::vector<WmoVisibilityPortal> portals;
};

class WmoVisibilityData {
 public:
  [[nodiscard]] static WmoVisibilityData Build(
      const data::wmo::WmoRoot& root,
      const std::vector<data::wmo::WmoGroup>& groups);
  [[nodiscard]] static WmoVisibilityData BuildRootMetadata(
      const data::wmo::WmoRoot& root);
  void PublishGroup(const data::wmo::WmoRoot& root,
                    std::size_t group_index,
                    const data::wmo::WmoGroup& group);

  [[nodiscard]] bool empty() const { return groups_.empty(); }
  [[nodiscard]] std::size_t group_count() const { return groups_.size(); }
  [[nodiscard]] const std::vector<WmoVisibilityGroup>& groups() const {
    return groups_;
  }

 private:
  std::vector<WmoVisibilityGroup> groups_;
};

using WmoVisibilityMask = std::vector<std::uint8_t>;

struct WmoPortalClipRect {
  float min_x{-1.0f};
  float min_y{-1.0f};
  float max_x{1.0f};
  float max_y{1.0f};
};

struct WmoVisibleGroupPath {
  std::uint16_t group_index{0};
  WmoPortalClipRect clip_rect{};
};

struct WmoSkyVisibility {
  bool visible{false};
  bool show_local_skybox{false};
  WmoPortalClipRect clip_rect{};
  bool terrain_visible{false};
  WmoPortalClipRect terrain_clip_rect{};

  float depth{-1.0f};
  float terrain_depth{-1.0f};
};

struct WmoExteriorPortalFill {
  WmoPortalClipRect viewport_rect{};
  std::uint32_t first_vertex{0u};
  std::uint32_t vertex_count{0u};
};

struct WmoExteriorPortalFillBatch {
  std::vector<std::array<float, 2u>> vertices;
  std::vector<WmoExteriorPortalFill> fills;

  void clear() {
    vertices.clear();
    fills.clear();
  }
  [[nodiscard]] bool empty() const { return fills.empty(); }
};

struct WmoVisibilityTraversalFrame {
  std::uint16_t group_index{0};
  std::uint16_t predecessor_group{0xFFFFu};
  std::uint32_t depth{0};
  std::size_t next_portal{0};

  float min_x{-1.0f};
  float min_y{-1.0f};
  float max_x{1.0f};
  float max_y{1.0f};
};

struct WmoVisibilityWorkspace {
  std::vector<Vec4> clipped_portal;
  std::vector<Vec4> clip_scratch;
  std::vector<WmoVisibilityTraversalFrame> traversal_stack;
  std::vector<std::uint16_t> exterior_seed_groups;

  WmoExteriorPortalFillBatch staged_portal_fills;
  std::vector<WmoPortalClipRect> portal_fill_blockers;
  std::vector<std::array<float, 2u>> projected_portal_ndc;
  std::vector<std::array<float, 3u>> occlusion_probe;
};

enum class WmoTraversalLanes : std::uint8_t {
  kCamera,
  kExterior,
};

void ComputeVisibleWmoGroups(
    const WmoVisibilityData& visibility, const Matrix4& model_mtx,
    const Matrix4& view_projection,
    const std::array<float, 6>& placement_world_bounds,
    std::span<const std::array<float, 6>> group_world_bounds,
    const Frustum& frustum, float camera_x, float camera_y, float camera_z,
    const std::array<float, 3>& camera_forward,
    std::span<const std::uint16_t> seed_groups,
    WmoVisibilityWorkspace& workspace, WmoVisibilityMask& out_mask,
    std::vector<WmoVisibleGroupPath>* out_visible_group_paths = nullptr,
    WmoSkyVisibility* out_sky_visibility = nullptr,
    WmoExteriorPortalFillBatch* out_portal_fills = nullptr,
    WmoTraversalLanes lanes = WmoTraversalLanes::kCamera,
    const WorldOccluderVolumes* occluders = nullptr, bool append = false);

[[nodiscard]] bool IsWmoBoundsVisibleInPortalClip(
    std::span<const float, 3> bounds_min,
    std::span<const float, 3> bounds_max,
    const Matrix4& model_view_projection,
    const WmoPortalClipRect& clip_rect) noexcept;

}

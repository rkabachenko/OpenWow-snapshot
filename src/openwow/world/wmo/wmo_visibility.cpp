#include "openwow/world/wmo/wmo_visibility.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace openwow::world {

namespace {

constexpr std::uint32_t kMaximumPortalTraversalDepth = 99u;

constexpr float kRetailPortalCameraOnPlaneEpsilon = 0.01f;

constexpr std::array<std::array<std::size_t, 2u>, 3u>
    kRetailDominantAxisProjectionPairs{{{1u, 2u}, {2u, 0u}, {0u, 1u}}};

using TraversalPlane = Vec4;
using TraversalPoint = Vec3;

using ScreenRect = WmoVisibilityTraversalFrame;

[[nodiscard]] std::size_t DominantAxisIndex(const TraversalPlane& plane) {
  const float abs_x = std::abs(plane[0]);
  const float abs_y = std::abs(plane[1]);
  const bool y_over_x = abs_x <= abs_y;
  const float larger_xy = y_over_x ? abs_y : abs_x;
  if (larger_xy <= std::abs(plane[2])) return 2u;
  return y_over_x ? 1u : 0u;
}

[[nodiscard]] bool IsPointInsidePortalPolygon(
    const TraversalPoint& point, const std::vector<Vec3>& vertices,
    const std::size_t dominant_axis) {
  if (vertices.size() < 3u) return false;
  const std::size_t u = kRetailDominantAxisProjectionPairs[dominant_axis][0];
  const std::size_t v = kRetailDominantAxisProjectionPairs[dominant_axis][1];
  const float point_v = point[v];
  bool inside = false;
  std::size_t previous = vertices.size() - 1u;
  bool previous_side = point_v <= vertices[previous][v];
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    const Vec3& current = vertices[index];
    const bool side = point_v <= current[v];
    if (previous_side != side) {
      const Vec3& prior = vertices[previous];
      const bool crossing_is_right =
          (current[u] - point[u]) * (prior[v] - current[v]) <=
          (current[v] - point_v) * (prior[u] - current[u]);
      if (side == crossing_is_right) inside = !inside;
    }
    previous = index;
    previous_side = side;
  }
  return inside;
}

[[nodiscard]] TraversalPoint InverseTransformPoint(const Matrix4& model_mtx,
                                                   const TraversalPoint& world) {
  const float m00 = model_mtx[0];
  const float m01 = model_mtx[1];
  const float m02 = model_mtx[2];
  const float m10 = model_mtx[4];
  const float m11 = model_mtx[5];
  const float m12 = model_mtx[6];
  const float m20 = model_mtx[8];
  const float m21 = model_mtx[9];
  const float m22 = model_mtx[10];
  const float cofactor_00 = m11 * m22 - m12 * m21;
  const float cofactor_01 = m12 * m20 - m10 * m22;
  const float cofactor_02 = m10 * m21 - m11 * m20;
  const float determinant =
      m00 * cofactor_00 + m01 * cofactor_01 + m02 * cofactor_02;
  if (std::abs(determinant) <= 1.0e-8f) return world;

  const float cofactor_10 = m02 * m21 - m01 * m22;
  const float cofactor_11 = m00 * m22 - m02 * m20;
  const float cofactor_12 = m01 * m20 - m00 * m21;
  const float cofactor_20 = m01 * m12 - m02 * m11;
  const float cofactor_21 = m02 * m10 - m00 * m12;
  const float cofactor_22 = m00 * m11 - m01 * m10;
  const float inverse_determinant = 1.0f / determinant;
  const float dx = world[0] - model_mtx[12];
  const float dy = world[1] - model_mtx[13];
  const float dz = world[2] - model_mtx[14];
  return {
      (dx * cofactor_00 + dy * cofactor_01 + dz * cofactor_02) *
          inverse_determinant,
      (dx * cofactor_10 + dy * cofactor_11 + dz * cofactor_12) *
          inverse_determinant,
      (dx * cofactor_20 + dy * cofactor_21 + dz * cofactor_22) *
          inverse_determinant,
  };
}

[[nodiscard]] TraversalPlane TransformPlane(const Matrix4& model_mtx,
                                            const TraversalPlane& plane) {
  const float m00 = model_mtx[0];
  const float m01 = model_mtx[1];
  const float m02 = model_mtx[2];
  const float m10 = model_mtx[4];
  const float m11 = model_mtx[5];
  const float m12 = model_mtx[6];
  const float m20 = model_mtx[8];
  const float m21 = model_mtx[9];
  const float m22 = model_mtx[10];
  const float determinant =
      m00 * (m11 * m22 - m12 * m21) -
      m01 * (m10 * m22 - m12 * m20) +
      m02 * (m10 * m21 - m11 * m20);
  if (std::abs(determinant) <= 1.0e-8f) {
    return plane;
  }

  const float inverse_determinant = 1.0f / determinant;
  const float nx = ((m11 * m22 - m12 * m21) * plane[0] +
                    (m02 * m21 - m01 * m22) * plane[1] +
                    (m01 * m12 - m02 * m11) * plane[2]) *
                   inverse_determinant;
  const float ny = ((m12 * m20 - m10 * m22) * plane[0] +
                    (m00 * m22 - m02 * m20) * plane[1] +
                    (m02 * m10 - m00 * m12) * plane[2]) *
                   inverse_determinant;
  const float nz = ((m10 * m21 - m11 * m20) * plane[0] +
                    (m01 * m20 - m00 * m21) * plane[1] +
                    (m00 * m11 - m01 * m10) * plane[2]) *
                   inverse_determinant;
  const float d = plane[3] -
                  (nx * model_mtx[12] + ny * model_mtx[13] +
                   nz * model_mtx[14]);

  const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 1e-8f) {
    const float inv_len = 1.0f / len;
    return {nx * inv_len, ny * inv_len, nz * inv_len, d * inv_len};
  }

  return plane;
}

[[nodiscard]] Vec4 TransformHomogeneous(const Vec3& point,
                                        const Matrix4& matrix) {
  return {
      point[0] * matrix[0] + point[1] * matrix[4] + point[2] * matrix[8] +
          matrix[12],
      point[0] * matrix[1] + point[1] * matrix[5] + point[2] * matrix[9] +
          matrix[13],
      point[0] * matrix[2] + point[1] * matrix[6] + point[2] * matrix[10] +
          matrix[14],
      point[0] * matrix[3] + point[1] * matrix[7] + point[2] * matrix[11] +
          matrix[15],
  };
}

template <typename Distance>
bool ClipPolygon(std::vector<Vec4>& polygon, std::vector<Vec4>& scratch,
                 const Distance& distance) {
  if (polygon.empty()) return false;
  scratch.clear();
  scratch.reserve(polygon.size() + 1u);
  Vec4 previous = polygon.back();
  float previous_distance = distance(previous);
  for (const Vec4& current : polygon) {
    const float current_distance = distance(current);
    if ((previous_distance >= 0.0f) != (current_distance >= 0.0f)) {
      const float t = previous_distance / (previous_distance - current_distance);
      Vec4 intersection{};
      for (std::size_t component = 0; component < intersection.size();
           ++component) {
        intersection[component] = previous[component] +
                                  (current[component] - previous[component]) * t;
      }
      scratch.push_back(intersection);
    }
    if (current_distance >= 0.0f) scratch.push_back(current);
    previous = current;
    previous_distance = current_distance;
  }
  polygon.swap(scratch);
  return polygon.size() >= 3u;
}

bool ProjectPortalScreenRect(const WmoVisibilityPortal& portal,
                             const Matrix4& model_view_projection,
                             WmoVisibilityWorkspace& workspace,
                             ScreenRect& out_rect) {
  workspace.clipped_portal.clear();
  workspace.clipped_portal.reserve(portal.vertices.size());
  for (const Vec3& vertex : portal.vertices) {
    workspace.clipped_portal.push_back(
        TransformHomogeneous(vertex, model_view_projection));
  }
  const auto clip = [&](const auto& distance) {
    return ClipPolygon(workspace.clipped_portal, workspace.clip_scratch,
                       distance);
  };
  if (!clip([](const Vec4& v) { return v[3] + v[0]; }) ||
      !clip([](const Vec4& v) { return v[3] - v[0]; }) ||
      !clip([](const Vec4& v) { return v[3] + v[1]; }) ||
      !clip([](const Vec4& v) { return v[3] - v[1]; }) ||
      !clip([](const Vec4& v) { return v[3] + v[2]; })) {
    return false;
  }

  out_rect.min_x = std::numeric_limits<float>::max();
  out_rect.min_y = std::numeric_limits<float>::max();
  out_rect.max_x = std::numeric_limits<float>::lowest();
  out_rect.max_y = std::numeric_limits<float>::lowest();
  for (const Vec4& vertex : workspace.clipped_portal) {
    if (std::abs(vertex[3]) <= 1.0e-8f) return false;
    const float inverse_w = 1.0f / vertex[3];
    out_rect.min_x = std::min(out_rect.min_x, vertex[0] * inverse_w);
    out_rect.min_y = std::min(out_rect.min_y, vertex[1] * inverse_w);
    out_rect.max_x = std::max(out_rect.max_x, vertex[0] * inverse_w);
    out_rect.max_y = std::max(out_rect.max_y, vertex[1] * inverse_w);
  }
  return true;
}

bool IntersectPortalRectWithParent(const ScreenRect& portal_rect,
                                   const ScreenRect& parent,
                                   ScreenRect& child) {
  child.min_x = std::max(portal_rect.min_x, parent.min_x);
  child.min_y = std::max(portal_rect.min_y, parent.min_y);
  child.max_x = std::min(portal_rect.max_x, parent.max_x);
  child.max_y = portal_rect.max_y;
  return child.max_x - child.min_x >= 0.001f &&
         child.max_y - child.min_y >= 0.001f;
}

bool IsExteriorSeed(std::uint32_t flags) {
  return (flags & data::wmo::kMogpExterior) != 0u;
}

}

WmoVisibilityData WmoVisibilityData::Build(
    const data::wmo::WmoRoot& root,
    const std::vector<data::wmo::WmoGroup>& groups) {
  WmoVisibilityData visibility = BuildRootMetadata(root);
  const std::size_t count = std::min(visibility.groups_.size(), groups.size());
  for (std::size_t group_index = 0; group_index < count; ++group_index) {
    visibility.PublishGroup(root, group_index, groups[group_index]);
  }
  return visibility;
}

WmoVisibilityData WmoVisibilityData::BuildRootMetadata(
    const data::wmo::WmoRoot& root) {
  WmoVisibilityData visibility;
  visibility.groups_.resize(root.groupInfos.size());
  for (std::size_t group_index = 0; group_index < root.groupInfos.size();
       ++group_index) {
    const auto& info = root.groupInfos[group_index];
    visibility.groups_[group_index].flags = info.flags;
    visibility.groups_[group_index].bounds = {
        info.boundingBox1[0], info.boundingBox1[1], info.boundingBox1[2],
        info.boundingBox2[0], info.boundingBox2[1], info.boundingBox2[2],
    };
  }
  return visibility;
}

void WmoVisibilityData::PublishGroup(
    const data::wmo::WmoRoot& root, const std::size_t group_index,
    const data::wmo::WmoGroup& group) {
  if (group_index >= groups_.size() || group_index >= root.groupInfos.size()) {
    return;
  }
  auto& out_group = groups_[group_index];

  out_group.flags = group.header.flags;
  if (root.skyboxName.empty()) {
    out_group.flags &=
        ~static_cast<std::uint32_t>(data::wmo::kMogpShowSkybox);
  }
  out_group.portals.clear();
  out_group.portals.reserve(group.header.portalCount);
  for (std::size_t portal_ref_index = 0;
       portal_ref_index < group.header.portalCount; ++portal_ref_index) {
    const std::size_t ref_index =
        static_cast<std::size_t>(group.header.portalStart) + portal_ref_index;
    if (ref_index >= root.portalRefs.size()) {
      break;
    }

    const auto& portal_ref = root.portalRefs[ref_index];
    if (portal_ref.portalIndex >= root.portals.size()) {
      continue;
    }

    const auto& portal = root.portals[portal_ref.portalIndex];
    if (portal.nVertices == 0) {
      continue;
    }

    constexpr std::size_t kRetailProjectedPortalVertexLimit = 12u;
    const std::size_t projected_vertex_count =
        std::min<std::size_t>(portal.nVertices,
                              kRetailProjectedPortalVertexLimit);
    WmoVisibilityPortal out_portal;
    out_portal.connected_group = portal_ref.groupIndex;
    out_portal.vertices.reserve(projected_vertex_count);

    for (std::size_t vertex_index = 0;
         vertex_index < projected_vertex_count;
         ++vertex_index) {
      const std::size_t source_vertex_index =
          static_cast<std::size_t>(portal.startVertex) + vertex_index;
      if (source_vertex_index >= root.portalVertices.size()) {
        break;
      }

      const auto& source_vertex = root.portalVertices[source_vertex_index];
      out_portal.vertices.push_back({
          source_vertex.x,
          source_vertex.y,
          source_vertex.z,
      });
    }

    {
      const auto& portal_data = root.portals[portal_ref.portalIndex];
      if (portal_ref.side < 0) {
        out_portal.plane_ = {portal_data.normal[0], portal_data.normal[1],
                             portal_data.normal[2], portal_data.distance};
      } else {
        out_portal.plane_ = {-portal_data.normal[0], -portal_data.normal[1],
                             -portal_data.normal[2], -portal_data.distance};
      }
    }

    if (out_portal.vertices.size() >= 3) {
      out_group.portals.push_back(std::move(out_portal));
    }
  }
}

void ComputeVisibleWmoGroups(
    const WmoVisibilityData& visibility, const Matrix4& model_mtx,
    const Matrix4& view_projection,
    const std::array<float, 6>& placement_world_bounds,
    const std::span<const std::array<float, 6>> retained_world_bounds,
    const Frustum& frustum, const float camera_x, const float camera_y,
    const float camera_z, const std::span<const std::uint16_t> seed_groups,
    WmoVisibilityWorkspace& workspace, WmoVisibilityMask& out_mask,
    std::vector<WmoVisibleGroupPath>* const out_visible_group_paths,
    WmoSkyVisibility* const out_sky_visibility) {
  const auto& groups = visibility.groups();
  out_mask.assign(groups.size(), 0);
  if (out_visible_group_paths != nullptr) out_visible_group_paths->clear();
  if (out_sky_visibility != nullptr) *out_sky_visibility = {};
  workspace.exterior_seed_groups.clear();
  if (groups.empty() || retained_world_bounds.size() < groups.size() ||
      !frustum.TestAABB(placement_world_bounds[0], placement_world_bounds[1],
                        placement_world_bounds[2], placement_world_bounds[3],
                        placement_world_bounds[4], placement_world_bounds[5])) {
    return;
  }
  const auto world_bounds = retained_world_bounds.first(groups.size());

  workspace.exterior_seed_groups.reserve(groups.size());
  for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
    const auto& group = groups[group_index];
    if ((group.flags & data::wmo::kMogpAlwaysDraw) != 0u ||
        !IsExteriorSeed(group.flags)) {
      continue;
    }

    const auto& bounds = world_bounds[group_index];
    if (frustum.TestAABB(bounds[0], bounds[1], bounds[2], bounds[3], bounds[4],
                         bounds[5])) {
      workspace.exterior_seed_groups.push_back(
          static_cast<std::uint16_t>(group_index));
    }
  }

  const Matrix4 model_view_projection = Multiply(model_mtx, view_projection);
  workspace.traversal_stack.clear();
  workspace.traversal_stack.reserve(groups.size());
  const TraversalPoint camera_position{camera_x, camera_y, camera_z};

  const TraversalPoint model_camera_position =
      InverseTransformPoint(model_mtx, camera_position);

  const auto visit_group = [&](const std::uint16_t group_index,
                               const WmoPortalClipRect& clip_rect) {
    out_mask[group_index] = 1u;
    if (out_visible_group_paths != nullptr) {
      out_visible_group_paths->push_back({group_index, clip_rect});
    }
  };

  const auto accumulate_aperture = [](bool& visible, WmoPortalClipRect& into,
                                      const WmoPortalClipRect& rect) {
    if (!visible) {
      into = rect;
    } else {
      into.min_x = std::min(into.min_x, rect.min_x);
      into.min_y = std::min(into.min_y, rect.min_y);
      into.max_x = std::max(into.max_x, rect.max_x);
      into.max_y = std::max(into.max_y, rect.max_y);
    }
    visible = true;
  };
  const auto admit_sky = [&](const WmoPortalClipRect& clip_rect,
                             const std::uint32_t group_flags) {
    if (out_sky_visibility == nullptr) return;
    accumulate_aperture(out_sky_visibility->visible,
                        out_sky_visibility->clip_rect, clip_rect);
    out_sky_visibility->show_local_skybox |=
        (group_flags & data::wmo::kMogpShowSkybox) != 0u;
  };
  const auto admit_terrain = [&](const WmoPortalClipRect& clip_rect) {
    if (out_sky_visibility == nullptr) return;
    accumulate_aperture(out_sky_visibility->terrain_visible,
                        out_sky_visibility->terrain_clip_rect, clip_rect);
  };

  const auto traverse = [&](const std::uint16_t seed_group,
                            const bool camera_lane) {
    if (seed_group >= groups.size() ||
        (groups[seed_group].flags & data::wmo::kMogpAlwaysDraw) != 0u) {
      return;
    }
    visit_group(seed_group, {});
    workspace.traversal_stack.push_back(
        {.group_index = seed_group,
         .predecessor_group = 0xFFFFu,
         .depth = 0u,
         .next_portal = 0u});

    while (!workspace.traversal_stack.empty()) {
      auto& frame = workspace.traversal_stack.back();
      const auto& group = groups[frame.group_index];
      if (frame.next_portal >= group.portals.size()) {
        workspace.traversal_stack.pop_back();
        continue;
      }

      const auto& portal = group.portals[frame.next_portal++];
      if (portal.connected_group >= groups.size()) {
        continue;
      }
      if (portal.connected_group == frame.predecessor_group ||
          frame.depth + 1u > kMaximumPortalTraversalDepth) {
        continue;
      }

      const TraversalPlane portal_plane_world =
          TransformPlane(model_mtx, portal.plane_);
      const float camera_portal_dist =
          portal_plane_world[0] * camera_position[0] +
          portal_plane_world[1] * camera_position[1] +
          portal_plane_world[2] * camera_position[2] +
          portal_plane_world[3];
      if (camera_portal_dist > 0.0f) {
        continue;
      }

      const float model_camera_plane_distance =
          portal.plane_[0] * model_camera_position[0] +
          portal.plane_[1] * model_camera_position[1] +
          portal.plane_[2] * model_camera_position[2] + portal.plane_[3];
      const bool camera_occupies_portal =
          model_camera_plane_distance > -kRetailPortalCameraOnPlaneEpsilon &&
          model_camera_plane_distance < kRetailPortalCameraOnPlaneEpsilon &&
          IsPointInsidePortalPolygon(model_camera_position, portal.vertices,
                                     DominantAxisIndex(portal.plane_));

      ScreenRect portal_rect;
      bool portal_rect_projected = false;
      const auto project_portal_rect = [&]() {
        portal_rect_projected = ProjectPortalScreenRect(
            portal, model_view_projection, workspace, portal_rect);
        return portal_rect_projected;
      };

      ScreenRect child_rect;
      if (camera_occupies_portal) {
        child_rect.min_x = frame.min_x;
        child_rect.min_y = frame.min_y;
        child_rect.max_x = frame.max_x;
        child_rect.max_y = frame.max_y;
      } else if (!project_portal_rect() ||
                 !IntersectPortalRectWithParent(portal_rect, frame,
                                                child_rect)) {
        continue;
      }

      const std::uint32_t connected_flags =
          groups[portal.connected_group].flags;

      constexpr std::uint32_t kRetailSkyAdmissionFlags =
          data::wmo::kMogpShowSkybox | data::wmo::kMogpAlwaysDraw |
          data::wmo::kMogpShowExteriorSky | data::wmo::kMogpExteriorLit |
          data::wmo::kMogpExterior;
      static_assert(kRetailSkyAdmissionFlags == 0x00050148u);
      constexpr std::uint32_t kRetailTerrainAdmissionFlags =
          data::wmo::kMogpExterior | data::wmo::kMogpAlwaysDraw;
      static_assert(kRetailTerrainAdmissionFlags == 0x00010008u);
      if (camera_lane && (connected_flags & kRetailSkyAdmissionFlags) != 0u &&
          (portal_rect_projected || project_portal_rect())) {
        const WmoPortalClipRect aperture{portal_rect.min_x, portal_rect.min_y,
                                         portal_rect.max_x, portal_rect.max_y};
        admit_sky(aperture, connected_flags);
        if ((connected_flags & kRetailTerrainAdmissionFlags) != 0u) {
          admit_terrain(aperture);
        }
      }

      if ((connected_flags & kRetailTerrainAdmissionFlags) != 0u) {
        continue;
      }
      child_rect.group_index = portal.connected_group;
      child_rect.predecessor_group = frame.group_index;
      child_rect.depth = frame.depth + 1u;
      child_rect.next_portal = 0u;
      visit_group(portal.connected_group,
                  {child_rect.min_x, child_rect.min_y, child_rect.max_x,
                   child_rect.max_y});

      workspace.traversal_stack.push_back(child_rect);
    }
  };

  constexpr std::uint32_t kRetailCameraRoomFullSkyFlags =
      data::wmo::kMogpShowSkybox | data::wmo::kMogpShowExteriorSky |
      data::wmo::kMogpExteriorLit;
  static_assert(kRetailCameraRoomFullSkyFlags == 0x00040140u);
  for (const std::uint16_t seed_group : seed_groups) {
    if (seed_group < groups.size() &&
        (groups[seed_group].flags & kRetailCameraRoomFullSkyFlags) != 0u) {
      admit_sky({}, groups[seed_group].flags);
    }
    traverse(seed_group, true);
  }
  for (const std::uint16_t seed_group : workspace.exterior_seed_groups) {
    traverse(seed_group, false);
  }

  for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
    if ((groups[group_index].flags & data::wmo::kMogpAlwaysDraw) == 0u) {
      continue;
    }
    const auto& bounds = world_bounds[group_index];
    if (frustum.TestAABB(bounds[0], bounds[1], bounds[2], bounds[3], bounds[4],
                         bounds[5])) {
      visit_group(static_cast<std::uint16_t>(group_index), {});
    }
  }
}

bool IsWmoBoundsVisibleInPortalClip(
    const std::span<const float, 3> bounds_min,
    const std::span<const float, 3> bounds_max,
    const Matrix4& model_view_projection,
    const WmoPortalClipRect& clip_rect) noexcept {
  std::array<Vec4, 8> corners{};
  for (std::size_t corner = 0; corner < corners.size(); ++corner) {
    corners[corner] = TransformHomogeneous(
        {(corner & 1u) != 0u ? bounds_max[0] : bounds_min[0],
         (corner & 2u) != 0u ? bounds_max[1] : bounds_min[1],
         (corner & 4u) != 0u ? bounds_max[2] : bounds_min[2]},
        model_view_projection);
  }

  const auto entirely_outside = [&](const auto& signed_distance) {
    return std::all_of(corners.begin(), corners.end(),
                       [&](const Vec4& corner) {
                         return signed_distance(corner) < 0.0f;
                       });
  };

  return !entirely_outside([&](const Vec4& corner) {
           return corner[0] - clip_rect.min_x * corner[3];
         }) &&
         !entirely_outside([&](const Vec4& corner) {
           return clip_rect.max_x * corner[3] - corner[0];
         }) &&
         !entirely_outside([&](const Vec4& corner) {
           return corner[1] - clip_rect.min_y * corner[3];
         }) &&
         !entirely_outside([&](const Vec4& corner) {
           return clip_rect.max_y * corner[3] - corner[1];
         });
}

}

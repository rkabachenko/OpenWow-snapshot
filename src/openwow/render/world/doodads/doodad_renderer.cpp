#include "openwow/render/world/doodads/doodad_renderer.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/animation/animation_state.h"
#include "openwow/render/models/animation/model_light_record.h"
#include "openwow/world/coordinates/map_placement.h"
#include "openwow/world/environment/environment_detail.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

namespace openwow::render {

namespace {

constexpr std::uint32_t kInvalidWmoDoodadSetIndex = 0xFFFFFFFFu;

constexpr double kDoodadInstanceRenderMicroseconds = 1.15;

constexpr std::uint32_t kDestructibleTransitionAnimationId = AnimId::kCustom2;
constexpr std::uint32_t kDestructibleSteadyAnimationId = AnimId::kCustom0;
constexpr std::uint32_t kDestructibleLoopAnimationId = AnimId::kCustom1;

constexpr std::uint32_t kTransportShipStartAnimationId = AnimId::kShipStart;
constexpr std::uint32_t kTransportShipMovingAnimationId = AnimId::kShipMoving;
constexpr std::uint32_t kTransportShipStopAnimationId = AnimId::kShipStop;

[[nodiscard]] std::optional<std::uint32_t>
ResolveTransportAnimationRequestTransition(
    const std::uint32_t requested_animation_id) noexcept {
  switch (requested_animation_id) {
  case 0x92u:
    return 0x93u;
  case 0x94u:
    return 0x95u;
  case 0xA2u:
    return 0xA3u;
  case 0xA4u:
    return 0u;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] bool HasTransportAnimationControl(
    const std::array<world::WmoDoodadAnimationControl, 2>& controls) noexcept {
  for (const auto& control : controls) {
    if (control.doodad_set != 0u) {
      continue;
    }
    switch (control.animation) {
    case world::WmoDoodadAnimation::kTransportShipStart:
    case world::WmoDoodadAnimation::kTransportShipMoving:
    case world::WmoDoodadAnimation::kTransportShipStop:
      return true;
    default:
      break;
    }
  }
  return false;
}

constexpr std::uint32_t kRetailTransferredDoodadTargetGroupExclusionMask =
    0x410080u;

std::string NormalizePath(const std::string &raw) {
  std::string path = raw;
  for (auto &ch : path) {
    if (ch == '\\')
      ch = '/';
  }

  while (!path.empty() && (path.back() == ' ' || path.back() == '\0')) {
    path.pop_back();
  }
  return path;
}

std::string ResolveWmoDoodadPath(const std::vector<std::uint8_t> &doodad_names,
                                 const std::uint32_t name_offset_with_flags) {
  const std::uint32_t offset = name_offset_with_flags & 0x00FFFFFFu;
  if (offset >= doodad_names.size()) {
    return {};
  }

  const auto *const name = reinterpret_cast<const char *>(doodad_names.data() + offset);
  const std::size_t remaining = doodad_names.size() - offset;
  std::size_t length = 0u;
  while (length < remaining && name[length] != '\0') {
    ++length;
  }
  if (length == 0u || length == remaining) {
    return {};
  }

  return NormalizePath(std::string(name, length));
}

std::uint32_t ResolveWmoDoodadSetIndex(const data::wmo::WmoRoot &root,
                                       const std::uint16_t doodad_ref) {
  if (static_cast<std::size_t>(doodad_ref) >= root.doodadDefs.size() || root.doodadSets.empty()) {
    return kInvalidWmoDoodadSetIndex;
  }

  for (std::size_t set_index = 0; set_index < root.doodadSets.size(); ++set_index) {
    const auto &doodad_set = root.doodadSets[set_index];
    if (doodad_set.nDoodads == 0u) {
      continue;
    }

    const std::uint64_t first = doodad_set.startDoodad;
    const std::uint64_t past_last = first + doodad_set.nDoodads;
    if (static_cast<std::uint64_t>(doodad_ref) >= first &&
        static_cast<std::uint64_t>(doodad_ref) < past_last) {
      return static_cast<std::uint32_t>(set_index);
    }
  }

  return kInvalidWmoDoodadSetIndex;
}

std::array<std::uint16_t, 4> BuildWmoDoodadSetSelection(
    const std::uint16_t active_doodad_set,
    const std::array<std::uint16_t, 3>& additional_active_doodad_sets) noexcept {
  return {active_doodad_set, additional_active_doodad_sets[0],
          additional_active_doodad_sets[1], additional_active_doodad_sets[2]};
}

bool IsWmoDoodadSetSelected(
    const DoodadInstance& instance,
    const std::array<std::uint16_t, 4>& active_wmo_doodad_sets) noexcept {
  if (!instance.is_wmo_owned || instance.wmo_doodad_set_index == 0u ||
      instance.is_destructible_transfer) {
    return true;
  }
  return std::find(active_wmo_doodad_sets.begin(), active_wmo_doodad_sets.end(),
                   instance.wmo_doodad_set_index) != active_wmo_doodad_sets.end();
}

std::vector<std::uint16_t> BuildRetailTransferDestinationGroupIndices(
    const data::wmo::WmoRoot& root) {
  std::vector<std::uint16_t> indices;
  indices.reserve(root.groupInfos.size());
  for (std::size_t index = 0u;
       index < root.groupInfos.size() &&
       index <= std::numeric_limits<std::uint16_t>::max();
       ++index) {
    if ((root.groupInfos[index].flags &
         kRetailTransferredDoodadTargetGroupExclusionMask) == 0u) {
      indices.push_back(static_cast<std::uint16_t>(index));
    }
  }
  return indices;
}

[[nodiscard]] world::WmoDoodadAnimation ResolveDestructibleDoodadAnimation(
    const DoodadInstance& instance, const std::array<std::uint16_t, 4>& active_sets,
    const std::array<world::WmoDoodadAnimationControl, 2>& controls) noexcept {
  if (!IsWmoDoodadSetSelected(instance, active_sets)) {
    return world::WmoDoodadAnimation::kNone;
  }
  for (const auto& control : controls) {
    if (control.doodad_set != 0u &&
        control.doodad_set == instance.wmo_doodad_set_index) {
      return control.animation;
    }
  }
  return world::WmoDoodadAnimation::kNone;
}

[[nodiscard]] world::WmoDoodadAnimation ResolveTransportDoodadAnimation(
    const DoodadInstance& instance,
    const std::array<world::WmoDoodadAnimationControl, 2>& controls) noexcept {
  if (instance.wmo_doodad_set_index != 0u) {
    return world::WmoDoodadAnimation::kNone;
  }
  for (const auto& control : controls) {
    if (control.doodad_set != 0u) {
      continue;
    }
    switch (control.animation) {
    case world::WmoDoodadAnimation::kTransportShipStart:
    case world::WmoDoodadAnimation::kTransportShipMoving:
    case world::WmoDoodadAnimation::kTransportShipStop:
      return control.animation;
    default:
      break;
    }
  }
  return world::WmoDoodadAnimation::kNone;
}

[[nodiscard]] std::uint32_t ResolveTransportAnimationId(
    const world::WmoDoodadAnimation animation) noexcept {
  switch (animation) {
  case world::WmoDoodadAnimation::kTransportShipStart:
    return kTransportShipStartAnimationId;
  case world::WmoDoodadAnimation::kTransportShipMoving:
    return kTransportShipMovingAnimationId;
  case world::WmoDoodadAnimation::kTransportShipStop:
    return kTransportShipStopAnimationId;
  default:
    return 0u;
  }
}

[[nodiscard]] std::uint32_t ResolveDestructibleAnimationId(
    const world::WmoDoodadAnimation animation) noexcept {
  switch (animation) {
  case world::WmoDoodadAnimation::kDestructibleTransition:
    return kDestructibleTransitionAnimationId;
  case world::WmoDoodadAnimation::kDestructibleAmbientStop:
  case world::WmoDoodadAnimation::kDestructibleAmbientLoop:
  case world::WmoDoodadAnimation::kDestructibleImpact:
    return kDestructibleSteadyAnimationId;
  case world::WmoDoodadAnimation::kNone:

  case world::WmoDoodadAnimation::kTransportShipStart:
  case world::WmoDoodadAnimation::kTransportShipMoving:
  case world::WmoDoodadAnimation::kTransportShipStop:
    return 0u;
  }
  return 0u;
}

[[nodiscard]] RenderVec4 DecodeBgraColor(const std::uint32_t bgra) {
  const float scale = 1.0f / 255.0f;
  return {
      static_cast<float>((bgra >> 16) & 0xFFu) * scale,
      static_cast<float>((bgra >> 8) & 0xFFu) * scale,
      static_cast<float>(bgra & 0xFFu) * scale,
      static_cast<float>((bgra >> 24) & 0xFFu) * scale,
  };
}

struct WmoDoodadTintUsage {
  bool is_ambient_substitute{false};
};

[[nodiscard]] WmoDoodadTintUsage ResolveWmoDoodadTintUsage(
    const std::uint32_t owning_group_flags) noexcept {
  const bool is_indoor = (owning_group_flags & data::wmo::kMogpIndoor) != 0u;
  const bool is_exterior_lit =
      (owning_group_flags & (data::wmo::kMogpExterior | data::wmo::kMogpExteriorLit)) != 0u;
  return {.is_ambient_substitute = is_indoor && !is_exterior_lit};
}

bool TryTransformDoodadRenderBounds(const RenderAabb &local_bounds,
                                    const RenderMatrix4x4 &model_matrix,
                                    RenderAabb *const world_bounds) noexcept {
  if (world_bounds == nullptr) {
    return false;
  }
  math::row_major_mat4x4::TransformAABBByRowMajorAffine4x4(
      world_bounds->data(), local_bounds.data(), model_matrix.data());
  return std::all_of(world_bounds->begin(), world_bounds->end(),
                     [](const float value) { return std::isfinite(value); }) &&
         (*world_bounds)[0] <= (*world_bounds)[3] && (*world_bounds)[1] <= (*world_bounds)[4] &&
         (*world_bounds)[2] <= (*world_bounds)[5];
}

std::uint8_t SelectDoodadDistanceClass(const RenderAabb &world_bounds,
                                       const world::EnvironmentDetailDistances &detail) noexcept {
  const float extent_x = std::abs(world_bounds[3] - world_bounds[0]);
  const float extent_y = std::abs(world_bounds[4] - world_bounds[1]);
  const float extent_z = std::abs(world_bounds[5] - world_bounds[2]);
  const float size = std::max({extent_x, extent_y, extent_z});
  const auto &thresholds = detail.size_class;
  for (std::size_t index = 0; index + 1u < thresholds.size(); ++index) {
    if (size <= thresholds[index]) {
      return static_cast<std::uint8_t>(index);
    }
  }
  return static_cast<std::uint8_t>(thresholds.size() - 1u);
}

float ExtractMaximumAffineScale(const RenderMatrix4x4 &matrix) noexcept {
  const auto length = [&matrix](const std::size_t base) {
    return std::sqrt(matrix[base] * matrix[base] + matrix[base + 1u] * matrix[base + 1u] +
                     matrix[base + 2u] * matrix[base + 2u]);
  };
  return std::max({length(0u), length(4u), length(8u)});
}

float MaximumDoodadLoadingHysteresis(const world::EnvironmentDetailDistances &detail) noexcept {
  return detail.maximum.back() - detail.fade_start.back();
}

constexpr std::size_t kDoodadDistanceClassCount =
    std::tuple_size_v<decltype(world::EnvironmentDetailDistances::maximum_squared)>;

constexpr std::size_t kDoodadAdmissionCellPopulationTarget = 24u;

constexpr std::uint32_t kMaxDoodadAdmissionGridDimension = 64u;

struct DoodadAdmissionGeometry {

  RenderVec3 center{};

  RenderAabb center_bounds{};
  RenderAabb volume{};
};

[[nodiscard]] DoodadAdmissionGeometry
ResolveDoodadAdmissionGeometry(const DoodadInstance &instance) noexcept {
  DoodadAdmissionGeometry geometry;
  geometry.center = instance.position;
  if (instance.has_bounding_radius) {
    geometry.center = instance.bounding_center;
  } else if (instance.has_bounding_bounds) {
    geometry.center = {
        (instance.bounding_bounds[0] + instance.bounding_bounds[3]) * 0.5f,
        (instance.bounding_bounds[1] + instance.bounding_bounds[4]) * 0.5f,
        (instance.bounding_bounds[2] + instance.bounding_bounds[5]) * 0.5f,
    };
  }

  if (instance.has_bounding_radius && instance.bounding_radius > 0.0f) {
    const float radius = instance.bounding_radius;
    geometry.volume = {geometry.center[0] - radius, geometry.center[1] - radius,
                       geometry.center[2] - radius, geometry.center[0] + radius,
                       geometry.center[1] + radius, geometry.center[2] + radius};
  } else if (instance.has_bounding_bounds) {
    geometry.volume = instance.bounding_bounds;
  } else {
    geometry.volume = {geometry.center[0], geometry.center[1], geometry.center[2],
                       geometry.center[0], geometry.center[1], geometry.center[2]};
  }

  geometry.center_bounds = {geometry.center[0], geometry.center[1], geometry.center[2],
                            geometry.center[0], geometry.center[1], geometry.center[2]};
  for (std::size_t axis = 0; axis < 3u; ++axis) {
    geometry.volume[axis] = std::min(geometry.volume[axis], instance.position[axis]);
    geometry.volume[axis + 3u] =
        std::max(geometry.volume[axis + 3u], instance.position[axis]);
    geometry.center_bounds[axis] =
        std::min(geometry.center_bounds[axis], instance.position[axis]);
    geometry.center_bounds[axis + 3u] =
        std::max(geometry.center_bounds[axis + 3u], instance.position[axis]);
  }
  return geometry;
}

[[nodiscard]] float SquaredDistanceToAabb(const float x, const float y, const float z,
                                          const RenderAabb &box) noexcept {
  const float dx = std::max({box[0] - x, 0.0f, x - box[3]});
  const float dy = std::max({box[1] - y, 0.0f, y - box[4]});
  const float dz = std::max({box[2] - z, 0.0f, z - box[5]});
  return dx * dx + dy * dy + dz * dz;
}

void GrowAabb(RenderAabb &bounds, const RenderAabb &other) noexcept {
  for (std::size_t axis = 0; axis < 3u; ++axis) {
    bounds[axis] = std::min(bounds[axis], other[axis]);
    bounds[axis + 3u] = std::max(bounds[axis + 3u], other[axis + 3u]);
  }
}

[[nodiscard]] RenderAabb MakeEmptyAabb() noexcept {
  return {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
}

constexpr float kMaxDeferredDoodadAnimationSeconds = 3600.0f;

[[nodiscard]] bool IsDoodadAnimationEligible(
    const DoodadInstance &instance,
    const std::array<std::uint16_t, 4> &active_wmo_doodad_sets) noexcept {
  return instance.m2_instance_id != 0u &&
         (!instance.is_wmo_owned || IsWmoDoodadSetSelected(instance, active_wmo_doodad_sets));
}

[[nodiscard]] bool HasPositiveVolume(const RenderAabb &bounds) noexcept {
  return bounds[0] < bounds[3] && bounds[1] < bounds[4] && bounds[2] < bounds[5];
}

constexpr std::array<std::array<std::uint8_t, 3>, 12> kDoodadHeaderBoxTriangleCorners{{
    {{0, 4, 6}}, {{0, 6, 2}},
    {{1, 3, 7}}, {{1, 7, 5}},
    {{0, 1, 5}}, {{0, 5, 4}},
    {{2, 6, 7}}, {{2, 7, 3}},
    {{0, 2, 3}}, {{0, 3, 1}},
    {{4, 5, 7}}, {{4, 7, 6}},
}};

void EmitDoodadHeaderBoxTriangles(
    const RenderAabb &local_bounds, const RenderMatrix4x4 &model_matrix,
    const std::uint64_t owner_id, const std::uint64_t owner_guid,
    const std::function<void(const DoodadCollisionTriangle &)> &visitor) {
  const std::array<RenderVec3, 8> local_corners{{
      {local_bounds[0], local_bounds[1], local_bounds[2]},
      {local_bounds[3], local_bounds[1], local_bounds[2]},
      {local_bounds[0], local_bounds[4], local_bounds[2]},
      {local_bounds[3], local_bounds[4], local_bounds[2]},
      {local_bounds[0], local_bounds[1], local_bounds[5]},
      {local_bounds[3], local_bounds[1], local_bounds[5]},
      {local_bounds[0], local_bounds[4], local_bounds[5]},
      {local_bounds[3], local_bounds[4], local_bounds[5]},
  }};
  const RenderMatrix4x4View transform{model_matrix};
  std::array<RenderVec3, 8> world_corners{};
  for (std::size_t index = 0; index < local_corners.size(); ++index) {
    world_corners[index] =
        TransformAffinePoint4x4(RenderVec3View{local_corners[index]}, transform);
  }
  std::uint64_t facet_id = 0u;
  for (const auto &corners : kDoodadHeaderBoxTriangleCorners) {
    DoodadCollisionTriangle triangle;
    triangle.vertices[0] = world_corners[corners[0]];
    triangle.vertices[1] = world_corners[corners[1]];
    triangle.vertices[2] = world_corners[corners[2]];
    triangle.owner_id = owner_id;
    triangle.facet_id = facet_id++;
    triangle.owner_guid = owner_guid;
    visitor(triangle);
  }
}

constexpr std::uint16_t kM2PointLightType = 1u;

[[nodiscard]] SpatialPointLight MakeScenePointLight(
    const m2::M2LightSample &sample, const RenderMatrix4x4View model_world) noexcept {
  SpatialPointLight light;
  light.position = TransformAffinePoint4x4(RenderVec3View{sample.position}, model_world);
  light.diffuse = {
      sample.diffuse_color[0] * sample.diffuse_intensity,
      sample.diffuse_color[1] * sample.diffuse_intensity,
      sample.diffuse_color[2] * sample.diffuse_intensity,
  };
  light.attenuation = {
      ModelLightRecord::kRetailSceneLightScalar0,
      ModelLightRecord::kRetailSceneLightScalar1,
      ModelLightRecord::kRetailSceneLightScalar2,
  };
  return light;
}

}

float ResolveRetailDoodadDistanceAlpha(const float raw_alpha) noexcept {
  if (!std::isfinite(raw_alpha) || raw_alpha <= 0.01f) {
    return 0.0f;
  }
  if (raw_alpha > 0.99f) {
    return 1.0f;
  }
  return raw_alpha;
}

bool IsDoodadAuthoredBefore(const DoodadInstance &lhs, const DoodadInstance &rhs) noexcept {
  if (lhs.is_wmo_owned != rhs.is_wmo_owned) {
    return lhs.is_wmo_owned < rhs.is_wmo_owned;
  }
  if (lhs.render_owner_key != rhs.render_owner_key) {
    return lhs.render_owner_key < rhs.render_owner_key;
  }
  if (lhs.render_placement_index != rhs.render_placement_index) {
    return lhs.render_placement_index < rhs.render_placement_index;
  }

  return lhs.mddf_unique_id < rhs.mddf_unique_id;
}

std::array<std::uint8_t, 64>
BuildDoodadDepthBucketOrder(const m2::M2RenderPassScope pass_scope) noexcept {
  std::array<std::uint8_t, 64> order{};
  const bool back_to_front = pass_scope == m2::M2RenderPassScope::kTransparentOnly;
  for (std::size_t index = 0u; index < order.size(); ++index) {
    order[index] = static_cast<std::uint8_t>(back_to_front ? order.size() - 1u - index : index);
  }
  return order;
}

DoodadAdmission EvaluateDoodadAdmission(const DoodadInstance &instance,
                                        const world::Frustum *const frustum, const float camera_x,
                                        const float camera_y, const float camera_z,
                                        const RenderVec3 &camera_forward,
                                        const world::EnvironmentDetailDistances &lod) noexcept {
  RenderVec3 admission_center = instance.position;
  if (instance.has_bounding_radius) {
    admission_center = instance.bounding_center;
  } else if (instance.has_bounding_bounds) {
    admission_center = {
        (instance.bounding_bounds[0] + instance.bounding_bounds[3]) * 0.5f,
        (instance.bounding_bounds[1] + instance.bounding_bounds[4]) * 0.5f,
        (instance.bounding_bounds[2] + instance.bounding_bounds[5]) * 0.5f,
    };
  }

  if (frustum != nullptr) {
    bool inside_frustum = false;
    if (instance.has_bounding_radius && instance.bounding_radius > 0.0f) {
      inside_frustum = frustum->TestSphere(admission_center[0], admission_center[1],
                                           admission_center[2], instance.bounding_radius);
    } else if (instance.has_bounding_bounds) {

      inside_frustum = frustum->TestAABB(instance.bounding_bounds[0], instance.bounding_bounds[1],
                                         instance.bounding_bounds[2], instance.bounding_bounds[3],
                                         instance.bounding_bounds[4], instance.bounding_bounds[5]);
    } else {

      inside_frustum =
          frustum->TestSphere(admission_center[0], admission_center[1], admission_center[2], 0.0f);
    }
    if (!inside_frustum) {
      return {};
    }
  }

  const float dx = admission_center[0] - camera_x;
  const float dy = admission_center[1] - camera_y;
  const float dz = admission_center[2] - camera_z;
  const float distance_squared = dx * dx + dy * dy + dz * dz;
  if (!std::isfinite(distance_squared)) {
    return {};
  }

  const std::size_t distance_class =
      std::min<std::size_t>(instance.distance_class, lod.maximum_squared.size() - 1u);
  if (distance_squared >= lod.maximum_squared[distance_class]) {
    return {};
  }

  const float distance = std::sqrt(distance_squared);
  float distance_alpha = 1.0f;
  if (distance > lod.fade_start[distance_class]) {
    const float fade_width = lod.maximum[distance_class] - lod.fade_start[distance_class];
    if (!(fade_width > 0.0f)) {
      return {};
    }
    distance_alpha = ResolveRetailDoodadDistanceAlpha(
        std::clamp(1.0f - (distance - lod.fade_start[distance_class]) / fade_width, 0.0f, 1.0f));
    if (distance_alpha == 0.0f) {
      return {};
    }
  }

  const float forward_length_squared = camera_forward[0] * camera_forward[0] +
                                       camera_forward[1] * camera_forward[1] +
                                       camera_forward[2] * camera_forward[2];
  float projected_depth = 0.0f;
  if (std::isfinite(forward_length_squared) && forward_length_squared > 0.000001f) {
    const float inverse_forward_length = 1.0f / std::sqrt(forward_length_squared);
    projected_depth = (dx * camera_forward[0] + dy * camera_forward[1] + dz * camera_forward[2]) *
                      inverse_forward_length;
  }
  const float near_depth = std::max(
      0.0f, projected_depth - (instance.has_bounding_radius ? instance.bounding_radius : 0.0f));
  const auto depth_bucket =
      static_cast<std::uint8_t>(std::clamp(static_cast<int>(near_depth * 0.03f), 0, 63));
  return {
      .visible = true,
      .distance_alpha = distance_alpha,
      .depth_bucket = depth_bucket,
  };
}

void DoodadRenderer::PrepareAdmissionCache(const world::Frustum *const frustum,
                                           const float camera_x, const float camera_y,
                                           const float camera_z,
                                           const RenderVec3 &camera_forward) const {
  const AdmissionCacheKey key{
      .has_frustum = frustum != nullptr,
      .frustum_planes = frustum != nullptr ? frustum->planes
                                           : std::array<std::array<float, 4>, 6>{},
      .camera_x = camera_x,
      .camera_y = camera_y,
      .camera_z = camera_z,
      .camera_forward = camera_forward,
      .environment_detail = environment_detail_,
  };
  if (!(key == admission_cache_key_)) {
    admission_cache_key_ = key;
    ++admission_cache_epoch_;
  }
}

void DoodadRenderer::EnsureOwnerSpatialIndex(const OwnedDoodads &owner) const {
  if (owner.spatial_index_valid) {
    return;
  }
  owner.admission_groups.clear();
  owner.admission_group_members.clear();
  owner.combined_world_bounds = MakeEmptyAabb();
  owner.spatial_index_valid = true;
  const std::size_t instance_count = owner.instances.size();
  if (instance_count == 0u) {

    return;
  }

  admission_build_scratch_.clear();
  admission_build_scratch_.resize(instance_count);
  RenderAabb center_extent = MakeEmptyAabb();
  for (std::size_t index = 0; index < instance_count; ++index) {
    const DoodadInstance &instance = owner.instances[index];
    const DoodadAdmissionGeometry geometry = ResolveDoodadAdmissionGeometry(instance);
    DoodadAdmissionBuildEntry &entry = admission_build_scratch_[index];
    entry.volume = geometry.volume;
    entry.center_bounds = geometry.center_bounds;
    entry.center = geometry.center;
    entry.distance_class = static_cast<std::uint8_t>(
        std::min<std::size_t>(instance.distance_class, kDoodadDistanceClassCount - 1u));
    GrowAabb(owner.combined_world_bounds, geometry.volume);
    GrowAabb(center_extent, geometry.center_bounds);
  }

  const bool finite_extent =
      std::all_of(center_extent.begin(), center_extent.end(),
                  [](const float value) { return std::isfinite(value); });
  const float extent_x = finite_extent ? center_extent[3] - center_extent[0] : 0.0f;
  const float extent_y = finite_extent ? center_extent[4] - center_extent[1] : 0.0f;
  const float widest_horizontal_extent = std::max(extent_x, extent_y);

  const auto grid_dimension = static_cast<std::uint32_t>(std::clamp<std::size_t>(
      static_cast<std::size_t>(std::ceil(
          std::sqrt(static_cast<double>(instance_count) /
                    static_cast<double>(kDoodadAdmissionCellPopulationTarget)))),
      1u, kMaxDoodadAdmissionGridDimension));

  const float cell_pitch =
      widest_horizontal_extent > 0.0f
          ? widest_horizontal_extent / static_cast<float>(grid_dimension)
          : 0.0f;
  const float inverse_cell_pitch = cell_pitch > 0.0f ? 1.0f / cell_pitch : 0.0f;
  const auto last_cell = static_cast<std::int32_t>(grid_dimension - 1u);

  const std::size_t slot_count =
      static_cast<std::size_t>(grid_dimension) * grid_dimension * kDoodadDistanceClassCount;
  admission_slot_count_scratch_.assign(slot_count, 0u);
  for (DoodadAdmissionBuildEntry &entry : admission_build_scratch_) {
    std::int32_t cell_x = 0;
    std::int32_t cell_y = 0;
    if (inverse_cell_pitch > 0.0f) {
      cell_x = std::clamp(static_cast<std::int32_t>((entry.center[0] - center_extent[0]) *
                                                    inverse_cell_pitch),
                          0, last_cell);
      cell_y = std::clamp(static_cast<std::int32_t>((entry.center[1] - center_extent[1]) *
                                                    inverse_cell_pitch),
                          0, last_cell);
    }
    const auto cell =
        static_cast<std::size_t>(cell_y) * grid_dimension + static_cast<std::size_t>(cell_x);
    entry.group_slot = static_cast<std::uint32_t>(cell * kDoodadDistanceClassCount +
                                                  entry.distance_class);
    ++admission_slot_count_scratch_[entry.group_slot];
  }

  std::uint32_t running_offset = 0u;
  std::size_t occupied_slots = 0u;
  for (std::size_t slot = 0; slot < slot_count; ++slot) {
    const std::uint32_t population = admission_slot_count_scratch_[slot];
    admission_slot_count_scratch_[slot] = running_offset;
    running_offset += population;
    if (population != 0u) {
      ++occupied_slots;
    }
  }

  owner.admission_group_members.resize(instance_count);
  for (std::size_t index = 0; index < instance_count; ++index) {
    owner.admission_group_members[admission_slot_count_scratch_
                                      [admission_build_scratch_[index].group_slot]++] =
        static_cast<std::uint32_t>(index);
  }

  owner.admission_groups.reserve(occupied_slots);
  std::uint32_t group_first_member = 0u;
  for (std::size_t slot = 0; slot < slot_count; ++slot) {
    const std::uint32_t slot_end = admission_slot_count_scratch_[slot];
    if (slot_end == group_first_member) {
      continue;
    }
    DoodadAdmissionGroup group;
    group.first_member = group_first_member;
    group.member_count = slot_end - group_first_member;
    group.distance_class =
        static_cast<std::uint8_t>(slot % kDoodadDistanceClassCount);
    group.volume_bounds = MakeEmptyAabb();
    group.center_bounds = MakeEmptyAabb();
    for (std::uint32_t member = group_first_member; member < slot_end; ++member) {
      const DoodadAdmissionBuildEntry &entry =
          admission_build_scratch_[owner.admission_group_members[member]];
      GrowAabb(group.volume_bounds, entry.volume);
      GrowAabb(group.center_bounds, entry.center_bounds);
    }
    owner.admission_groups.push_back(group);
    group_first_member = slot_end;
  }
}

template <typename OwnerRef, typename Visitor>
void DoodadRenderer::ForEachAdmissionCandidate(
    OwnerRef &owner, const world::Frustum *const frustum, const float camera_x,
    const float camera_y, const float camera_z,
    const world::EnvironmentDetailDistances &detail, const Visitor &visit) const {
  EnsureOwnerSpatialIndex(owner);
  if (owner.admission_groups.empty()) {
    return;
  }
  if (frustum != nullptr) {
    const RenderAabb &bounds = owner.combined_world_bounds;
    if (!frustum->TestAABB(bounds[0], bounds[1], bounds[2], bounds[3], bounds[4],
                           bounds[5])) {
      return;
    }
  }
  for (const DoodadAdmissionGroup &group : owner.admission_groups) {
    if (frustum != nullptr &&
        !frustum->TestAABB(group.volume_bounds[0], group.volume_bounds[1],
                           group.volume_bounds[2], group.volume_bounds[3],
                           group.volume_bounds[4], group.volume_bounds[5])) {
      continue;
    }
    if (SquaredDistanceToAabb(camera_x, camera_y, camera_z, group.center_bounds) >=
        detail.maximum_squared[group.distance_class]) {
      continue;
    }
    const std::uint32_t group_end = group.first_member + group.member_count;
    for (std::uint32_t member = group.first_member; member < group_end; ++member) {
      visit(owner.instances[owner.admission_group_members[member]]);
    }
  }
}

DoodadAdmission DoodadRenderer::EvaluateCachedAdmission(
    const DoodadInstance &instance, const world::Frustum *const frustum, const float camera_x,
    const float camera_y, const float camera_z, const RenderVec3 &camera_forward) const {
  if (instance.admission_cache_epoch == admission_cache_epoch_) {
    return instance.admission_cache;
  }
  instance.admission_cache = EvaluateDoodadAdmission(instance, frustum, camera_x, camera_y,
                                                     camera_z, camera_forward, environment_detail_);
  instance.admission_cache_epoch = admission_cache_epoch_;
  return instance.admission_cache;
}

void DoodadRenderer::ApplyAnimationEligibility(DoodadInstance &inst,
                                               const bool is_eligible) const noexcept {
  const bool span_running = inst.animation_clock_mark != kInertDoodadAnimationClockMark;
  if (is_eligible == span_running) {
    return;
  }
  if (is_eligible) {

    inst.animation_clock_mark = animation_clock_seconds_;
    return;
  }

  inst.deferred_animation_seconds = static_cast<float>(
      std::min(static_cast<double>(inst.deferred_animation_seconds) +
                   (animation_clock_seconds_ - inst.animation_clock_mark),
               static_cast<double>(kMaxDeferredDoodadAnimationSeconds)));
  inst.animation_clock_mark = kInertDoodadAnimationClockMark;
}

DoodadRenderer::~DoodadRenderer() {
  Shutdown();
}

bool DoodadRenderer::Initialize() {
  if (initialized_)
    return true;

  const std::uint32_t hardware_threads = std::thread::hardware_concurrency();
  m2_system_.StartAsyncLoading(
      std::clamp(hardware_threads == 0u ? 2u : hardware_threads / 2u, 2u, 4u));
  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "DoodadRenderer: initialized");
  return true;
}

void DoodadRenderer::Shutdown() {
  if (!initialized_)
    return;
  Clear();
  initialized_ = false;
}

void DoodadRenderer::Clear() {
  for (auto &[key, owner] : tile_doodads_) {
    (void)key;
    for (auto &instance : owner.instances) {
      ClearM2Instance(instance);
    }
  }
  for (auto &[owner_id, owner] : wmo_doodads_) {
    (void)owner_id;
    for (auto &instance : owner.instances) {
      ClearM2Instance(instance);
    }
  }
  tile_doodads_.clear();
  wmo_doodads_.clear();
  tile_doodad_uid_refs_.clear();
  tile_referenced_uids_.clear();
  pending_load_queue_ = {};
  pending_publication_queue_.clear();
  for (auto &bucket : render_buckets_) {
    bucket.clear();
  }
  render_queue_ready_for_transparent_ = false;
  admitted_walk_scratch_.clear();
  admitted_walk_consumable_ = false;
}

void DoodadRenderer::LoadFromAdt(const data::terrain::AdtFile &adt, std::int32_t tile_x,
                                 std::int32_t tile_y) {
  const auto key = MakeTileKey(tile_x, tile_y);

  UnloadTile(tile_x, tile_y);

  if (adt.referenced_doodad_indices.empty())
    return;

  std::vector<DoodadInstance> instances;
  instances.reserve(adt.referenced_doodad_indices.size());

  for (const std::uint32_t placement_index : adt.referenced_doodad_indices) {
    if (placement_index >= adt.doodads.size())
      continue;
    const auto &placement = adt.doodads[placement_index];
    if (placement.name_id >= adt.models.size())
      continue;

    const std::string raw_path = adt.models[placement.name_id];
    if (raw_path.empty())
      continue;

    if (placement.unique_id != 0u) {
      auto &referencing_tiles = tile_doodad_uid_refs_[placement.unique_id];
      const bool first_reference =
          std::find(referencing_tiles.begin(), referencing_tiles.end(), key) ==
          referencing_tiles.end();
      if (first_reference) {
        referencing_tiles.push_back(key);
        tile_referenced_uids_[key].push_back(placement.unique_id);
      }

      if (!first_reference || referencing_tiles.front() != key) {
        continue;
      }
    }

    DoodadInstance inst;
    inst.mddf_unique_id = placement.unique_id;
    inst.model_path = NormalizePath(raw_path);
    inst.rotation[0] = placement.rotation[0];
    inst.rotation[1] = placement.rotation[1];
    inst.rotation[2] = placement.rotation[2];
    inst.scale = static_cast<float>(placement.scale) / 1024.0f;
    inst.render_owner_key = key;

    inst.render_placement_index = static_cast<std::uint32_t>(placement_index);

    ComputeModelMatrix(inst, placement);

    instances.push_back(std::move(inst));
  }

  if (!instances.empty()) {
    OwnedDoodads owner{.generation = next_owner_generation_++, .instances = std::move(instances)};
    auto [inserted, created] = tile_doodads_.insert_or_assign(key, std::move(owner));
    (void)created;
    PushPendingOwner(OwnerKind::Tile, key, inserted->second);
  }
}

void DoodadRenderer::UnloadTile(std::int32_t tile_x, std::int32_t tile_y) {
  const auto key = MakeTileKey(tile_x, tile_y);
  const auto owner = tile_doodads_.find(key);

  if (const auto referenced = tile_referenced_uids_.find(key);
      referenced != tile_referenced_uids_.end()) {
    for (const std::uint32_t unique_id : referenced->second) {
      const auto refs = tile_doodad_uid_refs_.find(unique_id);
      if (refs == tile_doodad_uid_refs_.end()) {
        continue;
      }
      auto &referencing_tiles = refs->second;
      const bool holds_instance =
          !referencing_tiles.empty() && referencing_tiles.front() == key;
      std::erase(referencing_tiles, key);
      if (referencing_tiles.empty()) {
        tile_doodad_uid_refs_.erase(refs);
        continue;
      }
      if (holds_instance && owner != tile_doodads_.end()) {
        TransferTileDoodadInstance(owner->second, unique_id,
                                   referencing_tiles.front());
      }
    }
    tile_referenced_uids_.erase(referenced);
  }
  if (owner != tile_doodads_.end()) {
    for (auto &instance : owner->second.instances) {
      ClearM2Instance(instance);
    }
    tile_doodads_.erase(owner);
  }
}

void DoodadRenderer::TransferTileDoodadInstance(
    OwnedDoodads &source, const std::uint32_t unique_id,
    const std::uint64_t destination_tile_key) {
  const auto instance_it =
      std::find_if(source.instances.begin(), source.instances.end(),
                   [unique_id](const DoodadInstance &candidate) {
                     return candidate.mddf_unique_id == unique_id;
                   });
  if (instance_it == source.instances.end()) {
    return;
  }
  auto destination = tile_doodads_.find(destination_tile_key);
  if (destination == tile_doodads_.end()) {

    destination = tile_doodads_
                      .emplace(destination_tile_key,
                               OwnedDoodads{.generation = next_owner_generation_++})
                      .first;
  }
  DoodadInstance moved = std::move(*instance_it);

  source.instances.erase(instance_it);
  moved.render_owner_key = destination_tile_key;
  destination->second.instances.push_back(std::move(moved));
  destination->second.spatial_index_valid = false;
  DoodadInstance &adopted = destination->second.instances.back();
  if (adopted.m2_instance_id == 0u) {

    const float dx = adopted.position[0] - loading_focus_[0];
    const float dy = adopted.position[1] - loading_focus_[1];
    const float dz = adopted.position[2] - loading_focus_[2];
    const PendingLoadHandle handle{
        .distance_squared = dx * dx + dy * dy + dz * dz,
        .sequence = next_pending_sequence_++,
        .owner = destination_tile_key,
        .generation = destination->second.generation,
        .instance_index =
            static_cast<std::uint32_t>(destination->second.instances.size() - 1u),
        .kind = OwnerKind::Tile,
    };
    if (adopted.m2_stream_ticket) {
      pending_publication_queue_.push_back(handle);
    } else if (!adopted.m2_load_attempted) {
      pending_load_queue_.push(handle);
    }
  }
}

void DoodadRenderer::ClearWmoInstances() {
  for (auto &[owner_id, owner] : wmo_doodads_) {
    (void)owner_id;
    for (auto &instance : owner.instances) {
      ClearM2Instance(instance);
    }
  }
  wmo_doodads_.clear();
  admitted_walk_scratch_.clear();
  admitted_walk_consumable_ = false;
}

void DoodadRenderer::LoadFromWmo(const WmoOwnerId owner_id, const data::wmo::WmoRoot &root,
                                 const std::vector<data::wmo::WmoGroup> &groups,
                                 const RenderMatrix4x4 &wmo_model_matrix,
                                 const std::uint16_t active_doodad_set,
                                 const std::array<std::uint16_t, 3> additional_active_doodad_sets,
                                 const std::uint64_t object_guid) {
  UnloadWmoInstance(owner_id);
  std::vector<DoodadInstance> instances;

  std::size_t total_refs = 0u;
  for (const auto &group : groups) {
    total_refs += group.doodadRefs.size();
  }
  instances.reserve(std::min(total_refs, root.doodadDefs.size()));
  std::unordered_map<std::uint16_t, std::size_t> instance_by_doodad_ref;
  instance_by_doodad_ref.reserve(instances.capacity());

  for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
    if (group_index > std::numeric_limits<std::uint16_t>::max()) {
      break;
    }
    const auto compact_group_index = static_cast<std::uint16_t>(group_index);
    const auto &group = groups[group_index];
    for (const auto doodad_ref : group.doodadRefs) {
      const auto doodad_set_index = ResolveWmoDoodadSetIndex(root, doodad_ref);
      if (doodad_set_index == kInvalidWmoDoodadSetIndex ||
          static_cast<std::size_t>(doodad_ref) >= root.doodadDefs.size()) {
        continue;
      }

      if (const auto existing = instance_by_doodad_ref.find(doodad_ref);
          existing != instance_by_doodad_ref.end()) {
        auto &group_indices = instances[existing->second].wmo_group_indices;
        if (group_indices.empty() || group_indices.back() != compact_group_index) {
          group_indices.push_back(compact_group_index);
        }
        continue;
      }

      const auto &doodad_def = root.doodadDefs[doodad_ref];
      const std::string model_path = ResolveWmoDoodadPath(root.doodadNames, doodad_def.nameOffset);
      if (model_path.empty()) {
        continue;
      }

      DoodadInstance instance;
      instance.model_path = model_path;
      instance.scale = doodad_def.scale;
      instance.is_wmo_owned = true;
      instance.wmo_group_indices.push_back(compact_group_index);
      instance.render_owner_key = owner_id;
      instance.object_guid = object_guid;
      instance.render_placement_index = doodad_ref;
      instance.wmo_doodad_set_index = static_cast<std::uint16_t>(doodad_set_index);
      instance.authored_wmo_doodad_set_index = instance.wmo_doodad_set_index;
      const auto tint_usage = ResolveWmoDoodadTintUsage(group.header.flags);
      if (tint_usage.is_ambient_substitute) {
        instance.tint_color = DecodeBgraColor(doodad_def.color);
        instance.wmo_color_is_ambient_substitute = true;
      }
      ComputeWmoModelMatrix(instance, doodad_def, wmo_model_matrix);
      instance_by_doodad_ref.emplace(doodad_ref, instances.size());
      instances.push_back(std::move(instance));
    }
  }

  OwnedDoodads owner{
      .generation = next_owner_generation_++,
      .object_guid = object_guid,
      .instances = std::move(instances),
      .group_count = groups.size(),
      .active_wmo_doodad_sets =
          BuildWmoDoodadSetSelection(active_doodad_set, additional_active_doodad_sets),
      .transfer_destination_group_indices =
          BuildRetailTransferDestinationGroupIndices(root),
      .wmo_instance_by_doodad_ref = std::move(instance_by_doodad_ref),
  };
  auto [inserted, created] = wmo_doodads_.insert_or_assign(owner_id, std::move(owner));
  (void)created;
  PushPendingOwner(OwnerKind::Wmo, owner_id, inserted->second);
}

void DoodadRenderer::BeginStreamingWmoInstance(
    const WmoOwnerId owner_id, const std::size_t group_count,
    const std::uint16_t active_doodad_set,
    const std::array<std::uint16_t, 3> additional_active_doodad_sets,
    const std::uint64_t object_guid) {
  UnloadWmoInstance(owner_id);
  OwnedDoodads owner{
      .generation = next_owner_generation_++,
      .object_guid = object_guid,
      .group_count = group_count,
      .active_wmo_doodad_sets =
          BuildWmoDoodadSetSelection(active_doodad_set, additional_active_doodad_sets),
  };
  wmo_doodads_.insert_or_assign(owner_id, std::move(owner));
}

void DoodadRenderer::PublishStreamingWmoGroup(
    const WmoOwnerId owner_id, const data::wmo::WmoRoot &root, const data::wmo::WmoGroup &group,
    const std::uint16_t group_index, const RenderMatrix4x4 &wmo_model_matrix) {
  const auto owner_it = wmo_doodads_.find(owner_id);
  if (owner_it == wmo_doodads_.end() || group_index >= owner_it->second.group_count) {
    return;
  }
  auto &owner = owner_it->second;
  for (const std::uint16_t doodad_ref : group.doodadRefs) {
    const auto doodad_set_index = ResolveWmoDoodadSetIndex(root, doodad_ref);
    if (doodad_set_index == kInvalidWmoDoodadSetIndex ||
        static_cast<std::size_t>(doodad_ref) >= root.doodadDefs.size()) {
      continue;
    }
    if (const auto existing = owner.wmo_instance_by_doodad_ref.find(doodad_ref);
        existing != owner.wmo_instance_by_doodad_ref.end()) {
      auto &group_indices = owner.instances[existing->second].wmo_group_indices;
      if (std::find(group_indices.begin(), group_indices.end(), group_index) ==
          group_indices.end()) {
        group_indices.push_back(group_index);
      }
      continue;
    }

    const auto &doodad_def = root.doodadDefs[doodad_ref];
    const std::string model_path = ResolveWmoDoodadPath(root.doodadNames, doodad_def.nameOffset);
    if (model_path.empty()) {
      continue;
    }
    DoodadInstance instance;
    instance.model_path = model_path;
    instance.scale = doodad_def.scale;
    instance.is_wmo_owned = true;
    instance.wmo_group_indices.push_back(group_index);
    instance.render_owner_key = owner_id;
    instance.object_guid = owner.object_guid;
    instance.render_placement_index = doodad_ref;
    instance.wmo_doodad_set_index = static_cast<std::uint16_t>(doodad_set_index);
    instance.authored_wmo_doodad_set_index = instance.wmo_doodad_set_index;
    const auto tint_usage = ResolveWmoDoodadTintUsage(group.header.flags);
    if (tint_usage.is_ambient_substitute) {
      instance.tint_color = DecodeBgraColor(doodad_def.color);
      instance.wmo_color_is_ambient_substitute = true;
    }
    ComputeWmoModelMatrix(instance, doodad_def, wmo_model_matrix);
    owner.wmo_instance_by_doodad_ref.emplace(doodad_ref, owner.instances.size());
    owner.instances.push_back(std::move(instance));
  }
  owner.spatial_index_valid = false;
  PushPendingOwner(OwnerKind::Wmo, owner_id, owner);
}

void DoodadRenderer::UnloadWmoInstance(const WmoOwnerId owner_id) {
  const auto owner = wmo_doodads_.find(owner_id);
  if (owner == wmo_doodads_.end()) {
    return;
  }
  for (auto &instance : owner->second.instances) {
    ClearM2Instance(instance);
  }
  wmo_doodads_.erase(owner);
}

void DoodadRenderer::SetWmoInstanceDoodadSets(
    const WmoOwnerId owner_id, const std::uint16_t active_doodad_set,
    const std::array<std::uint16_t, 3> additional_active_doodad_sets) {
  const auto owner = wmo_doodads_.find(owner_id);
  if (owner == wmo_doodads_.end()) {
    return;
  }
  const auto next =
      BuildWmoDoodadSetSelection(active_doodad_set, additional_active_doodad_sets);
  if (owner->second.active_wmo_doodad_sets == next) {
    return;
  }
  owner->second.active_wmo_doodad_sets = next;

  AdvanceCollisionRevision();
  for (DoodadInstance& instance : owner->second.instances) {

    ApplyAnimationEligibility(
        instance, IsDoodadAnimationEligible(instance, owner->second.active_wmo_doodad_sets));
    SynchronizeDestructibleAnimation(instance, owner->second);
    SynchronizeTransportAnimation(instance, owner->second);
  }
}

void DoodadRenderer::SetWmoInstanceDoodadAnimations(
    const WmoOwnerId owner_id,
    const std::array<world::WmoDoodadAnimationControl, 2> controls) {
  const auto owner = wmo_doodads_.find(owner_id);
  if (owner == wmo_doodads_.end() || owner->second.doodad_animation_controls == controls) {
    return;
  }
  owner->second.doodad_animation_controls = controls;
  for (DoodadInstance& instance : owner->second.instances) {
    SynchronizeDestructibleAnimation(instance, owner->second);
    SynchronizeTransportAnimation(instance, owner->second);
  }
}

void DoodadRenderer::SetWmoInstanceTransferDestinationGroups(
    const WmoOwnerId owner_id, const data::wmo::WmoRoot& root) {
  const auto owner = wmo_doodads_.find(owner_id);
  if (owner == wmo_doodads_.end()) {
    return;
  }
  owner->second.transfer_destination_group_indices =
      BuildRetailTransferDestinationGroupIndices(root);
}

void DoodadRenderer::TransferWmoDoodadSet(
    const WmoOwnerId source_owner_id, const WmoOwnerId destination_owner_id,
    const std::uint16_t source_doodad_set,
    const std::uint16_t destination_doodad_set) {
  if (source_owner_id == destination_owner_id || source_doodad_set == 0u ||
      destination_doodad_set == 0u) {
    return;
  }
  const auto source_it = wmo_doodads_.find(source_owner_id);
  const auto destination_it = wmo_doodads_.find(destination_owner_id);
  if (source_it == wmo_doodads_.end() || destination_it == wmo_doodads_.end()) {
    return;
  }
  OwnedDoodads& source = source_it->second;
  OwnedDoodads& destination = destination_it->second;
  if (destination.transfer_destination_group_indices.empty()) {
    return;
  }

  std::vector<DoodadInstance> transferred;
  transferred.reserve(source.instances.size());
  auto retained = source.instances.begin();
  for (auto current = source.instances.begin(); current != source.instances.end(); ++current) {
    if (current->is_wmo_owned &&
        current->wmo_doodad_set_index == source_doodad_set) {
      transferred.push_back(std::move(*current));
    } else {
      if (retained != current) {
        *retained = std::move(*current);
      }
      ++retained;
    }
  }
  source.instances.erase(retained, source.instances.end());
  if (transferred.empty()) {
    return;
  }

  source.wmo_instance_by_doodad_ref.clear();
  for (std::size_t index = 0u; index < source.instances.size(); ++index) {
    source.wmo_instance_by_doodad_ref.emplace(
        static_cast<std::uint16_t>(source.instances[index].render_placement_index), index);
  }
  ++source.generation;
  if (source.generation == 0u) {
    source.generation = next_owner_generation_++;
  }

  for (DoodadInstance& instance : transferred) {

    instance.wmo_group_indices = destination.transfer_destination_group_indices;
    instance.render_owner_key = destination_owner_id;

    instance.object_guid = destination.object_guid;
    instance.wmo_doodad_set_index = destination_doodad_set;
    instance.is_destructible_transfer = true;

    ApplyAnimationEligibility(
        instance, IsDoodadAnimationEligible(instance, destination.active_wmo_doodad_sets));
    if (destination_doodad_set == instance.authored_wmo_doodad_set_index &&
        !destination.wmo_instance_by_doodad_ref.contains(
            static_cast<std::uint16_t>(instance.render_placement_index))) {
      destination.wmo_instance_by_doodad_ref.emplace(
          static_cast<std::uint16_t>(instance.render_placement_index),
          destination.instances.size());
    }
    destination.instances.push_back(std::move(instance));
  }
  source.spatial_index_valid = false;
  destination.spatial_index_valid = false;
  AdvanceCollisionRevision();
  PushPendingOwner(OwnerKind::Wmo, source_owner_id, source);
  PushPendingOwner(OwnerKind::Wmo, destination_owner_id, destination);
}

void DoodadRenderer::SetWmoInstanceEnabled(const WmoOwnerId owner_id,
                                           const bool enabled) {
  const auto owner = wmo_doodads_.find(owner_id);
  if (owner == wmo_doodads_.end() || owner->second.enabled == enabled) {
    return;
  }
  owner->second.enabled = enabled;

  AdvanceCollisionRevision();
}

void DoodadRenderer::SetWmoInstanceTransform(
    const WmoOwnerId owner_id, const RenderMatrix4x4& wmo_model_matrix) {
  const auto owner = wmo_doodads_.find(owner_id);
  if (owner == wmo_doodads_.end()) {
    return;
  }

  bool moved_collision = false;
  for (DoodadInstance& instance : owner->second.instances) {
    if (instance.is_destructible_transfer) {
      continue;
    }
    instance.model_matrix =
        MultiplyMatrix4x4(instance.wmo_local_model_matrix, wmo_model_matrix);
    instance.model_matrix_revision = next_model_matrix_revision_++;
    instance.position = {instance.model_matrix[12], instance.model_matrix[13],
                         instance.model_matrix[14]};
    if (instance.collision_geometry) {
      moved_collision = true;
    }
    RefreshSpatialBounds(instance);
  }
  owner->second.spatial_index_valid = false;
  if (moved_collision) {
    AdvanceCollisionRevision();
  }
}

void DoodadRenderer::ClearM2Instance(DoodadInstance &inst) {

  const bool removed_collision =
      static_cast<bool>(inst.collision_geometry) ||
      (!inst.collision_ready && HasPositiveVolume(inst.header_local_bounds));
  if (inst.m2_instance_id != 0u) {
    static_cast<void>(m2_system_.ClearTriggeredEventCallback(inst.m2_instance_id));
    static_cast<void>(m2_system_.ClearAnimationRequestCallback(inst.m2_instance_id));
    const auto status = m2_system_.DestroyInstance(inst.m2_instance_id);
    if (status != m2::M2ResultStatus::kReady) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       std::string("DoodadRenderer: M2 instance destroy ") +
                           m2::M2ResultStatusName(status));
    }
  }
  if (inst.m2_stream_ticket) {
    m2_system_.ReleaseModelAsync(inst.m2_stream_ticket);
    inst.m2_stream_ticket = {};
  }

  ApplyAnimationEligibility(inst, false);
  inst.m2_instance_id = 0u;
  inst.m2_model_id = 0u;
  inst.shadow_class_memo = -1;
  inst.destructible_animation = world::WmoDoodadAnimation::kNone;
  inst.transport_animation = world::WmoDoodadAnimation::kNone;
  inst.transport_animation_callback_installed = false;
  inst.render_ready_latched = false;
  inst.collision_geometry.reset();
  inst.bounding_center = {};
  inst.bounding_radius = 0.0f;
  inst.bounding_bounds = {};
  inst.has_bounding_radius = false;
  inst.has_bounding_bounds = false;

  inst.header_local_bounds = {};
  inst.collision_ready = false;

  inst.m2_load_attempted = false;

  inst.admission_cache_epoch = 0u;
  if (removed_collision) {
    AdvanceCollisionRevision();
  }
}

void DoodadRenderer::AdvanceCollisionRevision() noexcept {
  ++collision_revision_;
  if (collision_revision_ == 0u) {
    collision_revision_ = 1u;
  }
}

void DoodadRenderer::AdoptEarlyCollisionData(DoodadInstance& inst,
                                             const m2::M2StreamQuery& streamed) {
  if (inst.collision_ready) {

    return;
  }

  if (streamed.header_bounds_ready && !HasPositiveVolume(inst.header_local_bounds)) {
    const RenderAabb early_bounds = streamed.spatial_info.local_bounds;
    if (HasPositiveVolume(early_bounds)) {
      inst.header_local_bounds = early_bounds;
      AdvanceCollisionRevision();
    }
  }

  if (!streamed.cpu_model_ready) {
    return;
  }
  inst.header_local_bounds = streamed.spatial_info.local_bounds;
  inst.collision_geometry = streamed.collision_geometry;
  inst.collision_ready = true;

  AdvanceCollisionRevision();
}

bool DoodadRenderer::PublishPreparedM2Instance(DoodadInstance &inst, const std::uint32_t model_id,
                                               const std::uint32_t instance_id) {
  auto &m2_system = m2_system_;
  if (model_id == 0u || instance_id == 0u) {
    return false;
  }

  inst.m2_model_id = model_id;
  inst.m2_instance_id = instance_id;
  inst.shadow_class_memo = -1;

  if (!inst.collision_ready) {
    const auto collision_geometry = m2_system.QueryModelCollisionGeometry(model_id);
    if (collision_geometry.status == m2::M2ResultStatus::kReady && collision_geometry.geometry) {
      inst.collision_geometry = collision_geometry.geometry;
      inst.collision_ready = true;
      AdvanceCollisionRevision();
    }
  }
  RefreshSpatialBounds(inst);

  if (inst.is_wmo_owned) {
    if (const auto owner = wmo_doodads_.find(inst.render_owner_key);
        owner != wmo_doodads_.end()) {
      owner->second.spatial_index_valid = false;
      ApplyAnimationEligibility(
          inst, IsDoodadAnimationEligible(inst, owner->second.active_wmo_doodad_sets));
      SynchronizeDestructibleAnimation(inst, owner->second);
      SynchronizeTransportAnimation(inst, owner->second);
    }
  } else if (const auto owner = tile_doodads_.find(inst.render_owner_key);
             owner != tile_doodads_.end()) {
    owner->second.spatial_index_valid = false;
    ApplyAnimationEligibility(
        inst, IsDoodadAnimationEligible(inst, owner->second.active_wmo_doodad_sets));
  }
  return true;
}

void DoodadRenderer::SynchronizeDestructibleAnimation(
    DoodadInstance& inst, const OwnedDoodads& owner) {
  if (inst.is_destructible_transfer) {

    return;
  }
  const world::WmoDoodadAnimation desired = ResolveDestructibleDoodadAnimation(
      inst, owner.active_wmo_doodad_sets, owner.doodad_animation_controls);
  if (inst.m2_instance_id == 0u || inst.destructible_animation == desired) {
    return;
  }

  static_cast<void>(m2_system_.ClearTriggeredEventCallback(inst.m2_instance_id));
  static_cast<void>(m2_system_.ClearAnimationRequestCallback(inst.m2_instance_id));
  if (desired == world::WmoDoodadAnimation::kNone) {
    if (inst.destructible_animation != world::WmoDoodadAnimation::kNone) {
      static_cast<void>(m2_system_.SetAnimation(inst.m2_instance_id, 0u));
    }
    inst.destructible_animation = desired;
    return;
  }

  const WmoOwnerId owner_id = inst.render_owner_key;
  static_cast<void>(m2_system_.SetTriggeredEventCallback(
      inst.m2_instance_id, [this, owner_id](const m2::M2TriggeredEvent& event) {
        if (wmo_doodad_m2_event_sink_) {
          wmo_doodad_m2_event_sink_({.owner = owner_id, .event = event});
        }
      }));
  static_cast<void>(m2_system_.SetAnimationRequestCallback(
      inst.m2_instance_id, [this, instance_id = inst.m2_instance_id, desired](
                               const m2::M2AnimationRequestEvent &event) {
        if (event.resolved_sub_animation_index != 0) {
          return;
        }
        switch (desired) {
        case world::WmoDoodadAnimation::kDestructibleTransition:

          if (event.requested_animation_id == kDestructibleSteadyAnimationId ||
              event.requested_animation_id == kDestructibleLoopAnimationId) {
            static_cast<void>(m2_system_.SetAnimation(
                instance_id, kDestructibleTransitionAnimationId));
          } else if (event.requested_animation_id ==
                     kDestructibleTransitionAnimationId) {
            static_cast<void>(m2_system_.SetAnimation(instance_id, 0u));
          }
          break;
        case world::WmoDoodadAnimation::kDestructibleAmbientStop:
          if (event.requested_animation_id != 0u) {
            static_cast<void>(m2_system_.SetAnimation(instance_id, 0u));
          }
          break;
        case world::WmoDoodadAnimation::kDestructibleAmbientLoop:
          if (event.requested_animation_id != kDestructibleLoopAnimationId) {
            static_cast<void>(m2_system_.SetAnimation(
                instance_id, kDestructibleLoopAnimationId));
          }
          break;
        case world::WmoDoodadAnimation::kDestructibleImpact:

          static_cast<void>(m2_system_.SetAnimation(
              instance_id, event.requested_animation_id ==
                                   kDestructibleSteadyAnimationId
                               ? kDestructibleLoopAnimationId
                               : 0u));
          break;
        case world::WmoDoodadAnimation::kNone:

        case world::WmoDoodadAnimation::kTransportShipStart:
        case world::WmoDoodadAnimation::kTransportShipMoving:
        case world::WmoDoodadAnimation::kTransportShipStop:
          break;
        }
      }));
  static_cast<void>(m2_system_.SetAnimation(
      inst.m2_instance_id, ResolveDestructibleAnimationId(desired)));
  inst.destructible_animation = desired;
}

void DoodadRenderer::SynchronizeTransportAnimation(DoodadInstance& inst,
                                                   const OwnedDoodads& owner) {
  if (inst.m2_instance_id == 0u || inst.wmo_doodad_set_index != 0u ||
      !HasTransportAnimationControl(owner.doodad_animation_controls)) {
    return;
  }

  if (!inst.transport_animation_callback_installed) {

    const auto readiness = m2_system_.QueryInstanceReadiness(inst.m2_instance_id);
    if (readiness.status == m2::M2ResultStatus::kReady) {
      const auto status = m2_system_.SetAnimationRequestCallback(
          inst.m2_instance_id,
          [this, instance_id = inst.m2_instance_id](
              const m2::M2AnimationRequestEvent &event) {
            if (event.resolved_sub_animation_index != 0) {
              return;
            }
            const auto mapped = ResolveTransportAnimationRequestTransition(
                event.requested_animation_id);
            if (!mapped.has_value()) {
              return;
            }
            static_cast<void>(m2_system_.SetAnimation(instance_id, *mapped, 1.0f));
          });
      if (status == m2::M2ResultStatus::kReady) {
        inst.transport_animation_callback_installed = true;
      } else if (m2::IsTerminalM2ResultStatus(status)) {
        ClearM2Instance(inst);
        return;
      }
    }
  }

  if (!inst.transport_animation_callback_installed) {
    return;
  }

  const world::WmoDoodadAnimation desired =
      ResolveTransportDoodadAnimation(inst, owner.doodad_animation_controls);
  if (inst.m2_instance_id == 0u || inst.transport_animation == desired) {
    return;
  }
  inst.transport_animation = desired;
  if (desired == world::WmoDoodadAnimation::kNone) {

    return;
  }
  static_cast<void>(m2_system_.SetAnimation(inst.m2_instance_id,
                                            ResolveTransportAnimationId(desired)));
}

void DoodadRenderer::RefreshSpatialBounds(DoodadInstance& inst) {

  inst.admission_cache_epoch = 0u;
  if (inst.collision_geometry && inst.collision_geometry->radius > 0.0f) {
    inst.bounding_radius =
        inst.collision_geometry->radius * ExtractMaximumAffineScale(inst.model_matrix);
    inst.bounding_center = inst.position;
    inst.has_bounding_radius = true;
  } else {
    inst.bounding_radius = 0.0f;
    inst.has_bounding_radius = false;
  }

  const auto spatial = m2_system_.QueryModelSpatialInfo(inst.m2_model_id);
  if (spatial.status == m2::M2ResultStatus::kReady) {
    RenderAabb world_bounds{};
    if (TryTransformDoodadRenderBounds(spatial.spatial.local_bounds, inst.model_matrix,
                                       &world_bounds)) {
      inst.bounding_bounds = world_bounds;
      inst.has_bounding_bounds = true;
      inst.distance_class = SelectDoodadDistanceClass(world_bounds, environment_detail_);
    }
  }
  if (spatial.status == m2::M2ResultStatus::kReady &&
      std::isfinite(spatial.spatial.local_bounding_sphere[0]) &&
      std::isfinite(spatial.spatial.local_bounding_sphere[1]) &&
      std::isfinite(spatial.spatial.local_bounding_sphere[2]) &&
      std::isfinite(spatial.spatial.local_bounding_sphere[3]) &&
      spatial.spatial.local_bounding_sphere[3] >= 0.0f) {
    const RenderVec3 world_center =
        TransformAffinePoint4x4(RenderVec3View{spatial.spatial.local_bounding_sphere.data(), 3u},
                                RenderMatrix4x4View{inst.model_matrix});
    const float world_radius =
        spatial.spatial.local_bounding_sphere[3] * ExtractMaximumAffineScale(inst.model_matrix);
    if (std::isfinite(world_center[0]) && std::isfinite(world_center[1]) &&
        std::isfinite(world_center[2]) && std::isfinite(world_radius)) {
      inst.bounding_center = world_center;
      inst.bounding_radius = world_radius;
      inst.has_bounding_radius = true;
    }
  }
}

void DoodadRenderer::SetLoadingFocus(const float x, const float y, const float z) {
  const RenderVec3 next{x, y, z};
  const float dx = next[0] - loading_focus_[0];
  const float dy = next[1] - loading_focus_[1];
  const float dz = next[2] - loading_focus_[2];
  if (!has_loading_focus_) {
    loading_focus_ = next;
    has_loading_focus_ = true;
    if (!pending_load_queue_.empty()) {
      RebuildLoadingQueue();
    }
    return;
  }
  if (pending_load_queue_.empty()) {
    loading_focus_ = next;
    return;
  }

  const float reprioritize_distance = MaximumDoodadLoadingHysteresis(environment_detail_);
  if (dx * dx + dy * dy + dz * dz >= reprioritize_distance * reprioritize_distance) {
    loading_focus_ = next;
    RebuildLoadingQueue();
  }
}

void DoodadRenderer::PushPendingOwner(const OwnerKind kind, const std::uint64_t owner_id,
                                      const OwnedDoodads &owner) {
  for (std::size_t index = 0; index < owner.instances.size(); ++index) {
    const auto &instance = owner.instances[index];
    if (instance.m2_instance_id != 0u || instance.m2_load_attempted) {
      continue;
    }
    const float dx = instance.position[0] - loading_focus_[0];
    const float dy = instance.position[1] - loading_focus_[1];
    const float dz = instance.position[2] - loading_focus_[2];
    pending_load_queue_.push(PendingLoadHandle{
        .distance_squared = dx * dx + dy * dy + dz * dz,
        .sequence = next_pending_sequence_++,
        .owner = owner_id,
        .generation = owner.generation,
        .instance_index = static_cast<std::uint32_t>(index),
        .kind = kind,
    });
  }
}

bool DoodadRenderer::IsWorldEntryLoadDrained() const {
  if (!pending_publication_queue_.empty()) {
    return false;
  }
  if (pending_load_queue_.empty()) {
    return true;
  }
  if (!has_loading_focus_) {
    return false;
  }

  const float admission_distance =
      environment_detail_.maximum.back() +
      MaximumDoodadLoadingHysteresis(environment_detail_);
  return pending_load_queue_.top().distance_squared >
         admission_distance * admission_distance;
}

void DoodadRenderer::RebuildLoadingQueue() {
  pending_load_queue_ = {};
  for (const auto &[tile_key, owner] : tile_doodads_) {
    PushPendingOwner(OwnerKind::Tile, tile_key, owner);
  }
  for (const auto &[owner_id, owner] : wmo_doodads_) {
    PushPendingOwner(OwnerKind::Wmo, owner_id, owner);
  }
}

DoodadInstance *DoodadRenderer::ResolvePending(const PendingLoadHandle &handle) {
  OwnedDoodads *owner = nullptr;
  if (handle.kind == OwnerKind::Tile) {
    const auto found = tile_doodads_.find(handle.owner);
    if (found != tile_doodads_.end()) {
      owner = &found->second;
    }
  } else {
    const auto found = wmo_doodads_.find(handle.owner);
    if (found != wmo_doodads_.end()) {
      owner = &found->second;
    }
  }
  if (owner == nullptr || owner->generation != handle.generation ||
      handle.instance_index >= owner->instances.size()) {
    return nullptr;
  }
  return &owner->instances[handle.instance_index];
}

void DoodadRenderer::UpdateLoading(const int max_per_frame) {
  if (!load_file_ || !initialized_ || max_per_frame <= 0) {
    return;
  }

  static_cast<void>(m2_system_.PumpAsyncLoading());

  int published_this_frame = 0;
  std::size_t publication_checks = std::min<std::size_t>(pending_publication_queue_.size(),
                                                         static_cast<std::size_t>(max_per_frame));
  while (publication_checks-- != 0u && !pending_publication_queue_.empty()) {
    const PendingLoadHandle handle = pending_publication_queue_.front();
    pending_publication_queue_.pop_front();
    DoodadInstance *const pending = ResolvePending(handle);
    if (pending == nullptr) {
      continue;
    }
    DoodadInstance &inst = *pending;
    if (inst.m2_instance_id != 0u || !inst.m2_stream_ticket) {
      continue;
    }

    const auto streamed = m2_system_.QueryModelAsync(inst.m2_stream_ticket);

    AdoptEarlyCollisionData(inst, streamed);
    if (streamed.state == m2::M2StreamState::kPreparing ||
        streamed.state == m2::M2StreamState::kCommitPending) {
      pending_publication_queue_.push_back(handle);
      continue;
    }
    const auto created = m2_system_.CreateInstanceAsync(inst.m2_stream_ticket);
    if (streamed.state != m2::M2StreamState::kReady || streamed.model_id == 0u ||
        created.status != m2::M2ResultStatus::kReady ||
        !PublishPreparedM2Instance(inst, streamed.model_id, created.instance_id)) {
      m2_system_.ReleaseModelAsync(inst.m2_stream_ticket);
      inst.m2_stream_ticket = {};
      continue;
    }
    ++published_this_frame;
  }

  int requested_this_frame = 0;
  while (!pending_load_queue_.empty() && requested_this_frame < max_per_frame) {
    if (has_loading_focus_) {
      const float admission_distance =
          environment_detail_.maximum.back() + MaximumDoodadLoadingHysteresis(environment_detail_);
      if (pending_load_queue_.top().distance_squared > admission_distance * admission_distance) {

        break;
      }
    }

    const PendingLoadHandle handle = pending_load_queue_.top();
    pending_load_queue_.pop();
    DoodadInstance *const pending = ResolvePending(handle);
    if (pending == nullptr) {
      continue;
    }
    DoodadInstance &inst = *pending;
    if (inst.m2_instance_id != 0u || inst.m2_load_attempted) {
      continue;
    }

    inst.m2_load_attempted = true;
    ++requested_this_frame;
    inst.m2_stream_ticket =
        m2_system_.AcquireModelAsync(inst.model_path, m2::M2StreamPriority::kAmbient);
    if (!inst.m2_stream_ticket) {
      continue;
    }
    pending_publication_queue_.push_back(handle);
  }
  (void)published_this_frame;
}

void DoodadRenderer::ComputeModelMatrix(DoodadInstance &inst,
                                        const data::terrain::DoodadPlacement &placement) {
  inst.model_matrix = world::BuildDoodadModelMatrix(placement);
  inst.model_matrix_revision = next_model_matrix_revision_++;
  inst.position = {
      inst.model_matrix[12],
      inst.model_matrix[13],
      inst.model_matrix[14],
  };
}

void DoodadRenderer::ComputeWmoModelMatrix(DoodadInstance &inst, const data::wmo::WmoDoodadDef &def,
                                           const RenderMatrix4x4 &wmo_model_matrix) {
  inst.wmo_local_model_matrix = BuildRotationMatrix4x4Quaternion(
      RenderVec4{def.orientation[0], def.orientation[1], def.orientation[2], def.orientation[3]});
  inst.wmo_local_model_matrix[12] = def.position[0];
  inst.wmo_local_model_matrix[13] = def.position[1];
  inst.wmo_local_model_matrix[14] = def.position[2];
  if (std::abs(def.scale - 1.0f) > 0.001f && def.scale > 0.0f) {
    inst.wmo_local_model_matrix = ScaleMatrix4x4BasisRows(
        inst.wmo_local_model_matrix, RenderVec3{def.scale, def.scale, def.scale});
  }

  inst.model_matrix = MultiplyMatrix4x4(inst.wmo_local_model_matrix, wmo_model_matrix);
  inst.model_matrix_revision = next_model_matrix_revision_++;
  inst.position[0] = inst.model_matrix[12];
  inst.position[1] = inst.model_matrix[13];
  inst.position[2] = inst.model_matrix[14];
}

void DoodadRenderer::Update(const float dt) {
  if (!initialized_ || !std::isfinite(dt)) {

    scene_point_lights_.clear();
    return;
  }

  animation_update_ids_scratch_.clear();
  animation_update_instances_scratch_.clear();
  animation_update_missing_scratch_.clear();
  scene_light_candidates_scratch_.clear();

  const double previous_clock = animation_clock_seconds_;
  animation_clock_seconds_ = previous_clock + static_cast<double>(dt);

  world::Frustum cached_frustum{};
  cached_frustum.planes = admission_cache_key_.frustum_planes;
  const world::Frustum *const frustum =
      admission_cache_key_.has_frustum ? &cached_frustum : nullptr;

  const auto collect_owner = [&](OwnedDoodads &owner, const bool honor_wmo_doodad_sets) {
    ForEachAdmissionCandidate(
        owner, frustum, admission_cache_key_.camera_x, admission_cache_key_.camera_y,
        admission_cache_key_.camera_z, admission_cache_key_.environment_detail,
        [&](DoodadInstance &instance) {
          if (instance.m2_instance_id == 0u ||
              (honor_wmo_doodad_sets &&
               !IsWmoDoodadSetSelected(instance, owner.active_wmo_doodad_sets))) {
            return;
          }
          const bool admitted_last_frame =
              instance.admission_cache_epoch == admission_cache_epoch_ &&
              instance.admission_cache.visible;
          if (!admitted_last_frame) {

            return;
          }

          if (instance.scene_point_light_state != 0) {
            scene_light_candidates_scratch_.push_back(&instance);
          }

          const bool advanced_previous_update =
              instance.deferred_animation_seconds == 0.0f &&
              instance.animation_clock_mark == previous_clock;
          const double deferred =
              static_cast<double>(instance.deferred_animation_seconds) +
              (animation_clock_seconds_ - instance.animation_clock_mark);
          instance.deferred_animation_seconds = 0.0f;
          instance.animation_clock_mark = animation_clock_seconds_;
          if (advanced_previous_update) {
            animation_update_ids_scratch_.push_back(instance.m2_instance_id);
            animation_update_instances_scratch_.push_back(&instance);
            return;
          }

          const auto catch_up = static_cast<float>(
              std::min(deferred, static_cast<double>(kMaxDeferredDoodadAnimationSeconds)));
          if (m2_system_.UpdateAnimation(instance.m2_instance_id, catch_up) ==
              m2::M2ResultStatus::kFailed) {
            ClearM2Instance(instance);
          }
        });
  };

  for (auto &[tile, owner] : tile_doodads_) {
    static_cast<void>(tile);
    collect_owner(owner, false);
  }
  for (auto &[wmo_owner, owner] : wmo_doodads_) {
    static_cast<void>(wmo_owner);
    collect_owner(owner, true);
  }
  if (animation_update_ids_scratch_.empty()) {

    CollectScenePointLights();
    return;
  }
  m2_system_.UpdateAnimations(animation_update_ids_scratch_, dt,
                              &animation_update_missing_scratch_);

  if (!animation_update_missing_scratch_.empty()) {
    std::size_t missing_index = 0u;
    for (std::size_t index = 0; index < animation_update_ids_scratch_.size() &&
                                missing_index < animation_update_missing_scratch_.size();
         ++index) {
      if (animation_update_ids_scratch_[index] ==
          animation_update_missing_scratch_[missing_index]) {
        ClearM2Instance(*animation_update_instances_scratch_[index]);
        ++missing_index;
      }
    }
  }

  CollectScenePointLights();
}

void DoodadRenderer::CollectScenePointLights() {
  scene_point_lights_.clear();
  for (DoodadInstance *const instance : scene_light_candidates_scratch_) {

    if (instance->m2_instance_id == 0u || instance->m2_model_id == 0u) {
      continue;
    }

    const auto light_count = m2_system_.QueryModelLightCount(instance->m2_model_id);
    if (light_count.status != m2::M2ResultStatus::kReady) {

      continue;
    }
    if (light_count.light_count == 0u) {
      instance->scene_point_light_state = 0;
      continue;
    }

    if (instance->scene_point_light_state < 0) {

      std::int8_t resolved = 0;
      for (int light_index = 0; light_index < static_cast<int>(light_count.light_count);
           ++light_index) {
        const auto probe = m2_system_.QueryLightSample(instance->m2_model_id, light_index, 0,
                                                       0u, std::span<const float>{});
        if (probe.status == m2::M2ResultStatus::kReady &&
            probe.light.type == kM2PointLightType) {
          resolved = 1;
          break;
        }
      }
      instance->scene_point_light_state = resolved;
      if (resolved == 0) {
        continue;
      }
    }

    auto bones = m2_system_.QueryBoneMatrices(instance->m2_instance_id);
    if (bones.status != m2::M2ResultStatus::kReady) {
      continue;
    }
    const RenderMatrix4x4View model_world{instance->model_matrix};
    for (int light_index = 0; light_index < static_cast<int>(light_count.light_count);
         ++light_index) {
      const auto sample = m2_system_.QueryInstanceLightSample(instance->m2_instance_id,
                                                              light_index, bones.bone_matrices);
      if (sample.status != m2::M2ResultStatus::kReady ||
          sample.light.type != kM2PointLightType) {
        continue;
      }

      if (!sample.light.visible) {
        continue;
      }
      scene_point_lights_.push_back(MakeScenePointLight(sample.light, model_world));
    }
  }
}

void DoodadRenderer::Render(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
                            const world::Frustum *frustum, float camera_x, float camera_y,
                            float camera_z, const RenderVec3 &camera_forward,
                            const m2::M2RenderPassScope pass_scope,
                            m2::M2TransparentDrawOrder *const transparent_draw_order) {

  static_cast<void>(proj_mtx);

  const bool admitted_walk_offered = admitted_walk_consumable_;
  admitted_walk_consumable_ = false;
  if (!initialized_)
    return;
  m2::M2BatchUniforms world_uniforms;
  ApplyWorldM2SceneState(world_m2_scene_state_, &world_uniforms);

  const m2::M2SharedBatchUniformsLease world_uniforms_lease(m2_system_, world_uniforms);

  const bool reuse_opaque_queue =
      pass_scope == m2::M2RenderPassScope::kTransparentOnly && render_queue_ready_for_transparent_;
  if (!reuse_opaque_queue) {
    for (auto &bucket : render_buckets_) {
      bucket.clear();
    }
    PrepareAdmissionCache(frustum, camera_x, camera_y, camera_z, camera_forward);

    const auto queue_admitted = [&](DoodadInstance &inst) {
      if (inst.m2_instance_id == 0u) {
        return;
      }
      const DoodadAdmission admission =
          EvaluateCachedAdmission(inst, frustum, camera_x, camera_y, camera_z, camera_forward);
      if (!admission.visible) {
        return;
      }
      render_buckets_[admission.depth_bucket].push_back({
          .instance = &inst,
          .distance_alpha = admission.distance_alpha,
      });
    };

    if (admitted_walk_offered && admitted_walk_epoch_ == admission_cache_epoch_) {

      for (DoodadInstance *const inst : admitted_walk_scratch_) {
        queue_admitted(*inst);
      }
    } else {
      const auto gather_instances =
          [&](OwnedDoodads &owner,
              const std::array<std::uint16_t, 4> *active_wmo_doodad_sets) {
            ForEachAdmissionCandidate(
                owner, frustum, camera_x, camera_y, camera_z, environment_detail_,
                [&](DoodadInstance &inst) {
                  if (active_wmo_doodad_sets != nullptr &&
                      !IsWmoDoodadSetSelected(inst, *active_wmo_doodad_sets)) {
                    return;
                  }
                  queue_admitted(inst);
                });
          };

      for (auto &[key, owner] : tile_doodads_) {
        (void)key;
        gather_instances(owner, nullptr);
      }

      for (auto &[owner_id, owner] : wmo_doodads_) {
        (void)owner_id;
        if (!owner.enabled) {
          continue;
        }
        gather_instances(owner, &owner.active_wmo_doodad_sets);
      }
    }

    const auto authored_order_less = [](const QueuedDoodadDraw &lhs, const QueuedDoodadDraw &rhs) {
      return IsDoodadAuthoredBefore(*lhs.instance, *rhs.instance);
    };
    for (auto &bucket : render_buckets_) {
      std::sort(bucket.begin(), bucket.end(), authored_order_less);
    }
    render_queue_ready_for_transparent_ = pass_scope == m2::M2RenderPassScope::kOpaqueOnly;
  }

  auto &system = m2_system_;

  const bool instancing_pass = m2::M2RenderPassScopeIncludesOpaque(pass_scope);
  constexpr std::size_t kMinInstancedDoodadGroupSize = 2u;
  if (instancing_pass) {
    instanced_groups_scratch_.clear();
  }

  render_batch_ids_scratch_.clear();
  render_batch_targets_scratch_.clear();
  frame_state_requests_scratch_.clear();
  frame_state_statuses_scratch_.clear();
  frame_state_work_scratch_.clear();
  frame_state_uniforms_scratch_.clear();
  const std::array<std::uint8_t, 64> bucket_order =
      BuildDoodadDepthBucketOrder(pass_scope);

  std::size_t queued_total = 0u;
  for (const std::uint8_t bucket_index : bucket_order) {
    queued_total += render_buckets_[bucket_index].size();
  }
  frame_state_uniforms_scratch_.reserve(queued_total);

  for (const std::uint8_t bucket_index : bucket_order) {
    for (QueuedDoodadDraw &queued : render_buckets_[bucket_index]) {
      DoodadInstance &inst = *queued.instance;
      if (queued.routed_to_instanced) {

        continue;
      }
      if (!inst.render_ready_latched) {
        const auto readiness = system.QueryInstanceReadiness(inst.m2_instance_id);
        if (readiness.status != m2::M2ResultStatus::kReady ||
            !readiness.render_ready) {
          continue;
        }
        inst.render_ready_latched = true;
      }

      const RenderVec4 material_tint =
          inst.wmo_color_is_ambient_substitute
              ? RenderVec4{1.0f, 1.0f, 1.0f, inst.tint_color[3]}
              : inst.tint_color;

      if (queued.render_state_prepared) {

        frame_state_work_scratch_.push_back({.target = &queued,
                                             .material_tint = material_tint,
                                             .request_index =
                                                 kNoDoodadFrameRequest});
        continue;
      }

      const m2::M2BatchUniforms *instance_uniforms = nullptr;
      m2::M2SharedBatchUniformsHandle instance_shared_uniforms =
          world_uniforms_lease.handle();
      if (inst.wmo_color_is_ambient_substitute) {
        m2::M2BatchUniforms &interior_uniforms =
            frame_state_uniforms_scratch_.emplace_back(world_uniforms);
        interior_uniforms.light_ambient = {inst.tint_color[0], inst.tint_color[1],
                                           inst.tint_color[2], 0.0f};
        interior_uniforms.light_count = {0.0f, 0.0f, 0.0f, 0.0f};
        instance_uniforms = &interior_uniforms;
        instance_shared_uniforms = {};
      }
      frame_state_work_scratch_.push_back(
          {.target = &queued,
           .material_tint = material_tint,
           .request_index =
               static_cast<std::uint32_t>(frame_state_requests_scratch_.size())});
      frame_state_requests_scratch_.push_back(
          {.instance_id = inst.m2_instance_id,
           .world_transform = &inst.model_matrix,
           .uniforms = instance_uniforms,
           .shared_uniforms = instance_shared_uniforms,
           .tint_rgba = material_tint,
           .alpha = inst.alpha * queued.distance_alpha,
           .world_transform_revision = inst.model_matrix_revision});
    }
  }

  if (!frame_state_requests_scratch_.empty()) {
    frame_state_statuses_scratch_.resize(frame_state_requests_scratch_.size());
    system.SetDoodadFrameRenderStates(frame_state_requests_scratch_,
                                      frame_state_statuses_scratch_);
  }

  for (const PendingDoodadFrameState &pending : frame_state_work_scratch_) {
    QueuedDoodadDraw &queued = *pending.target;
    DoodadInstance &inst = *queued.instance;
    const RenderVec4 &material_tint = pending.material_tint;
    const m2::M2ResultStatus setup_status =
        pending.request_index == kNoDoodadFrameRequest
            ? m2::M2ResultStatus::kReady
            : frame_state_statuses_scratch_[pending.request_index];
    if (m2::IsTerminalM2ResultStatus(setup_status)) {
      ClearM2Instance(inst);
      continue;
    }
    if (setup_status != m2::M2ResultStatus::kReady) {
      continue;
    }
    queued.render_state_prepared = true;

    if (instancing_pass && inst.alpha == 1.0f &&
        queued.distance_alpha == 1.0f && material_tint[3] == 1.0f) {
      if (inst.static_instancing_state < 0) {
        const auto profile =
            system.QueryStaticInstancingProfile(inst.m2_instance_id);
        inst.static_instancing_state =
            !profile.opaque_instanceable
                ? 0
                : (profile.has_transparent_residue ? 2 : 1);
      }
      if (inst.static_instancing_state >= 1) {
        const bool interior = inst.wmo_color_is_ambient_substitute;
        auto &group = instanced_groups_scratch_[
            (static_cast<std::uint64_t>(inst.m2_model_id) << 1u) |
            (interior ? 1u : 0u)];
        if (group.records.empty()) {
          group.exemplar_instance_id = inst.m2_instance_id;
        }

        const RenderVec4 record_color =
            interior ? RenderVec4{std::clamp(inst.tint_color[0], 0.0f, 1.0f),
                                  std::clamp(inst.tint_color[1], 0.0f, 1.0f),
                                  std::clamp(inst.tint_color[2], 0.0f, 1.0f),
                                  1.0f}
                     : RenderVec4{material_tint[0], material_tint[1],
                                  material_tint[2], 1.0f};
        group.records.push_back({.transform = inst.model_matrix,
                                 .color = record_color});
        group.members.push_back(&queued);

        queued.routed_to_instanced = inst.static_instancing_state == 1;
        continue;
      }
    }

    render_batch_ids_scratch_.push_back(inst.m2_instance_id);
    render_batch_targets_scratch_.push_back(&queued);
  }

  if (instancing_pass) {
    for (auto &[group_key, group] : instanced_groups_scratch_) {
      (void)group_key;
      if (group.records.size() >= kMinInstancedDoodadGroupSize) {
        continue;
      }
      for (QueuedDoodadDraw *const member : group.members) {
        member->routed_to_instanced = false;
        render_batch_ids_scratch_.push_back(member->instance->m2_instance_id);
        render_batch_targets_scratch_.push_back(member);
      }
      group.records.clear();
      group.members.clear();
    }
  }

  render_batch_results_scratch_.resize(render_batch_ids_scratch_.size());
  render_batch_draw_ordinals_scratch_.clear();
  if (m2::M2RenderPassScopeIncludesTransparent(pass_scope) &&
      transparent_draw_order != nullptr) {
    const std::uint32_t first_ordinal = transparent_draw_order->Reserve(
        static_cast<std::uint32_t>(render_batch_ids_scratch_.size()));
    render_batch_draw_ordinals_scratch_.resize(render_batch_ids_scratch_.size());
    std::iota(render_batch_draw_ordinals_scratch_.begin(),
              render_batch_draw_ordinals_scratch_.end(), first_ordinal);
  }
  {
    const m2::M2TransparentDrawOrdinalScope draw_order_scope(
        render_batch_draw_ordinals_scratch_);
    system.RenderInstanceBatch(view_id, render_batch_ids_scratch_,
                               RenderMatrix4x4View{view_mtx, 16u}, pass_scope,
                               m2_system_.frame_job_system(),
                               kDoodadInstanceRenderMicroseconds,
                               render_batch_results_scratch_);
  }

  for (std::size_t i = 0; i < render_batch_results_scratch_.size(); ++i) {
    if (m2::IsTerminalM2ResultStatus(render_batch_results_scratch_[i].status)) {
      ClearM2Instance(*render_batch_targets_scratch_[i]->instance);
    }
  }

  if (instancing_pass) {
    for (auto &[group_key, group] : instanced_groups_scratch_) {
      if (group.records.size() < kMinInstancedDoodadGroupSize) {
        continue;
      }

      m2::M2BatchUniforms group_uniforms = world_uniforms;
      if ((group_key & 1u) != 0u) {
        group_uniforms.light_ambient = {1.0f, 1.0f, 1.0f, 0.0f};
        group_uniforms.light_count = {0.0f, 0.0f, 0.0f, 0.0f};
      }
      const auto result = system.RenderInstancedGroup(
          view_id, group.exemplar_instance_id, group.records,
          group_uniforms);
      if (result.status == m2::M2ResultStatus::kUnsupported) {

        for (QueuedDoodadDraw *const member : group.members) {
          member->instance->static_instancing_state = 0;
          member->routed_to_instanced = false;
        }
      } else if (m2::IsTerminalM2ResultStatus(result.status)) {

        for (QueuedDoodadDraw *const member : group.members) {
          ClearM2Instance(*member->instance);
        }
      }
    }
  }

  if (pass_scope == m2::M2RenderPassScope::kOpaqueOnly &&
      render_queue_ready_for_transparent_) {

    transparent_effect_ids_scratch_.assign(render_batch_ids_scratch_.begin(),
                                           render_batch_ids_scratch_.end());
    for (const auto &[group_key, group] : instanced_groups_scratch_) {
      (void)group_key;
      for (const QueuedDoodadDraw *const member : group.members) {
        if (!member->routed_to_instanced) {
          transparent_effect_ids_scratch_.push_back(
              member->instance->m2_instance_id);
        }
      }
    }
    system.PrepareParticleDrawGeometry(transparent_effect_ids_scratch_,
                                       RenderMatrix4x4View{view_mtx, 16u});
  }

  if (pass_scope == m2::M2RenderPassScope::kTransparentOnly) {
    render_queue_ready_for_transparent_ = false;
  }
}

void DoodadRenderer::VisitInstances(
    const std::function<void(const DoodadInstance &)> &visitor) const {
  if (!visitor) {
    return;
  }

  for (const auto &[key, owner] : tile_doodads_) {
    (void)key;
    for (const auto &instance : owner.instances) {
      visitor(instance);
    }
  }
  for (const auto &[owner_id, owner] : wmo_doodads_) {
    (void)owner_id;
    for (const auto &instance : owner.instances) {
      visitor(instance);
    }
  }
}

void DoodadRenderer::CollectAdmittedWalk(const world::Frustum &frustum, const float camera_x,
                                         const float camera_y, const float camera_z,
                                         const RenderVec3 &camera_forward) {

  PrepareAdmissionCache(&frustum, camera_x, camera_y, camera_z, camera_forward);
  admitted_walk_scratch_.clear();

  const auto visit_owner = [&](
                               OwnedDoodads &owner,
                               const std::array<std::uint16_t, 4> *const active_wmo_doodad_sets) {
    ForEachAdmissionCandidate(
        owner, &frustum, camera_x, camera_y, camera_z, environment_detail_,
        [&](DoodadInstance &instance) {
          if (active_wmo_doodad_sets != nullptr &&
              !IsWmoDoodadSetSelected(instance, *active_wmo_doodad_sets)) {
            return;
          }
          if (EvaluateCachedAdmission(instance, &frustum, camera_x, camera_y, camera_z,
                                      camera_forward)
                  .visible) {
            admitted_walk_scratch_.push_back(&instance);
          }
        });
  };

  for (auto &[key, owner] : tile_doodads_) {
    (void)key;
    visit_owner(owner, nullptr);
  }
  for (auto &[owner_id, owner] : wmo_doodads_) {
    (void)owner_id;
    if (!owner.enabled) {
      continue;
    }
    visit_owner(owner, &owner.active_wmo_doodad_sets);
  }
  admitted_walk_epoch_ = admission_cache_epoch_;
  admitted_walk_consumable_ = true;
}

void DoodadRenderer::VisitCollisionTriangles(
    const std::array<float, 6>& world_bounds,
    const std::function<void(const DoodadCollisionTriangle&)>& visitor,
    const bool include_object_owned) const {
  if (!visitor) {
    return;
  }

  const auto overlaps_query = [&world_bounds](const RenderAabb& bounds) {
    return bounds[0] <= world_bounds[3] && bounds[3] >= world_bounds[0] &&
           bounds[1] <= world_bounds[4] && bounds[4] >= world_bounds[1] &&
           bounds[2] <= world_bounds[5] && bounds[5] >= world_bounds[2];
  };

  const auto reject_outside_query = [&world_bounds](const DoodadCollisionTriangle& triangle) {
    const float min_x = std::min({triangle.vertices[0][0], triangle.vertices[1][0],
                                  triangle.vertices[2][0]});
    const float max_x = std::max({triangle.vertices[0][0], triangle.vertices[1][0],
                                  triangle.vertices[2][0]});
    const float min_y = std::min({triangle.vertices[0][1], triangle.vertices[1][1],
                                  triangle.vertices[2][1]});
    const float max_y = std::max({triangle.vertices[0][1], triangle.vertices[1][1],
                                  triangle.vertices[2][1]});
    const float min_z = std::min({triangle.vertices[0][2], triangle.vertices[1][2],
                                  triangle.vertices[2][2]});
    const float max_z = std::max({triangle.vertices[0][2], triangle.vertices[1][2],
                                  triangle.vertices[2][2]});
    return min_x > world_bounds[3] || max_x < world_bounds[0] ||
           min_y > world_bounds[4] || max_y < world_bounds[1] ||
           min_z > world_bounds[5] || max_z < world_bounds[2];
  };
  const auto visit_instance = [&](const DoodadInstance& instance) {
    if (!include_object_owned && instance.object_guid != 0u) {
      return;
    }

    RenderAabb probe_bounds{};
    bool have_probe_bounds = instance.has_bounding_bounds;
    if (have_probe_bounds) {
      probe_bounds = instance.bounding_bounds;
    } else if (HasPositiveVolume(instance.header_local_bounds)) {
      have_probe_bounds = TryTransformDoodadRenderBounds(
          instance.header_local_bounds, instance.model_matrix, &probe_bounds);
    }
    if (have_probe_bounds && !overlaps_query(probe_bounds)) {
      return;
    }

    const std::uint64_t owner_id =
        instance.render_owner_key ^
        (static_cast<std::uint64_t>(instance.render_placement_index) *
         0x9e3779b97f4a7c15ull);

    if (!instance.collision_ready) {

      if (HasPositiveVolume(instance.header_local_bounds)) {
        EmitDoodadHeaderBoxTriangles(
            instance.header_local_bounds, instance.model_matrix, owner_id,
            instance.object_guid,
            [&](const DoodadCollisionTriangle& triangle) {
              if (!reject_outside_query(triangle)) {
                visitor(triangle);
              }
            });
      }
      return;
    }

    const auto& geometry = instance.collision_geometry;
    if (!geometry || geometry->vertices.empty() || geometry->triangles.size() < 3u) {
      return;
    }

    const RenderMatrix4x4View transform{instance.model_matrix};
    for (std::size_t index = 0u; index + 2u < geometry->triangles.size(); index += 3u) {
      const std::uint16_t i0 = geometry->triangles[index];
      const std::uint16_t i1 = geometry->triangles[index + 1u];
      const std::uint16_t i2 = geometry->triangles[index + 2u];
      if (i0 >= geometry->vertices.size() || i1 >= geometry->vertices.size() ||
          i2 >= geometry->vertices.size()) {
        continue;
      }

      DoodadCollisionTriangle triangle;
      triangle.vertices[0] =
          TransformAffinePoint4x4(RenderVec3View{geometry->vertices[i0]}, transform);
      triangle.vertices[1] =
          TransformAffinePoint4x4(RenderVec3View{geometry->vertices[i1]}, transform);
      triangle.vertices[2] =
          TransformAffinePoint4x4(RenderVec3View{geometry->vertices[i2]}, transform);
      if (reject_outside_query(triangle)) {
        continue;
      }
      triangle.owner_id = owner_id;
      triangle.facet_id = index / 3u;
      triangle.owner_guid = instance.object_guid;
      visitor(triangle);
    }
  };

  for (const auto& [tile, owner] : tile_doodads_) {
    static_cast<void>(tile);
    for (const auto& instance : owner.instances) {
      visit_instance(instance);
    }
  }
  for (const auto& [wmo_owner, owner] : wmo_doodads_) {
    static_cast<void>(wmo_owner);
    if (!owner.enabled) {
      continue;
    }
    for (const auto& instance : owner.instances) {
      if (IsWmoDoodadSetSelected(instance, owner.active_wmo_doodad_sets)) {
        visit_instance(instance);
      }
    }
  }
}
}

#include "openwow/world/environment/wmo_fog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace openwow::world {
namespace {

constexpr float kByteToUnit = 1.0f / 255.0f;

struct FogBand {
  float end = 0.0f;
  float start_scalar = 0.0f;
  std::uint32_t color = 0u;
};

struct FogPair {
  FogBand normal{};
  FogBand underwater{};
};

struct FogCandidate {
  float distance = 0.0f;
  std::uint8_t fog_index = 0u;
};

class RetailFogCandidateHeap {
 public:
  void Push(const FogCandidate candidate) noexcept {
    std::size_t index = count_++;
    while (index > 1u) {
      const std::size_t parent = index >> 1u;
      if (candidate.distance < entries_[parent].distance) {
        break;
      }
      entries_[index] = entries_[parent];
      index = parent;
    }
    entries_[index] = candidate;
  }

  [[nodiscard]] bool Empty() const noexcept { return count_ < 2u; }

  [[nodiscard]] FogCandidate Pop() noexcept {
    const FogCandidate root = entries_[1u];
    const FogCandidate tail = entries_[count_ - 1u];
    --count_;
    if (Empty()) {
      return root;
    }

    const std::size_t last = count_ - 1u;
    std::size_t index = 1u;
    while ((index << 1u) <= last) {
      const std::size_t left = index << 1u;
      std::size_t child = left;
      if (left < last &&
          !(entries_[left + 1u].distance < entries_[left].distance)) {
        child = left + 1u;
      }
      if (entries_[child].distance <= tail.distance) {
        break;
      }
      entries_[index] = entries_[child];
      index = child;
    }
    entries_[index] = tail;
    return root;
  }

 private:

  std::array<FogCandidate, 5u> entries_{};
  std::size_t count_ = 1u;
};

[[nodiscard]] constexpr std::uint8_t Channel(
    const std::uint32_t color, const unsigned shift) noexcept {
  return static_cast<std::uint8_t>((color >> shift) & 0xffu);
}

[[nodiscard]] constexpr int FloorDivideBy256(const int value) noexcept {
  return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

[[nodiscard]] constexpr std::uint8_t BlendChannel(
    const std::uint8_t from, const std::uint8_t to,
    const std::uint8_t weight) noexcept {
  if (weight == 0u) {
    return from;
  }
  if (weight == 0xffu) {
    return to;
  }
  const int blended = static_cast<int>(from) + FloorDivideBy256(
      (static_cast<int>(to) - static_cast<int>(from)) *
      static_cast<int>(weight));
  return static_cast<std::uint8_t>(blended);
}

[[nodiscard]] constexpr std::uint32_t BlendPackedColor(
    const std::uint32_t from, const std::uint32_t to,
    const std::uint8_t weight) noexcept {
  return (from & 0xff000000u) |
         (static_cast<std::uint32_t>(BlendChannel(
              Channel(from, 16u), Channel(to, 16u), weight))
          << 16u) |
         (static_cast<std::uint32_t>(BlendChannel(
              Channel(from, 8u), Channel(to, 8u), weight))
          << 8u) |
         static_cast<std::uint32_t>(BlendChannel(
             Channel(from, 0u), Channel(to, 0u), weight));
}

[[nodiscard]] constexpr FogPair ReadFogPair(
    const data::wmo::WmoFog& fog) noexcept {
  return {
      .normal = {fog.fogEnd, fog.fogStartScalar, fog.fogColor},
      .underwater = {fog.uwFogEnd, fog.uwFogStartScalar, fog.uwFogColor},
  };
}

void BlendBand(FogBand& base, const FogBand& overlay,
               const float factor) noexcept {
  base.end += (overlay.end - base.end) * factor;
  base.start_scalar +=
      (overlay.start_scalar - base.start_scalar) * factor;

  const auto byte_weight = static_cast<std::uint8_t>(std::clamp(
      std::nearbyint(factor * 255.0f), 0.0f, 255.0f));
  base.color = BlendPackedColor(base.color, overlay.color, byte_weight);
}

[[nodiscard]] FogState FinalizeFog(
    const FogBand& band, const float far_clip) noexcept {
  const float capped_end = std::min(band.end, far_clip);
  constexpr float kRetailMinimumFogEnd = 30.0f;
  return {
      .params = {capped_end * band.start_scalar,
                 std::max(capped_end, kRetailMinimumFogEnd), 1.0f, 0.0f},
      .color = {
          static_cast<float>(Channel(band.color, 16u)) * kByteToUnit,
          static_cast<float>(Channel(band.color, 8u)) * kByteToUnit,
          static_cast<float>(Channel(band.color, 0u)) * kByteToUnit,
          static_cast<float>(Channel(band.color, 24u)) * kByteToUnit,
      },
  };
}

[[nodiscard]] std::uint8_t UnitToByte(const float unit) noexcept {
  return static_cast<std::uint8_t>(std::clamp(
      std::nearbyint(unit * 255.0f), 0.0f, 255.0f));
}

[[nodiscard]] float DistanceToConvexPolygon(
    const Vec3& point, const std::vector<Vec3>& vertices,
    const Vec4& plane) noexcept {
  if (vertices.size() < 3u) {
    return std::numeric_limits<float>::max();
  }

  const Vec3 normal{plane[0], plane[1], plane[2]};
  const float normal_length_sq =
      normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
  if (normal_length_sq < 1e-8f) {
    return std::numeric_limits<float>::max();
  }
  const float inv_normal_length = 1.0f / std::sqrt(normal_length_sq);
  const Vec3 unit_normal{normal[0] * inv_normal_length,
                         normal[1] * inv_normal_length,
                         normal[2] * inv_normal_length};
  const float signed_distance =
      (point[0] * normal[0] + point[1] * normal[1] + point[2] * normal[2] +
       plane[3]) *
      inv_normal_length;
  const Vec3 projected{point[0] - unit_normal[0] * signed_distance,
                       point[1] - unit_normal[1] * signed_distance,
                       point[2] - unit_normal[2] * signed_distance};

  bool inside_every_edge = true;
  float nearest_edge_distance_sq = std::numeric_limits<float>::max();
  const std::size_t vertex_count = vertices.size();
  for (std::size_t edge = 0; edge < vertex_count; ++edge) {
    const Vec3& a = vertices[edge];
    const Vec3& b = vertices[(edge + 1u) % vertex_count];
    const Vec3 edge_dir{b[0] - a[0], b[1] - a[1], b[2] - a[2]};

    const Vec3 edge_normal{
        edge_dir[1] * unit_normal[2] - edge_dir[2] * unit_normal[1],
        edge_dir[2] * unit_normal[0] - edge_dir[0] * unit_normal[2],
        edge_dir[0] * unit_normal[1] - edge_dir[1] * unit_normal[0]};
    const Vec3 to_projected{projected[0] - a[0], projected[1] - a[1],
                            projected[2] - a[2]};
    const float side = to_projected[0] * edge_normal[0] +
                       to_projected[1] * edge_normal[1] +
                       to_projected[2] * edge_normal[2];
    if (side <= 0.0f) {
      continue;
    }
    inside_every_edge = false;
    const float edge_length_sq = edge_dir[0] * edge_dir[0] +
                                 edge_dir[1] * edge_dir[1] +
                                 edge_dir[2] * edge_dir[2];
    float t = edge_length_sq > 1e-8f
                  ? (to_projected[0] * edge_dir[0] +
                     to_projected[1] * edge_dir[1] +
                     to_projected[2] * edge_dir[2]) /
                        edge_length_sq
                  : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const Vec3 clamped{a[0] + edge_dir[0] * t, a[1] + edge_dir[1] * t,
                       a[2] + edge_dir[2] * t};
    const float dx = point[0] - clamped[0];
    const float dy = point[1] - clamped[1];
    const float dz = point[2] - clamped[2];
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    if (distance_sq < nearest_edge_distance_sq) {
      nearest_edge_distance_sq = distance_sq;
    }
  }

  if (inside_every_edge) {
    const float dx = point[0] - projected[0];
    const float dy = point[1] - projected[1];
    const float dz = point[2] - projected[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  return std::sqrt(nearest_edge_distance_sq);
}

constexpr std::uint32_t kWmoFogPortalSearchMaxDepth = 4u;
constexpr float kWmoFogPortalBlendDistance = 25.0f;

void SearchNearestExteriorPortal(const WmoVisibilityData& visibility,
                                 const std::size_t group_index,
                                 const std::size_t predecessor_group_index,
                                 const bool has_predecessor,
                                 const std::uint32_t depth,
                                 const Vec3& local_camera_position,
                                 float& best_distance) noexcept {
  if (depth >= kWmoFogPortalSearchMaxDepth) {
    return;
  }
  if (group_index >= visibility.groups().size()) {
    return;
  }
  const WmoVisibilityGroup& group = visibility.groups()[group_index];
  for (const WmoVisibilityPortal& portal : group.portals) {
    if (portal.connected_group >= visibility.groups().size()) {
      continue;
    }
    if (has_predecessor &&
        portal.connected_group == predecessor_group_index) {
      continue;
    }
    const WmoVisibilityGroup& connected =
        visibility.groups()[portal.connected_group];
    constexpr std::uint32_t kExteriorFlags =
        data::wmo::kMogpExterior | data::wmo::kMogpExteriorLit;
    if ((connected.flags & kExteriorFlags) != 0u) {
      const float distance = DistanceToConvexPolygon(
          local_camera_position, portal.vertices, portal.plane_);
      if (distance < kWmoFogPortalBlendDistance &&
          distance < best_distance) {
        best_distance = distance;
      }
    } else {
      SearchNearestExteriorPortal(visibility, portal.connected_group,
                                  group_index, true,
                                  depth + 1u, local_camera_position,
                                  best_distance);
    }
  }
}

}

float ResolveWmoFogPortalBlendProgress(
    const WmoVisibilityData& visibility,
    const std::span<const std::size_t> containment_group_indices,
    const Vec3& local_camera_position) noexcept {

  float best_distance = std::numeric_limits<float>::max();
  bool any_interior_lane = false;
  constexpr std::uint32_t kExteriorFlags =
      data::wmo::kMogpExterior | data::wmo::kMogpExteriorLit;
  for (const std::size_t group_index : containment_group_indices) {
    if (group_index >= visibility.groups().size()) {
      continue;
    }
    if ((visibility.groups()[group_index].flags & kExteriorFlags) != 0u) {
      continue;
    }
    any_interior_lane = true;
    SearchNearestExteriorPortal(visibility, group_index, 0u,
                                false, 0u,
                                local_camera_position, best_distance);
  }
  if (!any_interior_lane) {
    return 0.0f;
  }

  return std::clamp(best_distance / kWmoFogPortalBlendDistance, 0.0f, 1.0f);
}

FogState BlendWmoFogPortalTransition(const FogState& outdoor_fog,
                                     const FogState& wmo_fog,
                                     const float progress) noexcept {

  FogState result = outdoor_fog;
  for (std::size_t field = 0; field < result.params.size(); ++field) {
    result.params[field] +=
        (wmo_fog.params[field] - outdoor_fog.params[field]) * progress;
  }

  const std::uint8_t weight = UnitToByte(progress);
  for (std::size_t channel = 0; channel < result.color.size(); ++channel) {
    const std::uint8_t from = UnitToByte(outdoor_fog.color[channel]);
    const std::uint8_t to = UnitToByte(wmo_fog.color[channel]);
    result.color[channel] =
        static_cast<float>(BlendChannel(from, to, weight)) * kByteToUnit;
  }
  return result;
}

std::optional<FogState> ResolveRetailWmoFog(
    const data::wmo::WmoRoot& root,
    const data::wmo::WmoGroupHeader& containing_group,
    const Vec3& local_camera_position, const float far_clip,
    const WmoFogLiquidState liquid) noexcept {

  if (root.fogs.size() <= 1u) {
    return std::nullopt;
  }

  FogPair resolved = ReadFogPair(root.fogs.front());
  RetailFogCandidateHeap candidates;

  for (const std::uint8_t fog_index : containing_group.fogIndices) {
    if (fog_index == 0u || fog_index >= root.fogs.size()) {
      continue;
    }
    const data::wmo::WmoFog& fog = root.fogs[fog_index];
    const float dx = fog.position[0] - local_camera_position[0];
    const float dy = fog.position[1] - local_camera_position[1];
    const float dz = fog.position[2] - local_camera_position[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance < fog.largerRadius && (fog.flags & 1u) == 0u) {
      candidates.Push({distance, fog_index});
    }
  }

  while (!candidates.Empty()) {
    const FogCandidate candidate = candidates.Pop();
    const data::wmo::WmoFog& fog = root.fogs[candidate.fog_index];
    const float factor = candidate.distance < fog.smallerRadius
                             ? 1.0f
                             : 1.0f - (candidate.distance - fog.smallerRadius) /
                                          (fog.largerRadius - fog.smallerRadius);
    const FogPair overlay = ReadFogPair(fog);
    BlendBand(resolved.normal, overlay.normal, factor);
    BlendBand(resolved.underwater, overlay.underwater, factor);
  }

  bool use_underwater_band = false;
  if (liquid.liquid_type_id != 0u) {

    constexpr std::uint32_t kLiquidForcesNormalFog = 0x20u;
    constexpr std::uint32_t kLiquidRequiresFogOptIn = 0x100u;
    constexpr std::uint32_t kFogAllowsRequiredLiquid = 0x10u;
    constexpr std::uint32_t kFogForcesUnderwaterBand = 0x100u;
    const std::uint32_t base_flags = root.fogs.front().flags;
    use_underwater_band =
        (liquid.liquid_type_flags & kLiquidForcesNormalFog) == 0u ||
        (base_flags & kFogForcesUnderwaterBand) != 0u;
    if ((liquid.liquid_type_flags & kLiquidRequiresFogOptIn) != 0u &&
        (base_flags & kFogAllowsRequiredLiquid) == 0u) {
      use_underwater_band = false;
    }
    if (!use_underwater_band) {
      return std::nullopt;
    }
  }

  return FinalizeFog(use_underwater_band ? resolved.underwater
                                         : resolved.normal,
                     far_clip);
}

}

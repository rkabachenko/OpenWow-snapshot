#pragma once

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/coordinates/world_geometry.h"
#include "openwow/world/streaming/world_streaming_ownership.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::world {

struct WmoAreaGroupRef {
  WmoPlacementKey placement{};
  std::uint32_t group_index{0u};

  [[nodiscard]] friend bool operator<(const WmoAreaGroupRef &lhs,
                                      const WmoAreaGroupRef &rhs) noexcept {
    return lhs.placement < rhs.placement ||
           (!(rhs.placement < lhs.placement) &&
            lhs.group_index < rhs.group_index);
  }

  [[nodiscard]] friend bool operator==(const WmoAreaGroupRef &lhs,
                                       const WmoAreaGroupRef &rhs) noexcept {
    return lhs.placement == rhs.placement &&
           lhs.group_index == rhs.group_index;
  }
};

inline constexpr float kWmoRoomHitFractionTolerance = 0.0001f;
inline constexpr float kWmoRoomInitialFraction = 1.05f;

[[nodiscard]] inline constexpr std::size_t RetailWmoRoomPlacementSlot(
    const std::uint16_t placement_flags) noexcept {
  return (placement_flags & 0x400u) != 0u ? 0u : 1u;
}

struct WmoRoomHit {
  float fraction{kWmoRoomInitialFraction};
  std::optional<WmoAreaGroupRef> room;
};

struct WmoRoomHitLane {
  WmoRoomHit primary;
  WmoRoomHit secondary;
};

inline bool AccumulateWmoRoomHit(WmoRoomHit &hit,
                                 const WmoAreaGroupRef ref,
                                 const float fraction) {

  if (!std::isfinite(fraction) || fraction > hit.fraction) {
    return false;
  }
  hit.fraction = fraction;
  hit.room = ref;
  return true;
}

inline void NormalizeRetailWmoRoomHits(
    std::array<WmoRoomHitLane, 2u> &placement_slots) {
  WmoRoomHitLane &slot0 = placement_slots[0];
  WmoRoomHitLane &slot1 = placement_slots[1];

  if (!slot0.primary.room.has_value() && slot1.primary.room.has_value()) {
    slot0.primary = slot1.primary;
  }

  if (!slot0.secondary.room.has_value() && slot1.secondary.room.has_value()) {
    slot0.secondary = slot1.secondary;
  }

  if (!slot0.primary.room.has_value()) {
    if (!slot0.secondary.room.has_value()) {
      return;
    }
    const WmoRoomHit original_slot1_secondary = slot1.secondary;
    slot0.primary = slot0.secondary;
    slot1.primary = original_slot1_secondary;
    slot0.secondary = WmoRoomHit{};
    slot1.secondary = WmoRoomHit{};
  }

  if (!slot1.primary.room.has_value()) {
    slot1.primary = slot0.primary;
  }

  if (!slot1.secondary.room.has_value()) {
    slot1.secondary = slot0.secondary;
  }
}

struct WmoPortalRoomCorrection {
  WmoAreaGroupRef primary{};
  std::optional<WmoAreaGroupRef> secondary{};
  float fraction{kWmoRoomInitialFraction};
};

namespace detail {

[[nodiscard]] inline float WmoPortalDot(
    const data::wmo::Vec3f& lhs,
    const data::wmo::Vec3f& rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] inline data::wmo::Vec3f WmoPortalSubtract(
    const data::wmo::Vec3f& lhs,
    const data::wmo::Vec3f& rhs) noexcept {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] inline data::wmo::Vec3f WmoPortalCross(
    const data::wmo::Vec3f& lhs,
    const data::wmo::Vec3f& rhs) noexcept {
  return {lhs.y * rhs.z - lhs.z * rhs.y,
          lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] inline bool WmoPointInsidePortal(
    const data::wmo::Vec3f& point, const data::wmo::Vec3f& normal,
    const std::span<const data::wmo::Vec3f> vertices) noexcept {
  if (vertices.size() < 3u) {
    return false;
  }
  float winding_sign = 0.0f;
  for (std::size_t index = 0u; index < vertices.size(); ++index) {
    const auto& start = vertices[index];
    const auto& end = vertices[(index + 1u) % vertices.size()];
    const float side = WmoPortalDot(
        WmoPortalCross(WmoPortalSubtract(end, start),
                       WmoPortalSubtract(point, start)),
        normal);
    if (std::abs(side) <= 1.0e-5f) {
      continue;
    }
    if (winding_sign == 0.0f) {
      winding_sign = side;
    } else if (std::signbit(side) != std::signbit(winding_sign)) {
      return false;
    }
  }
  return true;
}

}

[[nodiscard]] inline std::optional<WmoPortalRoomCorrection>
CorrectWmoRoomHitThroughPortal(
    const data::wmo::WmoRoot& root, const data::wmo::WmoGroup& group,
    const WmoAreaGroupRef source_room,
    const std::array<float, 3>& segment_start,
    const std::array<float, 3>& segment_end,
    const float floor_fraction) noexcept {
  if ((group.header.flags & data::wmo::kMogpExterior) != 0u) {
    return std::nullopt;
  }

  const data::wmo::Vec3f start{segment_start[0], segment_start[1],
                               segment_start[2]};
  const data::wmo::Vec3f direction{
      segment_end[0] - segment_start[0],
      segment_end[1] - segment_start[1],
      segment_end[2] - segment_start[2]};
  float nearest_fraction = kWmoRoomInitialFraction;
  std::optional<WmoPortalRoomCorrection> correction;
  const std::size_t ref_begin = group.header.portalStart;
  const std::size_t ref_end = std::min(
      root.portalRefs.size(), ref_begin + group.header.portalCount);
  for (std::size_t ref_index = ref_begin; ref_index < ref_end; ++ref_index) {
    const auto& portal_ref = root.portalRefs[ref_index];
    if (portal_ref.portalIndex >= root.portals.size() ||
        portal_ref.groupIndex >= root.groupInfos.size() ||
        portal_ref.groupIndex == source_room.group_index) {
      continue;
    }
    const auto& portal = root.portals[portal_ref.portalIndex];
    const std::size_t vertex_begin = portal.startVertex;
    if (portal.nVertices < 3u || vertex_begin > root.portalVertices.size() ||
        portal.nVertices > root.portalVertices.size() - vertex_begin) {
      continue;
    }
    const data::wmo::Vec3f normal{portal.normal[0], portal.normal[1],
                                  portal.normal[2]};
    const float denominator = detail::WmoPortalDot(normal, direction);
    if (std::abs(denominator) <= 1.0e-8f) {
      continue;
    }
    const float start_distance =
        detail::WmoPortalDot(normal, start) + portal.distance;
    const float fraction = -start_distance / denominator;
    if (fraction < 0.0f || fraction > nearest_fraction ||
        !(fraction - floor_fraction < kWmoRoomHitFractionTolerance)) {
      continue;
    }
    const data::wmo::Vec3f intersection{
        start.x + direction.x * fraction,
        start.y + direction.y * fraction,
        start.z + direction.z * fraction};
    const auto vertices = std::span<const data::wmo::Vec3f>(
        root.portalVertices.data() + vertex_begin, portal.nVertices);
    if (!detail::WmoPointInsidePortal(intersection, normal, vertices)) {
      continue;
    }

    const bool camera_on_positive_side = start_distance >= 0.0f;
    const bool source_on_positive_side = portal_ref.side > 0;
    std::uint32_t primary_group_index;
    std::uint32_t secondary_group_index;
    if (camera_on_positive_side == source_on_positive_side) {
      primary_group_index = source_room.group_index;
      secondary_group_index = portal_ref.groupIndex;
    } else {
      primary_group_index = portal_ref.groupIndex;
      secondary_group_index = source_room.group_index;
    }
    nearest_fraction = fraction;

    std::optional<WmoAreaGroupRef> secondary_room;
    if (secondary_group_index < root.groupInfos.size() &&
        (root.groupInfos[secondary_group_index].flags &
         data::wmo::kMogpExterior) == 0u) {
      secondary_room = WmoAreaGroupRef{.placement = source_room.placement,
                                       .group_index = secondary_group_index};
    }
    correction = WmoPortalRoomCorrection{
        .primary = {.placement = source_room.placement,
                    .group_index = primary_group_index},
        .secondary = secondary_room,
        .fraction = fraction};
  }
  return correction;
}

class WmoAreaSpatialIndex {
public:
  using Cell = std::pair<std::int32_t, std::int32_t>;
  static constexpr float kCellSize = 64.0f;

  [[nodiscard]] static Cell CellFor(const float x, const float y) noexcept {
    return {static_cast<std::int32_t>(std::floor(x / kCellSize)),
            static_cast<std::int32_t>(std::floor(y / kCellSize))};
  }

  void Add(const WmoAreaGroupRef ref, const Bounds &world_bounds) {
    const Cell minimum = CellFor(world_bounds[0], world_bounds[1]);
    const Cell maximum = CellFor(world_bounds[3], world_bounds[4]);
    std::vector<Cell> &occupied = placement_cells_[ref.placement];
    for (std::int32_t cell_y = minimum.second; cell_y <= maximum.second;
         ++cell_y) {
      for (std::int32_t cell_x = minimum.first; cell_x <= maximum.first;
           ++cell_x) {
        const Cell cell{cell_x, cell_y};
        auto &entries = cells_[cell];
        const auto insertion = std::lower_bound(entries.begin(), entries.end(), ref);
        if (insertion == entries.end() || *insertion != ref) {
          entries.insert(insertion, ref);
        }
        const auto occupancy =
            std::lower_bound(occupied.begin(), occupied.end(), cell);
        if (occupancy == occupied.end() || *occupancy != cell) {
          occupied.insert(occupancy, cell);
        }
      }
    }
  }

  void RemovePlacement(const WmoPlacementKey placement) {

    const auto occupied = placement_cells_.find(placement);
    if (occupied == placement_cells_.end()) {
      return;
    }
    for (const Cell cell : occupied->second) {
      const auto entry = cells_.find(cell);
      if (entry == cells_.end()) {
        continue;
      }
      std::erase_if(entry->second, [&](const WmoAreaGroupRef &candidate) {
        return candidate.placement == placement;
      });
      if (entry->second.empty()) {
        cells_.erase(entry);
      }
    }
    placement_cells_.erase(occupied);
  }

  [[nodiscard]] const std::vector<WmoAreaGroupRef> *Candidates(
      const float x, const float y) const noexcept {
    const auto cell = cells_.find(CellFor(x, y));
    return cell == cells_.end() ? nullptr : &cell->second;
  }

  [[nodiscard]] std::vector<WmoAreaGroupRef> Candidates(
      const Bounds &world_bounds) const {
    std::vector<WmoAreaGroupRef> result;
    const Cell minimum = CellFor(world_bounds[0], world_bounds[1]);
    const Cell maximum = CellFor(world_bounds[3], world_bounds[4]);
    for (std::int32_t cell_y = minimum.second; cell_y <= maximum.second;
         ++cell_y) {
      for (std::int32_t cell_x = minimum.first; cell_x <= maximum.first;
           ++cell_x) {
        const auto cell = cells_.find({cell_x, cell_y});
        if (cell != cells_.end()) {
          result.insert(result.end(), cell->second.begin(), cell->second.end());
        }
      }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  }

  void Clear() noexcept {
    cells_.clear();
    placement_cells_.clear();
  }
  [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }

private:
  std::map<Cell, std::vector<WmoAreaGroupRef>> cells_;

  std::map<WmoPlacementKey, std::vector<Cell>> placement_cells_;
};

class WmoAreaTriangleIndex {
public:
  using Cell = std::pair<std::int32_t, std::int32_t>;
  static constexpr float kCellSize = 8.0f;

  static constexpr float kBarycentricEpsilon = 1.0e-5f;
  static constexpr float kSegmentEpsilon = 1.0e-6f;

  static constexpr float kFaceRejectAbsoluteSlop = 0.25f;

  [[nodiscard]] static WmoAreaTriangleIndex Build(
      const data::wmo::WmoGroup &group) {
    std::vector<PendingFace> primary_faces;
    std::vector<PendingFace> secondary_faces;

    primary_faces.reserve(group.bspFaceIndices.size());
    secondary_faces.reserve(group.bspFaceIndices.size());
    const auto add_triangle = [&](const std::size_t offset) {
      const std::size_t triangle = offset / 3u;
      if (triangle >= group.triangleMaterials.size()) {
        return;
      }
      const std::uint8_t flags = group.triangleMaterials[triangle].flags;

      constexpr std::uint8_t kWmoRoomQueryRejectMask =
          data::wmo::kMopyNoCamCollide | data::wmo::kMopyFCollideHit;
      if ((kWmoRoomQueryRejectMask & flags) != 0u) {
        return;
      }
      const bool primary =
          (flags & data::wmo::kMopyFRender) != 0u ||
          (flags & data::wmo::kMopyFCollision) != 0u;
      const bool secondary =
          (flags & data::wmo::kMopyFRender) != 0u ||
          ((flags & data::wmo::kMopyFCollision) == 0u &&
           (flags & data::wmo::kMopyFDetail) != 0u);
      if (!primary && !secondary) {
        return;
      }
      const std::uint16_t i0 = group.indices[offset];
      const std::uint16_t i1 = group.indices[offset + 1u];
      const std::uint16_t i2 = group.indices[offset + 2u];
      if (i0 >= group.vertices.size() || i1 >= group.vertices.size() ||
          i2 >= group.vertices.size()) {
        return;
      }
      const auto &a = group.vertices[i0];
      const auto &b = group.vertices[i1];
      const auto &c = group.vertices[i2];
      const PendingFace face{
          .key = 0u,
          .offset = static_cast<std::uint32_t>(offset),
          .bounds = RejectBoundsFor(a, b, c),
      };

      const Cell minimum = CellFor(face.bounds.min_x, face.bounds.min_y);
      const Cell maximum = CellFor(face.bounds.max_x, face.bounds.max_y);
      for (std::int32_t cell_y = minimum.second; cell_y <= maximum.second;
           ++cell_y) {
        for (std::int32_t cell_x = minimum.first; cell_x <= maximum.first;
             ++cell_x) {
          PendingFace binned = face;
          binned.key = CellKey(cell_x, cell_y);
          if (primary) {
            primary_faces.push_back(binned);
          }
          if (secondary) {
            secondary_faces.push_back(binned);
          }
        }
      }
    };
    std::unordered_set<std::uint32_t> indexed_triangle_offsets;
    indexed_triangle_offsets.reserve(group.bspFaceIndices.size());
    for (const std::uint16_t triangle : group.bspFaceIndices) {
      const std::size_t offset = static_cast<std::size_t>(triangle) * 3u;
      if (offset + 2u < group.indices.size() &&
          indexed_triangle_offsets
              .insert(static_cast<std::uint32_t>(offset))
              .second) {
        add_triangle(offset);
      }
    }
    WmoAreaTriangleIndex index;
    index.primary_ = BuildLane(std::move(primary_faces));
    index.secondary_ = BuildLane(std::move(secondary_faces));
    return index;
  }

  template <typename Visitor>
  void VisitSurfaceZ(const data::wmo::WmoGroup &group, const float x,
                     const float y, Visitor &&visitor) const {
    const FaceBounds query{x, y, x, y};
    const Cell cell = CellFor(x, y);
    ForEachFaceInCell(primary_, cell, query, [&](const std::uint32_t offset) {
      const std::uint16_t i0 = group.indices[offset];
      const std::uint16_t i1 = group.indices[offset + 1u];
      const std::uint16_t i2 = group.indices[offset + 2u];
      if (i0 >= group.vertices.size() || i1 >= group.vertices.size() ||
          i2 >= group.vertices.size()) {
        return;
      }
      const auto z = TriangleZ(x, y, group.vertices[i0], group.vertices[i1],
                               group.vertices[i2]);
      if (z.has_value()) {
        visitor(*z);
      }
    });
  }

  template <typename PrimaryVisitor, typename SecondaryVisitor>
  void VisitRoomSegmentFractions(const data::wmo::WmoGroup &group,
                                 const std::array<float, 3> &start,
                                 const std::array<float, 3> &end,
                                 PrimaryVisitor &&primary_visitor,
                                 SecondaryVisitor &&secondary_visitor) const {
    const std::array<float, 3> direction{
        end[0] - start[0], end[1] - start[1], end[2] - start[2]};

    const float pad_x = kSegmentEpsilon * std::abs(direction[0]);
    const float pad_y = kSegmentEpsilon * std::abs(direction[1]);
    const FaceBounds query{
        .min_x = std::min(start[0], end[0]) - pad_x,
        .min_y = std::min(start[1], end[1]) - pad_y,
        .max_x = std::max(start[0], end[0]) + pad_x,
        .max_y = std::max(start[1], end[1]) + pad_y,
    };
    const Cell minimum = CellFor(query.min_x, query.min_y);
    const Cell maximum = CellFor(query.max_x, query.max_y);
    const auto visit = [&](const Lane &lane, auto &&visitor) {
      const auto emit = [&](const std::uint32_t offset) {
        const std::uint16_t i0 = group.indices[offset];
        const std::uint16_t i1 = group.indices[offset + 1u];
        const std::uint16_t i2 = group.indices[offset + 2u];
        if (i0 >= group.vertices.size() || i1 >= group.vertices.size() ||
            i2 >= group.vertices.size()) {
          return;
        }
        const auto fraction = SegmentTriangleFraction(
            start, direction, group.vertices[i0], group.vertices[i1],
            group.vertices[i2]);
        if (fraction.has_value()) {
          visitor(*fraction);
        }
      };
      if (minimum == maximum) {
        ForEachFaceInCell(lane, minimum, query, emit);
        return;
      }
      for (std::int32_t cell_y = minimum.second; cell_y <= maximum.second;
           ++cell_y) {
        for (std::int32_t cell_x = minimum.first; cell_x <= maximum.first;
             ++cell_x) {
          ForEachOwnedFaceInCell(lane, Cell{cell_x, cell_y}, query, emit);
        }
      }
    };
    visit(primary_, primary_visitor);
    visit(secondary_, secondary_visitor);
  }

  [[nodiscard]] std::size_t cell_count() const noexcept {
    return primary_.cells.size();
  }

private:

  struct FaceBounds {
    float min_x{0.0f};
    float min_y{0.0f};
    float max_x{0.0f};
    float max_y{0.0f};
  };

  struct PendingFace {
    std::uint64_t key{0u};
    std::uint32_t offset{0u};
    FaceBounds bounds{};
  };

  struct CellSpan {
    std::uint64_t key{0u};
    std::uint32_t begin{0u};
    std::uint32_t end{0u};
  };

  struct Lane {

    std::vector<CellSpan> cells;

    std::vector<FaceBounds> face_bounds;
    std::vector<std::uint32_t> face_offsets;
  };

  [[nodiscard]] static std::uint64_t CellKey(const std::int32_t cell_x,
                                             const std::int32_t cell_y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_y))
            << 32u) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell_x));
  }

  [[nodiscard]] static Lane BuildLane(std::vector<PendingFace> faces) {

    std::stable_sort(faces.begin(), faces.end(),
                     [](const PendingFace &lhs, const PendingFace &rhs) {
                       return lhs.key < rhs.key;
                     });
    Lane lane;
    lane.face_bounds.reserve(faces.size());
    lane.face_offsets.reserve(faces.size());
    for (const PendingFace &face : faces) {
      if (lane.cells.empty() || lane.cells.back().key != face.key) {
        lane.cells.push_back(CellSpan{
            .key = face.key,
            .begin = static_cast<std::uint32_t>(lane.face_offsets.size()),
            .end = static_cast<std::uint32_t>(lane.face_offsets.size()),
        });
      }
      lane.face_bounds.push_back(face.bounds);
      lane.face_offsets.push_back(face.offset);
      lane.cells.back().end =
          static_cast<std::uint32_t>(lane.face_offsets.size());
    }
    return lane;
  }

  [[nodiscard]] static const CellSpan *FindCell(const Lane &lane,
                                                const Cell cell) noexcept {
    const std::uint64_t key = CellKey(cell.first, cell.second);
    const auto span = std::lower_bound(
        lane.cells.begin(), lane.cells.end(), key,
        [](const CellSpan &entry, const std::uint64_t probe) {
          return entry.key < probe;
        });
    if (span == lane.cells.end() || span->key != key) {
      return nullptr;
    }
    return &*span;
  }

  [[nodiscard]] static bool BoxesOverlap(const FaceBounds &face,
                                         const FaceBounds &query) noexcept {
    return face.min_x <= query.max_x && query.min_x <= face.max_x &&
           face.min_y <= query.max_y && query.min_y <= face.max_y;
  }

  template <typename FaceVisitor>
  static void ForEachFaceInCell(const Lane &lane, const Cell cell,
                                const FaceBounds &query,
                                FaceVisitor &&visitor) {
    const CellSpan *const span = FindCell(lane, cell);
    if (span == nullptr) {
      return;
    }
    for (std::uint32_t index = span->begin; index < span->end; ++index) {
      if (!BoxesOverlap(lane.face_bounds[index], query)) {
        continue;
      }
      visitor(lane.face_offsets[index]);
    }
  }

  template <typename FaceVisitor>
  static void ForEachOwnedFaceInCell(const Lane &lane, const Cell cell,
                                     const FaceBounds &query,
                                     FaceVisitor &&visitor) {
    const CellSpan *const span = FindCell(lane, cell);
    if (span == nullptr) {
      return;
    }
    for (std::uint32_t index = span->begin; index < span->end; ++index) {
      const FaceBounds &bounds = lane.face_bounds[index];
      if (!BoxesOverlap(bounds, query)) {
        continue;
      }
      if (CellFor(std::max(bounds.min_x, query.min_x),
                  std::max(bounds.min_y, query.min_y)) != cell) {
        continue;
      }
      visitor(lane.face_offsets[index]);
    }
  }

  [[nodiscard]] static FaceBounds RejectBoundsFor(
      const data::wmo::Vec3f &a, const data::wmo::Vec3f &b,
      const data::wmo::Vec3f &c) noexcept {
    constexpr float kBarycentricSlackTerms = 2.0f;
    const float pad_x =
        kBarycentricSlackTerms * kBarycentricEpsilon *
            (std::abs(b.x - a.x) + std::abs(c.x - a.x)) +
        kFaceRejectAbsoluteSlop;
    const float pad_y =
        kBarycentricSlackTerms * kBarycentricEpsilon *
            (std::abs(b.y - a.y) + std::abs(c.y - a.y)) +
        kFaceRejectAbsoluteSlop;
    return FaceBounds{
        .min_x = std::min({a.x, b.x, c.x}) - pad_x,
        .min_y = std::min({a.y, b.y, c.y}) - pad_y,
        .max_x = std::max({a.x, b.x, c.x}) + pad_x,
        .max_y = std::max({a.y, b.y, c.y}) + pad_y,
    };
  }

  [[nodiscard]] static Cell CellFor(const float x, const float y) noexcept {
    return {static_cast<std::int32_t>(std::floor(x / kCellSize)),
            static_cast<std::int32_t>(std::floor(y / kCellSize))};
  }

  [[nodiscard]] static std::optional<float> TriangleZ(
      const float x, const float y, const data::wmo::Vec3f &a,
      const data::wmo::Vec3f &b, const data::wmo::Vec3f &c) noexcept {
    const float denominator =
        (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(denominator) < 1.0e-10f) {
      return std::nullopt;
    }
    const float u = ((b.y - c.y) * (x - c.x) +
                     (c.x - b.x) * (y - c.y)) /
                    denominator;
    const float v = ((c.y - a.y) * (x - c.x) +
                     (a.x - c.x) * (y - c.y)) /
                    denominator;
    const float w = 1.0f - u - v;
    if (u < -kBarycentricEpsilon || v < -kBarycentricEpsilon ||
        w < -kBarycentricEpsilon) {
      return std::nullopt;
    }
    return u * a.z + v * b.z + w * c.z;
  }

  [[nodiscard]] static std::optional<float> SegmentTriangleFraction(
      const std::array<float, 3> &origin,
      const std::array<float, 3> &direction,
      const data::wmo::Vec3f &a, const data::wmo::Vec3f &b,
      const data::wmo::Vec3f &c) noexcept {
    const std::array<float, 3> edge1{b.x - a.x, b.y - a.y, b.z - a.z};
    const std::array<float, 3> edge2{c.x - a.x, c.y - a.y, c.z - a.z};
    const std::array<float, 3> p{
        direction[1] * edge2[2] - direction[2] * edge2[1],
        direction[2] * edge2[0] - direction[0] * edge2[2],
        direction[0] * edge2[1] - direction[1] * edge2[0]};
    const float determinant =
        edge1[0] * p[0] + edge1[1] * p[1] + edge1[2] * p[2];
    if (std::abs(determinant) < 1.0e-10f) {
      return std::nullopt;
    }
    const float inverse_determinant = 1.0f / determinant;
    const std::array<float, 3> t{origin[0] - a.x, origin[1] - a.y,
                                 origin[2] - a.z};
    const float u =
        (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) * inverse_determinant;
    if (u < -kBarycentricEpsilon || u > 1.0f + kBarycentricEpsilon) {
      return std::nullopt;
    }
    const std::array<float, 3> q{
        t[1] * edge1[2] - t[2] * edge1[1],
        t[2] * edge1[0] - t[0] * edge1[2],
        t[0] * edge1[1] - t[1] * edge1[0]};
    const float v = (direction[0] * q[0] + direction[1] * q[1] +
                     direction[2] * q[2]) * inverse_determinant;
    if (v < -kBarycentricEpsilon || u + v > 1.0f + kBarycentricEpsilon) {
      return std::nullopt;
    }
    const float fraction =
        (edge2[0] * q[0] + edge2[1] * q[1] + edge2[2] * q[2]) *
        inverse_determinant;
    if (fraction < -kSegmentEpsilon || fraction > 1.0f + kSegmentEpsilon) {
      return std::nullopt;
    }
    return std::clamp(fraction, 0.0f, 1.0f);
  }

  Lane primary_;

  Lane secondary_;
};

}

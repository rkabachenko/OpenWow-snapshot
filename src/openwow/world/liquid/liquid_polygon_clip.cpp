#include "openwow/world/liquid/liquid_polygon_clip.h"

#include <cmath>

namespace openwow::world {

void LiquidPolygonClipper::Begin() noexcept {
  vertices_.clear();
  edges_.clear();
  seed_vertex_count_ = 0u;

  plane_stamp_ = 0;
}

bool LiquidPolygonClipper::AddVertex(const std::array<float, 3>& position,
                                     const int original_index) noexcept {

  if (vertices_.size() >= kMaxVertices) {
    return false;
  }
  Vertex vertex{};
  vertex.position = position;
  vertex.original_index = original_index;
  vertices_.push_back(vertex);
  return true;
}

void LiquidPolygonClipper::Close() noexcept {

  seed_vertex_count_ = vertices_.size();
  if (seed_vertex_count_ == 0u) {
    return;
  }
  for (std::size_t index = 0u; index < seed_vertex_count_; ++index) {
    Vertex& vertex = vertices_[index];
    vertex.incoming_edge = static_cast<int>(index) - 1;
    vertex.outgoing_edge = static_cast<int>(index);
    vertex.plane_stamp = -1;
    edges_.push_back(Edge{.from = static_cast<int>(index),
                          .to = static_cast<int>(index) + 1,
                          .removed = false});
  }
  const int last_edge = static_cast<int>(edges_.size()) - 1;
  vertices_[0].incoming_edge = last_edge;
  edges_[static_cast<std::size_t>(last_edge)].to =
      edges_[static_cast<std::size_t>(last_edge)].to %
      static_cast<int>(seed_vertex_count_);
}

void LiquidPolygonClipper::ClipToPlane(const std::array<float, 4>& plane,
                                       const float side) noexcept {
  ++plane_stamp_;

  int crossings = 0;

  std::array<int, kMaxCrossingsPerPlane> crossing_edge{};
  std::array<int, kMaxCrossingsPerPlane> crossing_vertex{};
  std::array<int, kMaxCrossingsPerPlane> crossing_from_inside{};

  std::array<int, 2> closing_edge_endpoints{-1, -1};
  bool bailed = false;

  const std::size_t edge_count_at_entry = edges_.size();
  for (std::size_t edge_index = 0u; edge_index < edge_count_at_entry;
       ++edge_index) {
    if (edges_[edge_index].removed) {
      continue;
    }
    const int from_index = edges_[edge_index].from;
    const int to_index = edges_[edge_index].to;

    for (const int endpoint : {from_index, to_index}) {
      Vertex& vertex = vertices_[static_cast<std::size_t>(endpoint)];
      if (vertex.plane_stamp != plane_stamp_) {
        vertex.plane_distance = (plane[0] * vertex.position[0] +
                                 plane[1] * vertex.position[1] +
                                 plane[2] * vertex.position[2] + plane[3]) *
                                side;
        vertex.plane_stamp = plane_stamp_;
      }
    }

    const float from_distance =
        vertices_[static_cast<std::size_t>(from_index)].plane_distance;
    const float to_distance =
        vertices_[static_cast<std::size_t>(to_index)].plane_distance;
    if (!(from_distance < 0.0f || to_distance < 0.0f)) {

      continue;
    }
    if (!(from_distance >= 0.0f || to_distance > 0.0f ||
          to_distance == 0.0f)) {

      edges_[edge_index].removed = true;
      continue;
    }

    const float denominator = std::fabs(to_distance) + std::fabs(from_distance);
    if (!(denominator > kDegenerateCrossingEpsilon)) {
      continue;
    }
    ++crossings;
    if (crossings > kMaxCrossingsPerPlane) {
      bailed = true;
      break;
    }
    const float blend = std::fabs(from_distance) / denominator;

    const int from_inside = from_distance >= 0.0f ? 1 : 0;
    const auto slot = static_cast<std::size_t>(crossings - 1);
    crossing_edge[slot] = static_cast<int>(edge_index);
    crossing_from_inside[slot] = from_inside;
    const auto new_vertex_index = static_cast<int>(vertices_.size());
    crossing_vertex[slot] = new_vertex_index;
    closing_edge_endpoints[static_cast<std::size_t>(1 - from_inside)] =
        new_vertex_index;

    Vertex created{};
    const Vertex& from_vertex = vertices_[static_cast<std::size_t>(from_index)];
    const Vertex& to_vertex = vertices_[static_cast<std::size_t>(to_index)];
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
      created.position[axis] =
          from_vertex.position[axis] +
          (to_vertex.position[axis] - from_vertex.position[axis]) * blend;
    }
    created.original_index = -1;
    created.parent_a = from_index;
    created.parent_b = to_index;
    created.parent_blend = blend;

    if (from_inside != 0) {
      created.incoming_edge = static_cast<int>(edge_index);
      created.outgoing_edge = static_cast<int>(edges_.size());
    } else {
      created.outgoing_edge = static_cast<int>(edge_index);
      created.incoming_edge = static_cast<int>(edges_.size());
    }
    created.plane_stamp = plane_stamp_;
    created.plane_distance = 0.0f;
    vertices_.push_back(created);
  }

  if (!bailed && crossings == kMaxCrossingsPerPlane) {
    for (std::size_t slot = 0u; slot < kMaxCrossingsPerPlane; ++slot) {
      Edge& edge = edges_[static_cast<std::size_t>(crossing_edge[slot])];

      if (crossing_from_inside[slot] != 0) {
        edge.to = crossing_vertex[slot];
      } else {
        edge.from = crossing_vertex[slot];
      }
    }
    edges_.push_back(Edge{.from = closing_edge_endpoints[0],
                          .to = closing_edge_endpoints[1],
                          .removed = false});
    return;
  }

  vertices_.resize(vertices_.size() - static_cast<std::size_t>(crossings));
}

std::vector<int> LiquidPolygonClipper::BuildRing() const {
  std::vector<int> ring;

  std::size_t first_edge = edges_.size();
  for (std::size_t index = 0u; index < edges_.size(); ++index) {
    if (!edges_[index].removed) {
      first_edge = index;
      break;
    }
  }
  if (first_edge >= edges_.size()) {
    return ring;
  }

  const int start = edges_[first_edge].from;
  int current = start;
  for (std::size_t guard = 0u; guard <= vertices_.size(); ++guard) {
    ring.push_back(current);
    const Vertex& vertex = vertices_[static_cast<std::size_t>(current)];
    if (vertex.outgoing_edge < 0 ||
        static_cast<std::size_t>(vertex.outgoing_edge) >= edges_.size()) {
      return {};
    }
    const Edge& edge = edges_[static_cast<std::size_t>(vertex.outgoing_edge)];
    if (edge.removed) {
      return {};
    }
    current = edge.to;
    if (current == start) {
      return ring.size() >= 3u ? ring : std::vector<int>{};
    }
    if (current < 0 || static_cast<std::size_t>(current) >= vertices_.size()) {
      return {};
    }
  }
  return {};
}

}

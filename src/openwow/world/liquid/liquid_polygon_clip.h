#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::world {

class LiquidPolygonClipper {
 public:

  static constexpr std::size_t kMaxVertices = 32u;

  static constexpr std::size_t kMaxEdges = 16u;

  static constexpr float kDegenerateCrossingEpsilon = 1.1920929e-07f;

  static constexpr int kMaxCrossingsPerPlane = 2;

  struct Vertex {
    std::array<float, 3> position{};

    int original_index{-1};
    int parent_a{-1};
    int parent_b{-1};
    float parent_blend{0.0f};
    int incoming_edge{-1};
    int outgoing_edge{-1};
    std::int32_t plane_stamp{-1};
    float plane_distance{0.0f};
  };

  struct Edge {
    int from{-1};
    int to{-1};
    bool removed{false};
  };

  void Begin() noexcept;

  bool AddVertex(const std::array<float, 3>& position,
                 int original_index) noexcept;

  void Close() noexcept;

  void ClipToPlane(const std::array<float, 4>& plane, float side) noexcept;

  [[nodiscard]] std::vector<int> BuildRing() const;

  [[nodiscard]] const std::vector<Vertex>& vertices() const noexcept {
    return vertices_;
  }
  [[nodiscard]] const std::vector<Edge>& edges() const noexcept {
    return edges_;
  }

  template <typename Attribute, typename LerpFn>
  [[nodiscard]] Attribute ResolveAttribute(
      const int vertex_index, const std::vector<Attribute>& originals,
      LerpFn lerp, std::vector<Attribute>& memo,
      std::vector<bool>& memo_valid) const {
    if (vertex_index < 0 ||
        static_cast<std::size_t>(vertex_index) >= vertices_.size()) {
      return Attribute{};
    }
    const Vertex& vertex = vertices_[static_cast<std::size_t>(vertex_index)];
    if (vertex.original_index >= 0) {
      const auto original = static_cast<std::size_t>(vertex.original_index);
      return original < originals.size() ? originals[original] : Attribute{};
    }
    if (memo_valid[static_cast<std::size_t>(vertex_index)]) {
      return memo[static_cast<std::size_t>(vertex_index)];
    }
    const Attribute a = ResolveAttribute(vertex.parent_a, originals, lerp, memo,
                                         memo_valid);
    const Attribute b = ResolveAttribute(vertex.parent_b, originals, lerp, memo,
                                         memo_valid);
    Attribute resolved = lerp(a, b, vertex.parent_blend);
    memo[static_cast<std::size_t>(vertex_index)] = resolved;
    memo_valid[static_cast<std::size_t>(vertex_index)] = true;
    return resolved;
  }

  [[nodiscard]] std::size_t seed_vertex_count() const noexcept {
    return seed_vertex_count_;
  }

 private:
  std::vector<Vertex> vertices_;
  std::vector<Edge> edges_;
  std::size_t seed_vertex_count_{0u};
  std::int32_t plane_stamp_{0};
};

}

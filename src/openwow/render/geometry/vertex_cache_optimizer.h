#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::render {

inline constexpr std::size_t kPostTransformVertexCacheSize = 32u;

class VertexCacheOptimizer {
 public:

  void OptimizeTriangleList(std::span<std::uint16_t> triangle_indices,
                            std::size_t vertex_count);

 private:

  [[nodiscard]] std::size_t SimulateLocalMisses(
      std::span<const std::uint32_t> order, std::size_t local_vertex_count);

  std::vector<std::uint32_t> local_id_;
  std::vector<std::uint32_t> local_id_stamp_;
  std::uint32_t stamp_{0u};

  std::vector<std::uint16_t> local_vertices_;
  std::vector<std::uint32_t> local_indices_;
  std::vector<std::uint32_t> active_triangles_;
  std::vector<std::int32_t> cache_position_;
  std::vector<float> vertex_score_;
  std::vector<std::uint32_t> adjacency_offset_;
  std::vector<std::uint32_t> adjacency_;
  std::vector<std::uint8_t> triangle_emitted_;

  std::vector<std::uint32_t> triangle_rescored_step_;
  std::vector<std::uint32_t> emitted_order_;
  std::vector<std::uint32_t> dead_end_;
  std::vector<std::uint32_t> candidate_;
  std::vector<std::int32_t> fifo_slot_;
};

void OptimizeTriangleListForVertexCache(
    std::span<std::uint16_t> triangle_indices, std::size_t vertex_count);

[[nodiscard]] std::size_t SimulateVertexCacheMisses(
    std::span<const std::uint16_t> triangle_indices,
    std::size_t cache_size = kPostTransformVertexCacheSize);

}

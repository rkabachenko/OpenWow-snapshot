#include "openwow/render/geometry/vertex_cache_optimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace openwow::render {

namespace {

constexpr float kCacheDecayPower = 1.5f;

constexpr float kLastTriangleScore = 0.75f;

constexpr float kValenceBoostScale = 2.0f;
constexpr float kValenceBoostPower = 0.5f;

constexpr std::size_t kValenceScoreTableSize = 64u;

constexpr float kNoActiveTriangleScore = -1.0f;

struct ScoreTables {
  std::array<float, kPostTransformVertexCacheSize> cache_position{};
  std::array<float, kValenceScoreTableSize> valence{};

  ScoreTables() {
    constexpr float kPositionScaler =
        1.0f / static_cast<float>(kPostTransformVertexCacheSize - 3u);
    for (std::size_t position = 0u; position < kPostTransformVertexCacheSize;
         ++position) {
      if (position < 3u) {
        cache_position[position] = kLastTriangleScore;
        continue;
      }
      const float ramp =
          1.0f - static_cast<float>(position - 3u) * kPositionScaler;
      cache_position[position] = std::pow(ramp, kCacheDecayPower);
    }
    valence[0] = 0.0f;
    for (std::size_t valence_count = 1u; valence_count < kValenceScoreTableSize;
         ++valence_count) {
      valence[valence_count] =
          kValenceBoostScale *
          std::pow(static_cast<float>(valence_count), -kValenceBoostPower);
    }
  }
};

const ScoreTables& Tables() {
  static const ScoreTables tables;
  return tables;
}

[[nodiscard]] float ValenceScore(const std::uint32_t active_triangles) {
  if (active_triangles < kValenceScoreTableSize) {
    return Tables().valence[active_triangles];
  }
  return kValenceBoostScale *
         std::pow(static_cast<float>(active_triangles), -kValenceBoostPower);
}

[[nodiscard]] float VertexScore(const std::uint32_t active_triangles,
                                const std::int32_t cache_position) {
  if (active_triangles == 0u) {
    return kNoActiveTriangleScore;
  }
  const float position_score =
      cache_position < 0
          ? 0.0f
          : Tables().cache_position[static_cast<std::size_t>(cache_position)];
  return position_score + ValenceScore(active_triangles);
}

}

void VertexCacheOptimizer::OptimizeTriangleList(
    const std::span<std::uint16_t> triangle_indices,
    const std::size_t vertex_count) {
  const std::size_t index_count = triangle_indices.size();
  if (index_count % 3u != 0u || vertex_count == 0u) {
    return;
  }
  const std::size_t triangle_count = index_count / 3u;
  if (triangle_count < 2u) {
    return;
  }

  if (local_id_.size() < vertex_count) {
    local_id_.assign(vertex_count, 0u);
    local_id_stamp_.assign(vertex_count, 0u);
    stamp_ = 0u;
  }
  if (stamp_ == std::numeric_limits<std::uint32_t>::max()) {
    std::fill(local_id_stamp_.begin(), local_id_stamp_.end(), 0u);
    stamp_ = 0u;
  }
  ++stamp_;

  local_vertices_.clear();
  local_indices_.resize(index_count);
  for (std::size_t index = 0u; index < index_count; ++index) {
    const std::uint16_t global_vertex = triangle_indices[index];
    if (static_cast<std::size_t>(global_vertex) >= vertex_count) {
      return;
    }
    if (local_id_stamp_[global_vertex] != stamp_) {
      local_id_stamp_[global_vertex] = stamp_;
      local_id_[global_vertex] =
          static_cast<std::uint32_t>(local_vertices_.size());
      local_vertices_.push_back(global_vertex);
    }
    local_indices_[index] = local_id_[global_vertex];
  }
  const std::size_t local_vertex_count = local_vertices_.size();

  active_triangles_.assign(local_vertex_count, 0u);
  for (const std::uint32_t vertex : local_indices_) {
    ++active_triangles_[vertex];
  }
  adjacency_offset_.assign(local_vertex_count + 1u, 0u);
  std::uint32_t running_offset = 0u;
  for (std::size_t vertex = 0u; vertex < local_vertex_count; ++vertex) {
    adjacency_offset_[vertex] = running_offset;
    running_offset += active_triangles_[vertex];
  }
  adjacency_offset_[local_vertex_count] = running_offset;
  adjacency_.resize(index_count);

  for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const std::uint32_t vertex = local_indices_[triangle * 3u + corner];
      adjacency_[adjacency_offset_[vertex] + --active_triangles_[vertex]] =
          static_cast<std::uint32_t>(triangle);
    }
  }
  for (std::size_t vertex = 0u; vertex < local_vertex_count; ++vertex) {
    active_triangles_[vertex] =
        adjacency_offset_[vertex + 1u] - adjacency_offset_[vertex];
  }

  cache_position_.assign(local_vertex_count, -1);
  vertex_score_.resize(local_vertex_count);
  for (std::size_t vertex = 0u; vertex < local_vertex_count; ++vertex) {
    vertex_score_[vertex] = VertexScore(active_triangles_[vertex], -1);
  }
  triangle_emitted_.assign(triangle_count, 0u);
  triangle_rescored_step_.assign(triangle_count, 0u);
  std::int64_t best_triangle = -1;
  float best_score = kNoActiveTriangleScore;
  for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    const float score = vertex_score_[local_indices_[triangle * 3u]] +
                        vertex_score_[local_indices_[triangle * 3u + 1u]] +
                        vertex_score_[local_indices_[triangle * 3u + 2u]];
    if (score > best_score) {
      best_score = score;
      best_triangle = static_cast<std::int64_t>(triangle);
    }
  }

  const auto triangle_score_now = [&](const std::uint32_t triangle) {
    return vertex_score_[local_indices_[triangle * 3u]] +
           vertex_score_[local_indices_[triangle * 3u + 1u]] +
           vertex_score_[local_indices_[triangle * 3u + 2u]];
  };

  emitted_order_.clear();
  emitted_order_.reserve(triangle_count);
  dead_end_.clear();
  std::size_t scan_cursor = 0u;

  std::array<std::uint32_t, kPostTransformVertexCacheSize + 3u> cache{};
  std::array<std::uint32_t, kPostTransformVertexCacheSize + 3u> cache_next{};
  std::size_t cache_size = 0u;

  while (emitted_order_.size() < triangle_count) {
    if (best_triangle < 0) {

      while (!dead_end_.empty() && best_triangle < 0) {
        const std::uint32_t vertex = dead_end_.back();
        dead_end_.pop_back();
        if (active_triangles_[vertex] == 0u) {
          continue;
        }
        const std::uint32_t begin = adjacency_offset_[vertex];
        const std::uint32_t end = begin + active_triangles_[vertex];
        for (std::uint32_t slot = begin; slot < end; ++slot) {
          const std::uint32_t candidate = adjacency_[slot];
          const float score = triangle_score_now(candidate);
          if (score > best_score) {
            best_score = score;
            best_triangle = static_cast<std::int64_t>(candidate);
          }
        }
      }
      if (best_triangle < 0) {
        while (scan_cursor < triangle_count &&
               triangle_emitted_[scan_cursor] != 0u) {
          ++scan_cursor;
        }
        if (scan_cursor >= triangle_count) {
          break;
        }
        best_triangle = static_cast<std::int64_t>(scan_cursor);
      }
    }

    const auto emitted = static_cast<std::uint32_t>(best_triangle);
    triangle_emitted_[emitted] = 1u;
    emitted_order_.push_back(emitted);

    std::array<std::uint32_t, 3u> corners{};
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const std::uint32_t vertex = local_indices_[emitted * 3u + corner];
      corners[corner] = vertex;

      const std::uint32_t begin = adjacency_offset_[vertex];
      const std::uint32_t end = begin + active_triangles_[vertex];
      for (std::uint32_t slot = begin; slot < end; ++slot) {
        if (adjacency_[slot] != emitted) {
          continue;
        }
        adjacency_[slot] = adjacency_[end - 1u];
        adjacency_[end - 1u] = emitted;
        break;
      }
      --active_triangles_[vertex];
      dead_end_.push_back(vertex);
    }

    std::array<std::uint32_t, 3u> front{};
    std::size_t front_count = 0u;
    for (const std::uint32_t vertex : corners) {
      bool duplicate = false;
      for (std::size_t seen = 0u; seen < front_count; ++seen) {
        duplicate = duplicate || front[seen] == vertex;
      }
      if (!duplicate) {
        front[front_count++] = vertex;
      }
    }
    std::size_t next_size = front_count;
    for (std::size_t entry = 0u; entry < front_count; ++entry) {
      cache_next[entry] = front[entry];
    }
    for (std::size_t entry = 0u; entry < cache_size; ++entry) {
      const std::uint32_t vertex = cache[entry];
      bool moved_to_front = false;
      for (std::size_t seen = 0u; seen < front_count; ++seen) {
        moved_to_front = moved_to_front || front[seen] == vertex;
      }
      if (!moved_to_front) {
        cache_next[next_size++] = vertex;
      }
    }
    cache = cache_next;
    cache_size = next_size;

    for (std::size_t position = 0u; position < cache_size; ++position) {
      const std::uint32_t vertex = cache[position];
      cache_position_[vertex] =
          position < kPostTransformVertexCacheSize
              ? static_cast<std::int32_t>(position)
              : -1;
      vertex_score_[vertex] =
          VertexScore(active_triangles_[vertex], cache_position_[vertex]);
    }
    best_triangle = -1;
    best_score = kNoActiveTriangleScore;
    const auto step = static_cast<std::uint32_t>(emitted_order_.size());
    for (std::size_t position = 0u; position < cache_size; ++position) {
      const std::uint32_t vertex = cache[position];
      const std::uint32_t begin = adjacency_offset_[vertex];
      const std::uint32_t end = begin + active_triangles_[vertex];
      for (std::uint32_t slot = begin; slot < end; ++slot) {
        const std::uint32_t candidate = adjacency_[slot];
        if (triangle_rescored_step_[candidate] == step) {
          continue;
        }
        triangle_rescored_step_[candidate] = step;
        const float score = triangle_score_now(candidate);
        if (score > best_score) {
          best_score = score;
          best_triangle = static_cast<std::int64_t>(candidate);
        }
      }
    }
    cache_size = std::min(cache_size, kPostTransformVertexCacheSize);
  }

  if (emitted_order_.size() != triangle_count) {
    return;
  }

  candidate_.resize(index_count);
  for (std::size_t output = 0u; output < triangle_count; ++output) {
    const std::uint32_t source = emitted_order_[output];
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      candidate_[output * 3u + corner] = local_indices_[source * 3u + corner];
    }
  }

  if (SimulateLocalMisses(candidate_, local_vertex_count) >=
      SimulateLocalMisses(local_indices_, local_vertex_count)) {
    return;
  }

  for (std::size_t index = 0u; index < index_count; ++index) {
    triangle_indices[index] = local_vertices_[candidate_[index]];
  }
}

std::size_t VertexCacheOptimizer::SimulateLocalMisses(
    const std::span<const std::uint32_t> order,
    const std::size_t local_vertex_count) {
  fifo_slot_.assign(local_vertex_count, -1);
  std::array<std::uint32_t, kPostTransformVertexCacheSize> ring{};
  std::size_t head = 0u;
  std::size_t filled = 0u;
  std::size_t misses = 0u;
  for (const std::uint32_t vertex : order) {
    const std::int32_t slot = fifo_slot_[vertex];
    if (slot >= 0 && ring[static_cast<std::size_t>(slot)] == vertex) {
      continue;
    }
    ++misses;
    if (filled == kPostTransformVertexCacheSize) {
      fifo_slot_[ring[head]] = -1;
    } else {
      ++filled;
    }
    ring[head] = vertex;
    fifo_slot_[vertex] = static_cast<std::int32_t>(head);
    head = (head + 1u) % kPostTransformVertexCacheSize;
  }
  return misses;
}

void OptimizeTriangleListForVertexCache(
    const std::span<std::uint16_t> triangle_indices,
    const std::size_t vertex_count) {
  VertexCacheOptimizer optimizer;
  optimizer.OptimizeTriangleList(triangle_indices, vertex_count);
}

std::size_t SimulateVertexCacheMisses(
    const std::span<const std::uint16_t> triangle_indices,
    const std::size_t cache_size) {
  if (cache_size == 0u) {
    return triangle_indices.size();
  }
  std::vector<std::int32_t> fifo(cache_size, -1);
  std::size_t next_slot = 0u;
  std::size_t misses = 0u;
  for (const std::uint16_t vertex : triangle_indices) {
    const auto entry = static_cast<std::int32_t>(vertex);
    if (std::find(fifo.begin(), fifo.end(), entry) != fifo.end()) {
      continue;
    }
    ++misses;
    fifo[next_slot] = entry;
    next_slot = (next_slot + 1u) % cache_size;
  }
  return misses;
}

}

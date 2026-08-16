#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/geometry/vertex_cache_optimizer.h"

#include <bgfx/defines.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace openwow::render::m2 {

struct M2IndexSpan {
  std::uint32_t first_index{0};
  std::uint32_t index_count{0};

  std::uint32_t optimized_offset{0};
};

[[nodiscard]] constexpr bool M2DrawResolvesByDepthAlone(
    const std::uint64_t state) noexcept {
  return (state & BGFX_STATE_BLEND_MASK) == 0u &&
         (state & BGFX_STATE_BLEND_ALPHA_TO_COVERAGE) == 0u &&
         (state & BGFX_STATE_WRITE_Z) != 0u &&
         (state & BGFX_STATE_DEPTH_TEST_MASK) == BGFX_STATE_DEPTH_TEST_LESS &&
         (state & BGFX_STATE_CULL_MASK) != 0u;
}

inline constexpr std::size_t kM2VertexCacheOptimizeTriangleBudget = 2048u;

inline constexpr std::size_t kM2VertexCacheNoTriangleBudget =
    std::numeric_limits<std::size_t>::max();

struct M2VertexCacheOptimizedIndices {
  std::vector<std::uint16_t> indices;

  std::vector<M2IndexSpan> spans;
};

[[nodiscard]] inline M2VertexCacheOptimizedIndices
BuildM2VertexCacheOptimizedIndices(
    const std::span<const std::uint16_t> authored_indices,
    const std::span<const openwow::data::model::SkinSubmesh> submeshes,
    const std::size_t vertex_count,
    const std::size_t triangle_budget = kM2VertexCacheOptimizeTriangleBudget) {
  M2VertexCacheOptimizedIndices result;
  if (authored_indices.empty() || submeshes.empty() || vertex_count == 0u) {
    return result;
  }

  std::vector<std::size_t> by_first_index;
  by_first_index.reserve(submeshes.size());
  for (std::size_t submesh = 0u; submesh < submeshes.size(); ++submesh) {
    if (submeshes[submesh].index_count != 0u) {
      by_first_index.push_back(submesh);
    }
  }
  std::ranges::sort(by_first_index,
                    [submeshes](const std::size_t left, const std::size_t right) {
                      return submeshes[left].index_start <
                             submeshes[right].index_start;
                    });
  std::vector<std::uint8_t> overlapping(submeshes.size(), 0u);
  for (std::size_t position = 1u; position < by_first_index.size(); ++position) {
    const auto& previous = submeshes[by_first_index[position - 1u]];
    const auto& current = submeshes[by_first_index[position]];
    if (static_cast<std::uint32_t>(current.index_start) <
        static_cast<std::uint32_t>(previous.index_start) + previous.index_count) {
      overlapping[by_first_index[position - 1u]] = 1u;
      overlapping[by_first_index[position]] = 1u;
    }
  }

  std::vector<std::uint16_t> span_scratch;
  VertexCacheOptimizer optimizer;
  std::size_t remaining_triangles = triangle_budget;
  for (const std::size_t submesh_index : by_first_index) {
    if (overlapping[submesh_index]) {
      continue;
    }
    const auto& submesh = submeshes[submesh_index];
    const auto first_index = static_cast<std::uint32_t>(submesh.index_start);
    const auto index_count = static_cast<std::uint32_t>(submesh.index_count);

    if (index_count % 3u != 0u || first_index > authored_indices.size() ||
        index_count > authored_indices.size() - first_index) {
      continue;
    }
    const std::size_t triangles = index_count / 3u;
    if (triangles > remaining_triangles) {
      continue;
    }
    remaining_triangles -= triangles;
    const auto authored_span = authored_indices.subspan(first_index, index_count);
    span_scratch.assign(authored_span.begin(), authored_span.end());
    optimizer.OptimizeTriangleList(span_scratch, vertex_count);

    if (std::equal(span_scratch.begin(), span_scratch.end(),
                   authored_span.begin())) {
      continue;
    }
    result.spans.push_back({first_index, index_count,
                            static_cast<std::uint32_t>(result.indices.size())});
    result.indices.insert(result.indices.end(), span_scratch.begin(),
                          span_scratch.end());
  }
  return result;
}

[[nodiscard]] inline std::uint32_t ResolveM2DrawFirstIndex(
    const std::uint64_t state, const std::uint32_t first_index,
    const std::uint32_t index_count, const std::uint32_t optimized_base,
    const std::span<const M2IndexSpan> optimized_spans) noexcept {
  if (optimized_base == 0u || !M2DrawResolvesByDepthAlone(state)) {
    return first_index;
  }
  const auto span = std::ranges::lower_bound(
      optimized_spans, first_index, {}, &M2IndexSpan::first_index);
  if (span == optimized_spans.end() || span->first_index != first_index ||
      span->index_count != index_count) {
    return first_index;
  }
  return optimized_base + span->optimized_offset;
}

}

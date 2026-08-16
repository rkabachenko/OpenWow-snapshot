#include "openwow/render/m2/m2_cpu_skinning.h"

#include "openwow/render/api/math/render_math_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::render::m2 {

namespace {

constexpr float kInverseBoneWeightScale = 1.0f / 255.0f;

struct WeightedBoneMatrixCache {
  bool valid = false;
  std::uint32_t packed_weights = 0;
  std::uint32_t packed_indices = 0;
  RenderMatrix4x4 matrix{};
};

[[nodiscard]] std::uint32_t PackInfluenceBytes(
    const std::span<const std::uint8_t, 4> values) {
  return static_cast<std::uint32_t>(values[0]) |
         (static_cast<std::uint32_t>(values[1]) << 8u) |
         (static_cast<std::uint32_t>(values[2]) << 16u) |
         (static_cast<std::uint32_t>(values[3]) << 24u);
}

[[nodiscard]] std::optional<RenderMatrix4x4View> BoneMatrixAt(
    const std::span<const float> bone_matrices,
    const std::size_t matrix_index) noexcept {
  constexpr std::size_t kBoneMatrixFloatCount = 16u;
  const std::size_t matrix_count = bone_matrices.size() / kBoneMatrixFloatCount;
  if (matrix_index >= matrix_count) {
    return std::nullopt;
  }

  const std::size_t offset = matrix_index * kBoneMatrixFloatCount;
  return RenderMatrix4x4View{bone_matrices.data() + offset, kBoneMatrixFloatCount};
}

void ResetWeightedBoneMatrix(RenderMatrix4x4 &matrix) {
  std::fill(matrix.begin(), matrix.end(), 0.0f);
  matrix[15] = 1.0f;
}

void SetWeightedBoneMatrixIdentity(RenderMatrix4x4 &matrix) {
  ResetWeightedBoneMatrix(matrix);
  matrix[0] = 1.0f;
  matrix[5] = 1.0f;
  matrix[10] = 1.0f;
}

void AccumulateBoneMatrix(const RenderMatrix4x4View bone_matrix,
                          const float normalized_weight,
                          RenderMatrix4x4 &weighted_matrix) {
  weighted_matrix[0] += bone_matrix[0] * normalized_weight;
  weighted_matrix[1] += bone_matrix[1] * normalized_weight;
  weighted_matrix[2] += bone_matrix[2] * normalized_weight;
  weighted_matrix[4] += bone_matrix[4] * normalized_weight;
  weighted_matrix[5] += bone_matrix[5] * normalized_weight;
  weighted_matrix[6] += bone_matrix[6] * normalized_weight;
  weighted_matrix[8] += bone_matrix[8] * normalized_weight;
  weighted_matrix[9] += bone_matrix[9] * normalized_weight;
  weighted_matrix[10] += bone_matrix[10] * normalized_weight;
  weighted_matrix[12] += bone_matrix[12] * normalized_weight;
  weighted_matrix[13] += bone_matrix[13] * normalized_weight;
  weighted_matrix[14] += bone_matrix[14] * normalized_weight;
}

void RebuildWeightedBoneMatrix(const openwow::data::model::M2Vertex &vertex,
                               const std::span<const float> bone_matrices,
                               WeightedBoneMatrixCache &cache) {
  cache.packed_weights = PackInfluenceBytes(vertex.bone_weights);
  cache.packed_indices = PackInfluenceBytes(vertex.bone_indices);
  cache.valid = true;

  if (cache.packed_weights == 0u) {
    SetWeightedBoneMatrixIdentity(cache.matrix);
    return;
  }

  ResetWeightedBoneMatrix(cache.matrix);

  const auto accumulate_influence = [&](const int influence_index) {
    const auto raw_weight = vertex.bone_weights[influence_index];
    if (raw_weight == 0u) {
      return;
    }

    const std::size_t lookup_index =
        static_cast<std::size_t>(vertex.bone_indices[influence_index]);
    const auto bone_matrix = BoneMatrixAt(bone_matrices, lookup_index);
    if (!bone_matrix.has_value()) {
      return;
    }

    const float normalized_weight =
        static_cast<float>(raw_weight) * kInverseBoneWeightScale;
    AccumulateBoneMatrix(*bone_matrix, normalized_weight, cache.matrix);
  };

  accumulate_influence(0);
  for (int influence_index = 1; influence_index < 4; ++influence_index) {
    if (vertex.bone_weights[influence_index] == 0u) {
      break;
    }
    accumulate_influence(influence_index);
  }
}

[[nodiscard]] bx::Vec3 TransformNormalByWeightedBoneMatrix(
    const RenderMatrix4x4 &weighted_matrix,
    const openwow::data::model::M2Vertex &vertex) {
  const RenderVec3View normal{vertex.normal};
  return bx::Vec3{
      weighted_matrix[0] * normal[0] + weighted_matrix[4] * normal[1] +
          weighted_matrix[8] * normal[2],
      weighted_matrix[1] * normal[0] + weighted_matrix[5] * normal[1] +
          weighted_matrix[9] * normal[2],
      weighted_matrix[2] * normal[0] + weighted_matrix[6] * normal[1] +
          weighted_matrix[10] * normal[2],
  };
}

[[nodiscard]] bx::Vec3 TransformPositionByWeightedBoneMatrix(
    const RenderMatrix4x4 &weighted_matrix,
    const openwow::data::model::M2Vertex &vertex) {
  const RenderVec3View position{vertex.position};
  return bx::Vec3{
      weighted_matrix[0] * position[0] + weighted_matrix[4] * position[1] +
          weighted_matrix[8] * position[2] + weighted_matrix[12],
      weighted_matrix[1] * position[0] + weighted_matrix[5] * position[1] +
          weighted_matrix[9] * position[2] + weighted_matrix[13],
      weighted_matrix[2] * position[0] + weighted_matrix[6] * position[1] +
          weighted_matrix[10] * position[2] + weighted_matrix[14],
  };
}

}

CpuSkinnedVertexStreams ComputeCpuSkinnedVertexStreams(
    const openwow::data::model::M2Model &model,
    const std::span<const float> bone_matrices) {
  return ComputeCpuSkinnedVertexStreams(
      std::span<const openwow::data::model::M2Vertex>(model.vertices.data(),
                                                      model.vertices.size()),
      bone_matrices);
}

CpuSkinnedVertexStreams ComputeCpuSkinnedVertexStreams(
    const std::span<const openwow::data::model::M2Vertex> vertices,
    const std::span<const float> bone_matrices) {
  CpuSkinnedVertexStreams streams;
  streams.positions.assign(vertices.size(), bx::Vec3{0.0f, 0.0f, 0.0f});
  streams.normals.assign(vertices.size(), bx::Vec3{0.0f, 0.0f, 0.0f});

  WeightedBoneMatrixCache cache;
  for (std::size_t vertex_index = 0; vertex_index < vertices.size(); ++vertex_index) {
    const auto &vertex = vertices[vertex_index];
    const auto packed_weights = PackInfluenceBytes(vertex.bone_weights);
    const auto packed_indices = PackInfluenceBytes(vertex.bone_indices);
    if (!cache.valid || cache.packed_weights != packed_weights ||
        cache.packed_indices != packed_indices) {
      RebuildWeightedBoneMatrix(vertex, bone_matrices, cache);
    }

    streams.positions[vertex_index] =
        TransformPositionByWeightedBoneMatrix(cache.matrix, vertex);
    streams.normals[vertex_index] = TransformNormalByWeightedBoneMatrix(cache.matrix, vertex);
  }

  return streams;
}

}

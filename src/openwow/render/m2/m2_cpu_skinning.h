#pragma once

#include "openwow/data/model/m2_model.h"

#include <bx/math.h>

#include <span>
#include <vector>

namespace openwow::render::m2 {

struct CpuSkinnedVertexStreams {
  std::vector<bx::Vec3> positions;
  std::vector<bx::Vec3> normals;
};

[[nodiscard]] CpuSkinnedVertexStreams ComputeCpuSkinnedVertexStreams(
    const openwow::data::model::M2Model &model,
    std::span<const float> bone_matrices);

[[nodiscard]] CpuSkinnedVertexStreams ComputeCpuSkinnedVertexStreams(
    std::span<const openwow::data::model::M2Vertex> vertices,
    std::span<const float> bone_matrices);

}

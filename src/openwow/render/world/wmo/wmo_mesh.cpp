#include "openwow/render/world/wmo/wmo_mesh.h"

#include "openwow/render/geometry/vertex_cache_optimizer.h"
#include "openwow/render/world/wmo/wmo_renderer.h"
#include "openwow/world/wmo/wmo_vertex_color_query.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace openwow::render {

using namespace data::wmo;

namespace {

bool ShouldEmitDebugTriangle(const WmoTriangleMaterial &triangle_material,
                             WmoDebugGeometryMode mode) {
  const uint8_t flags = triangle_material.flags;
  switch (mode) {
  case WmoDebugGeometryMode::Collision:
    return (flags & kMopyFUnk01) != 0u &&
           ((flags & kMopyFRender) != 0u ||
            (flags & kMopyFDetail) != 0u);
  case WmoDebugGeometryMode::OutdoorSurface:

    return (flags & kMopyFRender) != 0u && (flags & kMopyFDetail) == 0u;
  case WmoDebugGeometryMode::Primary:
    return (flags & kMopyFDetail) != 0u;
  case WmoDebugGeometryMode::TwoPass:
    return false;
  }
  return false;
}

bool IsRenderableFaceImpl(uint8_t mopy_flags) {

  if (mopy_flags & kMopyFCollision)
    return true;
  if ((mopy_flags & kMopyFRender) && !(mopy_flags & kMopyFDetail))
    return true;
  return false;
}

std::vector<uint16_t> BuildDebugTriangleIndexList(const WmoGroup &group,
                                                  WmoDebugGeometryMode mode) {
  std::vector<uint16_t> debug_indices;

  const std::size_t triangle_count =
      std::min(group.triangleMaterials.size(), group.indices.size() / 3);
  debug_indices.reserve(triangle_count * 3);

  for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
    if (!ShouldEmitDebugTriangle(group.triangleMaterials[triangle_index], mode)) {
      continue;
    }

    const std::size_t index_base = triangle_index * 3;
    debug_indices.push_back(group.indices[index_base + 0]);
    debug_indices.push_back(group.indices[index_base + 1]);
    debug_indices.push_back(group.indices[index_base + 2]);
  }

  return debug_indices;
}

std::vector<uint16_t> BuildRenderableTriangleIndexList(
    const WmoGroup& group) {
  std::vector<uint16_t> indices;
  const std::size_t triangle_count =
      std::min(group.triangleMaterials.size(), group.indices.size() / 3u);
  indices.reserve(triangle_count * 3u);
  for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    if (!IsRenderableFaceImpl(group.triangleMaterials[triangle].flags)) {
      continue;
    }
    const std::size_t first = triangle * 3u;
    indices.insert(indices.end(), group.indices.begin() + first,
                   group.indices.begin() + first + 3u);
  }
  return indices;
}

bool GroupUsesCompositeVertices(const WmoRoot* const root,
                                const WmoGroup& group) {
  if (root == nullptr) {
    return false;
  }

  return std::ranges::any_of(group.renderBatches, [&](const auto& batch) {
    return batch.materialId < root->materials.size() &&
           root->materials[batch.materialId].shader == kShaderTwoLayer;
  });
}

}

bool IsRenderableFace(uint8_t mopy_flags) {
  return IsRenderableFaceImpl(mopy_flags);
}

world::WmoGroupPublicationStatus ClassifyWmoGroupPublication(
    const WmoGroup& group) {
  bool has_drawable_triangle = false;
  for (const WmoRenderBatch& batch : group.renderBatches) {
    if (batch.startIndex > group.indices.size() ||
        batch.nIndices > group.indices.size() - batch.startIndex ||
        batch.startIndex % 3u != 0u || batch.nIndices % 3u != 0u) {
      return world::WmoGroupPublicationStatus::kFailed;
    }
    for (std::size_t index = batch.startIndex;
         index < static_cast<std::size_t>(batch.startIndex) + batch.nIndices;
         index += 3u) {
      if (group.indices[index] >= group.vertices.size() ||
          group.indices[index + 1u] >= group.vertices.size() ||
          group.indices[index + 2u] >= group.vertices.size()) {
        return world::WmoGroupPublicationStatus::kFailed;
      }
      has_drawable_triangle = true;
    }
  }
  return has_drawable_triangle
             ? world::WmoGroupPublicationStatus::kDrawableReady
             : world::WmoGroupPublicationStatus::kNoGeometry;
}

world::WmoGroupPublicationStatus ClassifyWmoGroupPublication(
    const WmoGroup& group, const std::size_t material_count) {
  const auto geometry = ClassifyWmoGroupPublication(group);
  if (geometry != world::WmoGroupPublicationStatus::kDrawableReady) {
    return geometry;
  }
  return std::ranges::any_of(group.renderBatches, [&](const auto& batch) {
           return batch.materialId >= material_count;
         })
             ? world::WmoGroupPublicationStatus::kFailed
             : geometry;
}

namespace {

std::vector<std::uint8_t> FindOverlappingWmoBatches(
    const std::vector<WmoBatchMesh>& batches) {
  std::vector<std::uint8_t> overlapping(batches.size(), 0u);
  std::vector<std::size_t> by_start;
  by_start.reserve(batches.size());
  for (std::size_t batch = 0u; batch < batches.size(); ++batch) {
    if (batches[batch].index_count != 0u) {
      by_start.push_back(batch);
    }
  }
  std::ranges::sort(by_start, [&batches](const std::size_t a,
                                         const std::size_t b) {
    return batches[a].start_index < batches[b].start_index;
  });
  for (std::size_t position = 1u; position < by_start.size(); ++position) {
    const WmoBatchMesh& previous = batches[by_start[position - 1u]];
    const WmoBatchMesh& current = batches[by_start[position]];
    if (current.start_index <
        previous.start_index + previous.index_count) {
      overlapping[by_start[position - 1u]] = 1u;
      overlapping[by_start[position]] = 1u;
    }
  }
  return overlapping;
}

void OptimizeWmoBatchTriangleOrder(WmoGroupMesh& mesh,
                                   const WmoRoot* const root) {
  if (root == nullptr || mesh.batches.empty() || mesh.indices.empty()) {

    return;
  }
  const std::vector<std::uint8_t> overlapping =
      FindOverlappingWmoBatches(mesh.batches);
  VertexCacheOptimizer optimizer;
  for (std::size_t batch_index = 0u; batch_index < mesh.batches.size();
       ++batch_index) {
    const WmoBatchMesh& batch = mesh.batches[batch_index];
    if (overlapping[batch_index] || batch.index_count == 0u ||
        batch.material_index >= root->materials.size()) {
      continue;
    }
    const WmoMaterial& material = root->materials[batch.material_index];
    if (!WmoBatchTriangleOrderIsUnobservable(material.blendMode, material.flags,
                                             batch.region)) {
      continue;
    }
    if (batch.start_index > mesh.indices.size() ||
        batch.index_count > mesh.indices.size() - batch.start_index) {
      continue;
    }
    optimizer.OptimizeTriangleList(
        std::span<std::uint16_t>(mesh.indices)
            .subspan(batch.start_index, batch.index_count),
        mesh.vertices.size());
  }
}

WmoGroupMesh GenerateWmoGroupMeshImpl(const WmoGroup &group,
                                      const uint32_t mohd_flags,
                                      const WmoRoot* const root) {
  WmoGroupMesh mesh;
  mesh.flags = group.header.flags;

  mesh.bounds_min[0] = mesh.bounds_min[1] = mesh.bounds_min[2] = std::numeric_limits<float>::max();
  mesh.bounds_max[0] = mesh.bounds_max[1] = mesh.bounds_max[2] =
      std::numeric_limits<float>::lowest();

  const std::size_t vert_count = group.vertices.size();
  mesh.vertices.resize(vert_count);

  const bool has_normals = group.normals.size() == vert_count;
  const bool has_uvs = group.texCoords.size() == vert_count;
  const bool has_colors = group.vertexColors.size() == vert_count;
  const bool use_composite_vertices = GroupUsesCompositeVertices(root, group);
  const bool has_uvs2 = group.texCoords2.size() == vert_count;
  const bool has_colors2 = group.vertexColors2.size() == vert_count;
  if (use_composite_vertices) {
    mesh.composite_vertices.resize(vert_count);
  }
  const openwow::world::WmoVertexColorPreparation color_preparation =
      openwow::world::BuildWmoVertexColorPreparation(
          group.renderBatches, group.header.transBatchCount, mohd_flags);

  for (std::size_t i = 0; i < vert_count; ++i) {
    WmoVertex &v = mesh.vertices[i];

    v.position[0] = group.vertices[i].x;
    v.position[1] = group.vertices[i].y;
    v.position[2] = group.vertices[i].z;

    if (has_normals) {
      v.normal[0] = group.normals[i].x;
      v.normal[1] = group.normals[i].y;
      v.normal[2] = group.normals[i].z;
    } else {
      v.normal[0] = 0.0f;
      v.normal[1] = 0.0f;
      v.normal[2] = 1.0f;
    }

    if (has_uvs) {
      v.texcoord[0] = group.texCoords[i].u;
      v.texcoord[1] = group.texCoords[i].v;
    } else {
      v.texcoord[0] = 0.0f;
      v.texcoord[1] = 0.0f;
    }

    if (has_colors) {
      auto vc = openwow::world::PrepareWmoVertexColor(
          group.vertexColors[i], i, color_preparation);
      if (root != nullptr) {
        vc = openwow::world::PrepareWmoPortalVertexColor(
            *root, group, group.vertices[i], vc, i, color_preparation);
      }
      v.color = static_cast<uint32_t>(vc.r) | (static_cast<uint32_t>(vc.g) << 8) |
                (static_cast<uint32_t>(vc.b) << 16) | (static_cast<uint32_t>(vc.a) << 24);
    } else {

      v.color = (mohd_flags & data::wmo::kWmoFlagUnifiedRender) != 0u ? 0xFF000000u : 0xFF7F7F7Fu;
    }

    if (use_composite_vertices) {
      WmoCompositeVertex& composite = mesh.composite_vertices[i];
      std::copy(std::begin(v.position), std::end(v.position),
                std::begin(composite.position));
      std::copy(std::begin(v.normal), std::end(v.normal),
                std::begin(composite.normal));
      composite.color = v.color;
      if (has_colors2) {
        const WmoVertexColor& color2 = group.vertexColors2[i];
        composite.color2 = static_cast<uint32_t>(color2.r) |
                           (static_cast<uint32_t>(color2.g) << 8u) |
                           (static_cast<uint32_t>(color2.b) << 16u) |
                           (static_cast<uint32_t>(color2.a) << 24u);
      } else {
        composite.color2 = 0u;
      }
      std::copy(std::begin(v.texcoord), std::end(v.texcoord),
                std::begin(composite.texcoord));
      if (has_uvs2) {
        composite.texcoord2[0] = group.texCoords2[i].u;
        composite.texcoord2[1] = group.texCoords2[i].v;
      } else {
        composite.texcoord2[0] = 0.0f;
        composite.texcoord2[1] = 0.0f;
      }
    }

    mesh.bounds_min[0] = std::min(mesh.bounds_min[0], v.position[0]);
    mesh.bounds_min[1] = std::min(mesh.bounds_min[1], v.position[1]);
    mesh.bounds_min[2] = std::min(mesh.bounds_min[2], v.position[2]);
    mesh.bounds_max[0] = std::max(mesh.bounds_max[0], v.position[0]);
    mesh.bounds_max[1] = std::max(mesh.bounds_max[1], v.position[1]);
    mesh.bounds_max[2] = std::max(mesh.bounds_max[2], v.position[2]);
  }

  if (vert_count == 0) {
    std::memcpy(mesh.bounds_min, group.header.boundingBox1, sizeof(mesh.bounds_min));
    std::memcpy(mesh.bounds_max, group.header.boundingBox2, sizeof(mesh.bounds_max));
  }

  mesh.indices = group.indices;
  mesh.collision_indices = BuildDebugTriangleIndexList(group, WmoDebugGeometryMode::Collision);
  mesh.outdoor_surface_indices =
      BuildDebugTriangleIndexList(group, WmoDebugGeometryMode::OutdoorSurface);
  mesh.primary_debug_indices = BuildDebugTriangleIndexList(group, WmoDebugGeometryMode::Primary);
  mesh.renderable_indices = BuildRenderableTriangleIndexList(group);

  mesh.batches.reserve(group.renderBatches.size());
  for (std::size_t batch_index = 0; batch_index < group.renderBatches.size();
       ++batch_index) {
    const auto &rb = group.renderBatches[batch_index];
    WmoBatchMesh batch;
    batch.material_index = rb.materialId;
    batch.start_index = rb.startIndex;
    if (rb.startIndex <= group.indices.size() &&
        rb.nIndices <= group.indices.size() - rb.startIndex) {
      batch.index_count = rb.nIndices;
    }
    if (batch_index < group.header.transBatchCount) {
      batch.region = WmoBatchMesh::Region::Transition;
    } else if (batch_index < static_cast<std::size_t>(group.header.transBatchCount) +
                                 group.header.intBatchCount) {
      batch.region = WmoBatchMesh::Region::Interior;
    } else {
      batch.region = WmoBatchMesh::Region::Exterior;
    }
    mesh.batches.push_back(batch);
  }

  OptimizeWmoBatchTriangleOrder(mesh, root);

  return mesh;
}

}

WmoGroupMesh GenerateWmoGroupMesh(const WmoGroup& group,
                                  const uint32_t mohd_flags) {
  return GenerateWmoGroupMeshImpl(group, mohd_flags, nullptr);
}

WmoGroupMesh GenerateWmoGroupMesh(const WmoRoot& root,
                                  const WmoGroup& group) {
  return GenerateWmoGroupMeshImpl(group, root.header.flags, &root);
}

}

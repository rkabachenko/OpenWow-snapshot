#include "openwow/render/m2/m2_skin_geometry.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <numeric>

namespace {

constexpr std::size_t kBoneMatrixFloatCount = 16u;

void SetError(std::string* const out_error, const char* const message) {
  if (out_error != nullptr) {
    *out_error = message;
  }
}

}

namespace openwow::render::m2 {

bool BuildM2SkinGeometry(const openwow::data::model::M2Model& model,
                         const openwow::data::model::M2Skin& skin,
                         M2SkinGeometry* const out_geometry) {
  if (out_geometry == nullptr) {
    return false;
  }

  out_geometry->vertices.clear();
  out_geometry->query_vertices.clear();
  out_geometry->model_vertex_by_skin_vertex.clear();
  out_geometry->indices.clear();
  out_geometry->normalized_skin = {};
  out_geometry->vertex_cache_optimized = {};
  out_geometry->submesh_bone_index_bound.clear();
  out_geometry->error.clear();

  if (model.vertices.empty() || skin.indices.empty() || skin.triangles.empty()) {
    out_geometry->error = "missing vertices/indices";
    return false;
  }

  out_geometry->model_vertex_by_skin_vertex.reserve(skin.indices.size());
  out_geometry->vertices.reserve(skin.indices.size());
  out_geometry->query_vertices.reserve(skin.indices.size());
  for (const auto model_vertex_index : skin.indices) {
    if (static_cast<std::size_t>(model_vertex_index) >= model.vertices.size()) {
      out_geometry->error = "skin vertex lookup out of bounds";
      return false;
    }
    out_geometry->model_vertex_by_skin_vertex.push_back(model_vertex_index);
    out_geometry->vertices.push_back(model.vertices[model_vertex_index]);
    out_geometry->query_vertices.push_back(model.vertices[model_vertex_index]);
  }

  if (!skin.properties.empty() && skin.properties.size() < out_geometry->vertices.size() * 4u) {
    out_geometry->error = "skin bone properties out of bounds";
    return false;
  }

  for (const auto& submesh : skin.submeshes) {
    const std::size_t vertex_start = submesh.vertex_start;
    const std::size_t vertex_count = submesh.vertex_count;
    if (vertex_start > out_geometry->vertices.size() ||
        vertex_count > out_geometry->vertices.size() - vertex_start) {
      out_geometry->error = "submesh vertex range out of bounds";
      return false;
    }
    if (submesh.bone_influences > 4u) {
      out_geometry->error = "submesh bone influence count out of bounds";
      return false;
    }
    if (submesh.bone_influences == 0u || skin.properties.empty()) {
      continue;
    }

    for (std::size_t skin_vertex = vertex_start;
         skin_vertex < vertex_start + vertex_count; ++skin_vertex) {
      auto& vertex = out_geometry->vertices[skin_vertex];
      auto& query_vertex = out_geometry->query_vertices[skin_vertex];
      const std::size_t property_offset = skin_vertex * 4u;
      for (std::uint16_t influence = 0; influence < 4u; ++influence) {
        vertex.bone_indices[influence] = skin.properties[property_offset + influence];
      }
      for (std::uint16_t influence = 0; influence < submesh.bone_influences; ++influence) {
        if (vertex.bone_weights[influence] == 0u) {
          break;
        }
        if (vertex.bone_indices[influence] >= submesh.bone_count) {
          out_geometry->error = "skin bone lookup out of bounds";
          return false;
        }
        const std::size_t bone_lookup_index =
            static_cast<std::size_t>(submesh.bone_combo_index) + vertex.bone_indices[influence];
        if (bone_lookup_index >= model.bone_lookup.size() ||
            model.bone_lookup[bone_lookup_index] > 255u) {
          out_geometry->error = "skin bone lookup out of bounds";
          return false;
        }
        query_vertex.bone_indices[influence] =
            static_cast<std::uint8_t>(model.bone_lookup[bone_lookup_index]);
      }
    }
  }

  out_geometry->indices.reserve(skin.triangles.size());
  for (const auto skin_vertex_index : skin.triangles) {
    if (static_cast<std::size_t>(skin_vertex_index) >= skin.indices.size()) {
      out_geometry->error = "triangle index out of bounds";
      return false;
    }
    out_geometry->indices.push_back(skin_vertex_index);
  }
  if (out_geometry->vertices.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u) {
    out_geometry->error = "skin vertex stream too large";
    return false;
  }

  out_geometry->submesh_bone_index_bound.assign(skin.submeshes.size(), 0u);
  for (std::size_t submesh_index = 0; submesh_index < skin.submeshes.size();
       ++submesh_index) {
    const auto& submesh = skin.submeshes[submesh_index];
    const std::size_t index_start = submesh.index_start;
    const std::size_t index_count = submesh.index_count;
    if (index_count == 0u || index_start > out_geometry->indices.size() ||
        index_count > out_geometry->indices.size() - index_start) {

      continue;
    }
    std::uint32_t bound = 0u;
    for (std::size_t position = index_start; position < index_start + index_count;
         ++position) {
      const auto& vertex = out_geometry->vertices[out_geometry->indices[position]];
      for (const auto influence : vertex.bone_indices) {
        bound = std::max<std::uint32_t>(bound, static_cast<std::uint32_t>(influence) + 1u);
      }
    }
    out_geometry->submesh_bone_index_bound[submesh_index] =
        static_cast<std::uint16_t>(bound);
  }

  out_geometry->normalized_skin = skin;
  out_geometry->normalized_skin.indices.resize(out_geometry->vertices.size());
  std::iota(out_geometry->normalized_skin.indices.begin(),
            out_geometry->normalized_skin.indices.end(),
            static_cast<std::uint16_t>(0));
  out_geometry->normalized_skin.triangles = out_geometry->indices;
  out_geometry->normalized_skin.header.n_indices =
      static_cast<std::uint32_t>(out_geometry->normalized_skin.indices.size());
  out_geometry->normalized_skin.header.n_triangles =
      static_cast<std::uint32_t>(out_geometry->normalized_skin.triangles.size());
  out_geometry->normalized_skin.header.n_properties =
      static_cast<std::uint32_t>(out_geometry->normalized_skin.properties.size() / 4u);
  out_geometry->normalized_skin.header.n_submeshes =
      static_cast<std::uint32_t>(out_geometry->normalized_skin.submeshes.size());
  out_geometry->normalized_skin.header.n_texture_units =
      static_cast<std::uint32_t>(out_geometry->normalized_skin.texture_units.size());

  out_geometry->vertex_cache_optimized = BuildM2VertexCacheOptimizedIndices(
      out_geometry->indices, out_geometry->normalized_skin.submeshes,
      out_geometry->vertices.size(), kM2VertexCacheNoTriangleBudget);

  return true;
}

bool BuildM2SubmeshBonePalette(const openwow::data::model::M2Model& model,
                               const openwow::data::model::M2Skin& skin,
                               const std::size_t submesh_index,
                               const std::span<const float> global_bone_matrices,
                               M2BonePaletteBuffer* const out_palette,
                               std::string* const out_error) {
  if (out_palette == nullptr || submesh_index >= skin.submeshes.size()) {
    SetError(out_error, "skin submesh out of bounds");
    return false;
  }

  out_palette->Clear();
  const auto& submesh = skin.submeshes[submesh_index];
  if (submesh.bone_count == 0u) {
    out_palette->Assign({1.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f});
    return true;
  }

  if (submesh.bone_count > kM2MaxGpuBonePaletteMatrices ||
      static_cast<std::size_t>(submesh.bone_combo_index) + submesh.bone_count >
          model.bone_lookup.size()) {
    SetError(out_error, "skin bone lookup out of bounds");
    return false;
  }
  if (global_bone_matrices.size() % kBoneMatrixFloatCount != 0u) {
    SetError(out_error, "global bone matrix span is invalid");
    return false;
  }

  const std::size_t global_matrix_count = global_bone_matrices.size() / kBoneMatrixFloatCount;
  const std::size_t combo_index = static_cast<std::size_t>(submesh.bone_combo_index);
  const std::size_t bone_count = submesh.bone_count;

  for (std::size_t local_index = 0; local_index < bone_count; ++local_index) {
    if (static_cast<std::size_t>(model.bone_lookup[combo_index + local_index]) >=
        global_matrix_count) {
      SetError(out_error, "bone palette matrix out of bounds");
      return false;
    }
  }

  out_palette->ResizeUninitialized(bone_count * kBoneMatrixFloatCount);
  float* const palette = out_palette->data();
  const float* const global_matrices = global_bone_matrices.data();
  for (std::size_t local_index = 0; local_index < bone_count; ++local_index) {
    const std::size_t global_index = model.bone_lookup[combo_index + local_index];
    std::memcpy(palette + local_index * kBoneMatrixFloatCount,
                global_matrices + global_index * kBoneMatrixFloatCount,
                kBoneMatrixFloatCount * sizeof(float));
  }
  return true;
}

}

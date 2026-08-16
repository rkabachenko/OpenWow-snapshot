#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/m2/m2_index_order.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openwow::render::m2 {

inline constexpr std::uint32_t kM2MaxGpuBonePaletteMatrices = 256u;

struct M2SkinGeometry {
  std::vector<openwow::data::model::M2Vertex> vertices;

  std::vector<openwow::data::model::M2Vertex> query_vertices;
  std::vector<std::uint16_t> model_vertex_by_skin_vertex;
  std::vector<std::uint16_t> indices;
  openwow::data::model::M2Skin normalized_skin;

  M2VertexCacheOptimizedIndices vertex_cache_optimized;

  std::vector<std::uint16_t> submesh_bone_index_bound;
  std::string error;
};

[[nodiscard]] bool BuildM2SkinGeometry(
    const openwow::data::model::M2Model& model,
    const openwow::data::model::M2Skin& skin,
    M2SkinGeometry* out_geometry);

class M2BonePaletteBuffer {
 public:
  M2BonePaletteBuffer() = default;
  M2BonePaletteBuffer(M2BonePaletteBuffer&&) noexcept = default;
  M2BonePaletteBuffer& operator=(M2BonePaletteBuffer&&) noexcept = default;
  M2BonePaletteBuffer(const M2BonePaletteBuffer&) = delete;
  M2BonePaletteBuffer& operator=(const M2BonePaletteBuffer&) = delete;

  void ResizeUninitialized(std::size_t count) {
    if (count > capacity_) {
      std::unique_ptr<float[]> grown(new float[count]);
      if (size_ != 0u) {
        std::memcpy(grown.get(), storage_.get(), size_ * sizeof(float));
      }
      storage_ = std::move(grown);
      capacity_ = count;
    }
    size_ = count;
  }
  void Clear() noexcept { size_ = 0u; }

  void Assign(std::initializer_list<float> values) {
    ResizeUninitialized(values.size());
    std::copy(values.begin(), values.end(), storage_.get());
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0u; }
  [[nodiscard]] float* data() noexcept { return storage_.get(); }
  [[nodiscard]] const float* data() const noexcept { return storage_.get(); }
  [[nodiscard]] float& operator[](std::size_t index) noexcept {
    return storage_[index];
  }
  [[nodiscard]] const float& operator[](std::size_t index) const noexcept {
    return storage_[index];
  }
  [[nodiscard]] std::span<const float> Span() const noexcept {
    return std::span<const float>(storage_.get(), size_);
  }

 private:
  std::unique_ptr<float[]> storage_;
  std::size_t size_ = 0u;
  std::size_t capacity_ = 0u;
};

[[nodiscard]] bool BuildM2SubmeshBonePalette(
    const openwow::data::model::M2Model& model,
    const openwow::data::model::M2Skin& skin,
    std::size_t submesh_index,
    std::span<const float> global_bone_matrices,
    M2BonePaletteBuffer* out_palette,
    std::string* out_error = nullptr);

}

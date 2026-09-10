#pragma once

#include "openwow/render/api/math/render_math_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace bgfx {
struct Encoder;
}

namespace openwow::render {

inline constexpr std::size_t kWorldShadowProductCount = 4u;

enum class WorldShadowProduct : std::uint8_t {
  Center = 0,
  Near = 1,
  Mid = 2,
  Far = 3,
};

class ShadowRenderData {
 public:
  ShadowRenderData();
  ~ShadowRenderData();

  ShadowRenderData(const ShadowRenderData&) = delete;
  ShadowRenderData& operator=(const ShadowRenderData&) = delete;

  void Configure(std::uint8_t quality, std::uint16_t resolution);
  [[nodiscard]] std::uint8_t quality() const noexcept { return quality_; }
  [[nodiscard]] std::uint16_t resolution() const noexcept {
    return resolution_;
  }
  [[nodiscard]] std::size_t active_product_count() const noexcept;

  [[nodiscard]] bool CreateResources();
  void DestroyResources();
  [[nodiscard]] bool resources_valid() const noexcept;

  void SetLightDirection(const RenderVec3& surface_to_light) noexcept;
  [[nodiscard]] bool PrepareProduct(std::size_t product_index,
                                    const RenderVec3& target);
  void BeginShadowDepthPass(std::size_t product_index,
                            std::uint8_t view_id) const;

  [[nodiscard]] const float* light_view(std::size_t product_index) const;
  [[nodiscard]] const float* light_projection(std::size_t product_index) const;
  [[nodiscard]] RenderVec3 product_target(std::size_t product_index) const;
  [[nodiscard]] float product_half_extent(std::size_t product_index) const;

  void SetPublishedProductCount(std::size_t count) noexcept;
  [[nodiscard]] std::size_t published_product_count() const noexcept {
    return published_product_count_;
  }

  void BindTerrainShadowState(bgfx::Encoder* encoder = nullptr) const;
  void BindModelShadowState(bool enabled,
                            bgfx::Encoder* encoder = nullptr) const;

 private:
  struct BackendResources;

  void BindReceiverState(std::size_t first_product, bool enabled,
                         bgfx::Encoder* encoder) const;

  std::unique_ptr<BackendResources> backend_;
  std::array<RenderMatrix4x4, kWorldShadowProductCount> light_views_{};
  std::array<RenderMatrix4x4, kWorldShadowProductCount> light_projections_{};
  std::array<RenderMatrix4x4, kWorldShadowProductCount> receiver_matrices_{};
  std::array<RenderVec3, kWorldShadowProductCount> product_targets_{};
  RenderVec3 surface_to_light_{0.0f, -1.0f, 0.0f};
  std::uint8_t quality_{0u};
  std::uint16_t resolution_{1024u};
  std::size_t published_product_count_{0u};
};

void SetActiveWorldShadowRenderData(const ShadowRenderData* data) noexcept;
void BindActiveWorldModelShadowState(bool enabled,
                                     bgfx::Encoder* encoder = nullptr) noexcept;

}

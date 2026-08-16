#pragma once

#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <algorithm>

namespace openwow::render {

struct BgfxColumnMajorViewProjection {
  RenderMatrix4x4View view;
  RenderMatrix4x4View projection;
};

class ViewProjection {
 public:
  [[nodiscard]] static ViewProjection CopyOf(const float* view,
                                                    const float* projection,
                                                    const bool homogeneous_depth = true) noexcept {
    ViewProjection result;
    result.homogeneous_depth_ = homogeneous_depth;
    if (view != nullptr) {
      std::copy_n(view, result.view_.size(), result.view_.begin());
    }
    if (projection != nullptr) {
      std::copy_n(projection, result.projection_.size(), result.projection_.begin());
    }
    if (!homogeneous_depth) {
      result.bgfx_projection_ = result.projection_;

      for (std::size_t row = 0; row < 4; ++row) {
        const std::size_t row_offset = row * 4;
        result.bgfx_projection_[row_offset + 2] =
            0.5f * (result.projection_[row_offset + 2] +
                    result.projection_[row_offset + 3]);
      }
    }
    return result;
  }

  [[nodiscard]] const RenderMatrix4x4& view() const noexcept { return view_; }
  [[nodiscard]] const RenderMatrix4x4& projection() const noexcept {
    return projection_;
  }
  [[nodiscard]] bool homogeneous_depth() const noexcept { return homogeneous_depth_; }
  [[nodiscard]] const RenderMatrix4x4& bgfx_view() const noexcept { return view_; }
  [[nodiscard]] const RenderMatrix4x4& bgfx_projection() const noexcept {
    return homogeneous_depth_ ? projection_ : bgfx_projection_;
  }

  [[nodiscard]] RenderMatrix4x4 BuildViewProjection() const noexcept {
    return MultiplyMatrix4x4(view_, projection_);
  }

  [[nodiscard]] BgfxColumnMajorViewProjection AsBgfxColumnMajor() const noexcept {
    return {
        .view = RenderMatrix4x4View{view_},
        .projection = RenderMatrix4x4View{bgfx_projection()},
    };
  }

 private:
  RenderMatrix4x4 view_{kRenderIdentityMatrix4x4};
  RenderMatrix4x4 projection_{kRenderIdentityMatrix4x4};
  RenderMatrix4x4 bgfx_projection_{kRenderIdentityMatrix4x4};
  bool homogeneous_depth_ = true;
};

}

#pragma once

#include <algorithm>

namespace openwow::ui {

struct CRect {
  float left{0.0f};
  float bottom{0.0f};
  float right{0.0f};
  float top{0.0f};

  [[nodiscard]] constexpr bool IsEmpty() const noexcept {
    return right <= left || top <= bottom;
  }

  [[nodiscard]] static CRect Intersect(const CRect& a,
                                       const CRect& b) noexcept {
    return {
        std::max(a.left, b.left),
        std::max(a.bottom, b.bottom),
        std::min(a.right, b.right),
        std::min(a.top, b.top),
    };
  }

  CRect IntersectWith(const CRect& other) noexcept {
    const CRect result = Intersect(*this, other);
    *this = result;
    return result;
  }
};

struct RectEdgesYUp {
  float bottom{0.0f};
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
};

struct RectBoundsYUp {
  float min_x{0.0f};
  float min_y{0.0f};
  float max_x{0.0f};
  float max_y{0.0f};
};

struct RectEdgesYDown {
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
  float bottom{0.0f};
};

struct RectBoundsYDown {
  float min_left{0.0f};
  float min_top{0.0f};
  float max_right{0.0f};
  float max_bottom{0.0f};
};

inline void ClampRectEdgesYUpPreservingSpan(RectEdgesYUp* rect,
                                            const RectBoundsYUp& bounds) noexcept {
  if (rect == nullptr) {
    return;
  }

  const float height = rect->bottom - rect->top;
  const float width = rect->right - rect->left;

  if (bounds.max_y <= rect->bottom) {
    rect->bottom = bounds.max_y;
    rect->top = bounds.max_y - height;
  }
  if (rect->top < bounds.min_y) {
    rect->top = bounds.min_y;
    rect->bottom = bounds.min_y + height;
  }
  if (rect->left < bounds.min_x) {
    rect->left = bounds.min_x;
    rect->right = bounds.min_x + width;
  }
  if (bounds.max_x < rect->right) {
    rect->right = bounds.max_x;
    rect->left = bounds.max_x - width;
  }
}

inline void ClampRectEdgesYDownPreservingSpan(
    RectEdgesYDown* rect, const RectBoundsYDown& bounds) noexcept {
  if (rect == nullptr) {
    return;
  }

  const float height = rect->bottom - rect->top;
  const float width = rect->right - rect->left;

  if (rect->top < bounds.min_top) {
    rect->top = bounds.min_top;
    rect->bottom = bounds.min_top + height;
  }
  if (bounds.max_bottom < rect->bottom) {
    rect->bottom = bounds.max_bottom;
    rect->top = bounds.max_bottom - height;
  }
  if (rect->left < bounds.min_left) {
    rect->left = bounds.min_left;
    rect->right = bounds.min_left + width;
  }
  if (bounds.max_right < rect->right) {
    rect->right = bounds.max_right;
    rect->left = bounds.max_right - width;
  }
}

[[nodiscard]] constexpr bool RectContainsPointInclusive(float left,
                                                        float top,
                                                        float right,
                                                        float bottom,
                                                        float x,
                                                        float y) noexcept {
  return x >= left && x <= right && y >= top && y <= bottom;
}

struct RectIntersectionLTRB {
  float left;
  float top;
  float right;
  float bottom;
};

[[nodiscard]] constexpr RectIntersectionLTRB IntersectRectsLTRB(
    float a_left, float a_top, float a_right, float a_bottom,
    float b_left, float b_top, float b_right, float b_bottom) noexcept {
  return {
      a_left > b_left ? a_left : b_left,
      a_top > b_top ? a_top : b_top,
      a_right < b_right ? a_right : b_right,
      a_bottom < b_bottom ? a_bottom : b_bottom,
  };
}

}

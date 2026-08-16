#include "openwow/render/backend/bgfx/metal_presentation_layer.h"

#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>

namespace openwow::render {

void EnsureMetalMaximumDrawableCount(
    void* const metal_layer, const std::uint8_t requested_count) noexcept {
  if (metal_layer == nullptr) {
    return;
  }

  if (@available(macOS 10.13, *)) {
    auto* const layer = static_cast<CAMetalLayer*>(metal_layer);
    const auto drawable_count = static_cast<NSUInteger>(
        std::clamp<std::uint8_t>(requested_count, 2, 3));
    if (layer.maximumDrawableCount != drawable_count) {
      layer.maximumDrawableCount = drawable_count;
    }
  }
}

}

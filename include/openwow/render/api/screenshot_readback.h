#pragma once

#include <cstdint>
#include <vector>

namespace openwow::render::api {

class ScreenshotReadbackTarget {
public:
  virtual ~ScreenshotReadbackTarget() = default;
  [[nodiscard]] virtual bool HasPendingCapture() const = 0;
  virtual void CompleteCapture(std::vector<std::uint8_t> bgra_pixels,
                               std::uint32_t width,
                               std::uint32_t height) = 0;
  virtual void FailCapture() = 0;
};

}

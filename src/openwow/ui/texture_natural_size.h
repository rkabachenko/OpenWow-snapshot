#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::ui {

struct TexturePixelSize {
  std::uint32_t width{0};
  std::uint32_t height{0};
};

class TextureNaturalSizeSource {
 public:
  virtual ~TextureNaturalSizeSource() = default;

  virtual void QueueTextureLoad(const std::string& texture_path) = 0;

  [[nodiscard]] virtual std::optional<TexturePixelSize> ResolveTexturePixelSize(
      const std::string& texture_path) = 0;
};

inline constexpr TexturePixelSize kSolidTexturePixelSize{.width = 8u,
                                                         .height = 8u};

[[nodiscard]] constexpr float TexturePixelsToNaturalScriptUnits(
    const std::uint32_t pixels) noexcept {
  return static_cast<float>(pixels);
}

struct TextureNaturalSize {

  std::optional<float> width;
  std::optional<float> height;

  bool pending{false};
};

[[nodiscard]] inline TextureNaturalSize ResolveTextureNaturalSize(
    TextureNaturalSizeSource* const source, const std::string& texture_path,
    const bool solid_colour) {
  TextureNaturalSize natural;
  std::optional<TexturePixelSize> pixels;
  if (!texture_path.empty()) {
    if (source == nullptr) {
      return natural;
    }
    pixels = source->ResolveTexturePixelSize(texture_path);
    if (!pixels.has_value()) {
      natural.pending = true;
      return natural;
    }
  } else if (solid_colour) {
    pixels = kSolidTexturePixelSize;
  } else {
    return natural;
  }
  if (pixels->width != 0u) {
    natural.width = TexturePixelsToNaturalScriptUnits(pixels->width);
  }
  if (pixels->height != 0u) {
    natural.height = TexturePixelsToNaturalScriptUnits(pixels->height);
  }
  return natural;
}

inline bool SyncTextureNaturalSize(framexml::UiFrame& frame,
                                   TextureNaturalSizeSource* const source,
                                   const std::string& texture_path,
                                   const bool solid_colour) {

  if (!texture_path.empty() &&
      frame.texture_natural_size_path == texture_path &&
      (frame.texture_natural_width.has_value() ||
       frame.texture_natural_height.has_value())) {
    return false;
  }
  const TextureNaturalSize natural =
      ResolveTextureNaturalSize(source, texture_path, solid_colour);
  if (natural.pending) {

    frame.texture_natural_width.reset();
    frame.texture_natural_height.reset();
    frame.texture_natural_size_path.clear();
    return true;
  }
  frame.texture_natural_width = natural.width;
  frame.texture_natural_height = natural.height;
  frame.texture_natural_size_path = texture_path;
  return false;
}

}

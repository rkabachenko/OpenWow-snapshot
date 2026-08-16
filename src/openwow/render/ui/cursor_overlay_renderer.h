#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace openwow::render {

class TextureManager;

class CursorOverlayRenderer final {
 public:
  explicit CursorOverlayRenderer(TextureManager& texture_manager);
  ~CursorOverlayRenderer();

  CursorOverlayRenderer(const CursorOverlayRenderer&) = delete;
  CursorOverlayRenderer& operator=(const CursorOverlayRenderer&) = delete;

  [[nodiscard]] bool RenderTexturePath(std::uint8_t view_id,
                                       float screen_w,
                                       float screen_h,
                                       int mouse_x,
                                       int mouse_y,
                                       int hotspot_x,
                                       int hotspot_y,
                                       std::string_view texture_path);

  [[nodiscard]] bool RenderCustomPixels(std::uint8_t view_id,
                                        float screen_w,
                                        float screen_h,
                                        int mouse_x,
                                        int mouse_y,
                                        int hotspot_x,
                                        int hotspot_y,
                                        std::span<const std::uint8_t> rgba32x32,
                                        bool pixels_dirty);

  void ResetCustomTexture();
  void Shutdown();

 private:
  TextureManager& texture_manager_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#pragma once

#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/render/scene/world_overlay_metrics.h"
#include "openwow/render/ui/text_renderer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {

struct UnitNameDrawEntry {
  std::uint64_t guid{0};

  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
  std::string text;
  std::uint32_t lines{1};
  std::uint32_t color_argb{0xFFFFFFFFu};
  float scale{1.0f};
};

struct UnitNamePresentationSnapshot {
  std::vector<UnitNameDrawEntry> names;
};

class UnitNameRenderer {
 public:
  UnitNameRenderer() = default;
  ~UnitNameRenderer() = default;
  UnitNameRenderer(const UnitNameRenderer&) = delete;
  UnitNameRenderer& operator=(const UnitNameRenderer&) = delete;

  bool Initialize();
  void Shutdown();

  void ConsumePresentation(UnitNamePresentationSnapshot snapshot);

  void Render(std::uint8_t view_id, const WorldOverlayMetrics& metrics,
              const float* view_mtx, const float* proj_mtx);

  [[nodiscard]] std::size_t entry_count() const { return entries_.size(); }

 private:
  bool EnsureFont();

  std::vector<UnitNameDrawEntry> entries_;
  openwow::render::ui::TextRenderer text_renderer_;
  bool initialized_{false};

  std::unordered_map<std::string,
                     std::shared_ptr<const openwow::render::text::TextLayout>>
      line_layout_cache_;

  static constexpr float kWorldLineHeightPerScale = 0.2f;

  static constexpr int kBaseFontPixelHeight = kRetailMaxFontPixelHeight;

  static constexpr const char* kUnitNameFontPath = "Fonts\\FRIZQT__.TTF";
};

}

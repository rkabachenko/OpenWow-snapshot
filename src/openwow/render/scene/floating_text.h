#pragma once

#include "openwow/core/client_crt_random.h"
#include "openwow/render/scene/world_overlay_metrics.h"
#include "openwow/render/ui/text_renderer.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::render {

struct FloatingTextCreateSpec {
  std::uint32_t raw_type{0};
  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
  std::string_view text;
  std::uint32_t base_color{0xFFFFFFFF};
  float font_size{1.0f};
  float max_life{1.5f};
  float velocity_y{2.0f};
  bool critical{false};
  std::optional<std::uint32_t> color_override;
  std::optional<std::uint32_t> auxiliary_value;
  std::uint32_t creation_tick_ms{0};
};

struct FloatingTextEntry {
  float world_x, world_y, world_z;
  std::string text;
  std::uint32_t raw_type{0};
  std::uint32_t creation_tick_ms{0};
  std::optional<std::uint32_t> color_override;
  std::optional<std::uint32_t> auxiliary_value;
  std::uint32_t color;
  float font_size;
  float life;
  float max_life;
  float velocity_y;
  bool critical;
};

[[nodiscard]] std::optional<FloatingTextEntry> BuildFloatingTextEntry(
    const FloatingTextCreateSpec& spec);
[[nodiscard]] float ComputeFloatingTextRenderScale(std::uint32_t raw_type,
                                                   float font_size,
                                                   float progress);
[[nodiscard]] float ComputeFloatingTextRenderAlpha(std::uint32_t raw_type,
                                                   float progress);

class FloatingTextRenderer {
 public:
  FloatingTextRenderer() = default;
  ~FloatingTextRenderer() = default;

  FloatingTextRenderer(const FloatingTextRenderer&) = delete;
  FloatingTextRenderer& operator=(const FloatingTextRenderer&) = delete;

  bool Initialize();

  void Shutdown();
  void ReleaseRendererDeviceResources();
  bool RestoreRendererDeviceResources();

  void AddDamage(float wx, float wy, float wz, std::uint32_t amount,
                 bool critical = false);

  void AddHealing(float wx, float wy, float wz, std::uint32_t amount,
                  bool critical = false, std::uint32_t raw_type = 0);

  void AddMiss(float wx, float wy, float wz);

  void AddDodge(float wx, float wy, float wz);

  void AddParry(float wx, float wy, float wz);

  void AddBlock(float wx, float wy, float wz, std::uint32_t blocked_amount);

  void AddResist(float wx, float wy, float wz);

  void AddImmune(float wx, float wy, float wz);

  void AddText(float wx, float wy, float wz, const std::string& text,
               std::uint32_t color, std::uint32_t raw_type = 0,
               std::optional<std::uint32_t> color_override = std::nullopt,
               std::optional<std::uint32_t> auxiliary_value = std::nullopt);

  void Update(float dt);

  void Render(std::uint8_t view_id, const WorldOverlayMetrics& metrics,
               const float* view_mtx, const float* proj_mtx);

  void Clear() { entries_.clear(); }

  [[nodiscard]] std::size_t entry_count() const { return entries_.size(); }

 private:
  std::vector<FloatingTextEntry> entries_;
  core::ClientCrtRandom random_;
  openwow::render::ui::TextRenderer text_renderer_;
  int font_pixel_height_{0};
  bool initialized_{false};

  static constexpr float kDefaultLife = 1.5f;
  static constexpr float kCritLife = 2.0f;
  static constexpr float kFloatSpeed = 2.0f;
  static constexpr float kCritFloatSpeed = 2.5f;
  static constexpr float kNormalScale = 1.0f;
  static constexpr std::size_t kMaxEntries = 64;

  static constexpr const char* kDamageTextFontPath = "Fonts\\FRIZQT__.TTF";

  [[nodiscard]] static bool WorldToScreen(float wx, float wy, float wz,
                                           const float* view_mtx,
                                           const float* proj_mtx,
                                           const WorldOverlayMetrics& metrics,
                                           float& sx, float& sy);

  void AddEntry(FloatingTextEntry&& entry);
  bool EnsureDamageTextFont(const WorldOverlayMetrics& metrics);
};

}

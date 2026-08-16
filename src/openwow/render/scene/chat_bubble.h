#pragma once

#include "openwow/game/chat_bubble.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/ui/text_renderer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {
class ObjectRenderer;

struct ChatBubbleDrawInfo {
  game::ChatBubble bubble;
  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
  float screen_x{0.0f};
  float screen_y{0.0f};

  float projected_depth{0.0f};
  std::uint32_t frame_level{0u};
};

class ChatBubblePresenter {
 public:
  using FileLoader =
      std::function<std::vector<std::uint8_t>(const std::string&)>;

  explicit ChatBubblePresenter(TextureManager& texture_manager)
      : texture_manager_(texture_manager) {}
  ~ChatBubblePresenter() = default;

  ChatBubblePresenter(const ChatBubblePresenter&) = delete;
  ChatBubblePresenter& operator=(const ChatBubblePresenter&) = delete;

  bool Initialize();
  void Shutdown();
  void SetFileLoader(FileLoader loader);
  void SetFontPath(std::string path) { font_path_ = std::move(path); }

  [[nodiscard]] std::vector<ChatBubbleDrawInfo> BuildDrawList(
      std::span<const game::ChatBubble> bubbles,
      const game::ObjectPresentationSnapshot& objects,
      const ObjectRenderer& object_renderer, float screen_w, float screen_h,
      const float* view_mtx, const float* proj_mtx) const;

  void Render(std::uint8_t view_id, float screen_w, float screen_h,
              const float* view_mtx, const float* proj_mtx,
              std::span<const game::ChatBubble> bubbles,
              const game::ObjectPresentationSnapshot& objects,
              const ObjectRenderer& object_renderer,
              std::uint64_t compositor_generation = 0u);

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] bool stock_visuals_ready() const noexcept;
  [[nodiscard]] std::size_t last_rendered_bubble_count() const noexcept {
    return last_rendered_bubble_count_;
  }
  [[nodiscard]] std::uint64_t last_render_generation() const noexcept {
    return last_render_generation_;
  }

 private:
  TextureManager& texture_manager_;
  struct PreparedLayout {
    std::uint64_t insertion_order{0u};
    std::string text;
    float maximum_text_width{0.0f};
    float text_scale{0.0f};
    std::vector<std::string> lines;
    std::vector<float> line_widths;
    float text_width{0.0f};
    float text_height{0.0f};
  };

  static bool ProjectRetailAnchor(float world_x, float world_y, float world_z,
                                  float screen_w, float screen_h,
                                  const float* view_mtx,
                                  const float* proj_mtx, float* screen_x,
                                  float* screen_y, float* projected_depth);
  [[nodiscard]] std::vector<std::string> WrapText(
      const std::string& text, float maximum_width, float text_scale) const;
  [[nodiscard]] const PreparedLayout& PrepareLayout(
      const game::ChatBubble& bubble, float maximum_text_width,
      float text_scale);
  void DiscardInactiveLayouts(const std::vector<ChatBubbleDrawInfo>& draws);
  void EnsureFont();
  void EnsureTextures();

  FileLoader file_loader_;
  std::string font_path_;
  ui::TextRenderer text_renderer_;
  TextureLease background_lease_;
  TextureLease backdrop_lease_;
  TextureLease tail_lease_;
  std::unordered_map<std::uint32_t, PreparedLayout> prepared_layouts_;
  std::size_t last_rendered_bubble_count_{0u};
  std::uint64_t last_render_generation_{0u};
  bool initialized_{false};

  static constexpr float kHeadClearance = 0.7f;

  static constexpr float kMaxTextWidthReferencePixels = 204.8f;

  static constexpr float kStockUiCoordinateHeight = 768.0f;

  static constexpr float kMinimumTextWidth = 32.0f;

  static constexpr float kTextPadding = 12.8f;

  static constexpr float kUiCoordinateDiagonalReference = 1280.0f;

  static constexpr int kNameplateFontHeight = 14;

  static constexpr float kEdgeSize = 16.0f;
  static constexpr float kBackgroundTileSize = 16.0f;
  static constexpr float kBackgroundInset = 16.0f;

  static constexpr float kTailSize = 16.0f;
  static constexpr float kTailOverlap = 4.0f;
};

}

#pragma once

#include "openwow/game/localization.h"
#include "openwow/render/resources/fonts/font_face.h"
#include "openwow/render/resources/fonts/text_layout.h"
#include "openwow/ui/widgets/script_region.h"
#include "openwow/ui/widgets/simple_embedded_texture.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::ui::widgets {

class CSimpleRender {
 public:
  struct TextShadowState {
    std::uint32_t color{0xFF000000u};
    float offsetX{};
    float offsetY{};
  };

  struct TextGeometryInputs {
    float baseX{};
    float baseY{};
    float justifyOffsetX{};
    float justifyOffsetY{};
    float depth{};
  };

  struct TextPosition {
    float x{};
    float y{};
    float z{};
  };

  using BatchSink = SimpleRenderBatchSink;

  CSimpleRender() = default;
  ~CSimpleRender() { DestroyTextureNodes(); }
  CSimpleRender(const CSimpleRender&) = delete;
  CSimpleRender& operator=(const CSimpleRender&) = delete;
  CSimpleRender(CSimpleRender&&) noexcept = default;
  CSimpleRender& operator=(CSimpleRender&&) noexcept = default;

  void AllocTextBuffer(std::uint16_t maximum_bytes) {
    maximumTextBytes_ = maximum_bytes;
    if (maximumTextBytes_ != 0u && text_.size() >= maximumTextBytes_) {
      text_.resize(maximumTextBytes_ - 1u);
      Invalidate();
    }
  }

  void SetText(const char* text, bool process_localized_tags = false) {
    std::string next = text != nullptr ? text : "";
    if (process_localized_tags && !next.empty()) {
      next = openwow::game::ExpandLocalizedTextTags(
          next, openwow::game::Localization::Get().GetLocale());
    }
    if (maximumTextBytes_ != 0u && next.size() >= maximumTextBytes_) {
      next.resize(maximumTextBytes_ - 1u);
    }
    if (next == text_) return;
    text_ = std::move(next);
    Invalidate();
  }

  [[nodiscard]] const char* GetText() const noexcept { return text_.c_str(); }

  void Invalidate() {
    InvalidateLocalLayoutState();
    NotifyOwnerLayoutInvalidated();
  }

  void InvalidateFromParentLayout() { InvalidateLocalLayoutState(); }

  void ClearCachedDimensions() {
    cachedWidth_ = 0.0f;
    cachedHeight_ = 0.0f;
    NotifyOwnerLayoutInvalidated();
  }

  void SetLayoutOwner(CScriptRegion* owner) noexcept { layoutOwner_ = owner; }

  [[nodiscard]] const std::shared_ptr<openwow::render::text::FontFace>&
  GetFontFace() const noexcept {
    return fontFace_;
  }

  void SetFontFace(
      std::shared_ptr<openwow::render::text::FontFace> face) noexcept {
    fontFace_ = std::move(face);
  }

  [[nodiscard]] const std::optional<openwow::render::text::TextLayout>&
  GetTextLayout() const noexcept {
    return textLayout_;
  }

  void SetTextLayout(openwow::render::text::TextLayout layout) {
    textLayout_ = std::move(layout);
  }

  void ClearTextLayout() noexcept { textLayout_.reset(); }

  void AddToRenderBatch(BatchSink& sink) const;
  void CreateTextureNodes(float rendered_text_height);
  void DestroyTextureNodes() {
    embeddedTextures_.ForEach(
        [](CSimpleEmbeddedTexture& node) { node.PrepareForRemoval(); });
    embeddedTextures_.DestroyAll();
  }

  void SetTextGeometryInputs(float base_x, float base_y,
                             float justify_offset_x,
                             float justify_offset_y, float depth) noexcept;
  [[nodiscard]] const TextGeometryInputs& GetTextGeometryInputs()
      const noexcept {
    return textGeometryInputs_;
  }
  void SetTextDepth(float depth) noexcept;
  [[nodiscard]] TextPosition BuildCurrentTextPosition() const noexcept;
  bool OnOwnerRectChanged(const ScreenRect& old_rect,
                          const ScreenRect& new_rect);

  void SetFontHeight(float height) noexcept { fontHeight_ = height; }
  [[nodiscard]] float GetFontHeight() const noexcept { return fontHeight_; }
  void SetScaleFactor(float scale) noexcept { scaleFactor_ = scale; }
  [[nodiscard]] float GetScaleFactor() const noexcept { return scaleFactor_; }

  void SetTextColor(std::uint32_t color) noexcept { textColor_ = color; }
  [[nodiscard]] std::uint32_t GetTextColor() const noexcept {
    return textColor_;
  }
  void SetColorAlpha(const std::uint8_t alpha) noexcept {
    textColor_ = (textColor_ & 0x00FFFFFFu) |
                 (static_cast<std::uint32_t>(alpha) << 24u);
  }
  [[nodiscard]] std::uint8_t GetColorAlpha() const noexcept {
    return static_cast<std::uint8_t>(textColor_ >> 24u);
  }
  void GetColorFloats(float& red, float& green, float& blue,
                      float& alpha) const noexcept {
    red = ColorComponent(textColor_);
    green = ColorComponent(textColor_ >> 8u);
    blue = ColorComponent(textColor_ >> 16u);
    alpha = ColorComponent(textColor_ >> 24u);
  }

  void RefreshInheritedAlpha(std::uint8_t parent_alpha,
                             bool force_refresh) noexcept {
    if (force_refresh || inheritedParentAlpha_ != parent_alpha) {
      inheritedParentAlpha_ = parent_alpha;
    }
  }

  void SyncEmbeddedTextureAlphaFromTextColor() {
    const float alpha = ColorComponent(RenderableTextAlpha());
    embeddedTextures_.ForEach([alpha](CSimpleEmbeddedTexture& node) {
      auto* texture = node.GetTexture();
      if (texture == nullptr || !texture->HasRenderableContent()) return;
      float red = 1.0f;
      float green = 1.0f;
      float blue = 1.0f;
      float ignored = 1.0f;
      texture->GetVertexColor(red, green, blue, ignored);
      texture->SetVertexColor(red, green, blue, alpha);
    });
  }

  void SetJustifyFlags(std::uint32_t flags) noexcept {
    justifyFlags_ = flags;
  }
  [[nodiscard]] std::uint32_t GetJustifyFlags() const noexcept {
    return justifyFlags_;
  }
  [[nodiscard]] std::uint8_t GetHorizontalJustify() const noexcept {
    return static_cast<std::uint8_t>(justifyFlags_ & 0x7u);
  }
  [[nodiscard]] std::uint8_t GetVerticalJustify() const noexcept {
    return static_cast<std::uint8_t>(justifyFlags_ & 0x38u);
  }

  void SetActiveTextShadow(std::uint32_t color, float offset_x,
                           float offset_y) noexcept {
    textShadow_ = {color, offset_x, offset_y};
    justifyFlags_ |= 0x100u;
  }
  void ClearActiveTextShadow() noexcept { justifyFlags_ &= ~0x100u; }
  [[nodiscard]] bool HasActiveTextShadow() const noexcept {
    return (justifyFlags_ & 0x100u) != 0u;
  }
  [[nodiscard]] const TextShadowState& GetActiveTextShadow() const noexcept {
    return textShadow_;
  }

  [[nodiscard]] float GetCachedWidth() const noexcept {
    return cachedWidth_;
  }
  [[nodiscard]] float GetCachedHeight() const noexcept {
    return cachedHeight_;
  }
  void SetCachedDimensions(float width, float height) noexcept {
    cachedWidth_ = width;
    cachedHeight_ = height;
  }

  [[nodiscard]] bool IsLayoutValid() const noexcept {
    return layoutValid_;
  }
  void SetLayoutValid() noexcept { layoutValid_ = true; }

 private:
  static float ColorComponent(const std::uint32_t value) noexcept {
    return static_cast<float>(value & 0xFFu) / 255.0f;
  }

  [[nodiscard]] std::uint8_t RenderableTextAlpha() const noexcept {
    return static_cast<std::uint8_t>(
        static_cast<std::uint32_t>(GetColorAlpha()) *
        inheritedParentAlpha_ / 255u);
  }

  void InvalidateLocalLayoutState() noexcept {
    cachedWidth_ = 0.0f;
    cachedHeight_ = 0.0f;
    textLayout_.reset();
    layoutValid_ = false;
  }

  void NotifyOwnerLayoutInvalidated() noexcept {
    if (layoutOwner_ != nullptr) layoutOwner_->MarkLayoutDirty();
  }

  std::string text_;
  std::uint16_t maximumTextBytes_{};
  float cachedWidth_{};
  float cachedHeight_{};
  std::optional<openwow::render::text::TextLayout> textLayout_;
  CScriptRegion* layoutOwner_{};
  std::shared_ptr<openwow::render::text::FontFace> fontFace_;
  float fontHeight_{};
  float scaleFactor_{1.0f};
  std::uint32_t textColor_{0xFFFFFFFFu};
  std::uint32_t justifyFlags_{};
  std::uint8_t inheritedParentAlpha_{0xFFu};
  TextShadowState textShadow_;
  CSimpleEmbeddedTextureList embeddedTextures_;
  TextGeometryInputs textGeometryInputs_;
  TextPosition textPosition_;
  bool layoutValid_{};
};

}

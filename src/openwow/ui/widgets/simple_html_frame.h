#pragma once

#include "openwow/ui/widgets/csimple_html_content_node.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::widgets {

class CSimpleHTMLFrame : public CSimpleFrame {
 public:
  CSimpleHTMLFrame() : CSimpleFrame(ScriptObjectType::SimpleHTML) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::SimpleHTML || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "SimpleHTML") || CSimpleFrame::IsTypeOf(typeName);
  }

  void OnResize(const float* newRect) override {
    const float oldW = rect_.Width();
    const float oldH = rect_.Height();

    CSimpleFrame::OnResize(newRect);

    if (!newRect)
      return;

    const float newW = newRect[2] - newRect[0];
    const float newH = newRect[3] - newRect[1];

    if (std::fabs(newH - oldH) >= kHTMLSizeEpsilon ||
        std::fabs(newW - oldW) >= kHTMLSizeEpsilon) {
      needsHyperlinkRebuild_ = true;
    }
  }

  void SetText(const std::string& html);
  [[nodiscard]] const std::string& GetText() const noexcept {
    return htmlText_;
  }

  void SetHyperlinksEnabled(bool e) noexcept { hyperlinksEnabled_ = e; }
  [[nodiscard]] bool GetHyperlinksEnabled() const noexcept {
    return hyperlinksEnabled_;
  }
  void SetHyperlinkFormat(std::string format) { hyperlinkFormat_ = std::move(format); }
  [[nodiscard]] const std::string& GetHyperlinkFormat() const noexcept {
    return hyperlinkFormat_;
  }

  void SetHorizontalScroll(float v) noexcept { hScroll_ = v; }
  [[nodiscard]] float GetHorizontalScroll() const noexcept { return hScroll_; }

  void SetVerticalScroll(float v) noexcept { vScroll_ = v; }
  [[nodiscard]] float GetVerticalScroll() const noexcept { return vScroll_; }

  [[nodiscard]] float GetContentHeight() const noexcept { return contentH_; }
  [[nodiscard]] float GetContentWidth() const noexcept { return contentW_; }
  void SetContentSize(float w, float h) noexcept { contentW_ = w; contentH_ = h; }

  void DestroyContent();

  void RebuildHyperlinks();

  [[nodiscard]] std::vector<ContentNode>& GetContentNodes() noexcept {
    return contentNodes_;
  }
  [[nodiscard]] const std::vector<ContentNode>& GetContentNodes() const noexcept {
    return contentNodes_;
  }
  [[nodiscard]] std::size_t GetParsedHyperlinkRegionCount() const noexcept {
    return hyperlinkRegionCount_;
  }
  [[nodiscard]] std::size_t GetHyperlinkButtonCount() const noexcept {
    return hyperlinkButtons_.size();
  }

  void SetLastContentLayout(void* p) noexcept { lastContentLayout_ = p; }
  [[nodiscard]] void* GetLastContentLayout() const noexcept {
    return lastContentLayout_;
  }

  void SetContentYOffset(float y) noexcept { contentYOffset_ = y; }
  [[nodiscard]] float GetContentYOffset() const noexcept {
    return contentYOffset_;
  }

  [[nodiscard]] std::vector<void*>& GetChildElements() noexcept {
    return childElements_;
  }

  void NotifyFontStyleChanged([[maybe_unused]] int sourceIndex,
                              uint8_t changeFlags) noexcept {
    if ((changeFlags & 0x11u) != 0) {
      needsHyperlinkRebuild_ = true;
    }
  }

  [[nodiscard]] bool NeedsHyperlinkRebuild() const noexcept {
    return needsHyperlinkRebuild_;
  }
  void ClearHyperlinkRebuild() noexcept { needsHyperlinkRebuild_ = false; }

 private:

  static constexpr float kHTMLSizeEpsilon = 1.0e-5f;

  std::string htmlText_;
  bool hyperlinksEnabled_{true};
  std::string hyperlinkFormat_{"|H%s|h%s|h"};
  float hScroll_{0.0f}, vScroll_{0.0f};
  float contentW_{0.0f}, contentH_{0.0f};
  std::size_t hyperlinkRegionCount_{0};

  std::vector<ContentNode> contentNodes_;

  void* lastContentLayout_{nullptr};

  float contentYOffset_{0.0f};

  std::vector<void*> childElements_;

  std::vector<CSimpleFrame*> hyperlinkButtons_;

  bool needsHyperlinkRebuild_{false};
};

}

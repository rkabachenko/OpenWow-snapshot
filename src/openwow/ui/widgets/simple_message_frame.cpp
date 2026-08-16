
#include "openwow/ui/widgets/simple_message_frame.h"

#include <algorithm>
#include <cmath>

namespace openwow::ui::widgets {

void CSimpleMessageFrame::OnResize(const float* newRect) {
  CSimpleFrame::OnResize(newRect);

  LayoutMessageLines();
}

void CSimpleMessageFrame::OnFrameLayoutChanged(uint32_t flags) {
  constexpr uint32_t kPositionOrSizeChanged = 0x11;
  if ((flags & kPositionOrSizeChanged) != 0) {
    LayoutMessageLines();
  }
}

void CSimpleMessageFrame::LayoutMessageLines() {
  const auto& r = GetRect();
  const float frameHeight = r.bottom - r.top;
  const float frameWidth = r.right - r.left;
  if (frameHeight <= 0.0f || frameWidth <= 0.0f) {
    visibleLineCount_ = 0;
    return;
  }

  const float scale = std::max(GetEffectiveScale(), 0.001f);
  computedLeft_   = r.left   + textInsetLeft_   * scale;
  computedTop_    = r.top    + textInsetTop_     * scale;
  computedRight_  = r.right  - textInsetRight_   * scale;
  computedBottom_ = r.bottom - textInsetBottom_  * scale;

  const float linePitch = lineSpacing_ * scale;
  if (linePitch <= 0.0f) {
    visibleLineCount_ = 0;
    return;
  }

  const float availHeight = computedBottom_ - computedTop_;
  uint32_t lines = static_cast<uint32_t>(availHeight / linePitch);
  if (std::fabs(linePitch * static_cast<float>(lines + 1) - availHeight) < 1e-6f) {
    ++lines;
  }
  visibleLineCount_ = lines;

  if (lineEntries_.size() > visibleLineCount_) {
    for (size_t i = visibleLineCount_; i < lineEntries_.size(); ++i) {
      auto* line = lineEntries_[i].line;
      if (line && line->IsVisible()) {
        auto* fontStr = line->GetFontString();
        if (fontStr) {
          fontStr->Hide();
        }
        line->SetDisplayTime(0.0f);
        line->SetFadeTime(0.0f);
        line->SetVisible(false);
        if (numActiveLines_ > 0) --numActiveLines_;
      }
    }
  }
  ResizeLineEntries(visibleLineCount_);

  messageLinesDirty_ = true;
}

}

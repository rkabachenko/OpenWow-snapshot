#pragma once

#include "openwow/ui/widgets/simple_frame.h"

#include <algorithm>

namespace openwow::ui::widgets {

class CSimpleScrollFrame : public CSimpleFrame {
 public:
  CSimpleScrollFrame() : CSimpleFrame(ScriptObjectType::ScrollFrame) {}
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::ScrollFrame || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "ScrollFrame") || CSimpleFrame::IsTypeOf(typeName);
  }

  void SetScrollChild(CSimpleFrame* child) noexcept {
    if (scrollChild_) {
      scrollChild_->SetScrollChildHierarchyState(
          false, ScrollChildRootOverride::Clear);
      scrollChild_->SetParentFrame(nullptr);
    }

    scrollChild_ = child;

    if (scrollChild_) {
      scrollChild_->SetParentFrame(this);
      scrollChild_->SetScrollChildHierarchyState(
          true, ScrollChildRootOverride::Set);
    }

    MarkScrollChildRectDirty();
  }
  [[nodiscard]] CSimpleFrame* GetScrollChild() const noexcept {
    return scrollChild_;
  }

  bool SetHorizontalScroll(float v) noexcept {
    if (std::fabs(v - hScroll_) < 9.5367432e-7f) return false;
    hScroll_ = v;
    return true;
  }
  [[nodiscard]] float GetHorizontalScroll() const noexcept { return hScroll_; }

  bool SetVerticalScroll(float v) noexcept {
    if (std::fabs(v - vScroll_) < 9.5367432e-7f) return false;
    vScroll_ = v;
    return true;
  }
  [[nodiscard]] float GetVerticalScroll() const noexcept { return vScroll_; }

  [[nodiscard]] float GetHorizontalScrollRange() const noexcept {
    return hScrollRange_;
  }
  [[nodiscard]] float GetVerticalScrollRange() const noexcept {
    return vScrollRange_;
  }
  void SetScrollRange(float h, float v) noexcept {
    hScrollRange_ = h;
    vScrollRange_ = v;
  }
  void MarkScrollChildRectDirty() noexcept { scrollChildRectDirty_ = true; }
  [[nodiscard]] bool IsScrollChildRectDirty() const noexcept {
    return scrollChildRectDirty_;
  }

  void UpdateScrollChildRect() {
    if (scrollChild_) {
      const float childW = scrollChild_->GetWidth();
      const float childH = scrollChild_->GetHeight();
      hScrollRange_ = std::max(0.0f, childW - GetWidth());
      vScrollRange_ = std::max(0.0f, childH - GetHeight());
    } else {
      hScrollRange_ = 0.0f;
      vScrollRange_ = 0.0f;
    }

    hScroll_ = std::clamp(hScroll_, 0.0f, hScrollRange_);
    vScroll_ = std::clamp(vScroll_, 0.0f, vScrollRange_);
    scrollChildRectDirty_ = false;
  }

 private:
  CSimpleFrame* scrollChild_{nullptr};
  float hScroll_{0.0f}, vScroll_{0.0f};
  float hScrollRange_{0.0f}, vScrollRange_{0.0f};
  bool scrollChildRectDirty_{false};
};

}

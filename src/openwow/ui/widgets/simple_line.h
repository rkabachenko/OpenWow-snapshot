
#pragma once

#include "openwow/ui/widgets/script_region.h"

namespace openwow::ui::widgets {

class CSimpleLine : public CScriptRegion {
 public:
  CSimpleLine() : CScriptRegion(ScriptObjectType::Line) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Line || CScriptRegion::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "Line") || CScriptRegion::IsTypeOf(typeName);
  }

  void SetStartPoint(float x, float y) noexcept {
    startX_ = x;
    startY_ = y;
  }
  void SetEndPoint(float x, float y) noexcept {
    endX_ = x;
    endY_ = y;
  }
  void GetStartPoint(float& x, float& y) const noexcept {
    x = startX_;
    y = startY_;
  }
  void GetEndPoint(float& x, float& y) const noexcept {
    x = endX_;
    y = endY_;
  }

  void SetThickness(float t) noexcept { thickness_ = t; }
  [[nodiscard]] float GetThickness() const noexcept { return thickness_; }

  void SetColorTexture(float r, float g, float b, float a = 1.0f) noexcept {
    r_ = r;
    g_ = g;
    b_ = b;
    a_ = a;
  }
  void GetColor(float& r, float& g, float& b, float& a) const noexcept {
    r = r_;
    g = g_;
    b = b_;
    a = a_;
  }

  void RegisterRenderCallbacks(SimpleRenderBatchSink& sink) const override {
    sink.AddLine(*this);
  }

 private:
  float startX_{0.0f}, startY_{0.0f};
  float endX_{0.0f}, endY_{0.0f};
  float thickness_{1.0f};
  float r_{1.0f}, g_{1.0f}, b_{1.0f}, a_{1.0f};
};

}

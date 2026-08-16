#include "openwow/render/ui/ui_render_pipeline.h"

#include <algorithm>
#include <string>
#include <utility>

namespace openwow::render {

void UIRenderPipeline::Begin() {
  current_ = {};
  phase_ = UIRenderPhase::Background;
  inFrame_ = true;
}

UIRenderStats UIRenderPipeline::End() {
  inFrame_ = false;
  last_ = current_;
  lifetimeDrawCalls_ += current_.drawCalls;
  return last_;
}

void UIRenderPipeline::SetPhase(UIRenderPhase phase) { phase_ = phase; }
UIRenderPhase UIRenderPipeline::GetPhase() const { return phase_; }

void UIRenderPipeline::AddDrawCall(std::uint32_t triangles) {
  ++current_.drawCalls;
  current_.triangles += triangles;
}

void UIRenderPipeline::AddTextureBind() { ++current_.textureBinds; }
void UIRenderPipeline::AddBatch() { ++current_.batchCount; }

UIRenderStats UIRenderPipeline::GetLastFrameStats() const { return last_; }

void UIRenderPipeline::SetScreenSize(std::uint32_t width,
                                     std::uint32_t height) {
  screenWidth_ = width;
  screenHeight_ = height;
}

std::uint32_t UIRenderPipeline::GetScreenWidth() const {
  return screenWidth_;
}

std::uint32_t UIRenderPipeline::GetScreenHeight() const {
  return screenHeight_;
}

void UIRenderPipeline::SetUIScale(float scale) {
  uiScale_ = std::clamp(scale, 0.64f, 1.0f);
}

float UIRenderPipeline::GetUIScale() const { return uiScale_; }

float UIRenderPipeline::GetScaledWidth() const {
  return static_cast<float>(screenWidth_) / uiScale_;
}

float UIRenderPipeline::GetScaledHeight() const {
  return static_cast<float>(screenHeight_) / uiScale_;
}

void UIRenderPipeline::SetEnabled(bool v) { enabled_ = v; }
bool UIRenderPipeline::IsEnabled() const { return enabled_; }

std::uint64_t UIRenderPipeline::GetTotalDrawCalls() const {
  return lifetimeDrawCalls_;
}

void UIRenderPipeline::Reset() {
  phase_ = UIRenderPhase::Background;
  current_ = {};
  last_ = {};
  screenWidth_ = 1024;
  screenHeight_ = 768;
  uiScale_ = 1.0f;
  enabled_ = true;
  inFrame_ = false;
  lifetimeDrawCalls_ = 0;
}

std::string UIRenderPipeline::GetPhaseName(UIRenderPhase phase) {
  switch (phase) {
    case UIRenderPhase::Background:   return "Background";
    case UIRenderPhase::Frames:       return "Frames";
    case UIRenderPhase::Text:         return "Text";
    case UIRenderPhase::WorldOverlay: return "WorldOverlay";
    case UIRenderPhase::Cursor:       return "Cursor";
    case UIRenderPhase::Tooltip:      return "Tooltip";
    case UIRenderPhase::Cinematic:    return "Cinematic";
  }
  return "Unknown";
}

float UIRenderPipeline::GetAspectRatio() const {
  if (screenHeight_ == 0) return 1.0f;
  return static_cast<float>(screenWidth_) / static_cast<float>(screenHeight_);
}

std::pair<float, float> UIRenderPipeline::PixelToUI(float px, float py) const {
  float scaledW = GetScaledWidth();
  float scaledH = GetScaledHeight();
  if (scaledW <= 0.0f) scaledW = 1.0f;
  if (scaledH <= 0.0f) scaledH = 1.0f;
  return {px / uiScale_, py / uiScale_};
}

std::pair<float, float> UIRenderPipeline::UIToPixel(float ux, float uy) const {
  return {ux * uiScale_, uy * uiScale_};
}

bool UIRenderPipeline::IsInFrame() const { return inFrame_; }

UIRenderStats UIRenderPipeline::GetCurrentFrameStats() const { return current_; }

}

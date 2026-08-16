
#include "openwow/render/ui/ui_render_context.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::render {

ScissorRect ScissorRect::Intersect(const ScissorRect& other) const {
  const float x0 = std::max(x, other.x);
  const float y0 = std::max(y, other.y);
  const float x1 = std::min(x + w, other.x + other.w);
  const float y1 = std::min(y + h, other.y + other.h);
  if (x1 <= x0 || y1 <= y0) {
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

void UIRenderContext::Begin(float screenWidth, float screenHeight) {
  screenWidth_ = screenWidth;
  screenHeight_ = screenHeight;
  active_ = true;

  vertices_.clear();
  drawCommands_.clear();

  colorR_ = 1.0f;
  colorG_ = 1.0f;
  colorB_ = 1.0f;
  colorA_ = 1.0f;
  currentTexture_ = 0;

  while (!scissorStack_.empty()) scissorStack_.pop();
  while (!alphaStack_.empty()) alphaStack_.pop();

  scissorStack_.push({0.0f, 0.0f, screenWidth, screenHeight});

  alphaStack_.push(1.0f);
}

std::vector<UIDrawCommand> UIRenderContext::End() {
  active_ = false;
  return std::move(drawCommands_);
}

void UIRenderContext::PushScissor(float x, float y, float w, float h) {
  ScissorRect newRect{x, y, w, h};
  if (!scissorStack_.empty()) {
    newRect = scissorStack_.top().Intersect(newRect);
  }
  scissorStack_.push(newRect);
}

void UIRenderContext::PopScissor() {
  if (scissorStack_.size() > 1) {
    scissorStack_.pop();
  }
}

void UIRenderContext::SetColor(float r, float g, float b, float a) {
  colorR_ = std::clamp(r, 0.0f, 1.0f);
  colorG_ = std::clamp(g, 0.0f, 1.0f);
  colorB_ = std::clamp(b, 0.0f, 1.0f);
  colorA_ = std::clamp(a, 0.0f, 1.0f);
}

void UIRenderContext::SetTexture(std::uint32_t textureId) {
  currentTexture_ = textureId;
}

void UIRenderContext::SetAlpha(float alpha) {
  if (!alphaStack_.empty()) {
    alphaStack_.pop();
  }
  alphaStack_.push(std::clamp(alpha, 0.0f, 1.0f));
}

void UIRenderContext::PushAlpha(float alpha) {
  float effective = std::clamp(alpha, 0.0f, 1.0f);
  if (!alphaStack_.empty()) {
    effective *= alphaStack_.top();
  }
  alphaStack_.push(effective);
}

void UIRenderContext::PopAlpha() {
  if (alphaStack_.size() > 1) {
    alphaStack_.pop();
  }
}

void UIRenderContext::DrawRect(float x, float y, float w, float h) {
  EmitQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, 0);
}

void UIRenderContext::DrawTexturedRect(float x, float y, float w, float h,
                                       float u0, float v0, float u1,
                                       float v1) {
  EmitQuad(x, y, w, h, u0, v0, u1, v1, currentTexture_);
}

void UIRenderContext::DrawBorder(float x, float y, float w, float h,
                                 float thickness) {
  const float t = thickness;

  DrawRect(x, y, w, t);

  DrawRect(x, y + h - t, w, t);

  DrawRect(x, y + t, t, h - 2.0f * t);

  DrawRect(x + w - t, y + t, t, h - 2.0f * t);
}

void UIRenderContext::DrawLine(float x1, float y1, float x2, float y2,
                               float thickness) {
  const float dx = x2 - x1;
  const float dy = y2 - y1;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) return;

  const float nx = -dy / len * thickness * 0.5f;
  const float ny = dx / len * thickness * 0.5f;

  const std::uint32_t col = PackColor();
  const auto sci = GetEffectiveScissor();

  const auto start = static_cast<std::uint32_t>(vertices_.size());

  vertices_.push_back({x1 + nx, y1 + ny, 0.0f, 0.0f, col});
  vertices_.push_back({x1 - nx, y1 - ny, 0.0f, 0.0f, col});
  vertices_.push_back({x2 + nx, y2 + ny, 0.0f, 0.0f, col});
  vertices_.push_back({x2 + nx, y2 + ny, 0.0f, 0.0f, col});
  vertices_.push_back({x1 - nx, y1 - ny, 0.0f, 0.0f, col});
  vertices_.push_back({x2 - nx, y2 - ny, 0.0f, 0.0f, col});

  drawCommands_.push_back(
      {0, start, 6, sci, BlendMode::Alpha});
}

void UIRenderContext::DrawNineSlice(float x, float y, float w, float h,
                                    std::uint32_t textureId,
                                    float cornerSize) {
  const float cs = cornerSize;

  const float csX = std::min(cs, w * 0.5f);
  const float csY = std::min(cs, h * 0.5f);

  const float uvC = 0.25f;

  auto emitPiece = [&](float px, float py, float pw, float ph, float pu0,
                       float pv0, float pu1, float pv1) {
    EmitQuad(px, py, pw, ph, pu0, pv0, pu1, pv1, textureId);
  };

  emitPiece(x, y, csX, csY, 0.0f, 0.0f, uvC, uvC);

  emitPiece(x + w - csX, y, csX, csY, 1.0f - uvC, 0.0f, 1.0f, uvC);

  emitPiece(x, y + h - csY, csX, csY, 0.0f, 1.0f - uvC, uvC, 1.0f);

  emitPiece(x + w - csX, y + h - csY, csX, csY, 1.0f - uvC, 1.0f - uvC,
            1.0f, 1.0f);

  emitPiece(x + csX, y, w - 2.0f * csX, csY, uvC, 0.0f, 1.0f - uvC, uvC);

  emitPiece(x + csX, y + h - csY, w - 2.0f * csX, csY, uvC, 1.0f - uvC,
            1.0f - uvC, 1.0f);

  emitPiece(x, y + csY, csX, h - 2.0f * csY, 0.0f, uvC, uvC, 1.0f - uvC);

  emitPiece(x + w - csX, y + csY, csX, h - 2.0f * csY, 1.0f - uvC, uvC,
            1.0f, 1.0f - uvC);

  emitPiece(x + csX, y + csY, w - 2.0f * csX, h - 2.0f * csY, uvC, uvC,
            1.0f - uvC, 1.0f - uvC);
}

std::uint32_t UIRenderContext::GetVertexCount() const {
  return static_cast<std::uint32_t>(vertices_.size());
}

std::uint32_t UIRenderContext::GetDrawCallCount() const {
  return static_cast<std::uint32_t>(drawCommands_.size());
}

std::span<const UIVertex> UIRenderContext::GetVertices() const {
  return {vertices_.data(), vertices_.size()};
}

std::uint32_t UIRenderContext::PackColor() const {
  float effectiveAlpha = colorA_;
  if (!alphaStack_.empty()) {
    effectiveAlpha *= alphaStack_.top();
  }

  const auto r = static_cast<std::uint8_t>(colorR_ * 255.0f);
  const auto g = static_cast<std::uint8_t>(colorG_ * 255.0f);
  const auto b = static_cast<std::uint8_t>(colorB_ * 255.0f);
  const auto a = static_cast<std::uint8_t>(std::clamp(effectiveAlpha, 0.0f, 1.0f) * 255.0f);
  return (static_cast<std::uint32_t>(r) << 24) |
         (static_cast<std::uint32_t>(g) << 16) |
         (static_cast<std::uint32_t>(b) << 8) |
         static_cast<std::uint32_t>(a);
}

ScissorRect UIRenderContext::GetEffectiveScissor() const {
  if (scissorStack_.empty()) {
    return {0.0f, 0.0f, screenWidth_, screenHeight_};
  }
  return scissorStack_.top();
}

void UIRenderContext::EmitQuad(float x, float y, float w, float h, float u0,
                               float v0, float u1, float v1,
                               std::uint32_t textureId) {
  const std::uint32_t col = PackColor();
  const auto sci = GetEffectiveScissor();
  const auto start = static_cast<std::uint32_t>(vertices_.size());

  const float x0 = x;
  const float y0 = y;
  const float x1 = x + w;
  const float y1 = y + h;

  vertices_.push_back({x0, y0, u0, v0, col});
  vertices_.push_back({x1, y0, u1, v0, col});
  vertices_.push_back({x0, y1, u0, v1, col});
  vertices_.push_back({x1, y0, u1, v0, col});
  vertices_.push_back({x1, y1, u1, v1, col});
  vertices_.push_back({x0, y1, u0, v1, col});

  drawCommands_.push_back(
      {textureId, start, 6, sci, BlendMode::Alpha});
}

}

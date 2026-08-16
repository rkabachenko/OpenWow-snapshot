#pragma once

#include <cstdint>
#include <span>
#include <stack>
#include <vector>

namespace openwow::render {

enum class BlendMode : std::uint8_t {
  Opaque = 0,
  AlphaKey = 1,
  Alpha = 2,
  NoAlphaAdd = 3,
  Add = 4,
  Mod = 5,
  Mod2x = 6,
  BlendAdd = 7,
};

struct UIVertex {
  float x{0.0f};
  float y{0.0f};
  float u{0.0f};
  float v{0.0f};
  std::uint32_t color{0xFFFFFFFF};
};

struct ScissorRect {
  float x{0.0f};
  float y{0.0f};
  float w{0.0f};
  float h{0.0f};

  [[nodiscard]] bool IsEmpty() const { return w <= 0.0f || h <= 0.0f; }

  [[nodiscard]] ScissorRect Intersect(const ScissorRect& other) const;
};

struct UIDrawCommand {
  std::uint32_t textureId{0};
  std::uint32_t startVertex{0};
  std::uint32_t vertexCount{0};
  ScissorRect scissorRect;
  BlendMode blendMode{BlendMode::Alpha};
};

class UIRenderContext {
 public:
  UIRenderContext() = default;
  ~UIRenderContext() = default;

  UIRenderContext(const UIRenderContext&) = delete;
  UIRenderContext& operator=(const UIRenderContext&) = delete;

  void Begin(float screenWidth, float screenHeight);

  std::vector<UIDrawCommand> End();

  void PushScissor(float x, float y, float w, float h);

  void PopScissor();

  void SetColor(float r, float g, float b, float a);

  void SetTexture(std::uint32_t textureId);

  void SetAlpha(float alpha);

  void PushAlpha(float alpha);

  void PopAlpha();

  void DrawRect(float x, float y, float w, float h);

  void DrawTexturedRect(float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1);

  void DrawBorder(float x, float y, float w, float h, float thickness);

  void DrawLine(float x1, float y1, float x2, float y2, float thickness);

  void DrawNineSlice(float x, float y, float w, float h,
                     std::uint32_t textureId, float cornerSize);

  [[nodiscard]] std::uint32_t GetVertexCount() const;
  [[nodiscard]] std::uint32_t GetDrawCallCount() const;
  [[nodiscard]] std::span<const UIVertex> GetVertices() const;

  [[nodiscard]] float GetScreenWidth() const { return screenWidth_; }
  [[nodiscard]] float GetScreenHeight() const { return screenHeight_; }

 private:

  [[nodiscard]] std::uint32_t PackColor() const;

  [[nodiscard]] ScissorRect GetEffectiveScissor() const;

  void EmitQuad(float x, float y, float w, float h,
                float u0, float v0, float u1, float v1,
                std::uint32_t textureId);

  float screenWidth_{0.0f};
  float screenHeight_{0.0f};
  bool active_{false};

  float colorR_{1.0f};
  float colorG_{1.0f};
  float colorB_{1.0f};
  float colorA_{1.0f};
  std::uint32_t currentTexture_{0};

  std::stack<ScissorRect> scissorStack_;

  std::stack<float> alphaStack_;

  std::vector<UIVertex> vertices_;
  std::vector<UIDrawCommand> drawCommands_;
};

}

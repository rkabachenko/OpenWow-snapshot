#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace openwow::render {

enum class UIRenderPhase : std::uint8_t {
  Background = 0,
  Frames,
  Text,
  WorldOverlay,
  Cursor,
  Tooltip,
  Cinematic,
};

struct UIRenderStats {
  std::uint32_t drawCalls{0};
  std::uint32_t triangles{0};
  std::uint32_t textureBinds{0};
  std::uint32_t batchCount{0};
  float frameTime{0.0f};
};

class UIRenderPipeline {
 public:
  UIRenderPipeline() = default;

  void Begin();
  UIRenderStats End();

  void SetPhase(UIRenderPhase phase);
  [[nodiscard]] UIRenderPhase GetPhase() const;

  void AddDrawCall(std::uint32_t triangles);
  void AddTextureBind();
  void AddBatch();

  [[nodiscard]] UIRenderStats GetLastFrameStats() const;

  void SetScreenSize(std::uint32_t width, std::uint32_t height);
  [[nodiscard]] std::uint32_t GetScreenWidth() const;
  [[nodiscard]] std::uint32_t GetScreenHeight() const;

  void SetUIScale(float scale);
  [[nodiscard]] float GetUIScale() const;
  [[nodiscard]] float GetScaledWidth() const;
  [[nodiscard]] float GetScaledHeight() const;

  void SetEnabled(bool v);
  [[nodiscard]] bool IsEnabled() const;

  [[nodiscard]] std::uint64_t GetTotalDrawCalls() const;

  void Reset();

  [[nodiscard]] static std::string GetPhaseName(UIRenderPhase phase);

  [[nodiscard]] float GetAspectRatio() const;

  [[nodiscard]] std::pair<float, float> PixelToUI(float px, float py) const;

  [[nodiscard]] std::pair<float, float> UIToPixel(float ux, float uy) const;

  [[nodiscard]] bool IsInFrame() const;

  [[nodiscard]] UIRenderStats GetCurrentFrameStats() const;

 private:
  UIRenderPhase phase_{UIRenderPhase::Background};
  UIRenderStats current_;
  UIRenderStats last_;

  std::uint32_t screenWidth_{1024};
  std::uint32_t screenHeight_{768};
  float uiScale_{1.0f};

  bool enabled_{true};
  bool inFrame_{false};

  std::uint64_t lifetimeDrawCalls_{0};
};

}

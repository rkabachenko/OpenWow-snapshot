
#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace openwow::game {

enum class LoadingScreenPhase : std::uint8_t {
  Hidden  = 0,
  FadeIn  = 1,
  Loading = 2,
  FadeOut = 3,
};

struct LoadingScreenInfo {
  std::uint32_t textureId{0};
  std::string   zoneName;
  std::string   subText;
  std::uint32_t mapId{0};
  std::string   tipText;
  float         progress{0.0f};
};

class LoadingScreenController {
 public:
  LoadingScreenController() = default;

  void Show(const LoadingScreenInfo& info);
  void Hide();

  void  SetProgress(float value);
  [[nodiscard]] float GetProgress() const;

  void Update(float deltaTime);

  [[nodiscard]] LoadingScreenPhase GetPhase() const;
  [[nodiscard]] bool IsVisible() const;
  [[nodiscard]] bool IsLoading() const;

  [[nodiscard]] const std::string& GetZoneName() const;
  [[nodiscard]] const std::string& GetTipText() const;
  void SetTipText(const std::string& text);
  [[nodiscard]] std::uint32_t GetTextureId() const;
  [[nodiscard]] std::uint32_t GetMapId() const;

  [[nodiscard]] float GetFadeAlpha() const;
  void SetFadeDuration(float seconds);

  void SetRandomTip(const std::vector<std::string>& tips);

 private:
  LoadingScreenInfo  info_{};
  LoadingScreenPhase phase_{LoadingScreenPhase::Hidden};
  float              fadeAlpha_{0.0f};
  float              fadeDuration_{0.5f};
  float              fadeTimer_{0.0f};
};

}

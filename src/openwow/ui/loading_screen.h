#pragma once

#include "openwow/core/client_crt_random.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui {

enum class LoadingScreenType : uint8_t {
  Initial,
  Teleport,
  InstanceEntry,
  Dungeon,
  Raid,
  Battleground,
  Arena,
  DeathRelease,
};

class LoadingScreen {
 public:
  LoadingScreen() = default;

  void Show(LoadingScreenType type, const std::string& mapName,
            const std::string& flavorText = "");
  void Hide();
  [[nodiscard]] bool IsVisible() const;

  void SetProgress(float progress);
  [[nodiscard]] float GetProgress() const;

  void SetStatusText(const std::string& text);
  [[nodiscard]] std::string GetStatusText() const;

  [[nodiscard]] LoadingScreenType GetType() const;
  [[nodiscard]] std::string GetMapName() const;
  [[nodiscard]] std::string GetFlavorText() const;

  [[nodiscard]] std::string GetBackgroundTexture() const;
  void SetBackgroundTexture(const std::string& path);

  [[nodiscard]] std::string GetTipText() const;
  void SetTips(const std::vector<std::string>& tips);
  [[nodiscard]] std::string GetRandomTip() const;

  void Update(float dt);
  [[nodiscard]] float GetElapsedTime() const;

  void Reset();

 private:
  bool visible_ = false;
  LoadingScreenType type_ = LoadingScreenType::Initial;
  float progress_ = 0.0f;
  std::string map_name_;
  std::string flavor_text_;
  std::string status_text_;
  std::string background_texture_;
  std::string current_tip_;
  std::vector<std::string> tips_;
  mutable core::ClientCrtRandom random_;
  float elapsed_time_ = 0.0f;
};

}

#pragma once

#include "openwow/ui/widgets/simple_font_string.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace openwow::game {
class MinimapSystem;
}
namespace openwow::ui {
class MinimapSystem;
}

namespace openwow::ui::widgets {

class CSimpleMinimap : public CSimpleFrame {
 public:
  CSimpleMinimap();
  ~CSimpleMinimap() override;
  void BindPresentation(openwow::ui::MinimapSystem& state,
                        openwow::game::MinimapSystem& content) noexcept {
    minimap_state_ = &state;
    minimap_content_ = &content;
  }

  [[nodiscard]] static CSimpleMinimap* GetPrimaryInstance() noexcept {
    return s_primaryInstance_;
  }

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Minimap || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "Minimap") || CSimpleFrame::IsTypeOf(typeName);
  }

  void FireOnLeave(bool motion = false, bool clearDragState = true) override;
  bool RefreshScaleCascade(bool force) override;

  bool OnMouseMove(const void* inputEvent) override;

  void RegisterLayerRenderCallbacks(SimpleRenderBatchSink& sink,
                                    int layerIndex) override;

  void FireOnUpdate(float elapsed) override;

  void SetPlayerDirection(float facing) noexcept;
  [[nodiscard]] float GetPlayerDirection() const noexcept {
    return playerDirection_;
  }

  void PostLoadProcess(const void* xmlNode, void* errorHandler) override;

  void SetZoom(uint32_t zoom) noexcept { zoom_ = zoom; }
  [[nodiscard]] uint32_t GetZoom() const noexcept { return zoom_; }

  void SetMinMaxZoom(uint32_t min, uint32_t max) noexcept {
    minZoom_ = min;
    maxZoom_ = max;
  }
  [[nodiscard]] uint32_t GetMinZoom() const noexcept { return minZoom_; }
  [[nodiscard]] uint32_t GetMaxZoom() const noexcept { return maxZoom_; }

  void PingLocation(float x, float y) { pingX_ = x; pingY_ = y; }
  void GetPingPosition(float& x, float& y) const noexcept {
    x = pingX_;
    y = pingY_;
  }

  void SetBlipTexture(const std::string& tex) { blipTex_ = tex; }
  [[nodiscard]] const std::string& GetBlipTexture() const noexcept {
    return blipTex_;
  }

  void SetMaskTexture(const std::string& tex) { maskTex_ = tex; }
  [[nodiscard]] const std::string& GetMaskTexture() const noexcept {
    return maskTex_;
  }

  void SetIconTexture(const std::string& tex) { iconTex_ = tex; }
  [[nodiscard]] const CSimpleFontString* GetStatusFontString() const noexcept {
    return statusFontString_.get();
  }

  [[nodiscard]] CSimpleTexture* GetPlayerTexture() const noexcept {
    return playerTexture_.get();
  }

  [[nodiscard]] CSimpleTexture* GetCompassTexture() const noexcept {
    return compassTexture_;
  }

 private:
  void InitializeStatusFontString();

  openwow::ui::MinimapSystem* minimap_state_{nullptr};
  openwow::game::MinimapSystem* minimap_content_{nullptr};
  uint32_t zoom_{0};
  uint32_t minZoom_{0}, maxZoom_{5};
  float pingX_{0.0f}, pingY_{0.0f};
  std::string blipTex_, maskTex_, iconTex_;
  std::unique_ptr<CSimpleFontString> statusFontString_{};

  std::unique_ptr<CSimpleTexture> playerTexture_;

  float playerDirection_{0.0f};

  CSimpleTexture* compassTexture_{nullptr};

  std::uint32_t tooltipPinHitMask_{0};
  std::array<std::uint32_t, 23> tooltipCategoryHitMasks_{};
  bool tooltipVisible_{false};

  static CSimpleMinimap* s_primaryInstance_;
};

}

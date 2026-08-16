#pragma once

#include "openwow/ui/widgets/cooldown_visuals.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <string>

namespace openwow::ui::widgets {

class CSimpleCooldown : public CSimpleFrame {
 public:
  CSimpleCooldown() : CSimpleFrame(ScriptObjectType::Cooldown) {}
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Cooldown || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "Cooldown") || CSimpleFrame::IsTypeOf(typeName);
  }

  void SetCooldown(float startTime, float duration) noexcept {
    startTime_ = startTime;
    duration_ = duration;
    readyFlashActive_ = false;
    RefreshVisualColors();
  }
  [[nodiscard]] float GetCooldownStartTime() const noexcept {
    return startTime_;
  }
  [[nodiscard]] float GetCooldownDuration() const noexcept {
    return duration_;
  }
  void GetCooldownTimes(float& start, float& dur) const noexcept {
    start = startTime_;
    dur = duration_;
  }

  void SetReverse(bool r) noexcept { reverse_ = r; }
  [[nodiscard]] bool IsReverse() const noexcept { return reverse_; }

  void SetDrawSwipe(bool d) noexcept { drawSwipe_ = d; }
  [[nodiscard]] bool GetDrawSwipe() const noexcept { return drawSwipe_; }

  void SetDrawEdge(bool d) noexcept { drawEdge_ = d; }
  [[nodiscard]] bool GetDrawEdge() const noexcept { return drawEdge_; }

  void SetReadyFlashActive(bool active) noexcept {
    readyFlashActive_ = active;
    RefreshVisualColors();
  }
  [[nodiscard]] bool IsReadyFlashActive() const noexcept {
    return readyFlashActive_;
  }
  [[nodiscard]] const CooldownVisualColors& GetVisualColors() const noexcept {
    return visualColors_;
  }
  [[nodiscard]] std::uint32_t GetFillColorAbgr() const noexcept {
    return visualColors_.fill_abgr;
  }
  [[nodiscard]] std::uint32_t GetReadyFlashColorAbgr() const noexcept {
    return visualColors_.ready_flash_abgr;
  }
  [[nodiscard]] std::uint32_t GetEdgeColorAbgr() const noexcept {
    return visualColors_.edge_abgr;
  }

  void SetDrawBling(bool d) noexcept { drawBling_ = d; }
  [[nodiscard]] bool GetDrawBling() const noexcept { return drawBling_; }

  void SetSwipeColor(float r, float g, float b, float a = 1.0f) noexcept {
    swipeR_ = r; swipeG_ = g; swipeB_ = b; swipeA_ = a;
  }
  void SetEdgeTexture(const std::string& tex) { edgeTex_ = tex; }
  void SetBlingTexture(const std::string& tex) { blingTex_ = tex; }

  void SetHideCountdownNumbers(bool h) noexcept { hideNumbers_ = h; }
  [[nodiscard]] bool GetHideCountdownNumbers() const noexcept {
    return hideNumbers_;
  }

  void LoadXML(const void* xmlNode, void* errorHandler) override;

  void OnAlphaChanged() override {
    CSimpleFrame::OnAlphaChanged();
    RefreshVisualColors();
  }

 private:
  void RefreshVisualColors() noexcept {
    visualColors_ = ComputeCooldownVisualColors(
        readyFlashActive_, GetCurrentAlpha(), GetInheritedAlpha());
  }

  float startTime_{0.0f};
  float duration_{0.0f};
  bool reverse_{false};
  bool drawSwipe_{true};
  bool drawEdge_{false};
  bool drawBling_{false};
  bool readyFlashActive_{false};
  CooldownVisualColors visualColors_{};
  float swipeR_{0}, swipeG_{0}, swipeB_{0}, swipeA_{0.8f};
  std::string edgeTex_, blingTex_;
  bool hideNumbers_{false};
};

}

#pragma once

#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <cstdint>
#include <string>

namespace openwow::ui::widgets {

enum class ButtonState : uint8_t { Normal, Pushed, Disabled };

enum class ButtonTextureSlot : int { Disabled = 0, Normal = 1, Pushed = 2, COUNT_ = 3 };

class CSimpleButton : public CSimpleFrame {
 public:
  CSimpleButton() : CSimpleFrame(ScriptObjectType::Button) {}
  ~CSimpleButton() override;

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Button || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "Button") || CSimpleFrame::IsTypeOf(typeName);
  }

  void SetText(const std::string& text) { text_ = text; }
  [[nodiscard]] const std::string& GetText() const noexcept { return text_; }

  void SetState(ButtonState state) noexcept { state_ = state; }
  [[nodiscard]] ButtonState GetState() const noexcept { return state_; }

  void SetEnabled(bool e) noexcept {
    enabled_ = e;
    state_ = e ? ButtonState::Normal : ButtonState::Disabled;
  }
  [[nodiscard]] bool IsEnabled() const noexcept { return enabled_; }

  void Click(const char* button = "LeftButton", bool down = false) {
    FireOnClick(button, down);
  }
  void SetPushedTextOffset(float x, float y) noexcept {
    pushOffX_ = x;
    pushOffY_ = y;
  }
  void GetPushedTextOffset(float& x, float& y) const noexcept {
    x = pushOffX_;
    y = pushOffY_;
  }

  bool SetTextureSlotFromFile(ButtonTextureSlot slot, const char* filePath);

  void SetTextureSlot(ButtonTextureSlot slot, CSimpleTexture* texture);

  void SetNormalTexture(const std::string& tex) {
    textureSlotPaths_[static_cast<int>(ButtonTextureSlot::Normal)] = tex;
    SetTextureSlotFromFile(ButtonTextureSlot::Normal, tex.c_str());
  }
  void SetPushedTexture(const std::string& tex) {
    textureSlotPaths_[static_cast<int>(ButtonTextureSlot::Pushed)] = tex;
    SetTextureSlotFromFile(ButtonTextureSlot::Pushed, tex.c_str());
  }
  void SetDisabledTexture(const std::string& tex) {
    textureSlotPaths_[static_cast<int>(ButtonTextureSlot::Disabled)] = tex;
    SetTextureSlotFromFile(ButtonTextureSlot::Disabled, tex.c_str());
  }
  void SetHighlightTexture(const std::string& tex) { highlightTexPath_ = tex; }

  [[nodiscard]] const std::string& GetNormalTexture() const noexcept {
    return textureSlotPaths_[static_cast<int>(ButtonTextureSlot::Normal)];
  }
  [[nodiscard]] const std::string& GetPushedTexture() const noexcept {
    return textureSlotPaths_[static_cast<int>(ButtonTextureSlot::Pushed)];
  }
  [[nodiscard]] const std::string& GetDisabledTexture() const noexcept {
    return textureSlotPaths_[static_cast<int>(ButtonTextureSlot::Disabled)];
  }

  [[nodiscard]] CSimpleTexture* GetTextureSlot(ButtonTextureSlot slot) const noexcept {
    const auto idx = static_cast<int>(slot);
    return (idx >= 0 && idx < static_cast<int>(ButtonTextureSlot::COUNT_))
               ? textureSlots_[idx]
               : nullptr;
  }
  [[nodiscard]] CSimpleTexture* GetNormalTextureObj() const noexcept {
    return GetTextureSlot(ButtonTextureSlot::Normal);
  }
  [[nodiscard]] CSimpleTexture* GetPushedTextureObj() const noexcept {
    return GetTextureSlot(ButtonTextureSlot::Pushed);
  }
  [[nodiscard]] CSimpleTexture* GetDisabledTextureObj() const noexcept {
    return GetTextureSlot(ButtonTextureSlot::Disabled);
  }
  [[nodiscard]] CSimpleTexture* GetActiveTexture() const noexcept {
    return activeTexture_;
  }
  [[nodiscard]] CSimpleTexture* GetHighlightTextureObj() const noexcept {
    return highlightTexture_;
  }

  void LockHighlight() noexcept { highlightLocked_ = true; }
  void UnlockHighlight() noexcept { highlightLocked_ = false; }
  [[nodiscard]] bool IsHighlightLocked() const noexcept { return highlightLocked_; }

  void SetRegisteredForClicks(uint32_t downFlags, uint32_t upFlags) noexcept {
    registeredClickDownFlags_ = downFlags;
    registeredClickUpFlags_   = upFlags;
  }
  [[nodiscard]] uint32_t GetRegisteredClickDownFlags() const noexcept { return registeredClickDownFlags_; }
  [[nodiscard]] uint32_t GetRegisteredClickUpFlags()   const noexcept { return registeredClickUpFlags_; }

  void RegisterForClicks(const std::string& clicks) { (void)clicks; }
  [[nodiscard]] std::string GetRegisteredClicks() const noexcept { return {}; }

  [[nodiscard]] ButtonTextureSlot GetCurrentVisualSlot() const noexcept {
    return currentVisualSlot_;
  }

  bool FireOnMouseDown(uint32_t buttonFlag, float x, float y,
                       const char* buttonName = nullptr) override;

  bool FireOnMouseUp(uint32_t buttonFlag, float x, float y,
                     const char* buttonName = nullptr) override;

  virtual void FireOnClick(const char* buttonName, bool fromMouseDown);

  virtual void FireOnDoubleClick(const char* buttonName);

  virtual void SetButtonVisualState(ButtonTextureSlot slot, bool locked);

  void FireOnDragStart(uint32_t buttonFlags) override;

  void FireOnHide() override;

 private:
  std::string text_;
  ButtonState state_{ButtonState::Normal};
  bool enabled_{true};

  float pushOffX_{1.0f}, pushOffY_{-1.0f};
  std::string textureSlotPaths_[static_cast<int>(ButtonTextureSlot::COUNT_)];
  std::string highlightTexPath_;
  bool highlightLocked_{false};

  bool clickInProgress_{false};

  uint32_t lastClickTime_{0};

  uint32_t registeredClickDownFlags_{0x80000000};
  uint32_t registeredClickUpFlags_{0};

  CSimpleTexture* textureSlots_[static_cast<int>(ButtonTextureSlot::COUNT_)]{};
  CSimpleTexture* activeTexture_{nullptr};
  CSimpleTexture* highlightTexture_{nullptr};
  ButtonTextureSlot currentVisualSlot_{ButtonTextureSlot::Normal};
};

}

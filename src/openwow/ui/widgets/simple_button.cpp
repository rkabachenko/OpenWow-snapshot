
#include "openwow/ui/widgets/simple_button.h"
#include "openwow/ui/widgets/simple_check_button.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <utility>

namespace openwow::ui::widgets {
namespace {

thread_local bool g_button_click_dispatch_busy = false;

class ButtonClickDispatchScope final {
 public:
  ButtonClickDispatchScope()
      : entered_(!std::exchange(g_button_click_dispatch_busy, true)) {}

  ~ButtonClickDispatchScope() {
    if (entered_) {
      g_button_click_dispatch_busy = false;
    }
  }

  [[nodiscard]] bool entered() const noexcept { return entered_; }

 private:
  bool entered_{false};
};

}

CSimpleButton::~CSimpleButton() {
  for (auto*& tex : textureSlots_) {
    delete tex;
    tex = nullptr;
  }
  delete highlightTexture_;
  highlightTexture_ = nullptr;
  activeTexture_ = nullptr;
}

bool CSimpleButton::FireOnMouseDown(uint32_t buttonFlag, float x, float y,
                                    const char* buttonName) {
  if (CSimpleFrame::FireOnMouseDown(buttonFlag, x, y, buttonName)) {
    return true;
  }

  if (!buttonName) {
    if (!IsPointInFrame(x, y)) {
      return false;
    }
  }

  if (currentVisualSlot_ != ButtonTextureSlot::Disabled) {

    const uint32_t effectiveMask =
        buttonName ? 0x7FFFFFFFu : buttonFlag;

    if ((registeredClickDownFlags_ & effectiveMask) != 0) {
      const char* resolvedName =
          buttonName ? buttonName : MouseButtonName(buttonFlag);

      FireOnClick(resolvedName, true);

    }

    if (!highlightLocked_ &&
        currentVisualSlot_ != ButtonTextureSlot::Disabled) {
      SetButtonVisualState(ButtonTextureSlot::Pushed, false);
    }
  }

  return true;
}

bool CSimpleButton::FireOnMouseUp(uint32_t buttonFlag, float x, float y,
                                  const char* buttonName) {
  if (CSimpleFrame::FireOnMouseUp(buttonFlag, x, y, buttonName)) {
    return true;
  }

  if (currentVisualSlot_ != ButtonTextureSlot::Pushed) {
    return true;
  }

  bool registered = false;
  const char* resolvedName = buttonName;

  if (buttonName) {

    registered = ((registeredClickDownFlags_ & 0x80000000u) |
                  registeredClickUpFlags_) != 0;
  } else {
    if (!IsPointInFrame(x, y)) {
      goto stateCleanup;
    }

    const uint32_t maskLo = buttonFlag << 31;
    const uint32_t maskHi = buttonFlag >> 1;
    registered = ((registeredClickDownFlags_ & maskLo) |
                  (registeredClickUpFlags_ & maskHi)) != 0;
  }

  if (registered) {
    if (!resolvedName) {
      resolvedName = MouseButtonName(buttonFlag);
    }

    const uint32_t now = openwow::core::GameClock::GetTickCount32();

    if (HasScript("OnDoubleClick") &&
        lastClickTime_ != 0 &&
        (now - lastClickTime_) <= 300u) {
      FireOnDoubleClick(resolvedName);
      lastClickTime_ = 0;
    } else {
      FireOnClick(resolvedName, false);
      lastClickTime_ = now;
    }

  }

stateCleanup:

  if (!highlightLocked_ &&
      currentVisualSlot_ != ButtonTextureSlot::Disabled) {
    SetButtonVisualState(ButtonTextureSlot::Normal, false);
  }

  return true;
}

void CSimpleButton::FireOnDoubleClick(const char* buttonName) {

  if (clickInProgress_ ||
      currentVisualSlot_ == ButtonTextureSlot::Disabled) {
    return;
  }
  ButtonClickDispatchScope dispatch_scope;
  if (!dispatch_scope.entered()) {
    return;
  }

  const std::string savedButtonName = lastMouseButton_;
  lastMouseButton_ = buttonName ? buttonName : "";

  clickInProgress_ = true;

  if (HasScript("OnDoubleClick")) {
    RunScript("OnDoubleClick");
  }

  clickInProgress_ = false;

  lastMouseButton_ = savedButtonName;
}

void CSimpleButton::FireOnClick(const char* buttonName,
                                [[maybe_unused]] bool fromMouseDown) {

  if (clickInProgress_ ||
      currentVisualSlot_ == ButtonTextureSlot::Disabled) {
    return;
  }
  ButtonClickDispatchScope dispatch_scope;
  if (!dispatch_scope.entered()) {
    return;
  }

  const std::string savedButtonName = lastMouseButton_;
  lastMouseButton_ = buttonName ? buttonName : "";

  clickInProgress_ = true;

  if (HasScript("PreClick")) {
    RunScript("PreClick");
  }
  if (HasScript("OnClick")) {
    RunScript("OnClick");
  }
  if (HasScript("PostClick")) {
    RunScript("PostClick");
  }

  clickInProgress_ = false;

  lastMouseButton_ = savedButtonName;
}

bool CSimpleButton::SetTextureSlotFromFile(ButtonTextureSlot slot,
                                           const char* filePath) {
  const auto idx = static_cast<int>(slot);
  if (idx < 0 || idx >= static_cast<int>(ButtonTextureSlot::COUNT_)) {
    return false;
  }

  if (CSimpleTexture* existing = textureSlots_[idx]; existing != nullptr) {
    existing->SetTexture(filePath ? filePath : "");
    return true;
  }

  auto* tex = new CSimpleTexture();
  tex->SetDrawLayer(DrawLayer::Artwork);

  if (!tex->SetTexture(filePath ? filePath : "")) {
    delete tex;
    return false;
  }

  tex->SetAllPoints(this);

  SetTextureSlot(slot, tex);
  return true;
}

void CSimpleButton::SetTextureSlot(ButtonTextureSlot slot,
                                   CSimpleTexture* texture) {
  const auto idx = static_cast<int>(slot);
  if (idx < 0 || idx >= static_cast<int>(ButtonTextureSlot::COUNT_)) {
    return;
  }

  CSimpleTexture* old = textureSlots_[idx];
  if (texture == old) {
    return;
  }

  if (activeTexture_ == old) {
    activeTexture_ = nullptr;
  }

  delete old;

  if (texture != nullptr) {
    texture->SetParent(this);
    texture->SetDrawLayer(DrawLayer::Artwork);
  }

  textureSlots_[idx] = texture;

  if (texture != nullptr && slot == currentVisualSlot_) {
    activeTexture_ = texture;
    texture->Show();
  }
}

void CSimpleButton::SetButtonVisualState(ButtonTextureSlot slot, bool locked) {
  highlightLocked_ = locked;

  if (slot == currentVisualSlot_) {
    return;
  }

  if (activeTexture_) {
    const auto idx = static_cast<int>(slot);
    const bool newSlotHasTex =
        idx >= 0 && idx < static_cast<int>(ButtonTextureSlot::COUNT_) &&
        textureSlots_[idx] != nullptr;
    if (newSlotHasTex || slot == ButtonTextureSlot::Normal) {
      activeTexture_->Hide();
      activeTexture_ = nullptr;
    }
  }

  const auto idx = static_cast<int>(slot);
  if (idx >= 0 && idx < static_cast<int>(ButtonTextureSlot::COUNT_)) {
    CSimpleTexture* newTex = textureSlots_[idx];
    if (newTex) {
      activeTexture_ = newTex;
      newTex->Show();
    }
  }

  currentVisualSlot_ = slot;
}

void CSimpleButton::FireOnDragStart(uint32_t buttonFlags) {

  if (currentVisualSlot_ != ButtonTextureSlot::Disabled && !highlightLocked_) {
    SetButtonVisualState(ButtonTextureSlot::Normal, false);
  }

  if (currentVisualSlot_ != ButtonTextureSlot::Disabled) {
    CSimpleFrame::FireOnDragStart(buttonFlags);
  }
}

void CSimpleButton::FireOnHide() {
  if (currentVisualSlot_ != ButtonTextureSlot::Disabled && !highlightLocked_) {
    SetButtonVisualState(ButtonTextureSlot::Normal, false);
  }
  CSimpleFrame::FireOnHide();
}

}

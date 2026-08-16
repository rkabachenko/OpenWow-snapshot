#pragma once

#include "openwow/ui/widgets/simple_button.h"

namespace openwow::ui::widgets {

class CSimpleCheckButton : public CSimpleButton {
 public:

  CSimpleCheckButton() {
    type_ = ScriptObjectType::CheckButton;
  }

  ~CSimpleCheckButton() override;

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::CheckButton || CSimpleButton::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "CheckButton") || CSimpleButton::IsTypeOf(typeName);
  }

  void LoadXML(const void* xmlNode, void* errorHandler) override;

  void SetChecked(bool c) noexcept;
  [[nodiscard]] bool GetChecked() const noexcept { return checked_; }

  void SetCheckedTexture(CSimpleTexture* texture);

  bool SetCheckedTextureFromFile(const char* filePath);

  [[nodiscard]] CSimpleTexture* GetCheckedTexture() const noexcept {
    return checkedTexture_;
  }

  void SetDisabledCheckedTexture(CSimpleTexture* texture);

  bool SetDisabledCheckedTextureFromFile(const char* filePath);

  [[nodiscard]] CSimpleTexture* GetDisabledCheckedTexture() const noexcept {
    return disabledCheckedTexture_;
  }

 private:

  void UpdateCheckedTextureVisibility(bool prevChecked, bool force);

  bool checked_{false};
  CSimpleTexture* checkedTexture_{nullptr};
  CSimpleTexture* disabledCheckedTexture_{nullptr};
};

}

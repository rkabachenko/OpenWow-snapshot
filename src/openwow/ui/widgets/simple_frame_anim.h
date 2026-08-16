
#pragma once

#include <cstdint>

namespace openwow::ui::widgets {

class CSimpleFrame;

class CFrameAnimController {
 public:
  CFrameAnimController() = default;
  explicit CFrameAnimController(CSimpleFrame* owner) : owner_(owner) {}

  void SetOwner(CSimpleFrame* owner) noexcept { owner_ = owner; }
  [[nodiscard]] CSimpleFrame* GetOwner() const noexcept { return owner_; }

  [[nodiscard]] static bool IsKindOfToken(uint32_t token) noexcept {
    return token == kAnimControllerToken || token == kObjectToken;
  }

  [[nodiscard]] uint32_t GetStrata() const noexcept;

  void LoadAnimationsXML(const void* xmlNode, void* errorHandler);

  void ProcessAnimationChildren(const void* xmlNode, void* errorHandler);

  [[nodiscard]] void* FindChildByName(const char* name) const;

 private:
  CSimpleFrame* owner_{nullptr};

  static constexpr uint32_t kAnimControllerToken = 2;
  static constexpr uint32_t kObjectToken = 3;
};

}

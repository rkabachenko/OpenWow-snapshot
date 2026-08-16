#pragma once

#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/status_bar_definition.h"

#include <cstdint>
#include <optional>

namespace openwow::ui::widgets {

struct StatusBarSnapshot {
  float minimum{0.0F};
  float maximum{0.0F};
  float value{0.0F};
  StatusBarOrientation orientation{StatusBarOrientation::Horizontal};
  bool rotates_texture{false};
  bool has_texture{false};
  bool has_range{false};
  bool has_value{false};
};

class StatusBar final : public CSimpleFrame {
 public:
  StatusBar() noexcept : CSimpleFrame(ScriptObjectType::StatusBar) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType type) const noexcept override;
  [[nodiscard]] bool IsTypeOf(const char* type_name) const noexcept override;

  [[nodiscard]] StatusBarRangeChange SetRange(float minimum,
                                               float maximum) noexcept;
  [[nodiscard]] bool SetValue(float value) noexcept;

  void SetOrientation(StatusBarOrientation orientation) noexcept;
  [[nodiscard]] std::optional<StatusBarColor> AttachTexture(
      const void* texture_identity) noexcept;
  [[nodiscard]] bool AttachTextureDeferred(
      const void* texture_identity) noexcept;
  [[nodiscard]] bool DetachTexture(const void* texture_identity) noexcept;
  [[nodiscard]] bool OwnsTexture(const void* texture_identity) const noexcept;
  void SetInitialColor(float red, float green, float blue, float alpha) noexcept;
  [[nodiscard]] std::optional<StatusBarColor>
  TakeInitialColorForAttachedTexture() noexcept;
  void SetRotatesTexture(bool rotates) noexcept;

  [[nodiscard]] StatusBarSnapshot Snapshot() const noexcept;

 private:
  StatusBarValueState value_state_;
  StatusBarOrientation orientation_{StatusBarOrientation::Horizontal};
  std::optional<StatusBarColor> initial_color_;
  const void* texture_identity_{nullptr};
  bool rotates_texture_{false};
  bool has_texture_{false};
};

}

#include "openwow/ui/widgets/status_bar.h"

#include <utility>

namespace openwow::ui::widgets {

bool StatusBar::IsKindOf(const ScriptObjectType type) const noexcept {
  return IsScriptTypeKindOf(ScriptObjectType::StatusBar, type);
}

bool StatusBar::IsTypeOf(const char* type_name) const noexcept {
  return type_name != nullptr &&
         (StrCaseEq(type_name, "StatusBar") || StrCaseEq(type_name, "Frame") ||
          StrCaseEq(type_name, "Region") || StrCaseEq(type_name, "Object"));
}

StatusBarRangeChange StatusBar::SetRange(const float minimum,
                                         const float maximum) noexcept {
  return value_state_.SetRange(minimum, maximum);
}

bool StatusBar::SetValue(const float requested_value) noexcept {
  return value_state_.SetValue(requested_value);
}

void StatusBar::SetOrientation(const StatusBarOrientation orientation) noexcept {
  orientation_ = orientation;
}

std::optional<StatusBarColor> StatusBar::AttachTexture(
    const void* const texture_identity) noexcept {
  if (texture_identity == nullptr || texture_identity == texture_identity_) {
    return std::nullopt;
  }
  auto initial_color = std::exchange(initial_color_, std::nullopt);
  texture_identity_ = texture_identity;
  has_texture_ = true;
  return initial_color;
}

bool StatusBar::AttachTextureDeferred(
    const void* const texture_identity) noexcept {
  if (texture_identity == nullptr || texture_identity == texture_identity_) {
    return false;
  }
  texture_identity_ = texture_identity;
  has_texture_ = true;
  return true;
}

bool StatusBar::DetachTexture(const void* const texture_identity) noexcept {
  if (texture_identity == nullptr || texture_identity != texture_identity_) {
    return false;
  }
  texture_identity_ = nullptr;
  has_texture_ = false;
  return true;
}

bool StatusBar::OwnsTexture(const void* const texture_identity) const noexcept {
  return texture_identity != nullptr && texture_identity == texture_identity_;
}

void StatusBar::SetInitialColor(const float red, const float green,
                                const float blue, const float alpha) noexcept {
  initial_color_ = StatusBarColor{red, green, blue, alpha};
}

std::optional<StatusBarColor>
StatusBar::TakeInitialColorForAttachedTexture() noexcept {
  return has_texture_ ? std::exchange(initial_color_, std::nullopt)
                      : std::nullopt;
}

void StatusBar::SetRotatesTexture(const bool rotates) noexcept {
  rotates_texture_ = rotates;
}

StatusBarSnapshot StatusBar::Snapshot() const noexcept {
  const auto value = value_state_.Snapshot();
  return {
      .minimum = value.minimum,
      .maximum = value.maximum,
      .value = value.value,
      .orientation = orientation_,
      .rotates_texture = rotates_texture_,
      .has_texture = has_texture_,
      .has_range = value.has_range,
      .has_value = value.has_value,
  };
}

}

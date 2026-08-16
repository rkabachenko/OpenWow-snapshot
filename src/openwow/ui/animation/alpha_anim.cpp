
#include "openwow/ui/animation/alpha_anim.h"
#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace openwow::ui::anim {

namespace {

float ComputePreviewAlpha(const float from_alpha,
                          const float to_alpha,
                          const float factor) {
  return std::clamp(from_alpha + (to_alpha - from_alpha) * factor, 0.0f, 1.0f);
}

}

AlphaXmlChangeParseResult ParseAlphaXmlChangeAttribute(const char* raw_change,
                                                       std::string_view animation_name) {
  AlphaXmlChangeParseResult result;
  if (raw_change == nullptr || *raw_change == '\0') {
    return result;
  }

  result.has_value = true;

  bool has_digit = false;
  for (const char* cursor = raw_change; *cursor != '\0'; ++cursor) {
    if (std::isdigit(static_cast<unsigned char>(*cursor)) != 0) {
      has_digit = true;
      break;
    }
  }
  if (!has_digit) {
    result.value = 0.0f;
    return result;
  }

  char* end = nullptr;
  const float parsed = std::strtof(raw_change, &end);
  if (end == raw_change) {
    result.value = 0.0f;
    return result;
  }
  if (std::isnan(parsed)) {
    result.value = 0.0f;
    return result;
  }

  result.value = std::clamp(parsed, -1.0f, 1.0f);
  if (parsed >= -1.0f && parsed <= 1.0f) {
    return result;
  }

  const std::string name =
      animation_name.empty() ? std::string("<unnamed>") : std::string(animation_name);
  result.warning = name + ": Invalid change value: " + raw_change
      + ". Value must be between -1 and 1, inclusive.";
  return result;
}

void AlphaAnim::Apply(float progress) {
  ApplySignedFactor(progress);
}

void AlphaAnim::ResetEffect() {
  current_alpha_ = from_alpha_;
  current_change_ = 0.0f;
}

void AlphaAnim::SetChange(float change) {
  float v = -1.0f;
  if (change >= -1.0f) {
    v = change;
    if (change >= 1.0f) v = 1.0f;
  }
  change_short_ = static_cast<int16_t>(v * 255.0f);
  has_explicit_change_ = true;
}

void AlphaAnim::ApplySignedFactor(const float factor) {
  current_change_ = static_cast<float>(change_short_) / 255.0f * factor;

  if (group_ != nullptr && group_->GetOwnerRegion() != nullptr) {
    std::uint8_t alpha_byte = 0;
    const int scaled_step = static_cast<int>(static_cast<float>(change_short_) * factor);
    if (group_->ApplyParentFrameAlphaStep(static_cast<std::int16_t>(scaled_step),
                                          &alpha_byte)) {
      current_alpha_ =
          static_cast<float>(openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte));
      return;
    }
  }

  current_alpha_ = ComputePreviewAlpha(from_alpha_, to_alpha_, factor);
}

}

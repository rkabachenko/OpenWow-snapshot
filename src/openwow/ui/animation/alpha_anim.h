
#pragma once

#include "openwow/ui/animation/animation.h"

#include <string>
#include <string_view>

namespace openwow::ui::anim {

struct AlphaXmlChangeParseResult {
  bool has_value = false;
  float value = 0.0f;
  std::string warning;
};

AlphaXmlChangeParseResult ParseAlphaXmlChangeAttribute(const char* raw_change,
                                                       std::string_view animation_name);

class AlphaAnim : public Animation {
 public:
  AnimKind GetKind() const override { return AnimKind::Alpha; }

  void SetFromAlpha(float alpha) { from_alpha_ = alpha; }
  float GetFromAlpha() const     { return from_alpha_; }

  void SetToAlpha(float alpha)   { to_alpha_ = alpha; }
  float GetToAlpha() const       { return to_alpha_; }

  void SetChange(float change);
  [[nodiscard]] bool HasExplicitChange() const { return has_explicit_change_; }
  [[nodiscard]] float GetCurrentChange() const { return current_change_; }

  float GetChange() const { return static_cast<float>(change_short_) / 255.0f; }

  void Apply(float progress) override;
  void ResetEffect() override;

  float GetCurrentAlpha() const { return current_alpha_; }

 private:
  void ApplySignedFactor(float factor) override;

  float from_alpha_    = 1.0f;
  float to_alpha_      = 0.0f;
  float current_alpha_ = 1.0f;
  float current_change_ = 0.0f;
  int16_t change_short_ = 0;
  bool has_explicit_change_ = false;

};

}

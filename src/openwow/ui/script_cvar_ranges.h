#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace openwow::ui {

enum class ScriptCVarRangeQuery {
  kMin,
  kMax,
  kAbsoluteMin,
  kAbsoluteMax,
};

struct ExtShadowQualityScriptCaps {
  bool supports_basic_modes = true;
  bool supports_advanced_modes = true;
};

[[nodiscard]] std::optional<double> QueryScriptCVarRange(std::string_view name,
                                                         ScriptCVarRangeQuery query);

[[nodiscard]] std::uint32_t GetExtShadowQualityAbsoluteMaxValue() noexcept;
[[nodiscard]] std::uint32_t
GetExtShadowQualitySupportedMaxValue(const ExtShadowQualityScriptCaps &caps) noexcept;

void SetExtShadowQualityScriptCapsForTests(const ExtShadowQualityScriptCaps &caps);
void ResetExtShadowQualityScriptCapsForTests();

}

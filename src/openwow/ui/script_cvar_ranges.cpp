#include "openwow/ui/script_cvar_ranges.h"

#include "openwow/render/backend/bgfx/retail_render_profile.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::ui {

namespace {

constexpr std::uint32_t kExtShadowQualityAbsoluteMax = 5;
constexpr std::uint32_t kExtShadowQualityBasicMax = 2;
constexpr double kFarclipMin = 177.0;
constexpr double kFarclipMax = 1077.0;

std::optional<ExtShadowQualityScriptCaps> g_ext_shadow_quality_test_caps;

[[nodiscard]] ExtShadowQualityScriptCaps GetCurrentExtShadowQualityScriptCaps() {
  if (g_ext_shadow_quality_test_caps.has_value()) {
    return *g_ext_shadow_quality_test_caps;
  }

  const auto support = openwow::render::GetExtShadowQualitySupport();
  return {
      .supports_basic_modes = support.supports_basic_modes,
      .supports_advanced_modes = support.supports_advanced_modes,
  };
}

}

std::optional<double> QueryScriptCVarRange(std::string_view name,
                                           const ScriptCVarRangeQuery query) {
  if (openwow::text::EqualsIgnoreCaseAscii(name, "extShadowQuality")) {
    switch (query) {
    case ScriptCVarRangeQuery::kMin:
    case ScriptCVarRangeQuery::kAbsoluteMin:

      return 0.0;
    case ScriptCVarRangeQuery::kMax:
      return static_cast<double>(
          GetExtShadowQualitySupportedMaxValue(GetCurrentExtShadowQualityScriptCaps()));
    case ScriptCVarRangeQuery::kAbsoluteMax:
      return static_cast<double>(GetExtShadowQualityAbsoluteMaxValue());
    }
  }

  if (openwow::text::EqualsIgnoreCaseAscii(name, "farclip")) {
    switch (query) {
    case ScriptCVarRangeQuery::kMin:
      return kFarclipMin;
    case ScriptCVarRangeQuery::kMax:
      return kFarclipMax;
    case ScriptCVarRangeQuery::kAbsoluteMin:
    case ScriptCVarRangeQuery::kAbsoluteMax:
      return std::nullopt;
    }
  }

  return std::nullopt;
}

std::uint32_t GetExtShadowQualityAbsoluteMaxValue() noexcept {
  return kExtShadowQualityAbsoluteMax;
}

std::uint32_t
GetExtShadowQualitySupportedMaxValue(const ExtShadowQualityScriptCaps &caps) noexcept {

  if (caps.supports_basic_modes && caps.supports_advanced_modes) {
    return kExtShadowQualityAbsoluteMax;
  }

  if (caps.supports_basic_modes) {
    return kExtShadowQualityBasicMax;
  }

  return 0;
}

void SetExtShadowQualityScriptCapsForTests(const ExtShadowQualityScriptCaps &caps) {
  g_ext_shadow_quality_test_caps = caps;
}

void ResetExtShadowQualityScriptCapsForTests() {
  g_ext_shadow_quality_test_caps.reset();
}

}

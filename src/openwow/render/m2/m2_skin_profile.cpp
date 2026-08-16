#include "openwow/render/m2/m2_skin_profile.h"

#include <array>
#include <cstdio>

namespace openwow::render::m2 {

namespace {

inline constexpr std::array<std::uint32_t, 4> kM2SkinProfileQualityThresholds = {
    0x100u, 0x40u, 0x35u, 0x15u,
};
inline constexpr std::uint32_t kM2SkinProfileRetailScalarHighCutoff = 0xC1u;
inline constexpr std::uint32_t kM2SkinProfileRetailScalarLowBias = 0x20u;
inline constexpr std::uint32_t kM2SkinProfileRetailScalarHighBias = 0x40u;
inline constexpr std::uint32_t kM2SkinProfileRetailDivideByThreeMultiplier = 0xAAAAAAABu;
inline constexpr std::uint32_t kM2SkinProfileMaxDecodedQuality = 0xFFu;

std::uint32_t DivideByThreeLikeRetail(const std::uint32_t value) noexcept {
  const auto product =
      static_cast<std::uint64_t>(value) * kM2SkinProfileRetailDivideByThreeMultiplier;
  return static_cast<std::uint32_t>(product >> 33u);
}

}

std::string BuildM2SkinProfilePath(const std::string_view model_path,
                                   const std::uint32_t skin_profile) {
  std::string skin_path(model_path);
  const std::size_t slash = skin_path.find_last_of("/\\");
  const std::size_t dot = skin_path.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    skin_path.resize(dot);
  }

  char suffix[16];
  std::snprintf(suffix, sizeof(suffix), "%02u.skin", skin_profile);
  skin_path += suffix;
  return skin_path;
}

std::uint32_t
DecodeM2SkinProfileQualityFromRetailScalar(const std::uint32_t retail_quality_scalar) noexcept {
  const std::uint32_t bias =
      retail_quality_scalar < kM2SkinProfileRetailScalarHighCutoff
          ? kM2SkinProfileRetailScalarLowBias
          : kM2SkinProfileRetailScalarHighBias;
  const std::uint32_t decoded = DivideByThreeLikeRetail(retail_quality_scalar - bias);
  if (decoded > kM2SkinProfileMaxDecodedQuality) {
    return kM2SkinProfileFullQuality;
  }
  return decoded;
}

std::uint32_t ResolveM2SkinProfileQuality(const data::model::M2Model &model,
                                          const std::uint32_t skin_profile_quality) noexcept {
  if ((model.header.global_flags & kM2ModelFlagUsesSkinProfileQuality) == 0u) {
    return kM2SkinProfileFullQuality;
  }
  if (skin_profile_quality > kM2SkinProfileMaxDecodedQuality) {
    return kM2SkinProfileFullQuality;
  }
  return skin_profile_quality;
}

std::optional<std::uint32_t>
SelectM2SkinProfile(const data::model::M2Model &model,
                    const std::uint32_t skin_profile_quality) noexcept {
  const std::uint32_t view_count = model.header.num_views;
  if (view_count == 0u || view_count > kM2SkinProfileQualityThresholds.size()) {
    return std::nullopt;
  }

  const std::uint32_t quality = ResolveM2SkinProfileQuality(model, skin_profile_quality);
  const std::size_t first_threshold = kM2SkinProfileQualityThresholds.size() - view_count;
  for (std::uint32_t profile = 0; profile < view_count; ++profile) {
    if (quality >= kM2SkinProfileQualityThresholds[first_threshold + profile]) {
      return profile;
    }
  }
  return std::nullopt;
}

}

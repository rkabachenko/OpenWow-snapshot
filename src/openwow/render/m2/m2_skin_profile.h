#pragma once

#include "openwow/data/model/m2_model.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::render::m2 {

inline constexpr std::uint32_t kM2SkinProfileFullQuality = 0x100u;
inline constexpr std::uint32_t kM2ModelFlagUsesSkinProfileQuality = 0x08u;

[[nodiscard]] std::string BuildM2SkinProfilePath(std::string_view model_path,
                                                 std::uint32_t skin_profile);

[[nodiscard]] std::uint32_t
DecodeM2SkinProfileQualityFromRetailScalar(std::uint32_t retail_quality_scalar) noexcept;

[[nodiscard]] std::uint32_t
ResolveM2SkinProfileQuality(const data::model::M2Model &model,
                            std::uint32_t skin_profile_quality) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
SelectM2SkinProfile(const data::model::M2Model &model,
                    std::uint32_t skin_profile_quality = kM2SkinProfileFullQuality) noexcept;

}

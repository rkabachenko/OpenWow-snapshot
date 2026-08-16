#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::render {

inline constexpr std::uint32_t kFontFlagOutlineNormal = 1u;

inline constexpr std::uint32_t kFontFlagOutlineThickBit = 4u;

inline constexpr std::uint32_t kFontFlagOutlineThick = 5u;

inline constexpr std::uint32_t kFontFlagMonochrome = 2u;

inline constexpr int kRetailNormalOutlinePixels = 1;
inline constexpr int kRetailThickOutlinePixels = 2;

inline constexpr int kRetailMaxFontPixelHeight = 32;

[[nodiscard]] inline int ClampRetailFontPixelHeight(const int pixel_height) noexcept {
  return pixel_height > kRetailMaxFontPixelHeight ? kRetailMaxFontPixelHeight
                                                  : pixel_height;
}

[[nodiscard]] std::uint32_t ParseFontFlagsString(std::string_view flags);

[[nodiscard]] std::string CanonicalizeFontFlagsString(std::uint32_t flags);

[[nodiscard]] inline bool FontFlagsHasOutline(std::uint32_t flags) noexcept {
  return (flags & (kFontFlagOutlineNormal | kFontFlagOutlineThickBit)) != 0u;
}

[[nodiscard]] inline bool FontFlagsIsThickOutline(std::uint32_t flags) noexcept {
  return (flags & kFontFlagOutlineThickBit) != 0u;
}

[[nodiscard]] inline bool FontFlagsIsMonochrome(std::uint32_t flags) noexcept {
  return (flags & kFontFlagMonochrome) != 0u;
}

[[nodiscard]] float FontFlagsToOutlineSize(std::uint32_t flags) noexcept;

}

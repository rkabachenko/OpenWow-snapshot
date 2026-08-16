
#include "openwow/render/resources/fonts/font_string_flags.h"

#include <cctype>
#include <string>
#include <string_view>

namespace openwow::render {

namespace {

char ToUpper(const char c) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

bool ContainsCaseInsensitive(std::string_view haystack,
                             std::string_view needle) {
  if (needle.empty()) {
    return true;
  }

  if (haystack.size() < needle.size()) {
    return false;
  }

  for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
    bool matches = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (ToUpper(haystack[i + j]) != ToUpper(needle[j])) {
        matches = false;
        break;
      }
    }

    if (matches) {
      return true;
    }
  }

  return false;
}

}

std::uint32_t ParseFontFlagsString(const std::string_view flags) {
  if (flags.empty()) {
    return 0u;
  }

  std::uint32_t result = 0u;
  if (ContainsCaseInsensitive(flags, "OUTLINE")) {
    result |= kFontFlagOutlineNormal;
  }
  if (ContainsCaseInsensitive(flags, "THICKOUTLINE")) {
    result |= kFontFlagOutlineThick;
  }
  if (ContainsCaseInsensitive(flags, "MONOCHROME")) {
    result |= kFontFlagMonochrome;
  }
  return result;
}

std::string CanonicalizeFontFlagsString(const std::uint32_t flags) {
  std::string result;

  if ((flags & kFontFlagOutlineNormal) != 0u) {
    result = "OUTLINE";
  }

  if ((flags & kFontFlagOutlineThickBit) != 0u) {
    if (!result.empty()) {
      result += ", ";
    }
    result += "THICKOUTLINE";
  }

  if ((flags & kFontFlagMonochrome) != 0u) {
    if (!result.empty()) {
      result += ", ";
    }
    result += "MONOCHROME";
  }

  return result;
}

float FontFlagsToOutlineSize(std::uint32_t flags) noexcept {
  if (FontFlagsIsThickOutline(flags)) {
    return static_cast<float>(kRetailThickOutlinePixels);
  }
  if (FontFlagsHasOutline(flags)) {
    return static_cast<float>(kRetailNormalOutlinePixels);
  }
  return 0.0f;
}

}

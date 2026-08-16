
#include "openwow/ui/widgets/simple_font_string.h"

#include "openwow/core/storm_error.h"
#include "openwow/foundation/math/float_compare.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/font_asset_path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstring>

namespace openwow::ui::widgets {

namespace {
std::string NormalizeFontPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

bool IsAbsoluteFilesystemFontPath(const std::string &path) {
  if (std::filesystem::path(path).is_absolute()) {
    return true;
  }

  return path.size() > 2 &&
         std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
         path[1] == ':';
}

bool IsVfsFontPathCandidate(const std::string &path) {
  if (path.empty()) {
    return false;
  }

  if (path.find('/') != std::string::npos || path.find('\\') != std::string::npos) {
    return true;
  }

  const auto extension = std::filesystem::path(path).extension().string();
  if (openwow::text::EqualsIgnoreCaseAscii(extension, ".ttf") ||
      openwow::text::EqualsIgnoreCaseAscii(extension, ".ttc") ||
      openwow::text::EqualsIgnoreCaseAscii(extension, ".otf")) {
    return true;
  }

  return !openwow::ui::ResolveBuiltInFontAssetPath(path).empty();
}

constexpr bool SelectsStyleGroup(const std::uint32_t style_flags,
                                 const std::uint32_t present_bit,
                                 const std::uint32_t observer_mask,
                                 const std::uint32_t observer_bit) noexcept {
  return (style_flags & present_bit) != 0 &&
         (observer_mask & observer_bit) != 0;
}
}

void CSimpleFont::SetFontFlags(const std::string &flags) {
  fontFlagsBits_ = openwow::render::ParseFontFlagsString(flags);
  fontFlags_ = openwow::render::CanonicalizeFontFlagsString(fontFlagsBits_);
}

void CSimpleFont::SetFontFlagsBits(const std::uint32_t flags) {
  fontFlagsBits_ = flags;
  fontFlags_ = openwow::render::CanonicalizeFontFlagsString(flags);
}

bool CSimpleFont::CanUseStoredFontFace(const std::string &path, const float size) {
  if (path.empty() || !(size > 0.0f)) {
    return false;
  }

  if (!IsAbsoluteFilesystemFontPath(path)) {
    return IsVfsFontPathCandidate(path);
  }

  std::error_code error;
  return std::filesystem::is_regular_file(NormalizeFontPath(path), error);
}

bool CSimpleFont::SetFont(const std::string &path, const float size, const std::uint32_t flags) {
  if (path.empty() || !(size > 0.0f)) {
    return false;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(path, fontFile_) &&
      openwow::math::float_compare::WithinClientEpsilon(size, fontSize_) &&
      flags == fontFlagsBits_) {
    return true;
  }

  if (!CanUseStoredFontFace(path, size)) {
    return false;
  }

  fontFile_ = path;
  fontSize_ = size;
  SetFontFlagsBits(flags);
  styleFlags_ |= 0x101u;
  dirtyFlags_ &= ~0x1u;
  return true;
}

bool CSimpleFont::SetFont(const std::string &path, const float size, const std::string &flags) {
  return SetFont(path, size, openwow::render::ParseFontFlagsString(flags));
}

void CSimpleFont::CopyStyleFrom(const CSimpleFont &source) {
  const uint32_t srcFlags = source.styleFlags_;

  if ((srcFlags & 0x100u) != 0) {
    SetFont(source.fontFile_, source.fontSize_, source.fontFlagsBits_);
  }

  if ((srcFlags & 0x200u) != 0) {
    if (justifyH_ != source.justifyH_ ||
        justifyV_ != source.justifyV_ ||
        wordWrapFlags_ != source.wordWrapFlags_ ||
        (styleFlags_ & 0x200u) == 0) {
      styleFlags_ |= 0x202u;
      justifyH_ = source.justifyH_;
      justifyV_ = source.justifyV_;
      wordWrapFlags_ = source.wordWrapFlags_;
    }
  }

  if ((srcFlags & 0x400u) != 0) {
    const uint32_t srcColor = source.GetPackedTextColor();
    if (srcColor != GetPackedTextColor() || (styleFlags_ & 0x400u) == 0) {
      textR_ = source.textR_;
      textG_ = source.textG_;
      textB_ = source.textB_;
      textA_ = source.textA_;
      styleFlags_ |= 0x404u;
    }
  }

  if ((srcFlags & 0x800u) != 0) {
    const uint32_t srcShadowColor = source.GetPackedShadowColor();
    if (srcShadowColor != GetPackedShadowColor() ||
        shadow_.offsetX != source.shadow_.offsetX ||
        shadow_.offsetY != source.shadow_.offsetY ||
        (styleFlags_ & 0x800u) == 0) {
      shadow_.r = source.shadow_.r;
      shadow_.g = source.shadow_.g;
      shadow_.b = source.shadow_.b;
      shadow_.a = source.shadow_.a;
      shadow_.offsetX = source.shadow_.offsetX;
      shadow_.offsetY = source.shadow_.offsetY;
      styleFlags_ |= 0x808u;
    }
  }

  if ((srcFlags & 0x1000u) != 0) {
    if (!openwow::math::float_compare::WithinClientEpsilon(source.spacing_, spacing_) ||
        (styleFlags_ & 0x1000u) == 0) {
      styleFlags_ |= 0x1010u;
      spacing_ = source.spacing_;
    }
  }
}

void CSimpleFont::CopyMaskedStyleFrom(const CSimpleFont &source,
                                       uint32_t observerMask) {
  const uint32_t srcFlags = source.styleFlags_;

  if (SelectsStyleGroup(srcFlags, 0x100u, observerMask, 0x01u)) {
    SetFont(source.fontFile_, source.fontSize_, source.fontFlagsBits_);
  }

  if (SelectsStyleGroup(srcFlags, 0x200u, observerMask, 0x02u)) {
    if (justifyH_ != source.justifyH_ ||
        justifyV_ != source.justifyV_ ||
        wordWrapFlags_ != source.wordWrapFlags_ ||
        (styleFlags_ & 0x200u) == 0) {
      styleFlags_ |= 0x202u;
      justifyH_ = source.justifyH_;
      justifyV_ = source.justifyV_;
      wordWrapFlags_ = source.wordWrapFlags_;
    }
  }

  if (SelectsStyleGroup(srcFlags, 0x400u, observerMask, 0x04u)) {
    const uint32_t srcColor = source.GetPackedTextColor();
    if (srcColor != GetPackedTextColor() || (styleFlags_ & 0x400u) == 0) {
      textR_ = source.textR_;
      textG_ = source.textG_;
      textB_ = source.textB_;
      textA_ = source.textA_;
      styleFlags_ |= 0x404u;
    }
  }

  if (SelectsStyleGroup(srcFlags, 0x800u, observerMask, 0x08u)) {
    const uint32_t srcShadowColor = source.GetPackedShadowColor();
    if (srcShadowColor != GetPackedShadowColor() ||
        shadow_.offsetX != source.shadow_.offsetX ||
        shadow_.offsetY != source.shadow_.offsetY ||
        (styleFlags_ & 0x800u) == 0) {
      shadow_.r = source.shadow_.r;
      shadow_.g = source.shadow_.g;
      shadow_.b = source.shadow_.b;
      shadow_.a = source.shadow_.a;
      shadow_.offsetX = source.shadow_.offsetX;
      shadow_.offsetY = source.shadow_.offsetY;
      styleFlags_ |= 0x808u;
    }
  }

  if (SelectsStyleGroup(srcFlags, 0x1000u, observerMask, 0x10u)) {
    if (!openwow::math::float_compare::WithinClientEpsilon(source.spacing_, spacing_) ||
        (styleFlags_ & 0x1000u) == 0) {
      styleFlags_ |= 0x1010u;
      spacing_ = source.spacing_;
    }
  }
}

void CSimpleFont::ApplyStyleFromMask(CSimpleFontString *target, int applyMask) const {
  if (!target) {
    openwow::core::SErrSetLastError(87);
    return;
  }

  const auto observer_mask = static_cast<std::uint32_t>(applyMask);

  if (SelectsStyleGroup(styleFlags_, 0x100u, observer_mask, 0x01u)) {
    target->SetFont(fontFile_, fontSize_, fontFlagsBits_);
  }

  if (SelectsStyleGroup(styleFlags_, 0x200u, observer_mask, 0x02u)) {
    target->ApplyStyleTextLayoutFlags(
        static_cast<std::uint32_t>(justifyH_) | static_cast<std::uint32_t>(justifyV_) |
        (wordWrapFlags_ & 0x2107Fu));
  }

  if (SelectsStyleGroup(styleFlags_, 0x400u, observer_mask, 0x04u)) {
    target->SetTextColor(textR_, textG_, textB_, textA_);
  }

  if (SelectsStyleGroup(styleFlags_, 0x800u, observer_mask, 0x08u)) {
    if (shadow_.offsetX == 0.0f && shadow_.offsetY == 0.0f) {
      target->ClearActiveTextShadow();
    } else {
      target->ApplyShadowStyle(shadow_);
    }
  }

  if (SelectsStyleGroup(styleFlags_, 0x1000u, observer_mask, 0x10u)) {
    target->SetSpacing(spacing_);
  }
}

void CSimpleFont::LoadXML(const void * , float , int ) {

}

}

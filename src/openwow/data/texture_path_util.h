#pragma once

#include <cctype>
#include <string>
#include <string_view>

#include "openwow/foundation/text/ascii.h"

namespace openwow::data {

enum class TexturePathExtension : int {
  kNone = 0,
  kTga = 1,
  kBlp = 2,
};

[[nodiscard]] inline TexturePathExtension ClassifyTexturePathExtension(
    std::string_view path) {
  const auto dot = path.rfind('.');
  if (dot == std::string_view::npos) {
    return TexturePathExtension::kNone;
  }
  const auto ext = path.substr(dot);
  if (ext.size() != 4) {
    return TexturePathExtension::kNone;
  }
  if (text::EqualsIgnoreCaseAscii(ext, std::string_view(".TGA"))) {
    return TexturePathExtension::kTga;
  }
  if (text::EqualsIgnoreCaseAscii(ext, std::string_view(".BLP"))) {
    return TexturePathExtension::kBlp;
  }
  return TexturePathExtension::kNone;
}

[[nodiscard]] inline TexturePathExtension SwapTexturePathTgaBlpExtension(
    std::string_view src, TexturePathExtension mode, std::string& dst) {
  dst.assign(src);

  if (mode == TexturePathExtension::kNone) {
    return TexturePathExtension::kNone;
  }

  const auto dot = dst.rfind('.');
  if (dot != std::string::npos) {
    dst.resize(dot);
  }

  if (mode == TexturePathExtension::kTga) {
    dst += ".BLP";
    return TexturePathExtension::kBlp;
  }
  if (mode == TexturePathExtension::kBlp) {
    dst += ".TGA";
    return TexturePathExtension::kTga;
  }

  return mode;
}

[[nodiscard]] inline std::string StripTexture3CharExtension(std::string_view path) {
  if (path.size() >= 4 && path[path.size() - 4] == '.') {
    return std::string(path.substr(0, path.size() - 4));
  }
  return std::string(path);
}

[[nodiscard]] inline bool TexturePathMatchesIgnoreCaseSansExt(
    std::string_view stored_path, std::string_view input_path) {
  const auto stripped = StripTexture3CharExtension(input_path);
  if (stripped.size() != stored_path.size()) {
    return false;
  }
  for (std::size_t i = 0; i < stripped.size(); ++i) {
    auto normalize = [](char c) -> char {
      if (c == '\\') return '/';
      return static_cast<char>(
          std::tolower(static_cast<unsigned char>(c)));
    };
    if (normalize(stripped[i]) != normalize(stored_path[i])) {
      return false;
    }
  }
  return true;
}

}

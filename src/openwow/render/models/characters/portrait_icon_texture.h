#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::render {

inline constexpr std::string_view kPortraitIconTextureKeyPrefix =
    "__openwow_portrait_icon__|";
inline constexpr std::string_view kPortraitIconMaskTexturePath =
    "Interface\\CharacterFrame\\TempPortraitAlphaMaskSmall";
inline constexpr std::uint8_t kPortraitIconTextureMipLevel = 2u;
inline constexpr std::uint32_t kPortraitIconTextureExtent = 64u;

inline std::string BuildPortraitIconTextureKey(
    const std::string_view source_texture_path) {
  std::string key(kPortraitIconTextureKeyPrefix);
  key.append(source_texture_path.data(), source_texture_path.size());
  return key;
}

inline std::optional<std::string_view> TryParsePortraitIconTextureKey(
    const std::string_view texture_path) {
  if (!texture_path.starts_with(kPortraitIconTextureKeyPrefix)) {
    return std::nullopt;
  }

  return texture_path.substr(kPortraitIconTextureKeyPrefix.size());
}

}

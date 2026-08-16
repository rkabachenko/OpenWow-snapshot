#include "openwow/render/resources/textures/texture_asset.h"

#include <utility>

namespace openwow::render {

TextureAsset::TextureAsset(const TextureAssetKind kind,
                           std::string name,
                           const std::uint32_t width,
                           const std::uint32_t height,
                           const std::uint32_t rgba,
                           const bool ready) noexcept
    : kind_(kind),
      name_(std::move(name)),
      width_(width),
      height_(height),
      rgba_(rgba),
      ready_(ready) {}

TextureAssetPtr TextureAsset::File(std::string path) {
  if (path.empty()) {
    return {};
  }
  return TextureAssetPtr(
      new TextureAsset(TextureAssetKind::File, std::move(path), 0, 0, 0, true));
}

TextureAssetPtr TextureAsset::Solid(const std::uint32_t rgba) {
  return TextureAssetPtr(
      new TextureAsset(TextureAssetKind::Solid, {}, 1, 1, rgba, true));
}

TextureAssetPtr TextureAsset::RenderTarget(std::string name,
                                           const std::uint32_t width,
                                           const std::uint32_t height) {
  if (width == 0 || height == 0) {
    return {};
  }
  return TextureAssetPtr(new TextureAsset(TextureAssetKind::RenderTarget,
                                          std::move(name), width, height, 0,
                                          true));
}

}

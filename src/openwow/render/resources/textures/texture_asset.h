#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace openwow::render {

enum class TextureAssetKind : std::uint8_t {
  File,
  Solid,
  RenderTarget,
};

class TextureAsset final {
 public:
  [[nodiscard]] static std::shared_ptr<TextureAsset> File(std::string path);
  [[nodiscard]] static std::shared_ptr<TextureAsset> Solid(
      std::uint32_t rgba);
  [[nodiscard]] static std::shared_ptr<TextureAsset> RenderTarget(
      std::string name, std::uint32_t width, std::uint32_t height);

  [[nodiscard]] TextureAssetKind kind() const noexcept { return kind_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint32_t rgba() const noexcept { return rgba_; }
  [[nodiscard]] bool ready() const noexcept { return ready_; }

 private:
  TextureAsset(TextureAssetKind kind,
               std::string name,
               std::uint32_t width,
               std::uint32_t height,
               std::uint32_t rgba,
               bool ready) noexcept;

  TextureAssetKind kind_;
  std::string name_;
  std::uint32_t width_;
  std::uint32_t height_;
  std::uint32_t rgba_;
  bool ready_;
};

using TextureAssetPtr = std::shared_ptr<TextureAsset>;

}

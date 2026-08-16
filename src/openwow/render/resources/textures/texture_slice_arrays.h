#pragma once

#include "openwow/render/resources/textures/texture_mip_upload.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace openwow::render {

struct PreparedTextureUpload;

struct TextureSliceBucket {
  std::uint16_t width{0};
  std::uint16_t height{0};

  std::uint8_t mip_count{0};
  BlpUploadFormat format{BlpUploadFormat::kRgba8};

  [[nodiscard]] friend bool operator==(const TextureSliceBucket &,
                                       const TextureSliceBucket &) = default;
};

struct SolidTextureUnit {

  static constexpr std::size_t kMaxBytes = 16u;

  std::array<std::uint8_t, kMaxBytes> bytes{};
  std::uint8_t size{0};
  BlpUploadFormat format{BlpUploadFormat::kRgba8};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return size != 0u;
  }
};

inline constexpr std::uint16_t kInvalidTextureSliceArray = UINT16_MAX;

struct TextureSliceLocation {
  std::uint16_t array{kInvalidTextureSliceArray};
  std::uint16_t slice{0};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return array != kInvalidTextureSliceArray;
  }
  [[nodiscard]] friend constexpr bool operator==(const TextureSliceLocation &,
                                                 const TextureSliceLocation &) = default;
};

namespace detail {
class TextureSlicePool;
struct TextureSliceRef;
}

class TextureSliceLease {
 public:
  TextureSliceLease() = default;

  [[nodiscard]] TextureSliceLocation location() const noexcept;
  [[nodiscard]] bool valid() const noexcept {
    return ref_ != nullptr;
  }

 private:
  friend class TextureSliceArrays;
  explicit TextureSliceLease(std::shared_ptr<detail::TextureSliceRef> ref)
      : ref_(std::move(ref)) {}

  std::shared_ptr<detail::TextureSliceRef> ref_;
};

class TextureSliceArrays {
 public:

  static constexpr std::uint16_t kSlicesPerArray = 256u;
  static_assert(kSlicesPerArray <= 256u,
                "slice indices travel in a Uint8 vertex attribute");

  TextureSliceArrays();
  ~TextureSliceArrays();

  TextureSliceArrays(const TextureSliceArrays &) = delete;
  TextureSliceArrays &operator=(const TextureSliceArrays &) = delete;

  bool Initialize();

  [[nodiscard]] bool supported() const noexcept;

  [[nodiscard]] TextureSliceLease Acquire(std::uint32_t row_hash,
                                          const PreparedTextureUpload *upload,
                                          const TextureSliceBucket *promote_to = nullptr);

  [[nodiscard]] bool IsSolidColor(std::uint32_t row_hash,
                                  const PreparedTextureUpload &upload) const;

  [[nodiscard]] bgfx::TextureHandle ArrayTexture(std::uint16_t array) const noexcept;

  void ReclaimEmptyArrays();

  void Shutdown();

  [[nodiscard]] std::size_t array_count() const noexcept;

  [[nodiscard]] std::size_t live_slice_count() const noexcept;

  [[nodiscard]] static bool TryMakeBucket(const PreparedTextureUpload &upload,
                                          TextureSliceBucket &out) noexcept;

  [[nodiscard]] static std::uint8_t FullMipChainLength(std::uint16_t width,
                                                       std::uint16_t height) noexcept;

  [[nodiscard]] static bool TryMakeSolidUnit(const PreparedTextureUpload &upload,
                                             SolidTextureUnit &out) noexcept;

 private:
  std::shared_ptr<detail::TextureSlicePool> pool_;
};

}

#include "openwow/render/resources/textures/texture_mip_upload.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <optional>

namespace openwow::render {

namespace {

BlpUploadFormat SourceBlockFormat(const data::BLPTexHeader& header) noexcept {
  if (header.compression != data::BLPTexCompression::DXT) {
    return BlpUploadFormat::kRgba8;
  }
  switch (header.alphaEncoding) {
    case 0u:
      return BlpUploadFormat::kBc1;
    case 1u:
      return BlpUploadFormat::kBc2;
    case 7u:
      return BlpUploadFormat::kBc3;
    default:
      return BlpUploadFormat::kRgba8;
  }
}

constexpr std::uint8_t kBc1SupportedBit = 1u << 0u;
constexpr std::uint8_t kBc2SupportedBit = 1u << 1u;
constexpr std::uint8_t kBc3SupportedBit = 1u << 2u;

std::atomic<std::uint8_t>& BlockCompressionSupportBits() noexcept {
  static std::atomic<std::uint8_t> bits{0u};
  return bits;
}

bool CapsSupportTexture2D(const bgfx::TextureFormat::Enum format) noexcept {
  const bgfx::Caps* const caps = bgfx::getCaps();
  if (caps == nullptr) {
    return false;
  }
  return (caps->formats[format] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0u;
}

constexpr std::array<std::uint8_t, 8> kTransparentBlackBc1Block = {
    0x00u, 0x00u, 0x00u, 0x00u, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

constexpr std::array<std::uint8_t, 16> kTransparentBlackBc2Bc3Block = {};

void AppendTransparentBlackBlocks(const BlpUploadFormat format,
                                  const std::size_t block_count,
                                  std::vector<std::uint8_t>& bytes) {
  for (std::size_t block = 0; block < block_count; ++block) {
    switch (format) {
      case BlpUploadFormat::kBc1:
        bytes.insert(bytes.end(), kTransparentBlackBc1Block.begin(),
                     kTransparentBlackBc1Block.end());
        break;
      case BlpUploadFormat::kBc2:
      case BlpUploadFormat::kBc3:
        bytes.insert(bytes.end(), kTransparentBlackBc2Bc3Block.begin(),
                     kTransparentBlackBc2Bc3Block.end());
        break;
      case BlpUploadFormat::kRgba8:

        break;
    }
  }
}

bool AppendBlockMipChain(const data::BLPTextureData& blp,
                         const BlpUploadFormat format,
                         const std::uint8_t requested_mips,
                         BlpRgbaMipUpload& upload) {
  std::size_t reserved_bytes = 0;
  for (std::uint8_t level = 0;
       level < requested_mips && level < blp.mips.size(); ++level) {
    const auto mip_bytes =
        BlpUploadMipSize(upload.width, upload.height, level, format);
    if (!mip_bytes.has_value() ||
        *mip_bytes > std::numeric_limits<std::uint32_t>::max() - reserved_bytes) {
      break;
    }
    reserved_bytes += *mip_bytes;
  }
  upload.bytes.reserve(reserved_bytes);

  for (std::uint8_t level = 0; level < requested_mips; ++level) {
    if (level >= blp.mips.size()) {
      break;
    }
    const auto expected_size =
        BlpUploadMipSize(upload.width, upload.height, level, format);
    if (!expected_size.has_value() ||
        upload.bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        *expected_size > std::numeric_limits<std::uint32_t>::max() -
                             upload.bytes.size()) {
      break;
    }

    const std::uint32_t block_bytes = BlpUploadFormatBytesPerBlock(format);
    const std::size_t required_blocks = *expected_size / block_bytes;
    const auto& source = blp.mips[level].data;
    const std::size_t usable_blocks =
        std::min<std::size_t>(source.size() / block_bytes, required_blocks);

    upload.mip_offsets.push_back(static_cast<std::uint32_t>(upload.bytes.size()));
    upload.mip_sizes.push_back(*expected_size);
    upload.bytes.insert(
        upload.bytes.end(), source.begin(),
        source.begin() + static_cast<std::ptrdiff_t>(usable_blocks * block_bytes));
    AppendTransparentBlackBlocks(format, required_blocks - usable_blocks,
                                 upload.bytes);
    upload.decoded_mip_count =
        static_cast<std::uint8_t>(upload.decoded_mip_count + 1u);
  }

  return upload.decoded_mip_count > 0u;
}

void AppendRgbaMipChain(const data::BLPTextureData& blp,
                        const std::uint8_t requested_mips,
                        BlpRgbaMipUpload& upload) {

  std::size_t reserved_bytes = 0;
  for (std::uint8_t level = 0;
       level < requested_mips && level < blp.mips.size(); ++level) {
    const auto mip_bytes = BlpUploadMipSize(upload.width, upload.height, level,
                                             BlpUploadFormat::kRgba8);
    if (!mip_bytes.has_value() ||
        *mip_bytes > std::numeric_limits<std::uint32_t>::max() - reserved_bytes) {
      break;
    }
    reserved_bytes += *mip_bytes;
  }
  upload.bytes.reserve(reserved_bytes);

  for (std::uint8_t level = 0; level < requested_mips; ++level) {
    if (level >= blp.mips.size()) {
      break;
    }

    const auto expected_size = BlpUploadMipSize(upload.width, upload.height, level,
                                             BlpUploadFormat::kRgba8);
    if (!expected_size.has_value() ||
        upload.bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        *expected_size > std::numeric_limits<std::uint32_t>::max() - upload.bytes.size()) {
      break;
    }

    auto rgba = data::BLPTextureLoader::DecompressMip(blp, level);
    if (rgba.size() != *expected_size) {
      break;
    }

    upload.mip_offsets.push_back(static_cast<std::uint32_t>(upload.bytes.size()));
    upload.mip_sizes.push_back(*expected_size);
    upload.bytes.insert(upload.bytes.end(), rgba.begin(), rgba.end());
    upload.decoded_mip_count = static_cast<std::uint8_t>(upload.decoded_mip_count + 1u);
  }
}

}

std::optional<std::uint32_t> BlpUploadMipSize(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint8_t level, const BlpUploadFormat format) noexcept {
  const auto dims = data::BLPTextureLoader::GetMipDimensions(width, height, level);
  std::uint64_t bytes = 0;
  if (format == BlpUploadFormat::kRgba8) {
    bytes = static_cast<std::uint64_t>(dims.first) * dims.second * 4u;
  } else {

    const std::uint64_t blocks_x = (static_cast<std::uint64_t>(dims.first) + 3u) / 4u;
    const std::uint64_t blocks_y = (static_cast<std::uint64_t>(dims.second) + 3u) / 4u;
    bytes = blocks_x * blocks_y * BlpUploadFormatBytesPerBlock(format);
  }
  if (bytes == 0 || bytes > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(bytes);
}

std::optional<std::uint32_t> BlpUploadMipRowPitch(
    const std::uint32_t width, const BlpUploadFormat format) noexcept {
  std::uint64_t pitch = 0;
  if (format == BlpUploadFormat::kRgba8) {
    pitch = static_cast<std::uint64_t>(width) * 4u;
  } else {
    pitch = ((static_cast<std::uint64_t>(width) + 3u) / 4u) *
            BlpUploadFormatBytesPerBlock(format);
  }
  if (pitch == 0 || pitch > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(pitch);
}

bgfx::TextureFormat::Enum ToBgfxTextureFormat(
    const BlpUploadFormat format) noexcept {

  switch (format) {
    case BlpUploadFormat::kBc1:
      return bgfx::TextureFormat::BC1;
    case BlpUploadFormat::kBc2:
      return bgfx::TextureFormat::BC2;
    case BlpUploadFormat::kBc3:
      return bgfx::TextureFormat::BC3;
    case BlpUploadFormat::kRgba8:
      break;
  }
  return bgfx::TextureFormat::RGBA8;
}

BlockCompressionSupport QueryBlockCompressionSupport() {
  return BlockCompressionSupport{
      .bc1 = CapsSupportTexture2D(ToBgfxTextureFormat(BlpUploadFormat::kBc1)),
      .bc2 = CapsSupportTexture2D(ToBgfxTextureFormat(BlpUploadFormat::kBc2)),
      .bc3 = CapsSupportTexture2D(ToBgfxTextureFormat(BlpUploadFormat::kBc3)),
  };
}

BlockCompressionSupport RefreshBlockCompressionSupport() {
  const BlockCompressionSupport support = QueryBlockCompressionSupport();
  const auto bits = static_cast<std::uint8_t>(
      (support.bc1 ? kBc1SupportedBit : 0u) |
      (support.bc2 ? kBc2SupportedBit : 0u) |
      (support.bc3 ? kBc3SupportedBit : 0u));
  BlockCompressionSupportBits().store(bits, std::memory_order_relaxed);
  return support;
}

BlockCompressionSupport CurrentBlockCompressionSupport() noexcept {
  const std::uint8_t bits =
      BlockCompressionSupportBits().load(std::memory_order_relaxed);
  return BlockCompressionSupport{
      .bc1 = (bits & kBc1SupportedBit) != 0u,
      .bc2 = (bits & kBc2SupportedBit) != 0u,
      .bc3 = (bits & kBc3SupportedBit) != 0u,
  };
}

BlpRgbaMipUpload BuildBlpRgbaMipUpload(
    const data::BLPTextureData& blp,
    const BlockCompressionSupport gpu_support) {
  BlpRgbaMipUpload upload;
  if (!blp.isValid || blp.header.width == 0 || blp.header.height == 0 ||
      blp.header.width > std::numeric_limits<std::uint16_t>::max() ||
      blp.header.height > std::numeric_limits<std::uint16_t>::max() || blp.mips.empty()) {
    return upload;
  }

  upload.width = blp.header.width;
  upload.height = blp.header.height;
  const bool source_has_mips = (blp.header.hasMips & 0x0Fu) != 0u;
  upload.retail_mip_count =
      source_has_mips ? data::BLPTextureLoader::GetMipCount(upload.width, upload.height) : 1u;
  upload.retail_mip_count = std::max<std::uint8_t>(1u, upload.retail_mip_count);

  const std::uint8_t requested_mips =
      std::min<std::uint8_t>(upload.retail_mip_count, 16u);
  upload.mip_offsets.reserve(requested_mips);
  upload.mip_sizes.reserve(requested_mips);

  const BlpUploadFormat block_format = SourceBlockFormat(blp.header);
  if (block_format != BlpUploadFormat::kRgba8 &&
      gpu_support.Supports(block_format) &&
      AppendBlockMipChain(blp, block_format, requested_mips, upload)) {
    upload.format = block_format;
  } else {

    upload.bytes.clear();
    upload.mip_offsets.clear();
    upload.mip_sizes.clear();
    upload.decoded_mip_count = 0u;
    upload.format = BlpUploadFormat::kRgba8;
    AppendRgbaMipChain(blp, requested_mips, upload);
  }

  upload.complete_mip_chain =
      source_has_mips && upload.retail_mip_count > 1u &&
      upload.decoded_mip_count == upload.retail_mip_count;
  return upload;
}

}

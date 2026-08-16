#include "openwow/render/resources/readback/pixel_readback.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>

namespace openwow::render {
namespace {

struct Bgra32ReadbackLayout {
  std::size_t row_bytes = 0;
  std::size_t output_bytes = 0;
  std::size_t required_source_bytes = 0;
};

std::optional<Bgra32ReadbackLayout> ValidateBgra32Readback(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t pitch, const std::size_t source_size) noexcept {
  if (width == 0 || height == 0) {
    return std::nullopt;
  }

  constexpr std::uint64_t kBytesPerPixel = 4;
  const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * kBytesPerPixel;
  if (row_bytes > std::numeric_limits<std::uint32_t>::max() || pitch < row_bytes ||
      row_bytes > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }

  const std::uint64_t output_bytes = row_bytes * height;
  const std::uint64_t required_source_bytes =
      static_cast<std::uint64_t>(height - 1u) * pitch + row_bytes;
  if (output_bytes > std::numeric_limits<std::size_t>::max() ||
      required_source_bytes > std::numeric_limits<std::size_t>::max() ||
      required_source_bytes > source_size) {
    return std::nullopt;
  }

  return Bgra32ReadbackLayout{
      .row_bytes = static_cast<std::size_t>(row_bytes),
      .output_bytes = static_cast<std::size_t>(output_bytes),
      .required_source_bytes = static_cast<std::size_t>(required_source_bytes),
  };
}

void StoreU16Le(std::span<std::uint8_t> destination, const std::size_t offset,
                const std::uint16_t value) noexcept {
  destination[offset] = static_cast<std::uint8_t>(value);
  destination[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void StoreU32Le(std::span<std::uint8_t> destination, const std::size_t offset,
                const std::uint32_t value) noexcept {
  destination[offset] = static_cast<std::uint8_t>(value);
  destination[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
  destination[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
  destination[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

}

std::vector<std::uint8_t> NormalizeBgra32Readback(const std::uint32_t width,
                                                  const std::uint32_t height,
                                                  const std::uint32_t pitch,
                                                  const std::span<const std::uint8_t> data,
                                                  const bool yflip) {
  std::vector<std::uint8_t> normalized;
  const auto layout = ValidateBgra32Readback(width, height, pitch, data.size());
  if (!layout.has_value() || layout->output_bytes > normalized.max_size()) {
    return normalized;
  }

  normalized.resize(layout->output_bytes);

  for (std::uint32_t y = 0; y < height; ++y) {
    const auto src_y = yflip ? (height - 1u - y) : y;
    std::memcpy(normalized.data() + static_cast<std::size_t>(y) * layout->row_bytes,
                data.data() + static_cast<std::size_t>(src_y) * pitch,
                layout->row_bytes);
  }

  return normalized;
}

std::vector<std::uint8_t> EncodeBgra32Bmp(
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t pitch, const std::span<const std::uint8_t> data,
    const bool yflip) {
  constexpr std::uint32_t kHeaderBytes = 54;
  std::vector<std::uint8_t> encoded;
  const auto layout = ValidateBgra32Readback(width, height, pitch, data.size());
  if (!layout.has_value() || width > std::numeric_limits<std::int32_t>::max() ||
      height > std::numeric_limits<std::int32_t>::max() ||
      layout->output_bytes > std::numeric_limits<std::uint32_t>::max() - kHeaderBytes) {
    return encoded;
  }

  const auto pixel_bytes = static_cast<std::uint32_t>(layout->output_bytes);

  const std::uint32_t file_bytes = kHeaderBytes + pixel_bytes;
  encoded.assign(file_bytes, 0);

  encoded[0] = 'B';
  encoded[1] = 'M';
  StoreU32Le(encoded, 2, file_bytes);
  StoreU32Le(encoded, 10, kHeaderBytes);
  StoreU32Le(encoded, 14, 40);
  StoreU32Le(encoded, 18, width);
  StoreU32Le(encoded, 22, height);
  StoreU16Le(encoded, 26, 1);
  StoreU16Le(encoded, 28, 32);
  StoreU32Le(encoded, 34, pixel_bytes);
  StoreU32Le(encoded, 38, 2835);
  StoreU32Le(encoded, 42, 2835);

  for (std::uint32_t output_row = 0; output_row < height; ++output_row) {
    const std::uint32_t bmp_y = height - 1u - output_row;
    const std::uint32_t source_y = yflip ? (height - 1u - bmp_y) : bmp_y;
    std::memcpy(encoded.data() + kHeaderBytes +
                    static_cast<std::size_t>(output_row) * layout->row_bytes,
                data.data() + static_cast<std::size_t>(source_y) * pitch,
                layout->row_bytes);
  }
  return encoded;
}

bool ReadbackAlphaIsFullyOpaque(const std::span<const std::uint8_t> bgra_pixels) noexcept {
  if ((bgra_pixels.size() % 4u) != 0u) {
    return false;
  }

  for (std::size_t i = 3; i < bgra_pixels.size(); i += 4) {
    if (bgra_pixels[i] != 0xFFu) {
      return false;
    }
  }

  return true;
}

std::size_t ApplyAlphaPlaneToBgra32(const std::span<std::uint8_t> bgra_pixels,
                                    const std::span<const std::uint8_t> alpha_plane) noexcept {
  const auto pixel_count = std::min<std::size_t>(bgra_pixels.size() / 4u, alpha_plane.size());
  for (std::size_t i = 0; i < pixel_count; ++i) {
    bgra_pixels[i * 4u + 3u] = alpha_plane[i];
  }

  return pixel_count;
}

}

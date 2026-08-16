#include "openwow/data/image/tga_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openwow::data {
namespace {

constexpr std::size_t kSourceBytesPerPixel = 4u;
constexpr std::size_t kEncodedBytesPerPixel = 3u;
constexpr std::size_t kMaximumRlePacketPixels = 128u;
constexpr std::uint8_t kUncompressedTrueColorImageType = 2u;
constexpr std::uint8_t kRleTrueColorImageType = 10u;
constexpr std::uint8_t kTopDownImageDescriptor = 0x20u;
constexpr std::uint8_t kRlePacketFlag = 0x80u;

[[nodiscard]] std::optional<std::size_t> CheckedProduct(
    const std::size_t left, const std::size_t right) {
  if (left != 0u && right > std::numeric_limits<std::size_t>::max() / left) {
    return std::nullopt;
  }
  return left * right;
}

[[nodiscard]] std::array<std::uint8_t, 18> BuildHeader(
    const std::uint16_t width, const std::uint16_t height,
    const bool compressed) {
  std::array<std::uint8_t, 18> header{};
  header[2] = compressed ? kRleTrueColorImageType
                         : kUncompressedTrueColorImageType;
  header[12] = static_cast<std::uint8_t>(width);
  header[13] = static_cast<std::uint8_t>(width >> 8u);
  header[14] = static_cast<std::uint8_t>(height);
  header[15] = static_cast<std::uint8_t>(height >> 8u);
  header[16] = 24u;
  header[17] = kTopDownImageDescriptor;
  return header;
}

[[nodiscard]] std::array<std::uint8_t, 26> BuildFooter() {
  std::array<std::uint8_t, 26> footer{};
  constexpr std::array signature{
      'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O',
      'N', '-', 'X', 'F', 'I', 'L', 'E', '.', '\0'};
  std::copy(signature.begin(), signature.end(), footer.begin() + 8);
  return footer;
}

[[nodiscard]] bool PixelsEqual(const std::span<const std::uint8_t> pixels,
                               const std::size_t left_pixel,
                               const std::size_t right_pixel) {
  const auto left = pixels.subspan(left_pixel * kEncodedBytesPerPixel,
                                   kEncodedBytesPerPixel);
  const auto right = pixels.subspan(right_pixel * kEncodedBytesPerPixel,
                                    kEncodedBytesPerPixel);
  return std::equal(left.begin(), left.end(), right.begin());
}

[[nodiscard]] std::size_t CountRun(
    const std::span<const std::uint8_t> row, const std::size_t first_pixel,
    const std::size_t row_pixels) {
  const std::size_t limit =
      std::min(row_pixels, first_pixel + kMaximumRlePacketPixels);
  std::size_t end = first_pixel + 1u;
  while (end < limit && PixelsEqual(row, first_pixel, end)) {
    ++end;
  }
  return end - first_pixel;
}

[[nodiscard]] std::vector<std::uint8_t> EncodeRle(
    const std::span<const std::uint8_t> pixels, const std::size_t width,
    const std::size_t height) {
  std::vector<std::uint8_t> encoded;
  encoded.reserve(pixels.size());

  const std::size_t row_bytes = width * kEncodedBytesPerPixel;
  for (std::size_t row_index = 0; row_index < height; ++row_index) {
    const auto row = pixels.subspan(row_index * row_bytes, row_bytes);
    std::size_t pixel = 0u;
    while (pixel < width) {
      const std::size_t run = CountRun(row, pixel, width);
      if (run >= 2u) {
        encoded.push_back(static_cast<std::uint8_t>(
            kRlePacketFlag | static_cast<std::uint8_t>(run - 1u)));
        const auto value = row.subspan(pixel * kEncodedBytesPerPixel,
                                       kEncodedBytesPerPixel);
        encoded.insert(encoded.end(), value.begin(), value.end());
        pixel += run;
        continue;
      }

      const std::size_t raw_start = pixel++;
      while (pixel < width && pixel - raw_start < kMaximumRlePacketPixels &&
             CountRun(row, pixel, width) < 2u) {
        ++pixel;
      }

      const std::size_t raw_count = pixel - raw_start;
      encoded.push_back(static_cast<std::uint8_t>(raw_count - 1u));
      const auto values =
          row.subspan(raw_start * kEncodedBytesPerPixel,
                      raw_count * kEncodedBytesPerPixel);
      encoded.insert(encoded.end(), values.begin(), values.end());
    }
  }

  return encoded;
}

[[nodiscard]] bool WriteExact(std::ofstream &output,
                              const std::span<const std::uint8_t> bytes) {
  if (bytes.size() >
      static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

}

bool WriteBgraScreenshotTga(const std::string_view file_path,
                            const std::span<const std::uint8_t> bgra_pixels,
                            const std::uint16_t width,
                            const std::uint16_t height) {
  if (file_path.empty() || width == 0u || height == 0u) {
    return false;
  }

  const auto pixel_count = CheckedProduct(width, height);
  const auto source_size =
      pixel_count ? CheckedProduct(*pixel_count, kSourceBytesPerPixel)
                  : std::nullopt;
  const auto encoded_size =
      pixel_count ? CheckedProduct(*pixel_count, kEncodedBytesPerPixel)
                  : std::nullopt;
  if (!source_size || !encoded_size || bgra_pixels.size() < *source_size) {
    return false;
  }

  try {
    std::vector<std::uint8_t> bgr_pixels(*encoded_size);
    for (std::size_t pixel = 0u; pixel < *pixel_count; ++pixel) {
      std::memcpy(bgr_pixels.data() + pixel * kEncodedBytesPerPixel,
                  bgra_pixels.data() + pixel * kSourceBytesPerPixel,
                  kEncodedBytesPerPixel);
    }

    auto rle_pixels = EncodeRle(bgr_pixels, width, height);
    const bool use_rle = rle_pixels.size() < bgr_pixels.size();
    const std::span<const std::uint8_t> output_pixels =
        use_rle ? std::span<const std::uint8_t>(rle_pixels)
                : std::span<const std::uint8_t>(bgr_pixels);
    const auto header = BuildHeader(width, height, use_rle);
    const auto footer = BuildFooter();

    const std::filesystem::path path{std::string(file_path)};
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const bool written =
        output.is_open() && WriteExact(output, header) &&
        WriteExact(output, output_pixels) && WriteExact(output, footer);
    output.close();
    if (!written) {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
    return written;
  } catch (const std::bad_alloc &) {
    return false;
  }
}

}

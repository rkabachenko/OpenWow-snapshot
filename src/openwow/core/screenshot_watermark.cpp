#include "openwow/core/screenshot_watermark.h"

#include "openwow/core/matrix_transform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

namespace openwow::core {

namespace {

constexpr std::uint32_t kTileWidth = 4;
constexpr std::uint32_t kTileHeight = 5;
constexpr std::uint32_t kTileArea = kTileWidth * kTileHeight;
constexpr std::uint32_t kRetailWatermarkMinDimension = 64;
constexpr double kTileQuantizationStep = 2.5;
constexpr double kLumaRed = 0.29899999;
constexpr double kLumaGreen = 0.58700001;
constexpr double kLumaBlue = 0.114;
constexpr double kWhiteThresholdRatio = 0.68000001;
constexpr double kMeanEpsilon = 0.0000099999997;
constexpr double kConvergenceTolerance = 0.02;
constexpr std::uint32_t kPayloadBitCount = 88;
constexpr std::uint32_t kEncodedColumnCount = 176;
constexpr std::uint32_t kParityBitCount = 88;
constexpr std::uint32_t kRepeatedColumnCopies = 3;
constexpr std::uint32_t kEncodedColumnRepeats = kRepeatedColumnCopies - 1;

struct WatermarkRect {
  int x_offset = 0;
  int width = 0;
  int y_offset = 0;
  int height = 0;
};

constexpr std::array<float, kTileArea> kTileWeights4x5 = {
    0.5f, 1.5f, 0.75f, 1.25f,
    1.5f, 0.25f, 1.75f, 0.5f,
    0.75f, 1.25f, 0.5f, 1.5f,
    1.25f, 0.5f, 1.5f, 0.75f,
    0.5f, 1.5f, 0.75f, 1.25f,
};

std::uint32_t ComputeEncodedPayloadSizeBytes(const std::size_t payload_size) {
  if (payload_size == 0) {
    return 0;
  }

  const std::uint32_t repeated_columns = kRepeatedColumnCopies * kEncodedColumnCount;
  const std::uint32_t encoded_groups = ((kRepeatedColumnCopies
                                        * (kEncodedColumnCount
                                           + 16u * static_cast<std::uint32_t>(payload_size)))
                                       - 1u) / repeated_columns;
  return ((repeated_columns * encoded_groups) + 7u) >> 3;
}

void WriteBit(std::vector<std::uint8_t>& buffer,
              const std::uint32_t bit_index,
              const bool value) {
  if (!value) {
    buffer[bit_index >> 3] &= static_cast<std::uint8_t>(~(1u << (bit_index & 7u)));
    return;
  }

  buffer[bit_index >> 3] |= static_cast<std::uint8_t>(1u << (bit_index & 7u));
}

std::vector<std::uint8_t> EncodePayload(const std::uint8_t* payload,
                                        const std::size_t payload_size) {
  if (payload == nullptr || payload_size == 0 || payload_size > 0x80u) {
    return {};
  }

  const RetailMatrixTransformTables* tables =
      GetRetailMatrixTransformTables(kEncodedColumnCount);
  if (tables == nullptr) {
    return {};
  }

  const std::uint32_t output_size = ComputeEncodedPayloadSizeBytes(payload_size);
  if (output_size == 0) {
    return {};
  }

  std::vector<std::uint8_t> encoded(output_size, 0);
  const std::uint32_t stripe_width =
      kEncodedColumnCount *
      ((16u * static_cast<std::uint32_t>(payload_size) + kEncodedColumnCount - 1u)
       / kEncodedColumnCount);
  const std::uint32_t total_bits = static_cast<std::uint32_t>(encoded.size() * 8u);
  std::array<std::uint8_t, kPayloadBitCount> source_bits{};
  std::array<std::uint8_t, kEncodedColumnCount> code_bits{};
  std::uint32_t input_bit = 0;
  std::uint32_t output_bit = 0;

  while (true) {
    source_bits.fill(0);
    for (std::uint32_t i = 0; i < kPayloadBitCount; ++i) {
      source_bits[i] = input_bit < payload_size * 8u
                           && ((payload[input_bit >> 3] >> (input_bit & 7u)) & 1u) != 0;
      ++input_bit;
    }

    for (std::uint32_t row = 0; row < kPayloadBitCount; ++row) {
      std::uint8_t parity = 0;
      for (std::uint32_t column = 0; column < kPayloadBitCount; ++column) {
        parity = static_cast<std::uint8_t>(
            parity + (source_bits[column] && ReadMatrixBit(tables->source_bits, row, column)));
      }
      code_bits[row] = static_cast<std::uint8_t>(parity & 1u);
    }

    for (std::uint32_t row = 0; row < kParityBitCount; ++row) {
      std::uint8_t parity = 0;
      for (std::uint32_t column = 0; column < kParityBitCount; ++column) {
        parity = static_cast<std::uint8_t>(
            parity + (code_bits[column] && ReadMatrixBit(tables->check_bits, row, column)));
      }
      code_bits[kPayloadBitCount + row] = static_cast<std::uint8_t>(parity & 1u);
    }

    for (std::uint32_t i = 0; i < kPayloadBitCount; ++i) {
      code_bits[i] = source_bits[i];
    }

    if (output_bit + stripe_width * kEncodedColumnRepeats > total_bits) {
      return {};
    }

    for (std::uint32_t column = 0; column < kEncodedColumnCount; ++column) {
      std::uint32_t write_bit = output_bit;
      for (std::uint32_t copy = 0; copy < kRepeatedColumnCopies; ++copy) {
        WriteBit(encoded, write_bit, code_bits[column] != 0);
        write_bit += stripe_width;
      }
      ++output_bit;
    }

    if (input_bit >= payload_size * 8u) {
      encoded.resize(
          (stripe_width * kEncodedColumnRepeats + output_bit + 7u) >> 3);
      return encoded;
    }
  }
}

bool BuildRects(const std::uint32_t height,
                const std::uint32_t width,
                const std::size_t payload_size,
                std::vector<WatermarkRect>* rects) {
  if (rects == nullptr) {
    return false;
  }

  rects->clear();
  if (width == 800 && height == 600) {
    *rects = {
        WatermarkRect{-384, 384, -220, 220},
        WatermarkRect{0, 300, -285, 285},
        WatermarkRect{-340, 340, 0, 250},
        WatermarkRect{0, 340, 0, 250},
    };
    return true;
  }

  const double band_units = ((static_cast<double>(height) * 0.2325581395348837) + 4.0) * 0.2;
  const int band_height = 5 * static_cast<int>(band_units);
  const int code_columns =
      static_cast<int>(static_cast<double>(
                           static_cast<int>(8u * ComputeEncodedPayloadSizeBytes(payload_size)))
                       / static_cast<double>(static_cast<int>(band_units)))
      + 1;
  const int rect_width = 4 * code_columns;
  if (code_columns > static_cast<int>(width)) {
    return false;
  }

  if (8 * code_columns >= static_cast<int>(width) - 16) {
    const double band_height_d = static_cast<double>(band_height);
    const int centered_x = static_cast<int>(-0.5 * static_cast<double>(rect_width));
    rects->push_back({centered_x, rect_width, static_cast<int>(-1.55 * band_height_d), band_height});
    rects->push_back({centered_x, rect_width, static_cast<int>(-0.5 * band_height_d), band_height});
    rects->push_back({centered_x, rect_width, static_cast<int>(0.55000001 * band_height_d), band_height});
    return true;
  }

  rects->push_back({-(rect_width >> 1), rect_width, -(band_height >> 1), band_height});
  std::uint32_t rect_count = 1;
  const int half_width = static_cast<int>(width >> 1);

  int mirrored_x = rect_width >> 1;
  if ((rect_width >> 1) + rect_width + 4 < half_width) {
    while (rect_count <= 0x1E) {
      const int right_x = mirrored_x + 4;
      rects->push_back({right_x, rect_width, static_cast<int>(-0.5 * static_cast<double>(band_height)), band_height});
      rects->push_back({-(rect_width + right_x), rect_width, static_cast<int>(-0.5 * static_cast<double>(band_height)), band_height});
      rect_count += 2;
      if (rect_width + right_x + rect_width + 4 >= half_width) {
        break;
      }
      mirrored_x = rect_width + right_x;
    }
  }

  int grid_x = 0;
  int occupied_width = rect_width + 4;
  if (rect_width + 4 < half_width) {
    while (rect_count <= 0x1C) {
      const double band_height_d = static_cast<double>(band_height);
      const int x = grid_x + 4;
      const int top_y = -4 - static_cast<int>(band_height_d * 1.5);
      const int bottom_y = 4 - static_cast<int>(band_height_d * -0.5);
      const int mirrored = -(rect_width + x);

      rects->push_back({x, rect_width, top_y, band_height});
      rects->push_back({x, rect_width, bottom_y, band_height});
      rects->push_back({mirrored, rect_width, top_y, band_height});
      rects->push_back({mirrored, rect_width, bottom_y, band_height});
      rect_count += 4;
      occupied_width += rect_width + 4;
      if (occupied_width >= half_width) {
        break;
      }
      grid_x = rect_width + x;
    }
  }

  return !rects->empty();
}

bool ValidateRects(const std::vector<WatermarkRect>& rects,
                   const std::uint32_t width,
                   const std::uint32_t height,
                   const std::size_t payload_size) {
  if (rects.empty()) {
    return false;
  }

  const int half_width = static_cast<int>(width >> 1);
  const int half_height = static_cast<int>(height >> 1);

  for (std::size_t i = 0; i < rects.size(); ++i) {
    const auto& rect = rects[i];
    if (rect.x_offset < -half_width) {
      return false;
    }
    if (rect.x_offset + rect.width > half_width) {
      return false;
    }
    if (rect.y_offset < -half_height) {
      return false;
    }
    if (rect.y_offset + rect.height > half_height) {
      return false;
    }
    if (8u * payload_size
        > static_cast<std::size_t>((rect.width / static_cast<int>(kTileWidth))
                                   * (rect.height / static_cast<int>(kTileHeight)))) {
      return false;
    }

    for (std::size_t other = i + 1; other < rects.size(); ++other) {
      const auto& rhs = rects[other];
      const bool overlap_x =
          rect.x_offset < rhs.x_offset + rhs.width && rhs.x_offset < rect.x_offset + rect.width;
      const bool overlap_y =
          rect.y_offset < rhs.y_offset + rhs.height && rhs.y_offset < rect.y_offset + rect.height;
      if (overlap_x && overlap_y) {
        return false;
      }
    }
  }

  return true;
}

double PixelLuminance(const std::uint8_t* pixel) {
  return static_cast<double>(pixel[2]) * kLumaRed
       + static_cast<double>(pixel[1]) * kLumaGreen
       + static_cast<double>(pixel[0]) * kLumaBlue;
}

double MeasureTileMeanLuminance(const std::vector<std::uint8_t>& pixels,
                                const std::uint32_t width,
                                const std::uint32_t bytes_per_pixel,
                                const int start_x,
                                const int start_y,
                                int* white_pixel_count) {
  double luminance_sum = 0.0;
  int bright_pixels = 0;

  for (std::uint32_t row = 0; row < kTileHeight; ++row) {
    const auto* pixel = pixels.data() + bytes_per_pixel * (start_x + (start_y + static_cast<int>(row)) * static_cast<int>(width));
    for (std::uint32_t column = 0; column < kTileWidth; ++column) {
      if ((255.0 - static_cast<double>(pixel[0])) < kTileQuantizationStep
          || (255.0 - static_cast<double>(pixel[1])) < kTileQuantizationStep
          || (255.0 - static_cast<double>(pixel[2])) < kTileQuantizationStep) {
        ++bright_pixels;
      }
      luminance_sum += PixelLuminance(pixel);
      pixel += bytes_per_pixel;
    }
  }

  if (white_pixel_count != nullptr) {
    *white_pixel_count = bright_pixels;
  }
  return luminance_sum / static_cast<double>(kTileArea);
}

bool NextSerpentineDirection() {
  static std::mutex mutex;
  static bool reverse = false;
  std::lock_guard lock(mutex);
  reverse = !reverse;
  return reverse;
}

double ApplyTileStrength(std::vector<std::uint8_t>& pixels,
                         const std::uint32_t width,
                         const std::uint32_t bytes_per_pixel,
                         const int start_x,
                         const int start_y,
                         const double strength) {
  const bool reverse = NextSerpentineDirection();
  std::array<double, 3> residual = {0.0, 0.0, 0.0};
  double luminance_sum = 0.0;

  if (reverse) {
    int weight_index = static_cast<int>(kTileArea) - 1;
    for (int row = start_y + static_cast<int>(kTileHeight) - 1; row >= start_y; --row) {
      auto* pixel = pixels.data() + bytes_per_pixel
                    * (start_x + static_cast<int>(kTileWidth) - 1 + row * static_cast<int>(width));
      for (int column = start_x + static_cast<int>(kTileWidth) - 1; column >= start_x; --column, --weight_index) {
        const double weight = static_cast<double>(kTileWeights4x5[weight_index]);
        for (int channel = 0; channel < 3; ++channel) {
          const double original = static_cast<double>(pixel[channel]);
          const double adjusted = original + weight * original * strength + residual[channel];
          int rounded = static_cast<int>(adjusted + 0.5);
          if (rounded < 0) {
            residual[channel] = adjusted;
            rounded = 0;
          } else if (rounded > 255) {
            residual[channel] = adjusted - 255.0;
            rounded = 255;
          } else {
            residual[channel] = adjusted - static_cast<double>(rounded);
          }
          pixel[channel] = static_cast<std::uint8_t>(rounded);
        }
        luminance_sum += PixelLuminance(pixel);
        pixel -= bytes_per_pixel;
      }
    }
  } else {
    int weight_index = 0;
    for (int row = start_y; row < start_y + static_cast<int>(kTileHeight); ++row) {
      auto* pixel = pixels.data() + bytes_per_pixel * (start_x + row * static_cast<int>(width));
      for (int column = start_x; column < start_x + static_cast<int>(kTileWidth); ++column, ++weight_index) {
        const double weight = static_cast<double>(kTileWeights4x5[weight_index]);
        for (int channel = 0; channel < 3; ++channel) {
          const double original = static_cast<double>(pixel[channel]);
          const double adjusted = original + weight * original * strength + residual[channel];
          int rounded = static_cast<int>(adjusted + 0.5);
          if (rounded < 0) {
            residual[channel] = adjusted;
            rounded = 0;
          } else if (rounded > 255) {
            residual[channel] = adjusted - 255.0;
            rounded = 255;
          } else {
            residual[channel] = adjusted - static_cast<double>(rounded);
          }
          pixel[channel] = static_cast<std::uint8_t>(rounded);
        }
        luminance_sum += PixelLuminance(pixel);
        pixel += bytes_per_pixel;
      }
    }
  }

  return luminance_sum / static_cast<double>(kTileArea);
}

void WritePackedRealmAddressField(
    std::array<std::uint8_t, kWotlkScreenshotWatermarkPayloadSize>* payload,
    std::string_view realm_address) {
  if (payload == nullptr) {
    return;
  }

  auto& bytes = *payload;
  std::fill(bytes.begin() + 68, bytes.begin() + 85, static_cast<std::uint8_t>('0'));
  if (realm_address.empty()) {
    return;
  }

  int cursor = static_cast<int>(realm_address.size()) - 1;
  auto write_digits = [&](int dest_index) {
    while (cursor >= 0
           && realm_address[static_cast<std::size_t>(cursor)] >= '0'
           && realm_address[static_cast<std::size_t>(cursor)] <= '9'
           && dest_index >= 0) {
      bytes[dest_index--] =
          static_cast<std::uint8_t>(realm_address[static_cast<std::size_t>(cursor)]);
      --cursor;
    }
  };

  if (realm_address[static_cast<std::size_t>(cursor)] >= '0'
      && realm_address[static_cast<std::size_t>(cursor)] <= '9') {
    write_digits(84);
  }
  if (cursor >= 0) {
    --cursor;
  }
  if (cursor >= 0
      && realm_address[static_cast<std::size_t>(cursor)] >= '0'
      && realm_address[static_cast<std::size_t>(cursor)] <= '9') {
    write_digits(79);
  }
  if (cursor >= 0) {
    --cursor;
  }
  if (cursor >= 0
      && realm_address[static_cast<std::size_t>(cursor)] >= '0'
      && realm_address[static_cast<std::size_t>(cursor)] <= '9') {
    write_digits(76);
  }
  if (cursor >= 0) {
    --cursor;
  }
  if (cursor >= 0
      && realm_address[static_cast<std::size_t>(cursor)] >= '0'
      && realm_address[static_cast<std::size_t>(cursor)] <= '9') {
    write_digits(73);
  }
  if (cursor >= 0) {
    --cursor;
  }

  for (int dest_index = 70; cursor >= 0 && dest_index >= 0; --dest_index, --cursor) {
    bytes[dest_index] =
        static_cast<std::uint8_t>(realm_address[static_cast<std::size_t>(cursor)]);
  }
}

}

std::array<std::uint8_t, kWotlkScreenshotWatermarkPayloadSize>
BuildWotlkScreenshotWatermarkPayload(std::string_view account_name,
                                    std::string_view realm_address,
                                    const std::uint32_t packed_time) {
  std::array<std::uint8_t, kWotlkScreenshotWatermarkPayloadSize> payload{};
  if (account_name.empty() || realm_address.empty()) {
    return payload;
  }

  const std::size_t copy_size =
      std::min<std::size_t>(account_name.size(), 64u - 1u);
  std::memcpy(payload.data(), account_name.data(), copy_size);
  payload[copy_size] = 0;
  std::memcpy(payload.data() + 64, &packed_time, sizeof(packed_time));
  WritePackedRealmAddressField(&payload, realm_address);
  payload[85] = 0xFF;
  payload[86] = 0x3F;
  payload[87] = 0x0F;
  return payload;
}

bool ApplyWotlkScreenshotWatermark(std::vector<std::uint8_t>& bgra_pixels,
                                   const std::uint32_t width,
                                   const std::uint32_t height,
                                   const std::uint8_t* payload,
                                   const std::size_t payload_size) {
  if (payload == nullptr || payload_size == 0 || payload_size > 0x80u) {
    return false;
  }
  if (width < kRetailWatermarkMinDimension
      || height < kRetailWatermarkMinDimension) {
    return false;
  }
  if (bgra_pixels.size() < static_cast<std::size_t>(width) * height * 4u) {
    return false;
  }

  std::vector<WatermarkRect> rects;
  if (!BuildRects(height, width, payload_size, &rects)
      || !ValidateRects(rects, width, height, payload_size)) {
    return false;
  }

  const std::vector<std::uint8_t> encoded_bits = EncodePayload(payload, payload_size);
  if (encoded_bits.empty()) {
    return false;
  }

  std::size_t bit_index = 0;
  for (const auto& rect : rects) {
    const int start_x = rect.x_offset + static_cast<int>(width >> 1);
    const int end_x = start_x + rect.width;
    const int start_y = rect.y_offset + static_cast<int>(height >> 1);
    const int end_y = start_y + rect.height;

    for (int tile_x = start_x; tile_x < end_x; tile_x += static_cast<int>(kTileWidth)) {
      for (int tile_y = start_y; tile_y < end_y; tile_y += static_cast<int>(kTileHeight)) {
        int bright_pixels = 0;
        const double original_mean =
            MeasureTileMeanLuminance(bgra_pixels, width, 4, tile_x, tile_y, &bright_pixels);
        double target_mean = original_mean;

        if (encoded_bits.size() > (bit_index >> 3)) {
          const bool encoded_bit =
              (encoded_bits[bit_index >> 3] & (1u << (bit_index & 7u))) != 0;
          if (encoded_bit) {
            target_mean =
                0.5 * kTileQuantizationStep
                + static_cast<double>(static_cast<int>(original_mean / kTileQuantizationStep))
                      * kTileQuantizationStep;
          } else {
            target_mean =
                static_cast<double>(
                    static_cast<int>((original_mean + 0.5 * kTileQuantizationStep)
                                     / kTileQuantizationStep))
                * kTileQuantizationStep;
          }
          ++bit_index;
        }

        if (std::fabs(target_mean - original_mean) <= kMeanEpsilon) {
          continue;
        }

        double adjusted_mean = ApplyTileStrength(
            bgra_pixels, width, 4, tile_x, tile_y, (target_mean - original_mean) / original_mean);
        if (std::fabs((adjusted_mean - target_mean) / kTileQuantizationStep)
            <= kConvergenceTolerance) {
          continue;
        }

        double working_target = target_mean;
        for (std::uint32_t iteration = 1; iteration <= 16; ++iteration) {
          adjusted_mean = ApplyTileStrength(
              bgra_pixels, width, 4, tile_x, tile_y, (working_target - adjusted_mean) / adjusted_mean);
          if (std::fabs((adjusted_mean - target_mean) / kTileQuantizationStep)
              <= kConvergenceTolerance) {
            break;
          }
          if (iteration == 8
              && bright_pixels > static_cast<int>(static_cast<double>(kTileArea)
                                                  * kWhiteThresholdRatio)) {
            working_target -= kTileQuantizationStep;
          }
        }
      }
    }
  }

  return true;
}

}

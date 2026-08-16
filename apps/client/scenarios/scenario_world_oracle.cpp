#include "scenario_world_oracle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace openwow::client::detail {
namespace {

constexpr std::size_t kBmpHeaderSize = 54u;
constexpr std::uint32_t kMaximumBmpDimension = 16'384u;
constexpr std::uint32_t kMetricGridWidth = 128u;
constexpr std::uint32_t kMetricGridHeight = 72u;
constexpr std::uint32_t kComparisonGridWidth = 128u;
constexpr std::uint32_t kComparisonGridHeight = 72u;

struct BgraPixel {
  std::uint8_t blue{0};
  std::uint8_t green{0};
  std::uint8_t red{0};
  std::uint8_t alpha{0};
};

struct BmpImage {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<BgraPixel> pixels;
};

enum class BmpReadStatus : std::uint8_t {
  kPending,
  kComplete,
  kInvalid,
};

struct BmpReadResult {
  BmpReadStatus status{BmpReadStatus::kPending};
  std::string reason;
  BmpImage image;
};

struct NormalizedRect {
  double left{0.0};
  double top{0.0};
  double right{1.0};
  double bottom{1.0};
};

std::uint16_t ReadLe16(const std::array<std::uint8_t, kBmpHeaderSize>& bytes,
                       const std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
}

std::uint32_t ReadLe32(const std::array<std::uint8_t, kBmpHeaderSize>& bytes,
                       const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u |
         static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u |
         static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u;
}

BmpReadResult ReadFinalBackbufferBmp(const std::filesystem::path& path) {
  std::error_code ec;
  const std::uintmax_t actual_file_size =
      std::filesystem::file_size(path, ec);
  if (ec) {
    return {.status = BmpReadStatus::kPending,
            .reason = "capture file is not available yet"};
  }
  if (actual_file_size < kBmpHeaderSize) {
    return {.status = BmpReadStatus::kPending,
            .reason = "capture header is still being written"};
  }

  std::ifstream input(path, std::ios::binary);
  std::array<std::uint8_t, kBmpHeaderSize> header{};
  if (!input.read(reinterpret_cast<char*>(header.data()),
                  static_cast<std::streamsize>(header.size()))) {
    return {.status = BmpReadStatus::kPending,
            .reason = "capture header is still being written"};
  }

  if (header[0] != 'B' || header[1] != 'M' || ReadLe32(header, 14u) != 40u ||
      ReadLe16(header, 26u) != 1u || ReadLe16(header, 28u) != 32u ||
      ReadLe32(header, 30u) != 0u) {
    return {.status = BmpReadStatus::kInvalid,
            .reason = "capture is not an uncompressed 32-bit BMP"};
  }

  const std::uint32_t declared_file_size = ReadLe32(header, 2u);
  const std::uint32_t pixel_offset = ReadLe32(header, 10u);
  const std::uint32_t width = ReadLe32(header, 18u);
  const std::uint32_t height = ReadLe32(header, 22u);
  if (width == 0u || height == 0u || pixel_offset < kBmpHeaderSize ||
      width > kMaximumBmpDimension || height > kMaximumBmpDimension) {
    return {.status = BmpReadStatus::kInvalid,
            .reason = "capture dimensions or pixel offset are invalid"};
  }

  const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4u;
  const std::uint64_t row_stride = (row_bytes + 3u) & ~std::uint64_t{3u};
  const std::uint64_t pixel_bytes =
      row_stride * static_cast<std::uint64_t>(height);
  const std::uint64_t required_file_size =
      static_cast<std::uint64_t>(pixel_offset) + pixel_bytes;
  if (declared_file_size < required_file_size) {
    return {.status = BmpReadStatus::kInvalid,
            .reason = "capture BMP declares a truncated pixel payload"};
  }
  if (actual_file_size < required_file_size) {
    return {.status = BmpReadStatus::kPending,
            .reason = "capture pixels are still being written"};
  }

  const std::uint64_t pixel_count =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (pixel_count >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                 sizeof(BgraPixel))) {
    return {.status = BmpReadStatus::kInvalid,
            .reason = "capture dimensions overflow host storage"};
  }

  BmpImage image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<std::size_t>(pixel_count));
  std::vector<std::uint8_t> row(static_cast<std::size_t>(row_bytes));
  for (std::uint32_t file_y = 0; file_y < height; ++file_y) {
    const std::uint64_t row_offset =
        static_cast<std::uint64_t>(pixel_offset) +
        static_cast<std::uint64_t>(file_y) * row_stride;
    input.seekg(static_cast<std::streamoff>(row_offset));
    if (!input.read(reinterpret_cast<char*>(row.data()),
                    static_cast<std::streamsize>(row.size()))) {
      return {.status = BmpReadStatus::kPending,
              .reason = "capture pixels are still being written"};
    }

    const std::uint32_t screen_y = height - 1u - file_y;
    const std::size_t destination_row =
        static_cast<std::size_t>(screen_y) * width;
    for (std::uint32_t x = 0; x < width; ++x) {
      const std::size_t source = static_cast<std::size_t>(x) * 4u;
      image.pixels[destination_row + x] = {
          .blue = row[source],
          .green = row[source + 1u],
          .red = row[source + 2u],
          .alpha = row[source + 3u],
      };
    }
  }

  return {.status = BmpReadStatus::kComplete,
          .reason = "capture is complete",
          .image = std::move(image)};
}

std::uint8_t PixelLuma(const BgraPixel pixel) {
  return static_cast<std::uint8_t>(
      (77u * pixel.red + 150u * pixel.green + 29u * pixel.blue) >> 8u);
}

std::uint16_t QuantizeColor(const BgraPixel pixel) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(pixel.red >> 4u) << 8u) |
      (static_cast<std::uint16_t>(pixel.green >> 4u) << 4u) |
      static_cast<std::uint16_t>(pixel.blue >> 4u));
}

bool IsYellowIntermediatePixel(const BgraPixel pixel) {
  return pixel.red >= 176u && pixel.green >= 160u && pixel.blue <= 96u &&
         static_cast<unsigned>(pixel.red) + pixel.green >=
             static_cast<unsigned>(pixel.blue) * 3u + 240u;
}

bool IsSaturatedPixel(const BgraPixel pixel) {
  const auto minimum = std::min({pixel.red, pixel.green, pixel.blue});
  const auto maximum = std::max({pixel.red, pixel.green, pixel.blue});
  return maximum >= 248u && minimum <= 16u;
}

std::uint32_t SampleCoordinate(const std::uint32_t sample_index,
                               const std::uint32_t sample_count,
                               const std::uint32_t first,
                               const std::uint32_t last) {
  if (sample_count <= 1u || last <= first) {
    return first;
  }
  return first + static_cast<std::uint32_t>(
                     static_cast<std::uint64_t>(sample_index) *
                     static_cast<std::uint64_t>(last - first) /
                     static_cast<std::uint64_t>(sample_count - 1u));
}

LiveE2eRegionMetrics MeasureRegion(const BmpImage& image,
                                   const NormalizedRect rect) {
  LiveE2eRegionMetrics metrics;
  if (image.width == 0u || image.height == 0u || image.pixels.empty()) {
    return metrics;
  }

  const auto clamp_x = [&](const double value) {
    return static_cast<std::uint32_t>(std::clamp(
        std::llround(value * static_cast<double>(image.width - 1u)),
        0LL, static_cast<long long>(image.width - 1u)));
  };
  const auto clamp_y = [&](const double value) {
    return static_cast<std::uint32_t>(std::clamp(
        std::llround(value * static_cast<double>(image.height - 1u)),
        0LL, static_cast<long long>(image.height - 1u)));
  };

  const std::uint32_t first_x = clamp_x(rect.left);
  const std::uint32_t last_x = std::max(first_x, clamp_x(rect.right));
  const std::uint32_t first_y = clamp_y(rect.top);
  const std::uint32_t last_y = std::max(first_y, clamp_y(rect.bottom));
  const std::uint32_t sample_columns =
      std::min(kMetricGridWidth, last_x - first_x + 1u);
  const std::uint32_t sample_rows =
      std::min(kMetricGridHeight, last_y - first_y + 1u);

  std::unordered_set<std::uint16_t> colors;
  colors.reserve(static_cast<std::size_t>(sample_columns) * sample_rows);
  std::uint64_t luma_sum = 0u;
  std::size_t dark_pixels = 0u;
  std::size_t yellow_pixels = 0u;
  std::size_t saturated_pixels = 0u;
  metrics.minimum_luma = std::numeric_limits<std::uint8_t>::max();
  metrics.maximum_luma = std::numeric_limits<std::uint8_t>::min();

  for (std::uint32_t sy = 0; sy < sample_rows; ++sy) {
    const std::uint32_t y = SampleCoordinate(
        sy, sample_rows, first_y, last_y);
    for (std::uint32_t sx = 0; sx < sample_columns; ++sx) {
      const std::uint32_t x = SampleCoordinate(
          sx, sample_columns, first_x, last_x);
      const BgraPixel pixel =
          image.pixels[static_cast<std::size_t>(y) * image.width + x];
      const std::uint8_t luma = PixelLuma(pixel);
      metrics.minimum_luma = std::min(metrics.minimum_luma, luma);
      metrics.maximum_luma = std::max(metrics.maximum_luma, luma);
      luma_sum += luma;
      dark_pixels += luma <= 12u ? 1u : 0u;
      yellow_pixels += IsYellowIntermediatePixel(pixel) ? 1u : 0u;
      saturated_pixels += IsSaturatedPixel(pixel) ? 1u : 0u;
      colors.insert(QuantizeColor(pixel));
      ++metrics.sample_count;
    }
  }

  metrics.quantized_color_count = colors.size();
  if (metrics.sample_count == 0u) {
    metrics.minimum_luma = 0u;
    return metrics;
  }
  const double denominator = static_cast<double>(metrics.sample_count);
  metrics.mean_luma = static_cast<double>(luma_sum) / denominator;
  metrics.dark_fraction = static_cast<double>(dark_pixels) / denominator;
  metrics.yellow_fraction = static_cast<double>(yellow_pixels) / denominator;
  metrics.saturated_fraction =
      static_cast<double>(saturated_pixels) / denominator;
  return metrics;
}

std::uint64_t ComputePerceptualHash(const BmpImage& image) {
  std::array<std::uint8_t, 64> luma{};
  std::uint64_t total = 0u;
  for (std::uint32_t y = 0; y < 8u; ++y) {
    for (std::uint32_t x = 0; x < 8u; ++x) {
      const std::uint32_t px = SampleCoordinate(x, 8u, 0u, image.width - 1u);
      const std::uint32_t py = SampleCoordinate(y, 8u, 0u, image.height - 1u);
      const std::uint8_t value = PixelLuma(
          image.pixels[static_cast<std::size_t>(py) * image.width + px]);
      luma[static_cast<std::size_t>(y) * 8u + x] = value;
      total += value;
    }
  }
  const std::uint8_t mean = static_cast<std::uint8_t>(total / luma.size());
  std::uint64_t hash = 0u;
  for (std::size_t index = 0; index < luma.size(); ++index) {
    if (luma[index] >= mean) {
      hash |= std::uint64_t{1} << index;
    }
  }
  return hash;
}

LiveE2eFrameValidation MeasureCompletedFrame(const BmpImage& image) {
  LiveE2eFrameValidation result;
  result.width = image.width;
  result.height = image.height;
  result.full_frame = MeasureRegion(image, {0.0, 0.0, 1.0, 1.0});
  result.world_viewport = MeasureRegion(image, {0.10, 0.12, 0.90, 0.76});
  result.player_ui_corner = MeasureRegion(image, {0.0, 0.0, 0.28, 0.26});
  result.minimap_ui_corner = MeasureRegion(image, {0.72, 0.0, 1.0, 0.30});
  result.bottom_ui_band = MeasureRegion(image, {0.0, 0.70, 1.0, 1.0});
  result.perceptual_hash = ComputePerceptualHash(image);
  result.quantized_color_count = result.world_viewport.quantized_color_count;
  result.minimum_luma = result.world_viewport.minimum_luma;
  result.maximum_luma = result.world_viewport.maximum_luma;
  return result;
}

std::string JsonEscape(const std::string_view text) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string escaped;
  escaped.reserve(text.size());
  for (const unsigned char ch : text) {
    switch (ch) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (ch < 0x20u) {
          escaped += "\\u00";
          escaped += kHex[ch >> 4u];
          escaped += kHex[ch & 0x0fu];
        } else {
          escaped.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  return escaped;
}

void WriteRegionJson(std::ostream& out,
                     const LiveE2eRegionMetrics& metrics) {
  out << "{\"samples\":" << metrics.sample_count
      << ",\"colors\":" << metrics.quantized_color_count
      << ",\"lumaMin\":" << static_cast<unsigned>(metrics.minimum_luma)
      << ",\"lumaMax\":" << static_cast<unsigned>(metrics.maximum_luma)
      << ",\"lumaMean\":" << metrics.mean_luma
      << ",\"darkFraction\":" << metrics.dark_fraction
      << ",\"yellowFraction\":" << metrics.yellow_fraction
      << ",\"saturatedFraction\":" << metrics.saturated_fraction << '}';
}

void WriteComparisonJson(std::ostream& out,
                         const LiveE2eFrameComparison& comparison) {
  out << "{\"comparable\":"
      << (comparison.comparable ? "true" : "false")
      << ",\"reason\":\"" << JsonEscape(comparison.reason)
      << "\",\"samples\":" << comparison.sample_count
      << ",\"changedFraction\":" << comparison.changed_fraction
      << ",\"worldViewportChangedFraction\":"
      << comparison.world_viewport_changed_fraction
      << ",\"minimapCornerChangedFraction\":"
      << comparison.minimap_corner_changed_fraction
      << ",\"bottomUiChangedFraction\":"
      << comparison.bottom_ui_changed_fraction
      << ",\"meanAbsoluteChannelDelta\":"
      << comparison.mean_absolute_channel_delta << '}';
}

}

LiveE2eFrameValidation ValidateLiveE2eWorldFrame(
    const std::filesystem::path& path) {
  const BmpReadResult read = ReadFinalBackbufferBmp(path);
  if (read.status == BmpReadStatus::kPending) {
    return {.status = LiveE2eFrameStatus::kPending, .reason = read.reason};
  }
  if (read.status == BmpReadStatus::kInvalid) {
    return {.status = LiveE2eFrameStatus::kInvalid, .reason = read.reason};
  }

  LiveE2eFrameValidation result = MeasureCompletedFrame(read.image);

  constexpr std::size_t kMinimumWorldColors = 16u;
  constexpr std::size_t kMinimumFullFrameColors = 24u;
  constexpr std::uint8_t kMinimumWorldLumaRange = 28u;
  constexpr double kMaximumWorldDarkFraction = 0.82;
  constexpr double kMaximumFullDarkFraction = 0.78;
  constexpr double kMaximumWorldYellowFraction = 0.48;
  constexpr double kMaximumFullYellowFraction = 0.42;
  constexpr double kMaximumWorldSaturatedFraction = 0.78;

  const std::uint8_t world_luma_range =
      result.world_viewport.maximum_luma - result.world_viewport.minimum_luma;
  if (result.world_viewport.quantized_color_count < kMinimumWorldColors ||
      result.full_frame.quantized_color_count < kMinimumFullFrameColors ||
      world_luma_range < kMinimumWorldLumaRange) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason = "final world/back-buffer is near-solid";
    return result;
  }
  if (result.world_viewport.dark_fraction > kMaximumWorldDarkFraction ||
      result.full_frame.dark_fraction > kMaximumFullDarkFraction) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason = "final world/back-buffer is predominantly black";
    return result;
  }
  if (result.world_viewport.yellow_fraction > kMaximumWorldYellowFraction ||
      result.full_frame.yellow_fraction > kMaximumFullYellowFraction) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason = "capture resembles a yellow intermediate render target";
    return result;
  }
  if (result.world_viewport.saturated_fraction >
      kMaximumWorldSaturatedFraction) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason = "world viewport is overwhelmingly channel-clipped";
    return result;
  }

  result.status = LiveE2eFrameStatus::kPlayable;
  result.reason = "final back-buffer has stable world/UI render variation";
  return result;
}

LiveE2eFrameValidation ValidateLiveE2eLoadingFrame(
    const std::filesystem::path& path) {
  const BmpReadResult read = ReadFinalBackbufferBmp(path);
  if (read.status == BmpReadStatus::kPending) {
    return {.status = LiveE2eFrameStatus::kPending, .reason = read.reason};
  }
  if (read.status == BmpReadStatus::kInvalid) {
    return {.status = LiveE2eFrameStatus::kInvalid, .reason = read.reason};
  }

  LiveE2eFrameValidation result = MeasureCompletedFrame(read.image);
  constexpr std::size_t kMinimumFullFrameColors = 24u;
  constexpr std::size_t kMinimumBackgroundColors = 16u;
  constexpr std::size_t kMinimumBottomBandColors = 8u;
  constexpr std::uint8_t kMinimumBackgroundLumaRange = 24u;
  constexpr std::uint8_t kMinimumBottomBandLumaRange = 18u;
  constexpr double kMaximumFullDarkFraction = 0.90;
  constexpr double kMaximumBackgroundDarkFraction = 0.90;
  constexpr double kMaximumBottomBandDarkFraction = 0.96;
  constexpr double kMaximumYellowFraction = 0.42;

  const auto luma_range = [](const LiveE2eRegionMetrics& metrics) {
    return static_cast<std::uint8_t>(metrics.maximum_luma -
                                     metrics.minimum_luma);
  };
  if (result.full_frame.quantized_color_count < kMinimumFullFrameColors ||
      result.world_viewport.quantized_color_count < kMinimumBackgroundColors ||
      result.bottom_ui_band.quantized_color_count < kMinimumBottomBandColors ||
      luma_range(result.world_viewport) < kMinimumBackgroundLumaRange ||
      luma_range(result.bottom_ui_band) < kMinimumBottomBandLumaRange) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason =
        "loading compositor lacks varied background/progress-bar content";
    return result;
  }
  if (result.full_frame.dark_fraction > kMaximumFullDarkFraction ||
      result.world_viewport.dark_fraction > kMaximumBackgroundDarkFraction ||
      result.bottom_ui_band.dark_fraction > kMaximumBottomBandDarkFraction) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason =
        "loading compositor resembles the detached black debug fallback";
    return result;
  }
  if (result.full_frame.yellow_fraction > kMaximumYellowFraction ||
      result.world_viewport.yellow_fraction > kMaximumYellowFraction) {
    result.status = LiveE2eFrameStatus::kDegenerate;
    result.reason = "loading capture resembles an intermediate render target";
    return result;
  }

  result.status = LiveE2eFrameStatus::kPlayable;
  result.reason =
      "final back-buffer contains the composed retail loading surface";
  return result;
}

LiveE2eFrameComparison CompareLiveE2eWorldFrames(
    const std::filesystem::path& first,
    const std::filesystem::path& second) {
  const BmpReadResult first_read = ReadFinalBackbufferBmp(first);
  const BmpReadResult second_read = ReadFinalBackbufferBmp(second);
  LiveE2eFrameComparison result;
  if (first_read.status != BmpReadStatus::kComplete ||
      second_read.status != BmpReadStatus::kComplete) {
    result.reason = "one or both captures are incomplete or invalid";
    return result;
  }

  result.first_width = first_read.image.width;
  result.first_height = first_read.image.height;
  result.second_width = second_read.image.width;
  result.second_height = second_read.image.height;
  if (result.first_width != result.second_width ||
      result.first_height != result.second_height) {
    result.reason = "capture dimensions differ";
    return result;
  }

  const std::uint32_t columns =
      std::min(kComparisonGridWidth, result.first_width);
  const std::uint32_t rows =
      std::min(kComparisonGridHeight, result.first_height);
  std::size_t changed = 0u;
  std::size_t world_samples = 0u;
  std::size_t world_changed = 0u;
  std::size_t minimap_samples = 0u;
  std::size_t minimap_changed = 0u;
  std::size_t bottom_samples = 0u;
  std::size_t bottom_changed = 0u;
  std::uint64_t absolute_delta = 0u;
  for (std::uint32_t sy = 0; sy < rows; ++sy) {
    const std::uint32_t y = SampleCoordinate(
        sy, rows, 0u, result.first_height - 1u);
    for (std::uint32_t sx = 0; sx < columns; ++sx) {
      const std::uint32_t x = SampleCoordinate(
          sx, columns, 0u, result.first_width - 1u);
      const std::size_t index =
          static_cast<std::size_t>(y) * result.first_width + x;
      const BgraPixel a = first_read.image.pixels[index];
      const BgraPixel b = second_read.image.pixels[index];
      const auto red_delta = static_cast<unsigned>(std::abs(
          static_cast<int>(a.red) - static_cast<int>(b.red)));
      const auto green_delta = static_cast<unsigned>(std::abs(
          static_cast<int>(a.green) - static_cast<int>(b.green)));
      const auto blue_delta = static_cast<unsigned>(std::abs(
          static_cast<int>(a.blue) - static_cast<int>(b.blue)));
      const unsigned pixel_delta = red_delta + green_delta + blue_delta;
      const bool pixel_changed = pixel_delta >= 30u;
      absolute_delta += pixel_delta;
      changed += pixel_changed ? 1u : 0u;
      const double normalized_x = result.first_width <= 1u
                                      ? 0.0
                                      : static_cast<double>(x) /
                                            (result.first_width - 1u);
      const double normalized_y = result.first_height <= 1u
                                      ? 0.0
                                      : static_cast<double>(y) /
                                            (result.first_height - 1u);
      if (normalized_x >= 0.10 && normalized_x <= 0.90 &&
          normalized_y >= 0.12 && normalized_y <= 0.76) {
        ++world_samples;
        world_changed += pixel_changed ? 1u : 0u;
      }
      if (normalized_x >= 0.72 && normalized_y <= 0.30) {
        ++minimap_samples;
        minimap_changed += pixel_changed ? 1u : 0u;
      }
      if (normalized_y >= 0.70) {
        ++bottom_samples;
        bottom_changed += pixel_changed ? 1u : 0u;
      }
      ++result.sample_count;
    }
  }

  if (result.sample_count == 0u) {
    result.reason = "capture comparison grid is empty";
    return result;
  }
  result.comparable = true;
  result.reason = "captures share a comparable final-back-buffer grid";
  result.changed_fraction =
      static_cast<double>(changed) / static_cast<double>(result.sample_count);
  result.world_viewport_changed_fraction =
      world_samples == 0u
          ? 0.0
          : static_cast<double>(world_changed) /
                static_cast<double>(world_samples);
  result.minimap_corner_changed_fraction =
      minimap_samples == 0u
          ? 0.0
          : static_cast<double>(minimap_changed) /
                static_cast<double>(minimap_samples);
  result.bottom_ui_changed_fraction =
      bottom_samples == 0u
          ? 0.0
          : static_cast<double>(bottom_changed) /
                static_cast<double>(bottom_samples);
  result.mean_absolute_channel_delta =
      static_cast<double>(absolute_delta) /
      (static_cast<double>(result.sample_count) * 3.0);
  return result;
}

std::string_view LiveE2eCapturePurposeName(
    const LiveE2eCapturePurpose purpose) noexcept {
  switch (purpose) {
    case LiveE2eCapturePurpose::kLoadingScreen: return "loading_screen";
    case LiveE2eCapturePurpose::kGameplayBaseline: return "gameplay_baseline";
    case LiveE2eCapturePurpose::kWorldUiInteractions:
      return "world_ui_interactions";
    case LiveE2eCapturePurpose::kWorldMapOpen: return "world_map_open";
    case LiveE2eCapturePurpose::kWorldMapReopened:
      return "world_map_reopened";
    case LiveE2eCapturePurpose::kCharacterPanelOpen:
      return "character_panel_open";
    case LiveE2eCapturePurpose::kPostMovement: return "post_movement";
    case LiveE2eCapturePurpose::kStableGameplay: return "stable_gameplay";
    case LiveE2eCapturePurpose::kFailureDiagnostic:
      return "failure_diagnostic";
  }
  return "unknown";
}

bool WriteLiveE2eWorldOracleReport(
    const std::filesystem::path& path,
    const LiveE2eWorldOracleReport& report) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  std::ofstream out(temporary, std::ios::trunc);
  if (!out) {
    return false;
  }

  out << "{\n  \"schema\": 1,\n  \"completed\": "
      << (report.completed ? "true" : "false")
      << ",\n  \"passed\": " << (report.passed ? "true" : "false")
      << ",\n  \"failure\": \"" << JsonEscape(report.failure) << "\",\n";
  out << "  \"milestones\": [\n";
  for (std::size_t index = 0; index < report.milestones.size(); ++index) {
    const auto& milestone = report.milestones[index];
    out << "    {\"name\":\"" << JsonEscape(milestone.name)
        << "\",\"elapsedMs\":" << milestone.elapsed_ms << '}';
    if (index + 1u != report.milestones.size()) out << ',';
    out << '\n';
  }
  out << "  ],\n  \"semanticSamples\": [\n";
  for (std::size_t index = 0; index < report.semantic_samples.size(); ++index) {
    const auto& sample = report.semantic_samples[index];
    out << "    {\"elapsedMs\":" << sample.elapsed_ms
        << ",\"frameGeneration\":" << sample.frame_generation
        << ",\"finalBackbufferReady\":"
        << (sample.final_backbuffer_ready ? "true" : "false")
        << ",\"loadingScreenVisible\":"
        << (sample.loading_screen_visible ? "true" : "false")
        << ",\"loadingScreenSoleOwner\":"
        << (sample.loading_screen_sole_owner ? "true" : "false")
        << ",\"loadingFinalBackbufferReady\":"
        << (sample.loading_final_backbuffer_ready ? "true" : "false")
        << ",\"loadingRenderSubmissions\":"
        << sample.loading_render_submissions
        << ",\"loadingSelfPresentedFrames\":"
        << sample.loading_self_presented_frames
        << ",\"loadingCoalescedCallbacks\":"
        << sample.loading_coalesced_callbacks
        << ",\"playerRenderReady\":"
        << (sample.player_render_ready ? "true" : "false")
        << ",\"worldUiReady\":"
        << (sample.world_ui_ready ? "true" : "false")
        << ",\"playerFrameReady\":"
        << (sample.player_frame_ready ? "true" : "false")
        << ",\"portraitReady\":"
        << (sample.portrait_ready ? "true" : "false")
        << ",\"healthPowerReady\":"
        << (sample.health_power_ready ? "true" : "false")
        << ",\"actionBarReady\":"
        << (sample.action_bar_ready ? "true" : "false")
        << ",\"chatReady\":" << (sample.chat_ready ? "true" : "false")
        << ",\"minimapReady\":"
        << (sample.minimap_ready ? "true" : "false")
        << ",\"worldMapReady\":"
        << (sample.world_map_ready ? "true" : "false")
        << ",\"worldMapVisible\":"
        << (sample.world_map_visible ? "true" : "false")
        << ",\"characterPanelReady\":"
        << (sample.character_panel_ready ? "true" : "false")
        << ",\"characterModelReady\":"
        << (sample.character_model_ready ? "true" : "false")
        << ",\"characterIdentityReady\":"
        << (sample.character_identity_ready ? "true" : "false")
        << ",\"characterPanelVisible\":"
        << (sample.character_panel_visible ? "true" : "false")
        << ",\"nameplatesReady\":"
        << (sample.nameplates_ready ? "true" : "false")
        << ",\"visibleNameplates\":" << sample.visible_nameplates
        << ",\"terrainTiles\":" << sample.terrain_tiles_loaded
        << ",\"objectInstances\":" << sample.object_instances
        << ",\"uiTraversalEntries\":" << sample.ui_traversal_entries
        << ",\"uiRenderCandidates\":" << sample.ui_render_candidates
        << ",\"renderDrawCalls\":" << sample.render_draw_calls
        << ",\"renderCpuMs\":" << sample.render_cpu_time_ms
        << ",\"renderGpuMs\":" << sample.render_gpu_time_ms
        << '}';
    if (index + 1u != report.semantic_samples.size()) out << ',';
    out << '\n';
  }
  out << "  ],\n  \"captures\": [\n";
  for (std::size_t index = 0; index < report.captures.size(); ++index) {
    const auto& capture = report.captures[index];
    const std::string sanitized_filename =
        std::filesystem::path(capture.filename).filename().string();
    out << "    {\"purpose\":\""
        << LiveE2eCapturePurposeName(capture.purpose)
        << "\",\"filename\":\"" << JsonEscape(sanitized_filename)
        << "\",\"elapsedMs\":" << capture.elapsed_ms
        << ",\"frameGeneration\":" << capture.frame_generation
        << ",\"status\":" << static_cast<unsigned>(capture.validation.status)
        << ",\"reason\":\"" << JsonEscape(capture.validation.reason)
        << "\",\"width\":" << capture.validation.width
        << ",\"height\":" << capture.validation.height
        << ",\"perceptualHash\":" << capture.validation.perceptual_hash
        << ",\"fullFrame\":";
    WriteRegionJson(out, capture.validation.full_frame);
    out << ",\"worldViewport\":";
    WriteRegionJson(out, capture.validation.world_viewport);
    out << ",\"playerUiCorner\":";
    WriteRegionJson(out, capture.validation.player_ui_corner);
    out << ",\"minimapUiCorner\":";
    WriteRegionJson(out, capture.validation.minimap_ui_corner);
    out << ",\"bottomUiBand\":";
    WriteRegionJson(out, capture.validation.bottom_ui_band);
    if (capture.comparison_to_baseline.has_value()) {
      out << ",\"comparisonToBaseline\":";
      WriteComparisonJson(out, *capture.comparison_to_baseline);
    }
    out << '}';
    if (index + 1u != report.captures.size()) out << ',';
    out << '\n';
  }
  out << "  ]\n}\n";
  out.close();
  if (!out) {
    std::filesystem::remove(temporary, ec);
    return false;
  }

  ec.clear();
  std::filesystem::rename(temporary, path, ec);
  if (ec) {

    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
  }
  if (ec) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  return true;
}

}

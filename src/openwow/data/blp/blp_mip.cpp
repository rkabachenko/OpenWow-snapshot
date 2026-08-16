
#include "openwow/data/blp/blp_mip.h"
#include "openwow/data/blp/blp_dxt_blit_short.h"
#include "openwow/data/blp/dxt_block_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace openwow::data {

namespace {

constexpr uint32_t kBlpImageMagic = 0x32504C42u;
constexpr std::size_t kBlpImageHeaderBytes = 0x494u;
constexpr uint32_t kBlpImageDefaultVersion = 1u;
constexpr uint8_t kBlpImageDefaultAlphaType = 2u;
constexpr uint32_t kBlpImageDefaultQuality = 100u;

void *BlpDxt_CopyBlockRows(const int *dims, const void *source_data, int source_stride,
                           void *destination_data, int destination_stride,
                           int bytes_per_width_unit) {
  int width = dims[0];
  if (width < 4) {
    width = 4;
  }

  int row_count = dims[1] >> 2;
  if (row_count < 1) {
    row_count = 1;
  }

  const std::size_t row_bytes =
      static_cast<std::size_t>(bytes_per_width_unit) * static_cast<std::size_t>(width);

  if (row_bytes == static_cast<std::size_t>(source_stride) &&
      row_bytes == static_cast<std::size_t>(destination_stride)) {
    return std::memcpy(destination_data, source_data,
                       row_bytes * static_cast<std::size_t>(row_count));
  }

  const auto *source = static_cast<const std::byte *>(source_data);
  auto *destination = static_cast<std::byte *>(destination_data);
  void *last_copy = nullptr;

  for (int row = 0; row < row_count; ++row) {
    last_copy = std::memcpy(destination, source, row_bytes);
    source += source_stride;
    destination += destination_stride;
  }

  return last_copy;
}

int BlpDxt_CopyWidthUnit(BlpDxtDispatchFormat dispatch_format) {
  switch (dispatch_format) {
  case BlpDxtDispatchFormat::kDxt1:
    return 2;
  case BlpDxtDispatchFormat::kDxt3:
  case BlpDxtDispatchFormat::kDxt5:
    return 4;
  default:
    return 0;
  }
}

uint16_t BlpDxtPackRgb565(uint8_t red5, uint8_t green6, uint8_t blue5) {
  return static_cast<uint16_t>(((red5 & 0x1Fu) << 11) | ((green6 & 0x3Fu) << 5) |
                               (blue5 & 0x1Fu));
}

uint16_t BlpDxtPackArgb1555(uint8_t red5, uint8_t green6, uint8_t blue5, bool opaque) {
  return static_cast<uint16_t>(((opaque ? 1u : 0u) << 15) | ((red5 & 0x1Fu) << 10) |
                               (((green6 >> 1) & 0x1Fu) << 5) | (blue5 & 0x1Fu));
}

uint16_t BlpDxtPackRgb565ToArgb1555(uint16_t color) {
  return BlpDxtPackArgb1555(static_cast<uint8_t>((color >> 11) & 0x1F),
                            static_cast<uint8_t>((color >> 5) & 0x3F),
                            static_cast<uint8_t>(color & 0x1F), true);
}

uint32_t BlpDxtPackArgb8888(uint8_t red5, uint8_t green6, uint8_t blue5, bool opaque) {
  return (opaque ? 0xFF000000u : 0u) | (static_cast<uint32_t>(red5 & 0x1Fu) << 19) |
         (static_cast<uint32_t>(green6 & 0x3Fu) << 10) |
         (static_cast<uint32_t>(blue5 & 0x1Fu) << 3);
}

uint32_t BlpDxtPackRgb565ToArgb8888(uint16_t color) {
  return BlpDxtPackArgb8888(static_cast<uint8_t>((color >> 11) & 0x1F),
                            static_cast<uint8_t>((color >> 5) & 0x3F),
                            static_cast<uint8_t>(color & 0x1F), true);
}

uint16_t BlpDxtPackArgb4444(uint8_t red5, uint8_t green6, uint8_t blue5) {
  return static_cast<uint16_t>(0xF000u | (static_cast<uint16_t>(red5 >> 1) << 8) |
                               (static_cast<uint16_t>(green6 >> 2) << 4) | (blue5 >> 1));
}

uint16_t BlpDxtPackRgb565ToArgb4444(uint16_t color) {
  return BlpDxtPackArgb4444(static_cast<uint8_t>((color >> 11) & 0x1F),
                            static_cast<uint8_t>((color >> 5) & 0x3F),
                            static_cast<uint8_t>(color & 0x1F));
}

std::array<uint32_t, 4> BlpDxtBuildDxt1Argb8888Palette(const uint8_t *block) {
  const uint16_t color0 = static_cast<uint16_t>(block[0] | (static_cast<uint16_t>(block[1]) << 8));
  const uint16_t color1 = static_cast<uint16_t>(block[2] | (static_cast<uint16_t>(block[3]) << 8));

  const uint8_t red0 = static_cast<uint8_t>((color0 >> 11) & 0x1F);
  const uint8_t green0 = static_cast<uint8_t>((color0 >> 5) & 0x3F);
  const uint8_t blue0 = static_cast<uint8_t>(color0 & 0x1F);
  const uint8_t red1 = static_cast<uint8_t>((color1 >> 11) & 0x1F);
  const uint8_t green1 = static_cast<uint8_t>((color1 >> 5) & 0x3F);
  const uint8_t blue1 = static_cast<uint8_t>(color1 & 0x1F);

  std::array<uint32_t, 4> palette{
      BlpDxtPackRgb565ToArgb8888(color0),
      BlpDxtPackRgb565ToArgb8888(color1),
      0u,
      0u,
  };

  if (color0 > color1) {
    palette[2] = BlpDxtPackArgb8888(static_cast<uint8_t>((2u * red0 + red1) / 3u),
                                    static_cast<uint8_t>((2u * green0 + green1) / 3u),
                                    static_cast<uint8_t>((2u * blue0 + blue1) / 3u), true);
    palette[3] = BlpDxtPackArgb8888(static_cast<uint8_t>((red0 + 2u * red1) / 3u),
                                    static_cast<uint8_t>((green0 + 2u * green1) / 3u),
                                    static_cast<uint8_t>((blue0 + 2u * blue1) / 3u), true);
  } else {
    palette[2] = BlpDxtPackArgb8888(static_cast<uint8_t>((red0 + red1) / 2u),
                                    static_cast<uint8_t>((green0 + green1) / 2u),
                                    static_cast<uint8_t>((blue0 + blue1) / 2u), true);
    palette[3] = 0u;
  }

  return palette;
}

std::array<uint32_t, 4> BlpDxtBuildOpaqueArgb8888Palette(const uint8_t *block) {
  const uint16_t color0 = static_cast<uint16_t>(block[0] | (static_cast<uint16_t>(block[1]) << 8));
  const uint16_t color1 = static_cast<uint16_t>(block[2] | (static_cast<uint16_t>(block[3]) << 8));

  const uint8_t red0 = static_cast<uint8_t>((color0 >> 11) & 0x1F);
  const uint8_t green0 = static_cast<uint8_t>((color0 >> 5) & 0x3F);
  const uint8_t blue0 = static_cast<uint8_t>(color0 & 0x1F);
  const uint8_t red1 = static_cast<uint8_t>((color1 >> 11) & 0x1F);
  const uint8_t green1 = static_cast<uint8_t>((color1 >> 5) & 0x3F);
  const uint8_t blue1 = static_cast<uint8_t>(color1 & 0x1F);

  return {
      BlpDxtPackRgb565ToArgb8888(color0),
      BlpDxtPackRgb565ToArgb8888(color1),
      BlpDxtPackArgb8888(static_cast<uint8_t>((2u * red0 + red1) / 3u),
                         static_cast<uint8_t>((2u * green0 + green1) / 3u),
                         static_cast<uint8_t>((2u * blue0 + blue1) / 3u), true),
      BlpDxtPackArgb8888(static_cast<uint8_t>((red0 + 2u * red1) / 3u),
                         static_cast<uint8_t>((green0 + 2u * green1) / 3u),
                         static_cast<uint8_t>((blue0 + 2u * blue1) / 3u), true),
  };
}

std::array<uint16_t, 4> BlpDxtBuildDxt1Argb1555Palette(const uint8_t *block) {
  const uint16_t color0 = static_cast<uint16_t>(block[0] | (static_cast<uint16_t>(block[1]) << 8));
  const uint16_t color1 = static_cast<uint16_t>(block[2] | (static_cast<uint16_t>(block[3]) << 8));

  const uint8_t red0 = static_cast<uint8_t>((color0 >> 11) & 0x1F);
  const uint8_t green0 = static_cast<uint8_t>((color0 >> 5) & 0x3F);
  const uint8_t blue0 = static_cast<uint8_t>(color0 & 0x1F);
  const uint8_t red1 = static_cast<uint8_t>((color1 >> 11) & 0x1F);
  const uint8_t green1 = static_cast<uint8_t>((color1 >> 5) & 0x3F);
  const uint8_t blue1 = static_cast<uint8_t>(color1 & 0x1F);

  std::array<uint16_t, 4> palette{
      BlpDxtPackRgb565ToArgb1555(color0),
      BlpDxtPackRgb565ToArgb1555(color1),
      0u,
      0u,
  };

  if (color0 > color1) {
    palette[2] = BlpDxtPackArgb1555(static_cast<uint8_t>((2u * red0 + red1) / 3u),
                                    static_cast<uint8_t>((2u * green0 + green1) / 3u),
                                    static_cast<uint8_t>((2u * blue0 + blue1) / 3u), true);
    palette[3] = BlpDxtPackArgb1555(static_cast<uint8_t>((red0 + 2u * red1) / 3u),
                                    static_cast<uint8_t>((green0 + 2u * green1) / 3u),
                                    static_cast<uint8_t>((blue0 + 2u * blue1) / 3u), true);
  } else {
    palette[2] = BlpDxtPackArgb1555(static_cast<uint8_t>((red0 + red1) / 2u),
                                    static_cast<uint8_t>((green0 + green1) / 2u),
                                    static_cast<uint8_t>((blue0 + blue1) / 2u), true);
    palette[3] = 0u;
  }

  return palette;
}

std::array<uint16_t, 4> BlpDxtBuildDxt1Rgb565Palette(const uint8_t *block) {
  const uint16_t color0 = static_cast<uint16_t>(block[0] | (static_cast<uint16_t>(block[1]) << 8));
  const uint16_t color1 = static_cast<uint16_t>(block[2] | (static_cast<uint16_t>(block[3]) << 8));

  const uint8_t red0 = static_cast<uint8_t>((color0 >> 11) & 0x1F);
  const uint8_t green0 = static_cast<uint8_t>((color0 >> 5) & 0x3F);
  const uint8_t blue0 = static_cast<uint8_t>(color0 & 0x1F);
  const uint8_t red1 = static_cast<uint8_t>((color1 >> 11) & 0x1F);
  const uint8_t green1 = static_cast<uint8_t>((color1 >> 5) & 0x3F);
  const uint8_t blue1 = static_cast<uint8_t>(color1 & 0x1F);

  std::array<uint16_t, 4> palette{
      color0,
      color1,
      0u,
      0u,
  };

  if (color0 > color1) {
    palette[2] = BlpDxtPackRgb565(static_cast<uint8_t>((2u * red0 + red1) / 3u),
                                  static_cast<uint8_t>((2u * green0 + green1) / 3u),
                                  static_cast<uint8_t>((2u * blue0 + blue1) / 3u));
    palette[3] = BlpDxtPackRgb565(static_cast<uint8_t>((red0 + 2u * red1) / 3u),
                                  static_cast<uint8_t>((green0 + 2u * green1) / 3u),
                                  static_cast<uint8_t>((blue0 + 2u * blue1) / 3u));
  } else {
    palette[2] = BlpDxtPackRgb565(static_cast<uint8_t>((red0 + red1) / 2u),
                                  static_cast<uint8_t>((green0 + green1) / 2u),
                                  static_cast<uint8_t>((blue0 + blue1) / 2u));
    palette[3] = 0u;
  }

  return palette;
}

template <typename Pixel, typename PaletteBuilder>
void BlpDxtDecodeDxt1Block(const uint8_t *source_block, Pixel *destination_block,
                           int destination_stride, int output_width, int output_height,
                           PaletteBuilder build_palette) {
  const auto palette = build_palette(source_block);
  const uint32_t selectors = static_cast<uint32_t>(source_block[4]) |
                             (static_cast<uint32_t>(source_block[5]) << 8) |
                             (static_cast<uint32_t>(source_block[6]) << 16) |
                             (static_cast<uint32_t>(source_block[7]) << 24);

  for (int y = 0; y < output_height; ++y) {
    auto *destination_row = reinterpret_cast<Pixel *>(reinterpret_cast<std::byte *>(destination_block) +
                                                      static_cast<std::ptrdiff_t>(y) * destination_stride);
    for (int x = 0; x < output_width; ++x) {
      const uint32_t selector = (selectors >> (2u * (4u * y + x))) & 0x3u;
      destination_row[x] = palette[selector];
    }
  }
}

template <typename Pixel, typename PaletteBuilder>
void BlpDxtDecodeDxt1Region(const uint8_t *source_data, int source_stride, Pixel *destination_data,
                            int destination_stride, int width, int height,
                            PaletteBuilder build_palette) {
  if (width <= 0 || height <= 0) {
    return;
  }

  for (int block_y = 0; block_y < height; block_y += 4) {
    const auto *source_row = source_data + static_cast<std::ptrdiff_t>(block_y / 4) * source_stride;
    auto *destination_row = reinterpret_cast<Pixel *>(
        reinterpret_cast<std::byte *>(destination_data) +
        static_cast<std::ptrdiff_t>(block_y) * destination_stride);
    const int block_height = std::min(height - block_y, 4);

    for (int block_x = 0; block_x < width; block_x += 4) {
      const int block_width = std::min(width - block_x, 4);
      const auto *source_block = source_row + static_cast<std::ptrdiff_t>(block_x / 4) * 8;
      auto *destination_block = destination_row + block_x;
      BlpDxtDecodeDxt1Block(source_block, destination_block, destination_stride, block_width,
                            block_height, build_palette);
    }
  }
}

template <typename Pixel, typename PaletteBuilder>
void BlpDxtDecodeDxt1Clipped(const int *dims, const void *source_data, int source_stride,
                             void *destination_data, int destination_stride,
                             PaletteBuilder build_palette) {
  const int width = dims[0];
  const int height = dims[1];

  if (width == 6 * height && height > 0) {
    const int face_width = width / 6;
    for (int face = 0; face < 6; ++face) {
      const auto *face_source = static_cast<const uint8_t *>(source_data) +
                                static_cast<std::ptrdiff_t>(face) * 8 * face_width;
      auto *face_destination = reinterpret_cast<Pixel *>(
          static_cast<std::byte *>(destination_data) +
          static_cast<std::ptrdiff_t>(face) * sizeof(Pixel) * face_width);
      BlpDxtDecodeDxt1Region(face_source, source_stride, face_destination, destination_stride,
                             face_width, height, build_palette);
    }
    return;
  }

  BlpDxtDecodeDxt1Region(static_cast<const uint8_t *>(source_data), source_stride,
                         static_cast<Pixel *>(destination_data), destination_stride, width, height,
                         build_palette);
}

template <typename Pixel, typename PaletteBuilder>
void BlpDxtDecodeDxt1Aligned(const int *dims, const void *source_data, int source_stride,
                             void *destination_data, int destination_stride,
                             PaletteBuilder build_palette) {
  BlpDxtDecodeDxt1Region(static_cast<const uint8_t *>(source_data), source_stride,
                         static_cast<Pixel *>(destination_data), destination_stride, dims[0],
                         dims[1], build_palette);
}

void BlpDxtDecodeDxt1ToArgb8888(const int *dims, const void *source_data, int source_stride,
                                void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt1Aligned<uint32_t>(dims, source_data, source_stride, destination_data,
                                      destination_stride, BlpDxtBuildDxt1Argb8888Palette);
    return;
  }

  BlpDxtDecodeDxt1Clipped<uint32_t>(dims, source_data, source_stride, destination_data,
                                    destination_stride, BlpDxtBuildDxt1Argb8888Palette);
}

void BlpDxtDecodeDxt1ToArgb1555(const int *dims, const void *source_data, int source_stride,
                                void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt1Aligned<uint16_t>(dims, source_data, source_stride, destination_data,
                                      destination_stride, BlpDxtBuildDxt1Argb1555Palette);
    return;
  }

  BlpDxtDecodeDxt1Clipped<uint16_t>(dims, source_data, source_stride, destination_data,
                                    destination_stride, BlpDxtBuildDxt1Argb1555Palette);
}

void BlpDxtDecodeDxt1ToRgb565(const int *dims, const void *source_data, int source_stride,
                              void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt1Aligned<uint16_t>(dims, source_data, source_stride, destination_data,
                                      destination_stride, BlpDxtBuildDxt1Rgb565Palette);
    return;
  }

  BlpDxtDecodeDxt1Clipped<uint16_t>(dims, source_data, source_stride, destination_data,
                                    destination_stride, BlpDxtBuildDxt1Rgb565Palette);
}

void BlpDxtDecodeDxt5Block(const uint8_t *source_block, uint32_t *destination_block,
                           int destination_stride, int output_width, int output_height) {
  const auto alpha_table =
      blp::detail::BuildDxt5AlphaTable(source_block[0], source_block[1]);
  const uint64_t alpha_selectors = blp::detail::ReadLittleEndian48(source_block + 2);
  const auto palette = BlpDxtBuildOpaqueArgb8888Palette(source_block + 8);
  const uint32_t color_selectors = static_cast<uint32_t>(source_block[12]) |
                                   (static_cast<uint32_t>(source_block[13]) << 8) |
                                   (static_cast<uint32_t>(source_block[14]) << 16) |
                                   (static_cast<uint32_t>(source_block[15]) << 24);

  for (int y = 0; y < output_height; ++y) {
    auto *destination_row = reinterpret_cast<uint32_t *>(
        reinterpret_cast<std::byte *>(destination_block) +
        static_cast<std::ptrdiff_t>(y) * destination_stride);
    for (int x = 0; x < output_width; ++x) {
      const int pixel_index = 4 * y + x;
      const uint32_t color_selector = (color_selectors >> (2 * pixel_index)) & 0x3u;
      const uint32_t alpha_selector =
          static_cast<uint32_t>((alpha_selectors >> (3 * pixel_index)) & 0x7u);
      destination_row[x] =
          (palette[color_selector] & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha_table[alpha_selector]) << 24);
    }
  }
}

void BlpDxtDecodeDxt5Region(const uint8_t *source_data, int source_stride,
                            uint32_t *destination_data, int destination_stride, int width,
                            int height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  for (int block_y = 0; block_y < height; block_y += 4) {
    const auto *source_row =
        source_data + static_cast<std::ptrdiff_t>(block_y / 4) * source_stride;
    auto *destination_row = reinterpret_cast<uint32_t *>(
        reinterpret_cast<std::byte *>(destination_data) +
        static_cast<std::ptrdiff_t>(block_y) * destination_stride);
    const int block_height = std::min(height - block_y, 4);

    for (int block_x = 0; block_x < width; block_x += 4) {
      const int block_width = std::min(width - block_x, 4);
      const auto *source_block = source_row + static_cast<std::ptrdiff_t>(block_x / 4) * 16;
      auto *destination_block = destination_row + block_x;
      BlpDxtDecodeDxt5Block(source_block, destination_block, destination_stride, block_width,
                            block_height);
    }
  }
}

void BlpDxtDecodeDxt5Clipped(const int *dims, const void *source_data, int source_stride,
                             void *destination_data, int destination_stride) {
  const int width = dims[0];
  const int height = dims[1];

  if (width == 6 * height && height > 0) {
    const int face_width = width / 6;
    const auto *face_source = static_cast<const uint8_t *>(source_data);
    auto *face_destination = static_cast<std::byte *>(destination_data);

    for (int face = 0; face < 6; ++face) {
      BlpDxtDecodeDxt5Region(face_source, source_stride,
                             reinterpret_cast<uint32_t *>(face_destination), destination_stride,
                             face_width, height);
      face_source += 16 * face_width;
      face_destination += 4 * face_width;
    }
    return;
  }

  BlpDxtDecodeDxt5Region(static_cast<const uint8_t *>(source_data), source_stride,
                         static_cast<uint32_t *>(destination_data), destination_stride, width,
                         height);
}

void BlpDxtDecodeDxt5Aligned(const int *dims, const void *source_data, int source_stride,
                             void *destination_data, int destination_stride) {
  BlpDxtDecodeDxt5Region(static_cast<const uint8_t *>(source_data), source_stride,
                         static_cast<uint32_t *>(destination_data), destination_stride, dims[0],
                         dims[1]);
}

void BlpDxtDecodeDxt5ToArgb8888(const int *dims, const void *source_data, int source_stride,
                                void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt5Aligned(dims, source_data, source_stride, destination_data,
                            destination_stride);
    return;
  }

  BlpDxtDecodeDxt5Clipped(dims, source_data, source_stride, destination_data,
                          destination_stride);
}

std::array<uint16_t, 4> BlpDxtBuildDxt3Argb4444ColorPalette(const uint8_t *block_colors) {
  const uint16_t c0 =
      static_cast<uint16_t>(block_colors[0] | (static_cast<uint16_t>(block_colors[1]) << 8));
  const uint16_t c1 =
      static_cast<uint16_t>(block_colors[2] | (static_cast<uint16_t>(block_colors[3]) << 8));

  const uint8_t r0 = static_cast<uint8_t>((c0 >> 11) & 0x1F);
  const uint8_t g0 = static_cast<uint8_t>((c0 >> 5) & 0x3F);
  const uint8_t b0 = static_cast<uint8_t>(c0 & 0x1F);
  const uint8_t r1 = static_cast<uint8_t>((c1 >> 11) & 0x1F);
  const uint8_t g1 = static_cast<uint8_t>((c1 >> 5) & 0x3F);
  const uint8_t b1 = static_cast<uint8_t>(c1 & 0x1F);

  return {
      BlpDxtPackRgb565ToArgb4444(c0),
      BlpDxtPackRgb565ToArgb4444(c1),
      BlpDxtPackArgb4444(static_cast<uint8_t>((2u * r0 + r1) / 3u),
                         static_cast<uint8_t>((2u * g0 + g1) / 3u),
                         static_cast<uint8_t>((2u * b0 + b1) / 3u)),
      BlpDxtPackArgb4444(static_cast<uint8_t>((r0 + 2u * r1) / 3u),
                         static_cast<uint8_t>((g0 + 2u * g1) / 3u),
                         static_cast<uint8_t>((b0 + 2u * b1) / 3u)),
  };
}

void BlpDxtDecodeDxt3BlockArgb4444(const uint8_t *source_block, uint16_t *destination_block,
                                   int destination_stride, int output_width, int output_height) {
  const auto palette = BlpDxtBuildDxt3Argb4444ColorPalette(source_block + 8);

  for (int y = 0; y < output_height; ++y) {
    auto *dest_row = reinterpret_cast<uint16_t *>(
        reinterpret_cast<std::byte *>(destination_block) +
        static_cast<std::ptrdiff_t>(y) * destination_stride);

    const uint8_t color_indices = source_block[12 + y];
    const uint16_t alpha_data = static_cast<uint16_t>(
        source_block[2 * y] | (static_cast<uint16_t>(source_block[2 * y + 1]) << 8));

    for (int x = 0; x < output_width; ++x) {
      const uint32_t ci = (color_indices >> (2 * x)) & 0x3u;
      const uint32_t alpha = (alpha_data >> (4 * x)) & 0xFu;
      dest_row[x] = static_cast<uint16_t>((alpha << 12) | (palette[ci] & 0xFFFu));
    }
  }
}

void BlpDxtDecodeDxt3BlockArgb8888(const uint8_t *source_block, uint32_t *destination_block,
                                   int destination_stride, int output_width, int output_height) {
  const auto palette = BlpDxtBuildOpaqueArgb8888Palette(source_block + 8);

  for (int y = 0; y < output_height; ++y) {
    auto *dest_row = reinterpret_cast<uint32_t *>(
        reinterpret_cast<std::byte *>(destination_block) +
        static_cast<std::ptrdiff_t>(y) * destination_stride);

    const uint8_t color_indices = source_block[12 + y];
    const uint16_t alpha_data = static_cast<uint16_t>(
        source_block[2 * y] | (static_cast<uint16_t>(source_block[2 * y + 1]) << 8));

    for (int x = 0; x < output_width; ++x) {
      const uint32_t ci = (color_indices >> (2 * x)) & 0x3u;
      const uint32_t alpha_nibble = (alpha_data >> (4 * x)) & 0xFu;
      const uint32_t alpha_byte = alpha_nibble << 4;
      dest_row[x] = (palette[ci] & 0x00FFFFFFu) | (alpha_byte << 24);
    }
  }
}

template <typename Pixel>
void BlpDxtDecodeDxt3Region(const uint8_t *source_data, int source_stride, Pixel *destination_data,
                            int destination_stride, int width, int height,
                            void (*decode_block)(const uint8_t *, Pixel *, int, int, int)) {
  if (width <= 0 || height <= 0) {
    return;
  }

  for (int block_y = 0; block_y < height; block_y += 4) {
    const auto *source_row = source_data + static_cast<std::ptrdiff_t>(block_y / 4) * source_stride;
    auto *destination_row = reinterpret_cast<Pixel *>(
        reinterpret_cast<std::byte *>(destination_data) +
        static_cast<std::ptrdiff_t>(block_y) * destination_stride);
    const int block_height = std::min(height - block_y, 4);

    for (int block_x = 0; block_x < width; block_x += 4) {
      const int block_width = std::min(width - block_x, 4);
      const auto *source_block = source_row + static_cast<std::ptrdiff_t>(block_x / 4) * 16;
      auto *destination_block = destination_row + block_x;
      decode_block(source_block, destination_block, destination_stride, block_width, block_height);
    }
  }
}

template <typename Pixel>
void BlpDxtDecodeDxt3Clipped(const int *dims, const void *source_data, int source_stride,
                             void *destination_data, int destination_stride,
                             void (*decode_block)(const uint8_t *, Pixel *, int, int, int)) {
  const int width = dims[0];
  const int height = dims[1];

  if (width == 6 * height && height > 0) {
    const int face_width = width / 6;
    const auto *face_source = static_cast<const uint8_t *>(source_data);
    auto *face_destination = static_cast<std::byte *>(destination_data);

    for (int face = 0; face < 6; ++face) {
      BlpDxtDecodeDxt3Region(face_source, source_stride,
                             reinterpret_cast<Pixel *>(face_destination), destination_stride,
                             face_width, height, decode_block);
      face_source += 16 * face_width;
      face_destination += sizeof(Pixel) * face_width;
    }
    return;
  }

  BlpDxtDecodeDxt3Region(static_cast<const uint8_t *>(source_data), source_stride,
                         static_cast<Pixel *>(destination_data), destination_stride, width, height,
                         decode_block);
}

template <typename Pixel>
void BlpDxtDecodeDxt3Aligned(const int *dims, const void *source_data, int source_stride,
                             void *destination_data, int destination_stride,
                             void (*decode_block)(const uint8_t *, Pixel *, int, int, int)) {
  BlpDxtDecodeDxt3Region(static_cast<const uint8_t *>(source_data), source_stride,
                         static_cast<Pixel *>(destination_data), destination_stride, dims[0],
                         dims[1], decode_block);
}

void BlpDxtDecodeDxt3ToArgb4444(const int *dims, const void *source_data, int source_stride,
                                void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt3Aligned<uint16_t>(dims, source_data, source_stride, destination_data,
                                      destination_stride, BlpDxtDecodeDxt3BlockArgb4444);
    return;
  }

  BlpDxtDecodeDxt3Clipped<uint16_t>(dims, source_data, source_stride, destination_data,
                                    destination_stride, BlpDxtDecodeDxt3BlockArgb4444);
}

void BlpDxtDecodeDxt3ToArgb8888(const int *dims, const void *source_data, int source_stride,
                                void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt3Aligned<uint32_t>(dims, source_data, source_stride, destination_data,
                                      destination_stride, BlpDxtDecodeDxt3BlockArgb8888);
    return;
  }

  BlpDxtDecodeDxt3Clipped<uint32_t>(dims, source_data, source_stride, destination_data,
                                    destination_stride, BlpDxtDecodeDxt3BlockArgb8888);
}

void BlpDxtDecodeDxt5BlockArgb4444(const uint8_t *source_block, uint16_t *destination_block,
                                   int destination_stride, int output_width, int output_height) {
  const auto palette = BlpDxtBuildDxt3Argb4444ColorPalette(source_block + 8);
  const auto alpha_table =
      blp::detail::BuildDxt5AlphaTable(source_block[0], source_block[1]);
  const uint64_t alpha_selectors = blp::detail::ReadLittleEndian48(source_block + 2);

  for (int y = 0; y < output_height; ++y) {
    auto *dest_row = reinterpret_cast<uint16_t *>(
        reinterpret_cast<std::byte *>(destination_block) +
        static_cast<std::ptrdiff_t>(y) * destination_stride);

    const uint8_t color_indices = source_block[12 + y];

    for (int x = 0; x < output_width; ++x) {
      const int pixel_index = 4 * y + x;
      const uint32_t ci = (color_indices >> (2 * x)) & 0x3u;
      const uint32_t alpha_sel =
          static_cast<uint32_t>((alpha_selectors >> (3 * pixel_index)) & 0x7u);
      const uint32_t alpha4 = alpha_table[alpha_sel] >> 4;
      dest_row[x] = static_cast<uint16_t>((alpha4 << 12) | (palette[ci] & 0xFFFu));
    }
  }
}

void BlpDxtDecodeDxt5RegionArgb4444(const uint8_t *source_data, int source_stride,
                                    uint16_t *destination_data, int destination_stride, int width,
                                    int height) {
  BlpDxtDecodeDxt3Region<uint16_t>(source_data, source_stride, destination_data, destination_stride,
                                   width, height, BlpDxtDecodeDxt5BlockArgb4444);
}

void BlpDxtDecodeDxt5ClippedArgb4444(const int *dims, const void *source_data, int source_stride,
                                     void *destination_data, int destination_stride) {
  const int width = dims[0];
  const int height = dims[1];

  if (width == 6 * height && height > 0) {
    const int face_width = width / 6;
    const auto *face_source = static_cast<const uint8_t *>(source_data);
    auto *face_destination = static_cast<std::byte *>(destination_data);

    for (int face = 0; face < 6; ++face) {
      BlpDxtDecodeDxt5RegionArgb4444(face_source, source_stride,
                                     reinterpret_cast<uint16_t *>(face_destination),
                                     destination_stride, face_width, height);
      face_source += 16 * face_width;
      face_destination += 2 * face_width;
    }
    return;
  }

  BlpDxtDecodeDxt5RegionArgb4444(static_cast<const uint8_t *>(source_data), source_stride,
                                 static_cast<uint16_t *>(destination_data), destination_stride,
                                 width, height);
}

void BlpDxtDecodeDxt5AlignedArgb4444(const int *dims, const void *source_data, int source_stride,
                                     void *destination_data, int destination_stride) {
  BlpDxtDecodeDxt5RegionArgb4444(static_cast<const uint8_t *>(source_data), source_stride,
                                 static_cast<uint16_t *>(destination_data), destination_stride,
                                 dims[0], dims[1]);
}

void BlpDxtDecodeDxt5ToArgb4444(const int *dims, const void *source_data, int source_stride,
                                void *destination_data, int destination_stride) {
  if (dims[0] >= 4 && dims[1] >= 4 && (dims[0] & 3) == 0 && (dims[1] & 3) == 0) {
    BlpDxtDecodeDxt5AlignedArgb4444(dims, source_data, source_stride, destination_data,
                                    destination_stride);
    return;
  }

  BlpDxtDecodeDxt5ClippedArgb4444(dims, source_data, source_stride, destination_data,
                                  destination_stride);
}

bool BlpDxtMapDispatchFormat(uint32_t format, BlpDxtDispatchFormat *dispatch_format) {
  switch (format) {
  case 0:
    *dispatch_format = BlpDxtDispatchFormat::kDxt1;
    return true;
  case 1:
    *dispatch_format = BlpDxtDispatchFormat::kDxt3;
    return true;
  case 2:
    *dispatch_format = BlpDxtDispatchFormat::kArgb8888;
    return true;
  case 3:
    *dispatch_format = BlpDxtDispatchFormat::kArgb1555;
    return true;
  case 4:
    *dispatch_format = BlpDxtDispatchFormat::kArgb4444;
    return true;
  case 5:
    *dispatch_format = BlpDxtDispatchFormat::kRgb565;
    return true;
  case 6:
    *dispatch_format = BlpDxtDispatchFormat::kA8;
    return true;
  case 7:
    *dispatch_format = BlpDxtDispatchFormat::kDxt5;
    return true;
  default:
    return false;
  }
}

uint32_t BlpDxtDispatchRowSize(BlpDxtDispatchFormat dispatch_format, uint32_t width,
                               uint32_t height) {
  switch (dispatch_format) {
  case BlpDxtDispatchFormat::kDxt1: {
    uint32_t row_width = width;
    if (row_width == 6 * height) {
      uint32_t face_height = height;
      if (face_height < 4) {
        face_height = 4;
      }
      row_width = 6 * face_height;
    } else if (row_width < 4) {
      row_width = 4;
    }
    return 2 * row_width;
  }
  case BlpDxtDispatchFormat::kDxt3:
  case BlpDxtDispatchFormat::kDxt5: {
    uint32_t row_width = width;
    if (row_width == 6 * height) {
      uint32_t face_height = height;
      if (face_height < 4) {
        face_height = 4;
      }
      row_width = 6 * face_height;
    } else if (row_width < 4) {
      row_width = 4;
    }
    return 4 * row_width;
  }
  case BlpDxtDispatchFormat::kArgb8888:
    return 4 * width;
  case BlpDxtDispatchFormat::kArgb4444:
  case BlpDxtDispatchFormat::kArgb1555:
  case BlpDxtDispatchFormat::kRgb565:
    return 2 * width;
  case BlpDxtDispatchFormat::kA8:
    return width;
  default:
    return 0;
  }
}

}

void *BlpDxt_CopyArgb8888Rows(const int *dims, const void *source_data, int source_stride,
                               void *destination_data, int destination_stride) {
  const std::size_t row_bytes = static_cast<std::size_t>(4) * static_cast<std::size_t>(dims[0]);

  if (row_bytes == static_cast<std::size_t>(source_stride) &&
      row_bytes == static_cast<std::size_t>(destination_stride)) {
    return std::memcpy(destination_data, source_data,
                       row_bytes * static_cast<std::size_t>(dims[1]));
  }

  const auto *source = static_cast<const std::byte *>(source_data);
  auto *destination = static_cast<std::byte *>(destination_data);
  void *last_copy = nullptr;

  for (int row = 0; row < dims[1]; ++row) {
    last_copy = std::memcpy(destination, source, row_bytes);
    source += source_stride;
    destination += destination_stride;
  }

  return last_copy;
}

void *BlpDxt_CopyPixel16Rows(const int *dims, const void *source_data, int source_stride,
                              void *destination_data, int destination_stride) {
  const std::size_t row_bytes = static_cast<std::size_t>(2) * static_cast<std::size_t>(dims[0]);

  if (row_bytes == static_cast<std::size_t>(source_stride) &&
      row_bytes == static_cast<std::size_t>(destination_stride)) {
    return std::memcpy(destination_data, source_data,
                       row_bytes * static_cast<std::size_t>(dims[1]));
  }

  const auto *source = static_cast<const std::byte *>(source_data);
  auto *destination = static_cast<std::byte *>(destination_data);
  void *last_copy = nullptr;

  for (int row = 0; row < dims[1]; ++row) {
    last_copy = std::memcpy(destination, source, row_bytes);
    source += source_stride;
    destination += destination_stride;
  }

  return last_copy;
}

static uint32_t BlpMip_CalcTotalDataSize(uint32_t numLevels, uint32_t width, uint32_t height,
                                         uint32_t format);

int BlpFormat_BitsPerPixel(uint32_t format) {
  switch (format) {
  case 0:
    return 4;
  case 1:
  case 6:
  case 7:
    return 8;
  case 2:
    return 32;
  case 3:
  case 4:
  case 5:
    return 16;
  default:
    return 0;
  }
}

uint32_t BlpMip_CalcLevelSize(uint8_t level, uint32_t width, uint32_t height, uint32_t format) {
  uint32_t w = width >> level;
  if (!w)
    w = 1;
  uint32_t h = height >> level;
  if (!h)
    h = 1;

  if (format < 2 || format == 7) {
    if (w == 6 * h) {

      if (h < 4)
        h = 4;
      w = 6 * h;
    } else {
      if (w < 4)
        w = 4;
      if (h < 4)
        h = 4;
    }
  }

  if (format == 9) {
    uint32_t pixels = w * h;
    uint32_t quarter = pixels >> 2;
    if (!quarter)
      quarter = 1;
    return 2 * pixels + quarter;
  }

  int bpp = BlpFormat_BitsPerPixel(format);
  return static_cast<uint32_t>(static_cast<uint64_t>(w) * h * bpp) >> 3;
}

int BlpMip_CountLevels(uint32_t width, uint32_t height) {
  uint32_t w = width;
  uint32_t h = height;

  if (w == 6 * h)
    w = w / 6;

  int count = 1;
  while (w > 1 || h > 1) {
    uint32_t nw = w >> 1;
    ++count;
    w = nw ? nw : 1;
    h = (h >> 1) ? (h >> 1) : 1;
  }
  return count;
}

static uint32_t BlpMip_CalcTotalDataSize(uint32_t numLevels, uint32_t width, uint32_t height,
                                         uint32_t format) {
  uint32_t total = 0;
  for (uint32_t i = 0; i < numLevels; ++i)
    total += BlpMip_CalcLevelSize(static_cast<uint8_t>(i), width, height, format);
  return total;
}

std::uintptr_t *BlpMip_AllocArray(uint32_t format, uint32_t width, uint32_t height,
                                  int , int ) {
  uint32_t numLevels = static_cast<uint32_t>(BlpMip_CountLevels(width, height));
  uint32_t dataSize = BlpMip_CalcTotalDataSize(numLevels, width, height, format);

  const std::size_t pointer_table_size = sizeof(std::uintptr_t) * numLevels;
  const std::size_t allocSize = dataSize + pointer_table_size + 16;
  auto *result = static_cast<std::uintptr_t *>(std::malloc(allocSize));
  if (!result)
    return nullptr;

  const auto base = reinterpret_cast<std::uintptr_t>(result);
  const auto dataStart = (base + pointer_table_size + 15u) & ~std::uintptr_t(0xFu);
  std::uintptr_t offset = 0;

  for (uint32_t i = 0; i < numLevels; ++i) {
    result[i] = dataStart + offset;
    offset += BlpMip_CalcLevelSize(static_cast<uint8_t>(i), width, height, format);
  }

  return result;
}

int BlpMip_CalcTotalMipSize(uint32_t format, uint32_t width, uint32_t height) {
  uint32_t numLevels = static_cast<uint32_t>(BlpMip_CountLevels(width, height));
  return static_cast<int>(BlpMip_CalcTotalDataSize(numLevels, width, height, format) +
                          sizeof(std::uintptr_t) * numLevels);
}

void BlpMip_InitExistingArray(uint32_t format, uint32_t width, uint32_t height,
                              std::uintptr_t *buf) {
  uint32_t numLevels = static_cast<uint32_t>(BlpMip_CountLevels(width, height));
  std::uintptr_t dataOffset = 0;

  for (uint32_t i = 0; i < numLevels; ++i) {

    buf[i] = reinterpret_cast<std::uintptr_t>(buf)
           + sizeof(std::uintptr_t) * numLevels + dataOffset;
    dataOffset += BlpMip_CalcLevelSize(static_cast<uint8_t>(i), width, height, format);
  }
}

void BlpMip_BoxFilterDownsample(uint32_t *dst, uint32_t outW, uint32_t outH, const uint8_t *src,
                                uint32_t srcStride, uint32_t srcH) {
  if (!outH)
    return;

  uint32_t scaleX = srcStride / outW;
  uint32_t scaleY = srcH / outH;

  for (uint32_t oy = 0; oy < outH; ++oy) {
    for (uint32_t ox = 0; ox < outW; ++ox) {
      int64_t sumR = 0, sumG = 0, sumB = 0;
      int64_t sumA = 0;
      int64_t wSumR = 0, wSumG = 0, wSumB = 0;
      int64_t count = 0;

      const uint8_t *rowBase = src + oy * scaleY * srcStride * 4 + ox * scaleX * 4;

      for (uint32_t sy = 0; sy < scaleY; ++sy) {
        const uint8_t *pixel = rowBase + sy * srcStride * 4;
        for (uint32_t sx = 0; sx < scaleX; ++sx) {
          uint8_t r = pixel[0];
          uint8_t g = pixel[1];
          uint8_t b = pixel[2];
          uint8_t a = pixel[3];

          sumA += a;

          wSumR += static_cast<int64_t>(a) * r;
          wSumG += static_cast<int64_t>(a) * g;
          wSumB += static_cast<int64_t>(a) * b;

          sumR += r;
          sumG += g;
          sumB += b;
          ++count;

          pixel += 4;
        }
      }

      uint32_t outPixel;
      if (sumA > 0) {
        uint8_t avgR = static_cast<uint8_t>(wSumR / sumA);
        uint8_t avgG = static_cast<uint8_t>(wSumG / sumA);
        uint8_t avgB = static_cast<uint8_t>(wSumB / sumA);
        uint8_t avgA = static_cast<uint8_t>(sumA / static_cast<int64_t>(scaleX * scaleY));
        outPixel = avgR | (avgG << 8) | (avgB << 16) | (avgA << 24);
      } else if (count > 0) {
        uint8_t avgR = static_cast<uint8_t>(sumR / count);
        uint8_t avgG = static_cast<uint8_t>(sumG / count);
        uint8_t avgB = static_cast<uint8_t>(sumB / count);
        outPixel = avgR | (avgG << 8) | (avgB << 16) | (0u << 24);
      } else {
        outPixel = 0;
      }

      *dst++ = outPixel;
    }
  }
}

static const uint32_t s_blpRowBlockSize[] = {
    0,
    4,
    4,
    2,
    2,
    2,
    8,
    16,
    16,
    2,
    4,
    4,
};

static const uint32_t s_blpRowShift[] = {
    0,
    0,
    0,
    0,
    0,
    0,
    2,
    2,
    2,
    0,
    0,
    0,
};

uint32_t BlpMip_CalcRowSize(uint32_t dispatch_format, uint32_t width, uint32_t height) {
  uint32_t w = width;

  if (dispatch_format - 6 <= 2) {
    if (width == 6 * height) {

      uint32_t h = height;
      if (h <= 4)
        h = 4;
      return s_blpRowBlockSize[dispatch_format]
             * ((6 * h) >> s_blpRowShift[dispatch_format]);
    }
    if (w <= 4)
      w = 4;
  }

  if (dispatch_format >= sizeof(s_blpRowBlockSize) / sizeof(s_blpRowBlockSize[0]))
    return 0;

  return s_blpRowBlockSize[dispatch_format] * (w >> s_blpRowShift[dispatch_format]);
}

static bool s_conversionTableInitialized = false;

void BlpDxt_InitConversionTable() {

  s_conversionTableInitialized = true;
}

int BlpDxt_ConvertFormat(const int *dims, int source_variant, const void *source_data,
                         int source_stride, const BlpDxtDispatchFormat source_dispatch_format,
                         void *destination_data, int destination_stride,
                         const BlpDxtDispatchFormat destination_dispatch_format) {
  if (!s_conversionTableInitialized) {
    BlpDxt_InitConversionTable();
  }

  if (source_variant != 0) {
    return 0;
  }

  if (source_dispatch_format == destination_dispatch_format) {

    if (source_dispatch_format == BlpDxtDispatchFormat::kArgb8888) {
      BlpDxt_CopyArgb8888Rows(dims, source_data, source_stride, destination_data,
                              destination_stride);
      return 1;
    }

    if (source_dispatch_format == BlpDxtDispatchFormat::kArgb4444 ||
        source_dispatch_format == BlpDxtDispatchFormat::kArgb1555 ||
        source_dispatch_format == BlpDxtDispatchFormat::kRgb565) {
      BlpDxt_CopyPixel16Rows(dims, source_data, source_stride, destination_data,
                             destination_stride);
      return 1;
    }

    const int bytes_per_width_unit = BlpDxt_CopyWidthUnit(source_dispatch_format);
    if (!bytes_per_width_unit) {
      return 0;
    }

    BlpDxt_CopyBlockRows(dims, source_data, source_stride, destination_data, destination_stride,
                         bytes_per_width_unit);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt1 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb8888) {
    BlpDxtDecodeDxt1ToArgb8888(dims, source_data, source_stride, destination_data,
                               destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt1 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb1555) {
    BlpDxtDecodeDxt1ToArgb1555(dims, source_data, source_stride, destination_data,
                               destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt1 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kRgb565) {
    BlpDxtDecodeDxt1ToRgb565(dims, source_data, source_stride, destination_data,
                             destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt5 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb8888) {
    BlpDxtDecodeDxt5ToArgb8888(dims, source_data, source_stride, destination_data,
                               destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kArgb8888 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb4444) {
    BlpDxt_BlitArgb8888ToArgb4444(dims, source_data, source_stride,
                                   destination_data, destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kArgb8888 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb1555) {
    BlpDxt_BlitArgb8888ToArgb1555(dims, source_data, source_stride,
                                   destination_data, destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kArgb8888 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kRgb565) {
    BlpDxt_BlitArgb8888ToRgb565(dims, source_data, source_stride,
                                 destination_data, destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt3 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb4444) {
    BlpDxtDecodeDxt3ToArgb4444(dims, source_data, source_stride, destination_data,
                               destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt3 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb8888) {
    BlpDxtDecodeDxt3ToArgb8888(dims, source_data, source_stride, destination_data,
                               destination_stride);
    return 1;
  }

  if (source_dispatch_format == BlpDxtDispatchFormat::kDxt5 &&
      destination_dispatch_format == BlpDxtDispatchFormat::kArgb4444) {
    BlpDxtDecodeDxt5ToArgb4444(dims, source_data, source_stride, destination_data,
                               destination_stride);
    return 1;
  }

  return 0;
}

void *BlpImage_InitDefaults(void *blpImage) {
  auto *img = static_cast<uint32_t *>(blpImage);
  auto *bytes = static_cast<uint8_t *>(blpImage);

  img[0] = 0;
  img[297] = kBlpImageDefaultQuality;
  std::memset(img + 1, 0, kBlpImageHeaderBytes);
  bytes[15] &= static_cast<uint8_t>(~0x10u);
  img[1] = kBlpImageMagic;
  img[2] = kBlpImageDefaultVersion;
  bytes[14] = kBlpImageDefaultAlphaType;
  img[294] = 0;
  img[299] = 0;
  return blpImage;
}

void BlpImage_Free(void *blpImage) {
  if (!blpImage)
    return;
  auto *img = static_cast<uint32_t *>(blpImage);

  void *data = reinterpret_cast<void *>(img[0]);
  img[294] = 0;
  if (data)
    std::free(data);
  img[0] = 0;
}

int BlpImage_ParseHeader(void *blpImage, const void *fileData) {
  auto *img = static_cast<uint32_t *>(blpImage);

  void *oldData = reinterpret_cast<void *>(img[0]);
  img[294] = 0;
  if (oldData)
    std::free(oldData);
  img[0] = 0;
  img[295] = 0;

  img[294] = reinterpret_cast<uintptr_t>(fileData);

  std::memcpy(&img[1], fileData, 0x494);

  if (img[1] != 0x32504C42 || img[2] != 1)
    return 0;

  const auto *headerBytes = reinterpret_cast<const uint8_t *>(&img[1]);
  if ((headerBytes[11] & 0xF) != 0) {

    img[296] = static_cast<uint32_t>(BlpMip_CountLevels(img[4], img[5]));
  } else {
    img[296] = 1;
  }
  return 1;
}

uint32_t BlpImage_GetMipWidth(const void *blpImage, uint8_t level) {
  const auto *img = static_cast<const uint32_t *>(blpImage);
  uint32_t w = img[4] >> level;
  return w >= 1 ? w : 1;
}

uint32_t BlpImage_GetMipHeight(const void *blpImage, uint8_t level) {
  const auto *img = static_cast<const uint32_t *>(blpImage);
  uint32_t h = img[5] >> level;
  return h >= 1 ? h : 1;
}

void BlpPalette_DecompressRGBA8(void *blpImage, void *dst, const uint8_t *indices,
                                int pixelCount) {
  auto *img = static_cast<uint32_t *>(blpImage);
  auto *out = reinterpret_cast<uint32_t *>(dst);

  for (int i = 0; i < pixelCount; ++i) {

    out[i] = img[38 + indices[i]];

    reinterpret_cast<uint8_t *>(&out[i])[3] = indices[pixelCount + i];
  }
}

static const uint8_t s_alpha4to8[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

static const uint8_t s_alpha1to8[2] = {0x00, 0xFF};

constexpr std::size_t kBlpPaletteTableOffsetDwords = 38u;
constexpr std::size_t kBlpDitherChannelsPerPixel = 3u;
constexpr std::int32_t kA1Rgb5FixedUnit = 0x80000;
constexpr std::int32_t kA1Rgb5FixedBias = 0x40000;
constexpr std::uint32_t kA1Rgb5FixedMask = 0xFFF80000u;
constexpr std::int32_t kArgb4FixedUnit = 0x100000;
constexpr std::int32_t kArgb4FixedBias = 0x80000;
constexpr std::uint32_t kArgb4FixedMask = 0xFFF00000u;

constexpr std::int32_t kRgb565GreenFixedBias = 0x20000;

constexpr std::uint32_t kRgb565GreenFixedMask = 0xFFFC0000u;

[[nodiscard]] std::int32_t ArithmeticShiftRight(std::int32_t value, unsigned shift) {
  if (value >= 0) {
    return value >> shift;
  }

  const auto magnitude = static_cast<std::uint64_t>(-static_cast<std::int64_t>(value));
  const auto rounded = (magnitude + ((std::uint64_t{1} << shift) - 1u)) >> shift;
  return -static_cast<std::int32_t>(rounded);
}

[[nodiscard]] std::int32_t RoundArgb4Fixed(std::int32_t value) {
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(value + kArgb4FixedBias)) &
                                   kArgb4FixedMask);
}

[[nodiscard]] std::int32_t RoundA1Rgb5Fixed(std::int32_t value) {
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(value + kA1Rgb5FixedBias)) &
                                   kA1Rgb5FixedMask);
}

[[nodiscard]] std::uint8_t QuantizeArgb4Channel(std::int32_t rounded_fixed) {
  const auto nibble = rounded_fixed / kArgb4FixedUnit;
  return static_cast<std::uint8_t>(std::clamp(nibble, 0, 15));
}

[[nodiscard]] std::uint8_t QuantizeA1Rgb5Channel(std::int32_t rounded_fixed) {
  const auto channel = rounded_fixed / kA1Rgb5FixedUnit;
  return static_cast<std::uint8_t>(std::clamp(channel, 0, 31));
}

[[nodiscard]] std::uint16_t PackArgb4444Color(std::uint8_t red, std::uint8_t green,
                                              std::uint8_t blue) {
  return static_cast<std::uint16_t>(blue | (green << 4u) | (red << 8u));
}

[[nodiscard]] std::uint16_t PackA1Rgb5Color(std::uint8_t red, std::uint8_t green,
                                            std::uint8_t blue) {
  return static_cast<std::uint16_t>(blue | (green << 5u) | (red << 10u));
}

template <typename RoundChannel, typename PackPixel>
void DitherPaletteTo16Bit(const std::uint32_t *img, std::uint16_t *dst, const std::uint8_t *indices,
                          std::uint32_t width, std::uint32_t height, RoundChannel &&round_channel,
                          PackPixel &&pack_pixel) {
  std::vector<std::int32_t> error_rows[2]{
      std::vector<std::int32_t>(kBlpDitherChannelsPerPixel * (static_cast<std::size_t>(width) + 2u),
                                0),
      std::vector<std::int32_t>(kBlpDitherChannelsPerPixel * (static_cast<std::size_t>(width) + 2u),
                                0),
  };

  std::uint32_t pixel_index = 0;
  for (std::uint32_t row = 0; row < height; ++row) {
    auto *const current_row = error_rows[row & 1u].data() + kBlpDitherChannelsPerPixel;
    auto *const next_row = error_rows[(row + 1u) & 1u].data() + kBlpDitherChannelsPerPixel;
    std::fill_n(next_row - kBlpDitherChannelsPerPixel, 6, 0);

    for (std::uint32_t column = 0; column < width; ++column, ++pixel_index) {
      const auto palette_entry = img[kBlpPaletteTableOffsetDwords + indices[pixel_index]];
      auto *const current_slot =
          current_row + static_cast<std::ptrdiff_t>(column * kBlpDitherChannelsPerPixel);
      auto *const next_slot =
          next_row + static_cast<std::ptrdiff_t>(column * kBlpDitherChannelsPerPixel);

      const auto red_fixed = ArithmeticShiftRight(current_slot[0], 4) +
                             (static_cast<std::int32_t>((palette_entry >> 16) & 0xFFu) << 16u);
      const auto green_fixed = ArithmeticShiftRight(current_slot[1], 4) +
                               (static_cast<std::int32_t>((palette_entry >> 8) & 0xFFu) << 16u);
      const auto blue_fixed = ArithmeticShiftRight(current_slot[2], 4) +
                              (static_cast<std::int32_t>(palette_entry & 0xFFu) << 16u);

      const auto rounded_red = round_channel(red_fixed);
      const auto rounded_green = round_channel(green_fixed);
      const auto rounded_blue = round_channel(blue_fixed);

      const auto red_error = red_fixed - rounded_red;
      const auto green_error = green_fixed - rounded_green;
      const auto blue_error = blue_fixed - rounded_blue;

      dst[pixel_index] = pack_pixel(rounded_red, rounded_green, rounded_blue);

      current_slot[3] += 7 * red_error;
      current_slot[4] += 7 * green_error;
      current_slot[5] += 7 * blue_error;

      next_slot[-3] += 5 * red_error;
      next_slot[-2] += 5 * green_error;
      next_slot[-1] += 5 * blue_error;
      next_slot[0] += 3 * red_error;
      next_slot[1] += 3 * green_error;
      next_slot[2] += 3 * blue_error;
      next_slot[3] = red_error;
      next_slot[4] = green_error;
      next_slot[5] = blue_error;
    }
  }
}

void ApplyArgb4Alpha(std::uint8_t alpha_depth, const std::uint8_t *alpha_stream, std::uint16_t *dst,
                     std::uint32_t pixel_count) {
  if (alpha_depth == 1) {
    std::uint32_t alpha_mask = 1u;
    for (std::uint32_t index = 0; index < pixel_count; ++index) {
      if ((*alpha_stream & alpha_mask) != 0) {
        dst[index] |= 0xF000u;
      }

      alpha_mask <<= 1u;
      if (alpha_mask >= 0x100u) {
        alpha_mask = 1u;
        ++alpha_stream;
      }
    }
    return;
  }

  if (alpha_depth == 4) {
    for (std::uint32_t index = 0; index < pixel_count; ++index) {
      const auto alpha_nibble = static_cast<std::uint16_t>(
          (index & 1u) == 0 ? (*alpha_stream & 0x0Fu) : ((*alpha_stream++ >> 4u) & 0x0Fu));
      dst[index] |= static_cast<std::uint16_t>(alpha_nibble << 12u);
    }
    return;
  }

  if (alpha_depth == 8) {
    for (std::uint32_t index = 0; index < pixel_count; ++index) {
      dst[index] |= static_cast<std::uint16_t>(static_cast<std::uint16_t>(*alpha_stream & 0xF0u)
                                               << 8u);
      ++alpha_stream;
    }
  }
}

void ApplyA1Rgb5Alpha(std::uint8_t alpha_depth, const std::uint8_t *alpha_stream, std::uint16_t *dst,
                      std::uint32_t pixel_count) {
  if (alpha_depth == 1) {
    std::uint32_t alpha_mask = 1u;
    for (std::uint32_t index = 0; index < pixel_count; ++index) {
      if ((*alpha_stream & alpha_mask) != 0u) {
        dst[index] |= 0x8000u;
      }

      alpha_mask <<= 1u;
      if (alpha_mask >= 0x100u) {
        alpha_mask = 1u;
        ++alpha_stream;
      }
    }
    return;
  }

  if (alpha_depth == 4) {
    for (std::uint32_t index = 0; index < pixel_count; ++index) {
      const auto alpha_bit = static_cast<std::uint8_t>(
          (index & 1u) == 0 ? (*alpha_stream & 0x08u) : ((*alpha_stream++ >> 4u) & 0x08u));
      if (alpha_bit != 0u) {
        dst[index] |= 0x8000u;
      }
    }
    return;
  }

  if (alpha_depth == 8) {
    for (std::uint32_t index = 0; index < pixel_count; ++index) {
      if ((*alpha_stream & 0x80u) != 0u) {
        dst[index] |= 0x8000u;
      }
      ++alpha_stream;
    }
  }
}

void BlpPalette_DecompressToRGBA(void *blpImage, uint8_t *dst, const uint8_t *indices,
                                 uint32_t pixelCount) {
  auto *img = static_cast<uint32_t *>(blpImage);
  auto *imgBytes = reinterpret_cast<uint8_t *>(blpImage);
  uint8_t alphaDepth = imgBytes[13];

  auto *out32 = reinterpret_cast<uint32_t *>(dst);
  for (uint32_t i = 0; i < pixelCount; ++i) {
    out32[i] = img[38 + indices[i]];
    dst[i * 4 + 3] = 0xFF;
  }

  const uint8_t *alphaStream = indices + pixelCount;

  if (alphaDepth == 8) {

    for (uint32_t i = 0; i < pixelCount; ++i)
      dst[i * 4 + 3] = alphaStream[i];
  } else if (alphaDepth == 4) {

    uint32_t halfCount = pixelCount >> 1;
    uint32_t outIdx = 0;
    for (uint32_t i = 0; i < halfCount; ++i) {
      dst[outIdx * 4 + 3] = s_alpha4to8[alphaStream[i] & 0xF];
      ++outIdx;
      dst[outIdx * 4 + 3] = s_alpha4to8[alphaStream[i] >> 4];
      ++outIdx;
    }
    if (pixelCount & 1) {
      dst[outIdx * 4 + 3] = s_alpha4to8[alphaStream[halfCount] & 0xF];
    }
  } else if (alphaDepth == 1) {

    uint32_t fullBytes = pixelCount >> 3;
    uint32_t outIdx = 0;
    for (uint32_t byteIdx = 0; byteIdx < fullBytes; ++byteIdx) {
      uint8_t alphaByte = alphaStream[byteIdx];
      for (int bit = 0; bit < 8; ++bit) {
        dst[outIdx * 4 + 3] = s_alpha1to8[alphaByte & 1];
        alphaByte >>= 1;
        ++outIdx;
      }
    }

    uint32_t remainder = pixelCount & 7;
    if (remainder) {
      uint8_t alphaByte = alphaStream[fullBytes];
      for (uint32_t bit = 0; bit < remainder; ++bit) {
        dst[outIdx * 4 + 3] = s_alpha1to8[alphaByte & 1];
        alphaByte >>= 1;
        ++outIdx;
      }
    }
  }

}

void BlpPalette_DitherToARGB4(void *blpImage, uint16_t *dst, const uint8_t *indices, uint32_t width,
                              uint32_t height) {
  const auto *img = static_cast<const uint32_t *>(blpImage);
  const auto *imgBytes = reinterpret_cast<const uint8_t *>(blpImage);
  const auto alphaDepth = imgBytes[13];
  const auto pixelCount = width * height;
  DitherPaletteTo16Bit(
      img, dst, indices, width, height, [](const std::int32_t value) { return RoundArgb4Fixed(value); },
      [](const std::int32_t rounded_red, const std::int32_t rounded_green,
         const std::int32_t rounded_blue) {
        return PackArgb4444Color(QuantizeArgb4Channel(rounded_red),
                                 QuantizeArgb4Channel(rounded_green),
                                 QuantizeArgb4Channel(rounded_blue));
      });

  ApplyArgb4Alpha(alphaDepth, indices + pixelCount, dst, pixelCount);
}

void BlpPalette_DitherToA1RGB5(void *blpImage, uint16_t *dst, const uint8_t *indices,
                               uint32_t width, uint32_t height) {
  const auto *img = static_cast<const uint32_t *>(blpImage);
  const auto *imgBytes = reinterpret_cast<const uint8_t *>(blpImage);
  const auto alphaDepth = imgBytes[13];
  const auto pixelCount = width * height;

  DitherPaletteTo16Bit(
      img, dst, indices, width, height, [](const std::int32_t value) { return RoundA1Rgb5Fixed(value); },
      [](const std::int32_t rounded_red, const std::int32_t rounded_green,
         const std::int32_t rounded_blue) {
        return PackA1Rgb5Color(QuantizeA1Rgb5Channel(rounded_red),
                               QuantizeA1Rgb5Channel(rounded_green),
                               QuantizeA1Rgb5Channel(rounded_blue));
      });

  ApplyA1Rgb5Alpha(alphaDepth, indices + pixelCount, dst, pixelCount);
}

void BlpPalette_DitherToRGB565(void *blpImage, void *dst, const void *indices, uint32_t width,
                               uint32_t height) {

  const auto *img = reinterpret_cast<const std::uint32_t *>(blpImage);
  auto *out = reinterpret_cast<std::uint16_t *>(dst);
  const auto *idx = reinterpret_cast<const std::uint8_t *>(indices);

  std::vector<std::int32_t> error_rows[2]{
      std::vector<std::int32_t>(kBlpDitherChannelsPerPixel * (static_cast<std::size_t>(width) + 2u),
                                0),
      std::vector<std::int32_t>(kBlpDitherChannelsPerPixel * (static_cast<std::size_t>(width) + 2u),
                                0),
  };

  std::uint32_t pixel_index = 0;
  for (std::uint32_t row = 0; row < height; ++row) {
    auto *const current_row = error_rows[row & 1u].data() + kBlpDitherChannelsPerPixel;
    auto *const next_row = error_rows[(row + 1u) & 1u].data() + kBlpDitherChannelsPerPixel;
    std::fill_n(next_row - kBlpDitherChannelsPerPixel, 6, 0);

    for (std::uint32_t column = 0; column < width; ++column, ++pixel_index) {
      const auto palette_entry = img[kBlpPaletteTableOffsetDwords + idx[pixel_index]];
      auto *const cur = current_row + static_cast<std::ptrdiff_t>(column * kBlpDitherChannelsPerPixel);
      auto *const nxt = next_row + static_cast<std::ptrdiff_t>(column * kBlpDitherChannelsPerPixel);

      const auto red_fixed = ArithmeticShiftRight(cur[0], 4) +
                             (static_cast<std::int32_t>((palette_entry >> 16) & 0xFFu) << 16);
      const auto green_fixed = ArithmeticShiftRight(cur[1], 4) +
                               (static_cast<std::int32_t>((palette_entry >> 8) & 0xFFu) << 16);
      const auto blue_fixed = ArithmeticShiftRight(cur[2], 4) +
                              (static_cast<std::int32_t>(palette_entry & 0xFFu) << 16);

      const auto rounded_red = static_cast<std::int32_t>(
          static_cast<std::uint32_t>(red_fixed + kA1Rgb5FixedBias) & kA1Rgb5FixedMask);
      const auto rounded_green = static_cast<std::int32_t>(
          static_cast<std::uint32_t>(green_fixed + kRgb565GreenFixedBias) & kRgb565GreenFixedMask);
      const auto rounded_blue = static_cast<std::int32_t>(
          static_cast<std::uint32_t>(blue_fixed + kA1Rgb5FixedBias) & kA1Rgb5FixedMask);

      const auto r5 = static_cast<std::uint16_t>(std::clamp(rounded_red >> 19, 0, 31));
      const auto g6 = static_cast<std::uint16_t>(std::clamp(rounded_green >> 18, 0, 63));
      const auto b5 = static_cast<std::uint16_t>(std::clamp(rounded_blue >> 19, 0, 31));

      out[pixel_index] = static_cast<std::uint16_t>(b5 | (g6 << 5u) | (r5 << 11u));

      const auto red_error = red_fixed - rounded_red;
      const auto green_error = green_fixed - rounded_green;
      const auto blue_error = blue_fixed - rounded_blue;

      cur[3] += 7 * red_error;
      cur[4] += 7 * green_error;
      cur[5] += 7 * blue_error;
      nxt[-3] += 5 * red_error;
      nxt[-2] += 5 * green_error;
      nxt[-1] += 5 * blue_error;
      nxt[0] += 3 * red_error;
      nxt[1] += 3 * green_error;
      nxt[2] += 3 * blue_error;
      nxt[3] = red_error;
      nxt[4] = green_error;
      nxt[5] = blue_error;
    }
  }
}

void BlpPalette_DitherToRGB565Alpha(void *blpImage, void *dst, const void *indices,
                                    uint32_t width,
                                    uint32_t height) {

  auto *imgBytes = reinterpret_cast<uint8_t *>(blpImage);
  uint8_t alphaDepth = imgBytes[13];
  auto *out = reinterpret_cast<uint16_t *>(dst);
  auto *idx = reinterpret_cast<const uint8_t *>(indices);
  const uint8_t *alphaStream = idx + width * height;

  BlpPalette_DitherToRGB565(blpImage, dst, indices, width, height);

  uint32_t pixelCount = width * height;
  auto *alphaDst = reinterpret_cast<uint8_t *>(out) + pixelCount * 2;

  if (alphaDepth == 8) {
    for (uint32_t i = 0; i < pixelCount; ++i) {
      uint32_t pos = i & 3;
      uint8_t a2 = alphaStream[i] >> 6;
      uint32_t byteIdx = i >> 2;
      static const int shifts[] = {0, 2, 4, 6};
      static const uint8_t masks[] = {0xFC, 0xF3, 0xCF, 0x3F};
      alphaDst[byteIdx] =
          (alphaDst[byteIdx] & masks[pos]) | static_cast<uint8_t>(a2 << shifts[pos]);
    }
  } else if (alphaDepth == 4) {
    for (uint32_t i = 0; i < pixelCount; ++i) {
      uint32_t pos = i & 3;
      uint8_t rawA;
      if (i & 1)
        rawA = alphaStream[i >> 1] >> 4;
      else
        rawA = alphaStream[i >> 1] & 0xF;
      uint8_t a2 = rawA >> 2;
      uint32_t byteIdx = i >> 2;
      static const int shifts[] = {0, 2, 4, 6};
      static const uint8_t masks[] = {0xFC, 0xF3, 0xCF, 0x3F};
      alphaDst[byteIdx] =
          (alphaDst[byteIdx] & masks[pos]) | static_cast<uint8_t>(a2 << shifts[pos]);
    }
  } else if (alphaDepth == 1) {
    for (uint32_t i = 0; i < pixelCount; ++i) {
      uint32_t pos = i & 3;
      uint8_t bit = (alphaStream[i >> 3] >> (i & 7)) & 1;
      uint8_t a2 = bit ? 3 : 0;
      uint32_t byteIdx = i >> 2;
      static const int shifts[] = {0, 2, 4, 6};
      static const uint8_t masks[] = {0xFC, 0xF3, 0xCF, 0x3F};
      alphaDst[byteIdx] =
          (alphaDst[byteIdx] & masks[pos]) | static_cast<uint8_t>(a2 << shifts[pos]);
    }
  }
}

namespace {

struct BlpImageLayout {
  uint32_t pad0[3];
  uint8_t compressionType;
  uint8_t alphaDepth;
  uint8_t alphaType;
  uint8_t hasMipsFlag;
  uint32_t width;
  uint32_t height;
  uint32_t mipOffsets[16];
  uint32_t mipSizes[16];
  uint8_t palette[1024];
  std::uintptr_t inMemoryImage;
  uint32_t pad_1180;
  uint32_t mipCount;
  uint32_t pad_1188[4];
  std::uintptr_t tempMipBuffer;
};

inline BlpImageLayout *AsBlp(void *p) {
  return reinterpret_cast<BlpImageLayout *>(p);
}
inline const BlpImageLayout *AsBlp(const void *p) {
  return reinterpret_cast<const BlpImageLayout *>(p);
}

inline uint32_t MipDim(uint32_t base, uint8_t level) {
  uint32_t v = base >> level;
  return v > 1 ? v : 1;
}

}

uint32_t BlpImage_GetMipPixelCount(const void *blpImage, uint8_t mipLevel) {
  auto *img = AsBlp(blpImage);
  return MipDim(img->width, mipLevel) * MipDim(img->height, mipLevel);
}

bool BlpImage_FreeMipBuffer(void *blpImage, uint32_t mipLevel) {
  auto *img = AsBlp(blpImage);
  if (img->tempMipBuffer) {
    free(reinterpret_cast<void *>(img->tempMipBuffer));
    img->tempMipBuffer = 0;
  }
  if (!mipLevel)
    return true;
  return (img->hasMipsFlag & 0xF) != 0 && mipLevel < img->mipCount;
}

bool BlpImage_CalcMipSize(const void *blpImage, uint32_t targetFormat, uint8_t mipLevel,
                          uint32_t *outSize, uint32_t *outStride) {
  auto *img = AsBlp(blpImage);
  uint32_t w = MipDim(img->width, mipLevel);
  uint32_t h = MipDim(img->height, mipLevel);
  uint32_t pixelCount = w * h;
  uint32_t blockCount = pixelCount >> 2;
  if (!blockCount)
    blockCount = 1;

  switch (targetFormat) {
  case 2:
    *outSize = 4 * pixelCount;
    *outStride = 4 * w;
    return true;
  case 3:
  case 4:
  case 5:
    *outSize = 2 * pixelCount;
    *outStride = 2 * w;
    return true;
  case 9:
    *outSize = blockCount + 2 * pixelCount;
    *outStride = 2 * w;
    return true;
  default:
    *outSize = 0;
    *outStride = 0;
    return false;
  }
}

bool BlpImage_DecompressPaletteMip(void *blpImage, uint32_t targetFormat, uint8_t mipLevel,
                                   uint8_t *dst, const uint8_t *indices) {
  auto *img = AsBlp(blpImage);
  uint32_t w = MipDim(img->width, mipLevel);
  uint32_t h = MipDim(img->height, mipLevel);

  switch (targetFormat) {
  case 2: {
    int pixelCount = static_cast<int>(BlpImage_GetMipPixelCount(blpImage, mipLevel));
    if (img->alphaDepth == 8)
      BlpPalette_DecompressRGBA8(blpImage, dst, indices, pixelCount);
    else
      BlpPalette_DecompressToRGBA(blpImage, dst, indices, static_cast<uint32_t>(pixelCount));
    return true;
  }
  case 3:
    BlpPalette_DitherToA1RGB5(blpImage, reinterpret_cast<uint16_t *>(dst), indices, w, h);
    return true;
  case 4:
    BlpPalette_DitherToARGB4(blpImage, reinterpret_cast<uint16_t *>(dst), indices, w, h);
    return true;
  case 5:
    BlpPalette_DitherToRGB565(blpImage, dst, indices,
                              w, h);
    return true;
  case 9:
    BlpPalette_DitherToRGB565Alpha(blpImage, dst, indices, w, h);
    return true;
  default:
    return false;
  }
}

bool BlpImage_GetMipData(void *blpImage, uint32_t targetFormat, uint32_t mipLevel, void **outData,
                         int *outStride) {
  auto *img = AsBlp(blpImage);
  if (!img->inMemoryImage)
    return false;

  img->tempMipBuffer = 0;

  if (mipLevel != 0 && ((img->hasMipsFlag & 0xF) == 0 || mipLevel >= img->mipCount))
    return false;

  uint8_t *mipPtr = reinterpret_cast<uint8_t *>(img->inMemoryImage) + img->mipOffsets[mipLevel];
  if (img->mipSizes[mipLevel] == 0)
    return false;

  switch (img->compressionType) {
  case 1: {
    uint32_t size;
    if (!BlpImage_CalcMipSize(blpImage, targetFormat, static_cast<uint8_t>(mipLevel), &size,
                              reinterpret_cast<uint32_t *>(outStride)))
      return false;
    uint8_t *buf = static_cast<uint8_t *>(malloc(size));
    *outData = buf;
    bool ok = BlpImage_DecompressPaletteMip(blpImage, targetFormat, static_cast<uint8_t>(mipLevel),
                                            buf, mipPtr);
    img->tempMipBuffer = reinterpret_cast<std::uintptr_t>(buf);
    return ok;
  }
  case 2: {
    switch (targetFormat) {
    case 0:
    case 1:
    case 7:
      *outData = mipPtr;
      return true;
    case 2:
    case 3:
    case 4:
    case 5: {
      uint32_t size;
      if (!BlpImage_CalcMipSize(blpImage, targetFormat, static_cast<uint8_t>(mipLevel), &size,
                                reinterpret_cast<uint32_t *>(outStride)))
        return false;
      BlpDxtDispatchFormat sourceDispatchFormat;
      BlpDxtDispatchFormat destinationDispatchFormat;
      if (!BlpDxtMapDispatchFormat(img->alphaType, &sourceDispatchFormat) ||
          !BlpDxtMapDispatchFormat(targetFormat, &destinationDispatchFormat)) {
        return false;
      }
      uint8_t *buf = static_cast<uint8_t *>(malloc(size));
      if (!buf) {
        return false;
      }
      *outData = buf;
      img->tempMipBuffer = reinterpret_cast<std::uintptr_t>(buf);
      const int dims[2] = {
          static_cast<int>(img->width >> mipLevel),
          static_cast<int>(img->height >> mipLevel),
      };
      const uint32_t sourceStride = BlpDxtDispatchRowSize(
          sourceDispatchFormat, static_cast<uint32_t>(dims[0]), static_cast<uint32_t>(dims[1]));
      if (!BlpDxt_ConvertFormat(dims, 0, mipPtr, static_cast<int>(sourceStride),
                                sourceDispatchFormat, buf, *outStride, destinationDispatchFormat)) {
        free(buf);
        *outData = nullptr;
        img->tempMipBuffer = 0;
        return false;
      }
      return true;
    }
    default:
      return false;
    }
  }
  case 3:
    *outData = mipPtr;
    return true;
  default:
    return false;
  }
}

bool BlpImage_DecompressAllMips(void *blpImage, uint32_t targetFormat,
                                std::uintptr_t *mipArray,
                                uint32_t startLevel) {
  auto *img = AsBlp(blpImage);
  if (startLevel != 0 && ((img->hasMipsFlag & 0xF) == 0 || startLevel >= img->mipCount))
    return false;

  uint32_t w = MipDim(img->width, static_cast<uint8_t>(startLevel));
  uint32_t h = MipDim(img->height, static_cast<uint8_t>(startLevel));
  const bool allocatedArray = *mipArray == 0;

  if (allocatedArray) {
    std::uintptr_t *arr = BlpMip_AllocArray(targetFormat, w, h, 0, 0x22B);
    if (!arr)
      return false;
    *mipArray = reinterpret_cast<std::uintptr_t>(arr);
  } else {
    BlpMip_InitExistingArray(targetFormat, w, h,
                             reinterpret_cast<std::uintptr_t *>(*mipArray));
  }

  if (startLevel >= img->mipCount)
    return true;

  void *src = nullptr;
  int stride = 0;
  for (uint32_t level = startLevel; level < img->mipCount; ++level) {
    if (!BlpImage_GetMipData(blpImage, targetFormat, level, &src, &stride)) {
      if (allocatedArray) {
        free(reinterpret_cast<void *>(*mipArray));
        *mipArray = 0;
      }
      return false;
    }

    uint32_t levelSize =
        BlpMip_CalcLevelSize(static_cast<uint8_t>(level), img->width, img->height, targetFormat);
    auto *arrPtr = reinterpret_cast<std::uintptr_t *>(*mipArray);
    memcpy(reinterpret_cast<void *>(arrPtr[level - startLevel]), src, levelSize);

    if (img->tempMipBuffer) {
      free(reinterpret_cast<void *>(img->tempMipBuffer));
      img->tempMipBuffer = 0;
    }
  }
  return true;
}

bool BlpImage_DecompressMipToBuffer(void *blpImage, const char * ,
                                    uint32_t targetFormat, uint32_t mipLevel, void *dstBuffer,
                                    uint32_t *outStride) {
  auto *img = AsBlp(blpImage);
  if (!img->inMemoryImage)
    return false;

  if (mipLevel != 0 && ((img->hasMipsFlag & 0xF) == 0 || mipLevel >= img->mipCount))
    return false;

  uint8_t *mipPtr = reinterpret_cast<uint8_t *>(img->inMemoryImage) + img->mipOffsets[mipLevel];
  uint32_t mipSize = img->mipSizes[mipLevel];
  if (!mipSize)
    return false;

  switch (img->compressionType) {
  case 0:
    return false;
  case 1: {
    uint32_t bufSize;
    if (!BlpImage_CalcMipSize(blpImage, targetFormat, static_cast<uint8_t>(mipLevel), &bufSize,
                              outStride))
      return false;
    bool ok = BlpImage_DecompressPaletteMip(blpImage, targetFormat, static_cast<uint8_t>(mipLevel),
                                            static_cast<uint8_t *>(dstBuffer), mipPtr);
    img->tempMipBuffer = reinterpret_cast<std::uintptr_t>(dstBuffer);
    return ok;
  }
  case 2: {
    switch (targetFormat) {
    case 0:
    case 1:
    case 7:
      memcpy(dstBuffer, mipPtr, mipSize);
      return true;
    case 2:
    case 3:
    case 4:
    case 5: {
      BlpDxtDispatchFormat sourceDispatchFormat;
      BlpDxtDispatchFormat destinationDispatchFormat;
      if (!BlpDxtMapDispatchFormat(img->alphaType, &sourceDispatchFormat) ||
          !BlpDxtMapDispatchFormat(targetFormat, &destinationDispatchFormat)) {
        return false;
      }

      const uint32_t width = BlpImage_GetMipWidth(blpImage, static_cast<uint8_t>(mipLevel));
      const uint32_t height = BlpImage_GetMipHeight(blpImage, static_cast<uint8_t>(mipLevel));
      const int dims[2] = {
          static_cast<int>(width),
          static_cast<int>(height),
      };
      const uint32_t sourceStride = BlpDxtDispatchRowSize(sourceDispatchFormat, width, height);
      const uint32_t destinationStride =
          BlpDxtDispatchRowSize(destinationDispatchFormat, width, height);

      return BlpDxt_ConvertFormat(
                 dims, 0, mipPtr, static_cast<int>(sourceStride), sourceDispatchFormat, dstBuffer,
                 static_cast<int>(destinationStride), destinationDispatchFormat) != 0;
    }
    case 9:
      return false;
    default:
      return false;
    }
  }
  case 3:
    memcpy(dstBuffer, mipPtr, mipSize);
    return true;
  default:
    return false;
  }
}

bool BlpImage_OpenFile(void * , const char *filename, bool ) {

  if (!filename)
    return false;
  return true;
}

bool BlpImage_DecompressToMipArray(void *blpImage, const char *funcName, uint32_t targetFormat,
                                   std::uintptr_t *mipArray, uint32_t startLevel,
                                   bool allowDirectPtrs) {
  auto *img = AsBlp(blpImage);
  if (startLevel != 0 && ((img->hasMipsFlag & 0xF) == 0 || startLevel >= img->mipCount))
    return false;

  if (!*mipArray) {
    uint32_t w = MipDim(img->width, static_cast<uint8_t>(startLevel));
    uint32_t h = MipDim(img->height, static_cast<uint8_t>(startLevel));
    std::uintptr_t *arr = BlpMip_AllocArray(targetFormat, w, h, 0, 0x28B);
    if (!arr)
      return false;
    *mipArray = reinterpret_cast<std::uintptr_t>(arr);
  } else {

    if (allowDirectPtrs &&
        ((img->compressionType == 2 &&
          (targetFormat != 2 && targetFormat != 3 && targetFormat != 4 && targetFormat != 5)) ||
         img->compressionType == 3)) {

      const auto fileData = img->inMemoryImage;
      for (uint32_t i = 0; img->mipSizes[i]; ++i) {
        mipArray[i] = fileData + img->mipOffsets[i];
      }
      img->inMemoryImage = 0;
      return true;
    }

    uint32_t w = MipDim(img->width, static_cast<uint8_t>(startLevel));
    uint32_t h = MipDim(img->height, static_cast<uint8_t>(startLevel));
    BlpMip_InitExistingArray(targetFormat, w, h, mipArray);
  }

  if (startLevel < img->mipCount) {
    for (uint32_t level = startLevel; level < img->mipCount; ++level) {
      uint32_t stride = 0;
      if (!BlpImage_DecompressMipToBuffer(blpImage, funcName, targetFormat, level,
                                          reinterpret_cast<void *>(mipArray[level - startLevel]),
                                          &stride)) {
        return false;
      }
    }
  }

  img->inMemoryImage = 0;
  return true;
}

}

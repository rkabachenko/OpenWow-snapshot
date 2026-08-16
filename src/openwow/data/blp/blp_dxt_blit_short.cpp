#include "openwow/data/blp/blp_dxt_blit_short.h"

#include <cstdint>
#include <cstring>

namespace openwow::data::blp::detail {

namespace {

inline uint16_t PackPixel(const uint8_t* bgra,
                          uint8_t alpha_rshift, uint8_t red_rshift,
                          uint8_t green_rshift, uint8_t blue_rshift,
                          uint8_t alpha_lshift, uint8_t red_lshift,
                          uint8_t green_lshift, uint8_t blue_lshift) {
  const uint8_t b = bgra[0];
  const uint8_t g = bgra[1];
  const uint8_t r = bgra[2];
  const uint8_t a = bgra[3];

  return static_cast<uint16_t>(
      (static_cast<uint8_t>(b >> blue_rshift)  << blue_lshift)  |
      (static_cast<uint8_t>(g >> green_rshift) << green_lshift) |
      (static_cast<uint8_t>(r >> red_rshift)   << red_lshift)   |
      (static_cast<uint8_t>(a >> alpha_rshift) << alpha_lshift));
}

}

void BlpDxt_BlitBgra8888ToShort(
    void* dst, const void* src, const int* dims,
    int src_stride, int dst_stride,
    uint8_t alpha_rshift, uint8_t red_rshift,
    uint8_t green_rshift, uint8_t blue_rshift,
    uint8_t alpha_lshift, uint8_t red_lshift,
    uint8_t green_lshift, uint8_t blue_lshift) {
  const int width  = dims[0];

  const int height = dims[1];

  if (height <= 0) {
    return;
  }

  auto* dst_row = static_cast<uint8_t*>(dst);
  auto* src_row = static_cast<const uint8_t*>(src);

  if (width >= 2) {

    for (int y = height; y > 0; --y) {
      const uint8_t* sp = src_row;
      auto* dp = reinterpret_cast<uint32_t*>(dst_row);

      for (int x = 0; x < width; x += 2) {
        const uint16_t lo = PackPixel(
            sp, alpha_rshift, red_rshift, green_rshift, blue_rshift,
            alpha_lshift, red_lshift, green_lshift, blue_lshift);
        sp += 4;

        const uint16_t hi = PackPixel(
            sp, alpha_rshift, red_rshift, green_rshift, blue_rshift,
            alpha_lshift, red_lshift, green_lshift, blue_lshift);
        sp += 4;

        *dp = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
        ++dp;
      }

      src_row += src_stride;

      dst_row += dst_stride;

    }
  } else {

    for (int y = height; y > 0; --y) {
      const uint8_t* sp = src_row;
      auto* dp = reinterpret_cast<uint16_t*>(dst_row);

      for (int x = 0; x < width; ++x) {
        *dp = PackPixel(
            sp, alpha_rshift, red_rshift, green_rshift, blue_rshift,
            alpha_lshift, red_lshift, green_lshift, blue_lshift);
        sp += 4;
        ++dp;
      }

      src_row += src_stride;

      dst_row += dst_stride;

    }
  }
}

}

namespace openwow::data {

void BlpDxt_BlitArgb8888ToArgb4444(const int* dims, const void* src,
                                    int src_stride, void* dst,
                                    int dst_stride) {
  blp::detail::BlpDxt_BlitBgra8888ToShort(
      dst, src, dims, src_stride, dst_stride,
      4, 4,
      4, 4,
      12, 8,
      4, 0);
}

void BlpDxt_BlitArgb8888ToArgb1555(const int* dims, const void* src,
                                    int src_stride, void* dst,
                                    int dst_stride) {
  blp::detail::BlpDxt_BlitBgra8888ToShort(
      dst, src, dims, src_stride, dst_stride,
      7, 3,
      3, 3,
      15, 10,
      5, 0);
}

void BlpDxt_BlitArgb8888ToRgb565(const int* dims, const void* src,
                                  int src_stride, void* dst,
                                  int dst_stride) {
  blp::detail::BlpDxt_BlitBgra8888ToShort(
      dst, src, dims, src_stride, dst_stride,
      8, 3,
      2, 3,
      0, 11,
      5, 0);
}

}

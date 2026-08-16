#pragma once

#include <cstdint>

namespace openwow::data::blp::detail {

void BlpDxt_BlitBgra8888ToShort(
    void* dst, const void* src, const int* dims,
    int src_stride, int dst_stride,
    uint8_t alpha_rshift, uint8_t red_rshift,
    uint8_t green_rshift, uint8_t blue_rshift,
    uint8_t alpha_lshift, uint8_t red_lshift,
    uint8_t green_lshift, uint8_t blue_lshift);

}

namespace openwow::data {

void BlpDxt_BlitArgb8888ToArgb4444(const int* dims, const void* src,
                                    int src_stride, void* dst,
                                    int dst_stride);

void BlpDxt_BlitArgb8888ToArgb1555(const int* dims, const void* src,
                                    int src_stride, void* dst,
                                    int dst_stride);

void BlpDxt_BlitArgb8888ToRgb565(const int* dims, const void* src,
                                  int src_stride, void* dst,
                                  int dst_stride);

}

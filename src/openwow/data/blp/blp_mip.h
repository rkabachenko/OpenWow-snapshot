#pragma once

#include <cstdint>
#include <cstddef>

namespace openwow::data {

enum BlpPixelFormat : uint32_t {
    kBlpPixelDxt1      = 0,
    kBlpPixelDxt3      = 1,
    kBlpPixelArgb8888  = 2,
    kBlpPixelArgb1555  = 3,
    kBlpPixelArgb4444  = 4,
    kBlpPixelRgb565    = 5,
    kBlpPixelA8        = 6,
    kBlpPixelDxt5      = 7,
    kBlpPixelNumFormats,
    kBlpPixelUv88      = 9,
};

enum class BlpDxtDispatchFormat : uint32_t {
    kA8 = 0,
    kArgb8888 = 2,
    kArgb4444 = 3,
    kArgb1555 = 4,
    kRgb565 = 5,
    kDxt1 = 6,
    kDxt3 = 7,
    kDxt5 = 8,
};

int BlpFormat_BitsPerPixel(uint32_t format);

uint32_t BlpMip_CalcLevelSize(uint8_t level, uint32_t width,
                              uint32_t height, uint32_t format);

int BlpMip_CountLevels(uint32_t width, uint32_t height);

std::uintptr_t* BlpMip_AllocArray(uint32_t format, uint32_t width,
                                  uint32_t height, int file, int line);

int BlpMip_CalcTotalMipSize(uint32_t format, uint32_t width, uint32_t height);

void BlpMip_InitExistingArray(uint32_t format, uint32_t width,
                              uint32_t height, std::uintptr_t* buf);

uint32_t BlpMip_CalcRowSize(uint32_t dispatch_format, uint32_t width, uint32_t height);

void BlpMip_BoxFilterDownsample(uint32_t* dst, uint32_t outW, uint32_t outH,
                                const uint8_t* src, uint32_t srcStride,
                                uint32_t srcH);

void BlpDxt_InitConversionTable();

int BlpDxt_ConvertFormat(const int* dims, int source_variant,
                         const void* source_data, int source_stride,
                         BlpDxtDispatchFormat source_dispatch_format,
                         void* destination_data, int destination_stride,
                         BlpDxtDispatchFormat destination_dispatch_format);

void* BlpDxt_CopyArgb8888Rows(const int* dims, const void* source_data,
                               int source_stride, void* destination_data,
                               int destination_stride);

void* BlpDxt_CopyPixel16Rows(const int* dims, const void* source_data,
                              int source_stride, void* destination_data,
                              int destination_stride);

void BlpDxt_BlitArgb8888ToArgb4444(const int* dims, const void* src,
                                    int src_stride, void* dst,
                                    int dst_stride);

void BlpDxt_BlitArgb8888ToArgb1555(const int* dims, const void* src,
                                    int src_stride, void* dst,
                                    int dst_stride);

void BlpDxt_BlitArgb8888ToRgb565(const int* dims, const void* src,
                                  int src_stride, void* dst,
                                  int dst_stride);

inline constexpr std::size_t kBlpImageStorageDwords = 301;
inline constexpr std::size_t kBlpImageStorageBytes =
    kBlpImageStorageDwords * sizeof(std::uint32_t);

void* BlpImage_InitDefaults(void* blpImage);

void BlpImage_Free(void* blpImage);

int BlpImage_ParseHeader(void* blpImage, const void* fileData);

uint32_t BlpImage_GetMipWidth(const void* blpImage, uint8_t level);

uint32_t BlpImage_GetMipHeight(const void* blpImage, uint8_t level);

void BlpPalette_DecompressRGBA8(void* blpImage, void* dst,
                                const uint8_t* indices, int pixelCount);

void BlpPalette_DecompressToRGBA(void* blpImage, uint8_t* dst,
                                 const uint8_t* indices, uint32_t pixelCount);

void BlpPalette_DitherToARGB4(void* blpImage, uint16_t* dst,
                              const uint8_t* indices,
                              uint32_t width, uint32_t height);

void BlpPalette_DitherToA1RGB5(void* blpImage, uint16_t* dst,
                               const uint8_t* indices,
                               uint32_t width, uint32_t height);

void BlpPalette_DitherToRGB565(void* blpImage, void* dst,
                               const void* indices, uint32_t width, uint32_t height);

void BlpPalette_DitherToRGB565Alpha(void* blpImage, void* dst,
                                    const void* indices, uint32_t width,
                                    uint32_t height);

uint32_t BlpImage_GetMipPixelCount(const void* blpImage, uint8_t mipLevel);

bool BlpImage_FreeMipBuffer(void* blpImage, uint32_t mipLevel);

bool BlpImage_CalcMipSize(const void* blpImage, uint32_t targetFormat,
                          uint8_t mipLevel, uint32_t* outSize,
                          uint32_t* outStride);

bool BlpImage_DecompressPaletteMip(void* blpImage, uint32_t targetFormat,
                                   uint8_t mipLevel, uint8_t* dst,
                                   const uint8_t* indices);

bool BlpImage_GetMipData(void* blpImage, uint32_t targetFormat,
                         uint32_t mipLevel, void** outData, int* outStride);

bool BlpImage_DecompressAllMips(void* blpImage, uint32_t targetFormat,
                                std::uintptr_t* mipArray, uint32_t startLevel);

bool BlpImage_DecompressMipToBuffer(void* blpImage, const char* funcName,
                                    uint32_t targetFormat, uint32_t mipLevel,
                                    void* dstBuffer, uint32_t* outStride);

bool BlpImage_OpenFile(void* blpImage, const char* filename, bool useLocale);

bool BlpImage_DecompressToMipArray(void* blpImage, const char* funcName,
                                   uint32_t targetFormat, std::uintptr_t* mipArray,
                                   uint32_t startLevel, bool allowDirectPtrs);

}

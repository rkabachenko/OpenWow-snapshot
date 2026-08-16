#include "openwow/data/blp/blp_info.h"

#include <algorithm>
#include <cstring>

namespace openwow::data {

namespace {

constexpr uint32_t kBLP2Magic = 0x32504C42;

#pragma pack(push, 1)
struct RawBLPHeader {
    uint32_t magic;
    uint32_t type;
    uint8_t  compression;
    uint8_t  alphaDepth;
    uint8_t  alphaType;
    uint8_t  hasMips;
    uint32_t width;
    uint32_t height;
    uint32_t mipOffsets[16];
    uint32_t mipSizes[16];
};
#pragma pack(pop)

constexpr size_t kMinBLPHeaderSize = sizeof(RawBLPHeader);

}

std::optional<BLPTextureInfo> BLPInfoReader::ParseHeader(const uint8_t* data, size_t size) {
    if (!data || size < kMinBLPHeaderSize) return std::nullopt;

    RawBLPHeader raw;
    std::memcpy(&raw, data, sizeof(raw));

    if (raw.magic != kBLP2Magic) return std::nullopt;
    if (raw.width == 0 || raw.height == 0) return std::nullopt;

    BLPTextureInfo info;
    info.version = raw.type;

    switch (raw.compression) {
        case 0: info.compression = BLPCompression::JPEG; break;
        case 1: info.compression = BLPCompression::Palette; break;
        case 2: info.compression = BLPCompression::DirectX; break;
        case 3: info.compression = BLPCompression::Uncompressed; break;
        default: info.compression = BLPCompression::JPEG; break;
    }

    switch (raw.alphaDepth) {
        case 0: info.alphaType = BLPAlphaType::NoAlpha; break;
        case 1: info.alphaType = BLPAlphaType::Alpha1Bit; break;
        case 4: info.alphaType = BLPAlphaType::Alpha4Bit; break;
        case 8: info.alphaType = BLPAlphaType::Alpha8Bit; break;
        default: info.alphaType = BLPAlphaType::NoAlpha; break;
    }

    info.hasMips = raw.hasMips != 0;
    info.width   = raw.width;
    info.height  = raw.height;

    uint32_t mipCount = 0;
    for (int i = 0; i < 16; ++i) {
        if (raw.mipSizes[i] > 0) {
            ++mipCount;
        } else {
            break;
        }
    }
    info.mipCount = mipCount;

    info.mips.reserve(mipCount);
    uint32_t w = raw.width;
    uint32_t h = raw.height;
    for (uint32_t i = 0; i < mipCount; ++i) {
        BLPMipInfo mip;
        mip.width  = w;
        mip.height = h;
        mip.offset = raw.mipOffsets[i];
        mip.size   = raw.mipSizes[i];
        info.mips.push_back(mip);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    return info;
}

bool BLPInfoReader::IsValidBLP(const uint8_t* data, size_t size) {
    if (!data || size < 4) return false;
    uint32_t magic;
    std::memcpy(&magic, data, 4);
    return magic == kBLP2Magic && size >= kMinBLPHeaderSize;
}

uint32_t BLPInfoReader::GetMipCount(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return 0;
    uint32_t maxDim = std::max(width, height);
    uint32_t count = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        ++count;
    }
    return count;
}

BLPMipDimensions BLPInfoReader::GetMipDimensions(uint32_t width, uint32_t height, uint32_t mipLevel) {
    uint32_t w = std::max(1u, width >> mipLevel);
    uint32_t h = std::max(1u, height >> mipLevel);
    return {w, h};
}

uint32_t BLPInfoReader::EstimateMemorySize(const BLPTextureInfo& info) {
    uint32_t total = 0;
    uint32_t w = info.width;
    uint32_t h = info.height;
    uint32_t count = info.mipCount > 0 ? info.mipCount : 1;
    for (uint32_t i = 0; i < count; ++i) {
        total += w * h * 4;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return total;
}

std::string BLPInfoReader::GetFormatName(BLPCompression c) {
    switch (c) {
        case BLPCompression::JPEG:         return "JPEG";
        case BLPCompression::Palette:      return "Palette";
        case BLPCompression::DirectX:      return "DirectX";
        case BLPCompression::Uncompressed: return "Uncompressed";
    }
    return "Unknown";
}

std::string BLPInfoReader::GetAlphaTypeName(BLPAlphaType a) {
    switch (a) {
        case BLPAlphaType::NoAlpha:   return "NoAlpha";
        case BLPAlphaType::Alpha1Bit: return "Alpha1Bit";
        case BLPAlphaType::Alpha4Bit: return "Alpha4Bit";
        case BLPAlphaType::Alpha8Bit: return "Alpha8Bit";
    }
    return "Unknown";
}

bool BLPInfoReader::IsPowerOfTwo(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

}

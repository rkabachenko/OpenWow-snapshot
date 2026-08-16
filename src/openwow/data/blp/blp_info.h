#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::data {

enum class BLPCompression : uint8_t {
    JPEG         = 0,
    Palette      = 1,
    DirectX      = 2,
    DXT          = 2,
    Uncompressed = 3,
};

enum class BLPAlphaType : uint8_t {
    NoAlpha   = 0,
    Alpha1Bit = 1,
    Alpha4Bit = 4,
    Alpha8Bit = 8,
};

struct BLPMipInfo {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t offset = 0;
    uint32_t size   = 0;
};

struct BLPTextureInfo {
    uint32_t       version     = 0;
    BLPCompression compression = BLPCompression::JPEG;
    BLPAlphaType   alphaType   = BLPAlphaType::NoAlpha;
    bool           hasMips     = false;
    uint32_t       width       = 0;
    uint32_t       height      = 0;
    uint32_t       mipCount    = 0;
    std::vector<BLPMipInfo> mips;
};

struct BLPMipDimensions {
    uint32_t w = 0;
    uint32_t h = 0;
};

class BLPInfoReader {
public:

    static std::optional<BLPTextureInfo> ParseHeader(const uint8_t* data, size_t size);

    static bool IsValidBLP(const uint8_t* data, size_t size);

    static uint32_t GetMipCount(uint32_t width, uint32_t height);

    static BLPMipDimensions GetMipDimensions(uint32_t width, uint32_t height, uint32_t mipLevel);

    static uint32_t EstimateMemorySize(const BLPTextureInfo& info);

    static std::string GetFormatName(BLPCompression c);

    static std::string GetAlphaTypeName(BLPAlphaType a);

    static bool IsPowerOfTwo(uint32_t v);
};

}

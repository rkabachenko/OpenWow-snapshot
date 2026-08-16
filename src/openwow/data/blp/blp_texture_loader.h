#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openwow::data {

enum class BLPTexCompression : uint8_t {
    JPEG          = 0,
    Palette       = 1,
    DXT           = 2,
    Uncompressed  = 3,
    BC5           = 4,
};

enum class BLPTexAlphaDepth : uint8_t {
    NoAlpha   = 0,
    Alpha1Bit = 1,
    Alpha4Bit = 4,
    Alpha8Bit = 8,
};

#pragma pack(push, 1)
struct BLPTexHeader {
    uint32_t                   magic;
    uint32_t                   version;
    BLPTexCompression          compression;
    BLPTexAlphaDepth           alphaDepth;
    uint8_t                    alphaEncoding;
    uint8_t                    hasMips;
    uint32_t                   width;
    uint32_t                   height;
    std::array<uint32_t, 16>   mipOffsets;
    std::array<uint32_t, 16>   mipSizes;
    std::array<uint32_t, 256>  palette;
};
#pragma pack(pop)

static_assert(sizeof(BLPTexHeader) == 1172, "BLPTexHeader size mismatch");

struct BLPTextureMip {
    uint32_t              width    = 0;
    uint32_t              height   = 0;
    std::vector<uint8_t>  data;
    uint8_t               mipLevel = 0;
};

struct BLPTextureData {
    BLPTexHeader                header{};
    std::vector<BLPTextureMip>  mips;
    uint32_t                    mipCount = 0;
    bool                        isValid  = false;
    std::string                 errorMsg;

    std::vector<uint8_t>        jpegHeader;
};

class BLPTextureLoader {
public:

    static BLPTextureData Load(const std::vector<uint8_t>& fileData);

    static std::vector<uint8_t> DecompressMip(const BLPTextureData& tex, uint8_t mipLevel);

    static std::pair<uint32_t, uint32_t> GetMipDimensions(uint32_t width, uint32_t height, uint8_t mipLevel);

    static uint8_t GetMipCount(uint32_t width, uint32_t height);

    static bool IsValidBLP(const std::vector<uint8_t>& data);

    static constexpr uint32_t kBLPMagic = 0x32504C42;

    static std::string GetCompressionName(BLPTexCompression comp);

private:

    static void DecompressDXT1Block(const uint8_t* block, uint32_t* output, uint32_t stride);
    static void DecompressDXT3Block(const uint8_t* block, uint32_t* output, uint32_t stride);
    static void DecompressDXT5Block(const uint8_t* block, uint32_t* output, uint32_t stride);

    static std::vector<uint8_t> DecompressDXTMip(const BLPTextureData& tex, uint8_t mipLevel);

    static std::vector<uint8_t> DecodePaletteMip(const BLPTextureData& tex, uint8_t mipLevel);

    static std::vector<uint8_t> DecodeUncompressedMip(const BLPTextureData& tex, uint8_t mipLevel);

    static std::vector<uint8_t> DecompressJPEGMip(const BLPTextureData& tex, uint8_t mipLevel);
};

}

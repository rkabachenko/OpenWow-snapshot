#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::data {

#pragma pack(push, 1)
struct BLPHeader {
    uint32_t magic;
    uint32_t type;
    uint8_t  compression;
    uint8_t  alpha_depth;
    uint8_t  alpha_type;
    uint8_t  has_mips;
    uint32_t width;
    uint32_t height;
    uint32_t mip_offsets[16];
    uint32_t mip_sizes[16];
    uint32_t palette[256];
};
#pragma pack(pop)

static_assert(sizeof(BLPHeader) == 1172, "BLP header size mismatch");

struct DecodedTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_levels = 0;

    enum class Format {
        RGBA8,
        DXT1,
        DXT3,
        DXT5,
    };
    Format format = Format::RGBA8;

    struct MipLevel {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> data;
    };
    std::vector<MipLevel> mips;
};

class BLPLoader {
public:

    static std::optional<DecodedTexture> Load(const uint8_t* data, size_t size);

    static std::optional<DecodedTexture> LoadFromVFS(
        const openwow::vfs::VirtualFileSystem& vfs, const std::string& path);

    static DecodedTexture DecompressToRGBA8(const DecodedTexture& tex);

private:
    static bool DecodePalettized(const BLPHeader& header, const uint8_t* data,
                                 size_t size, DecodedTexture& out);
    static bool DecodeDXT(const BLPHeader& header, const uint8_t* data,
                          size_t size, DecodedTexture& out);
    static bool DecodeUncompressed(const BLPHeader& header, const uint8_t* data,
                                   size_t size, DecodedTexture& out);

    static void DecompressDXT1Block(const uint8_t* block, uint32_t* output, uint32_t stride);
    static void DecompressDXT3Block(const uint8_t* block, uint32_t* output, uint32_t stride);
    static void DecompressDXT5Block(const uint8_t* block, uint32_t* output, uint32_t stride);

    static uint32_t CountMipLevels(const BLPHeader& header);
    static DecodedTexture::Format DetermineFormat(const BLPHeader& header);
};

}

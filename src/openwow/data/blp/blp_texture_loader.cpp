#include "openwow/data/blp/blp_texture_loader.h"
#include "openwow/data/blp/blp_mip.h"
#include "openwow/data/blp/dxt_block_codec.h"
#include "openwow/data/image/jpeg_decoder.h"

#include <algorithm>
#include <cstring>

namespace openwow::data {

static uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static void RGB565ToRGBA(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    g = static_cast<uint8_t>(((c >> 5)  & 0x3F) * 255 / 63);
    b = static_cast<uint8_t>(((c >> 0)  & 0x1F) * 255 / 31);
}

static uint8_t ExpandDxt3AlphaNibble(uint8_t nibble) {
    return static_cast<uint8_t>((nibble & 0x0F) << 4);
}

static void WritePalettePixelRGBA(std::vector<uint8_t>& rgba,
                                  const size_t pixelIndex,
                                  const uint32_t paletteEntry) {
    const auto offset = pixelIndex * 4;
    rgba[offset + 0] = static_cast<uint8_t>((paletteEntry >> 16) & 0xFF);
    rgba[offset + 1] = static_cast<uint8_t>((paletteEntry >> 8) & 0xFF);
    rgba[offset + 2] = static_cast<uint8_t>(paletteEntry & 0xFF);
}

static uint8_t ExpandPalette1BitAlpha(uint8_t alphaByte) {
    return (alphaByte & 1u) != 0u ? 0xFFu : 0x00u;
}

static uint8_t ExpandPalette4BitAlpha(uint8_t alphaNibble) {
    static constexpr uint8_t kAlpha4To8[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    };
    return kAlpha4To8[alphaNibble & 0x0Fu];
}

void BLPTextureLoader::DecompressDXT1Block(const uint8_t* block, uint32_t* output, uint32_t stride) {
    uint16_t c0 = ReadU16LE(block);
    uint16_t c1 = ReadU16LE(block + 2);

    uint8_t r0, g0, b0, r1, g1, b1;
    RGB565ToRGBA(c0, r0, g0, b0);
    RGB565ToRGBA(c1, r1, g1, b1);

    uint8_t colors[4][4];
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0; colors[0][3] = 255;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1; colors[1][3] = 255;

    if (c0 > c1) {
        colors[2][0] = static_cast<uint8_t>((2 * r0 + r1) / 3);
        colors[2][1] = static_cast<uint8_t>((2 * g0 + g1) / 3);
        colors[2][2] = static_cast<uint8_t>((2 * b0 + b1) / 3);
        colors[2][3] = 255;
        colors[3][0] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
        colors[3][1] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
        colors[3][2] = static_cast<uint8_t>((b0 + 2 * b1) / 3);
        colors[3][3] = 255;
    } else {
        colors[2][0] = static_cast<uint8_t>((r0 + r1) / 2);
        colors[2][1] = static_cast<uint8_t>((g0 + g1) / 2);
        colors[2][2] = static_cast<uint8_t>((b0 + b1) / 2);
        colors[2][3] = 255;
        colors[3][0] = 0; colors[3][1] = 0; colors[3][2] = 0; colors[3][3] = 0;
    }

    uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            uint32_t idx = (indices >> (2 * (y * 4 + x))) & 0x03;
            output[y * stride + x] =
                static_cast<uint32_t>(colors[idx][0])       |
                (static_cast<uint32_t>(colors[idx][1]) << 8) |
                (static_cast<uint32_t>(colors[idx][2]) << 16)|
                (static_cast<uint32_t>(colors[idx][3]) << 24);
        }
    }
}

void BLPTextureLoader::DecompressDXT3Block(const uint8_t* block, uint32_t* output, uint32_t stride) {

    const uint8_t* colorBlock = block + 8;
    uint16_t c0 = ReadU16LE(colorBlock);
    uint16_t c1 = ReadU16LE(colorBlock + 2);

    uint8_t r0, g0, b0, r1, g1, b1;
    RGB565ToRGBA(c0, r0, g0, b0);
    RGB565ToRGBA(c1, r1, g1, b1);

    uint8_t colors[4][3];
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
    colors[2][0] = static_cast<uint8_t>((2 * r0 + r1) / 3);
    colors[2][1] = static_cast<uint8_t>((2 * g0 + g1) / 3);
    colors[2][2] = static_cast<uint8_t>((2 * b0 + b1) / 3);
    colors[3][0] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
    colors[3][1] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
    colors[3][2] = static_cast<uint8_t>((b0 + 2 * b1) / 3);

    uint32_t indices = colorBlock[4] | (colorBlock[5] << 8) | (colorBlock[6] << 16) | (colorBlock[7] << 24);

    for (uint32_t y = 0; y < 4; ++y) {

        uint16_t alphaRow = ReadU16LE(block + y * 2);
        for (uint32_t x = 0; x < 4; ++x) {
            uint32_t idx = (indices >> (2 * (y * 4 + x))) & 0x03;
            uint8_t a4 = (alphaRow >> (4 * x)) & 0x0F;
            uint8_t a = ExpandDxt3AlphaNibble(a4);

            output[y * stride + x] =
                static_cast<uint32_t>(colors[idx][0])       |
                (static_cast<uint32_t>(colors[idx][1]) << 8) |
                (static_cast<uint32_t>(colors[idx][2]) << 16)|
                (static_cast<uint32_t>(a) << 24);
        }
    }
}

void BLPTextureLoader::DecompressDXT5Block(const uint8_t* block, uint32_t* output, uint32_t stride) {
    const auto alphaTable = blp::detail::BuildDxt5AlphaTable(block[0], block[1]);
    const uint64_t alphaBits = blp::detail::ReadLittleEndian48(block + 2);

    const uint8_t* colorBlock = block + 8;
    uint16_t c0 = ReadU16LE(colorBlock);
    uint16_t c1 = ReadU16LE(colorBlock + 2);

    uint8_t r0, g0, b0, r1, g1, b1;
    RGB565ToRGBA(c0, r0, g0, b0);
    RGB565ToRGBA(c1, r1, g1, b1);

    uint8_t colors[4][3];
    colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
    colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
    colors[2][0] = static_cast<uint8_t>((2 * r0 + r1) / 3);
    colors[2][1] = static_cast<uint8_t>((2 * g0 + g1) / 3);
    colors[2][2] = static_cast<uint8_t>((2 * b0 + b1) / 3);
    colors[3][0] = static_cast<uint8_t>((r0 + 2 * r1) / 3);
    colors[3][1] = static_cast<uint8_t>((g0 + 2 * g1) / 3);
    colors[3][2] = static_cast<uint8_t>((b0 + 2 * b1) / 3);

    uint32_t colorIndices = colorBlock[4] | (colorBlock[5] << 8) | (colorBlock[6] << 16) | (colorBlock[7] << 24);

    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            uint32_t pixel = y * 4 + x;
            uint32_t cidx = (colorIndices >> (2 * pixel)) & 0x03;
            uint32_t aidx = (alphaBits >> (3 * pixel)) & 0x07;

            output[y * stride + x] =
                static_cast<uint32_t>(colors[cidx][0])       |
                (static_cast<uint32_t>(colors[cidx][1]) << 8) |
                (static_cast<uint32_t>(colors[cidx][2]) << 16)|
                (static_cast<uint32_t>(alphaTable[aidx]) << 24);
        }
    }
}

std::vector<uint8_t> BLPTextureLoader::DecompressDXTMip(const BLPTextureData& tex, uint8_t mipLevel) {
    if (mipLevel >= tex.mips.size()) return {};

    const auto& mip = tex.mips[mipLevel];
    uint32_t w = mip.width;
    uint32_t h = mip.height;
    uint32_t bw = (w + 3) / 4;
    uint32_t bh = (h + 3) / 4;

    std::vector<uint8_t> rgba(w * h * 4, 0);
    auto* pixels = reinterpret_cast<uint32_t*>(rgba.data());

    uint8_t alphaEnc = tex.header.alphaEncoding;
    size_t blockSize = (alphaEnc == 0) ? 8 : 16;

    const uint8_t* src = mip.data.data();
    size_t srcSize = mip.data.size();

    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            size_t blockIdx = by * bw + bx;
            size_t offset = blockIdx * blockSize;
            if (offset + blockSize > srcSize) break;

            uint32_t tmp[4 * 4]{};
            const uint8_t* blockPtr = src + offset;
            if (alphaEnc == 0)
                DecompressDXT1Block(blockPtr, tmp, 4);
            else if (alphaEnc == 1)
                DecompressDXT3Block(blockPtr, tmp, 4);
            else
                DecompressDXT5Block(blockPtr, tmp, 4);

            for (uint32_t py = 0; py < 4 && (by * 4 + py) < h; ++py) {
                for (uint32_t px = 0; px < 4 && (bx * 4 + px) < w; ++px) {
                    pixels[(by * 4 + py) * w + (bx * 4 + px)] = tmp[py * 4 + px];
                }
            }
        }
    }

    return rgba;
}

std::vector<uint8_t> BLPTextureLoader::DecodePaletteMip(const BLPTextureData& tex, uint8_t mipLevel) {
    if (mipLevel >= tex.mips.size()) return {};

    const auto& mip = tex.mips[mipLevel];
    uint32_t w = mip.width;
    uint32_t h = mip.height;
    uint32_t pixelCount = w * h;

    std::vector<uint8_t> rgba(pixelCount * 4, 255);

    const auto& pal = tex.header.palette;
    const uint8_t* indices = mip.data.data();
    size_t dataSize = mip.data.size();
    const size_t availableIndices = std::min<size_t>(pixelCount, dataSize);

    if (tex.header.alphaDepth == BLPTexAlphaDepth::Alpha8Bit) {
        const size_t alphaBytes = dataSize > pixelCount ? dataSize - pixelCount : 0;
        const size_t pairedPixels = std::min(availableIndices, alphaBytes);

        for (size_t i = 0; i < pairedPixels; ++i) {
            WritePalettePixelRGBA(rgba, i, pal[indices[i]]);
            rgba[i * 4 + 3] = indices[pixelCount + i];
        }

        for (size_t i = pairedPixels; i < availableIndices; ++i) {
            WritePalettePixelRGBA(rgba, i, pal[indices[i]]);
        }

        return rgba;
    }

    for (size_t i = 0; i < availableIndices; ++i) {
        WritePalettePixelRGBA(rgba, i, pal[indices[i]]);
    }

    if (dataSize <= pixelCount) {
        return rgba;
    }

    const uint8_t* alphaPlane = indices + pixelCount;
    const size_t alphaBytes = dataSize - pixelCount;

    if (tex.header.alphaDepth == BLPTexAlphaDepth::Alpha4Bit) {
        size_t pixelIndex = 0;
        for (size_t byteIndex = 0; byteIndex < alphaBytes && pixelIndex < pixelCount; ++byteIndex) {
            const uint8_t alphaByte = alphaPlane[byteIndex];
            rgba[pixelIndex * 4 + 3] = ExpandPalette4BitAlpha(alphaByte);
            ++pixelIndex;
            if (pixelIndex >= pixelCount) {
                break;
            }
            rgba[pixelIndex * 4 + 3] = ExpandPalette4BitAlpha(alphaByte >> 4);
            ++pixelIndex;
        }
    } else if (tex.header.alphaDepth == BLPTexAlphaDepth::Alpha1Bit) {
        size_t pixelIndex = 0;
        for (size_t byteIndex = 0; byteIndex < alphaBytes && pixelIndex < pixelCount; ++byteIndex) {
            uint8_t alphaByte = alphaPlane[byteIndex];
            for (int bit = 0; bit < 8 && pixelIndex < pixelCount; ++bit) {
                rgba[pixelIndex * 4 + 3] = ExpandPalette1BitAlpha(alphaByte);
                alphaByte >>= 1;
                ++pixelIndex;
            }
        }
    }

    return rgba;
}

std::vector<uint8_t> BLPTextureLoader::DecodeUncompressedMip(const BLPTextureData& tex, uint8_t mipLevel) {

    if (mipLevel >= tex.mips.size()) return {};

    const auto& mip = tex.mips[mipLevel];
    uint32_t pixelCount = mip.width * mip.height;
    if (mip.data.size() < static_cast<size_t>(pixelCount) * 4u) {
        return {};
    }
    std::vector<uint8_t> rgba(pixelCount * 4);

    const uint8_t* src = mip.data.data();
    size_t srcSize = mip.data.size();

    for (uint32_t i = 0; i < pixelCount; ++i) {
        size_t off = i * 4;
        if (off + 3 >= srcSize) break;
        rgba[i * 4 + 0] = src[off + 2];
        rgba[i * 4 + 1] = src[off + 1];
        rgba[i * 4 + 2] = src[off + 0];
        rgba[i * 4 + 3] = src[off + 3];
    }

    return rgba;
}

bool BLPTextureLoader::IsValidBLP(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), 4);
    return magic == kBLPMagic;
}

std::pair<uint32_t, uint32_t> BLPTextureLoader::GetMipDimensions(uint32_t width, uint32_t height, uint8_t mipLevel) {
    uint32_t w = std::max(1u, width >> mipLevel);
    uint32_t h = std::max(1u, height >> mipLevel);
    return {w, h};
}

uint8_t BLPTextureLoader::GetMipCount(uint32_t width, uint32_t height) {
    return static_cast<uint8_t>(BlpMip_CountLevels(width, height));
}

std::string BLPTextureLoader::GetCompressionName(BLPTexCompression comp) {
    switch (comp) {
        case BLPTexCompression::JPEG:         return "JPEG";
        case BLPTexCompression::Palette:      return "Palette";
        case BLPTexCompression::DXT:          return "DXT";
        case BLPTexCompression::Uncompressed: return "Uncompressed";
        case BLPTexCompression::BC5:          return "BC5";
        default:                              return "Unknown";
    }
}

BLPTextureData BLPTextureLoader::Load(const std::vector<uint8_t>& fileData) {
    BLPTextureData result;

    if (fileData.size() < sizeof(BLPTexHeader)) {
        result.errorMsg = "File too small for BLP header";
        return result;
    }

    std::memcpy(&result.header, fileData.data(), sizeof(BLPTexHeader));

    if (result.header.magic != kBLPMagic) {
        result.errorMsg = "Invalid BLP magic";
        return result;
    }

    if (result.header.version != 1u) {
        result.errorMsg = "Invalid BLP version";
        return result;
    }

    uint32_t w = result.header.width;
    uint32_t h = result.header.height;

    uint32_t maxMips = (result.header.hasMips & 0x0Fu) != 0u ? GetMipCount(w, h) : 1u;
    uint32_t mipCount = 0;
    for (uint32_t i = 0; i < maxMips && i < 16; ++i) {
        if (result.header.mipOffsets[i] == 0 && result.header.mipSizes[i] == 0 && i > 0)
            break;
        if (result.header.mipOffsets[i] + result.header.mipSizes[i] > fileData.size())
            break;
        mipCount++;
    }

    if (result.header.compression == BLPTexCompression::JPEG) {
        constexpr size_t kHeaderEnd = sizeof(BLPTexHeader);
        if (fileData.size() >= kHeaderEnd + 4) {
            uint32_t jpegHeaderSize = 0;
            std::memcpy(&jpegHeaderSize, fileData.data() + kHeaderEnd, 4);
            if (jpegHeaderSize > 0 && kHeaderEnd + 4 + jpegHeaderSize <= fileData.size()) {
                result.jpegHeader.assign(
                    fileData.begin() + kHeaderEnd + 4,
                    fileData.begin() + kHeaderEnd + 4 + jpegHeaderSize);
            }
        }
    }

    result.mipCount = mipCount;
    result.mips.resize(mipCount);

    for (uint32_t i = 0; i < mipCount; ++i) {
        auto [mw, mh] = GetMipDimensions(w, h, static_cast<uint8_t>(i));
        result.mips[i].width    = mw;
        result.mips[i].height   = mh;
        result.mips[i].mipLevel = static_cast<uint8_t>(i);

        uint32_t off  = result.header.mipOffsets[i];
        uint32_t size = result.header.mipSizes[i];
        if (off + size <= fileData.size()) {
            result.mips[i].data.assign(fileData.begin() + off, fileData.begin() + off + size);
        }
    }

    result.isValid = true;
    return result;
}

std::vector<uint8_t> BLPTextureLoader::DecompressMip(const BLPTextureData& tex, uint8_t mipLevel) {
    if (!tex.isValid || mipLevel >= tex.mips.size()) return {};

    switch (tex.header.compression) {
        case BLPTexCompression::DXT:

            if (tex.header.alphaEncoding == 8u) {
                return DecodeUncompressedMip(tex, mipLevel);
            }
            return DecompressDXTMip(tex, mipLevel);
        case BLPTexCompression::Palette:
            return DecodePaletteMip(tex, mipLevel);
        case BLPTexCompression::JPEG:
            return DecompressJPEGMip(tex, mipLevel);
        case BLPTexCompression::Uncompressed:
            return DecodeUncompressedMip(tex, mipLevel);
        default:
            return {};
    }
}

std::vector<uint8_t> BLPTextureLoader::DecompressJPEGMip(const BLPTextureData& tex, uint8_t mipLevel) {
    if (mipLevel >= tex.mips.size()) return {};

    const auto& mip = tex.mips[mipLevel];
    if (mip.data.empty()) return {};

    std::vector<uint8_t> jpegBlob;
    if (!tex.jpegHeader.empty()) {
        jpegBlob.reserve(tex.jpegHeader.size() + mip.data.size());
        jpegBlob.insert(jpegBlob.end(), tex.jpegHeader.begin(), tex.jpegHeader.end());
        jpegBlob.insert(jpegBlob.end(), mip.data.begin(), mip.data.end());
    } else {

        jpegBlob = mip.data;
    }

    const auto decoded = DecodeJpeg(jpegBlob);
    if (!decoded.ok) {
        return {};
    }

    return decoded.pixels_rgba;
}

}

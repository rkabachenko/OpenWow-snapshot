#include "openwow/data/blp/blp_loader.h"
#include "openwow/data/blp/dxt_block_codec.h"

#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <cstring>

namespace openwow::data {

namespace {

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

inline uint8_t Expand5To8(uint8_t v) {
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

inline uint8_t Expand6To8(uint8_t v) {
    return static_cast<uint8_t>((v << 2) | (v >> 4));
}

struct Color565 { uint8_t r, g, b; };

inline Color565 DecodeRGB565(uint16_t c) {
    return {
        Expand5To8(static_cast<uint8_t>((c >> 11) & 0x1F)),
        Expand6To8(static_cast<uint8_t>((c >> 5) & 0x3F)),
        Expand5To8(static_cast<uint8_t>(c & 0x1F)),
    };
}

size_t DXTBlockSize(DecodedTexture::Format fmt) {
    switch (fmt) {
        case DecodedTexture::Format::DXT1: return 8;
        case DecodedTexture::Format::DXT3: return 16;
        case DecodedTexture::Format::DXT5: return 16;
        default: return 0;
    }
}

}

uint32_t BLPLoader::CountMipLevels(const BLPHeader& header) {
    uint32_t count = 0;
    for (int i = 0; i < 16; ++i) {
        if (header.mip_offsets[i] != 0 && header.mip_sizes[i] != 0) {
            ++count;
        } else {
            break;
        }
    }
    if (count == 0) return 0;
    if (header.has_mips == 0) return 1;
    return count;
}

DecodedTexture::Format BLPLoader::DetermineFormat(const BLPHeader& header) {
    if (header.compression == 1) return DecodedTexture::Format::RGBA8;
    if (header.compression == 3) return DecodedTexture::Format::RGBA8;
    if (header.compression == 2) {
        switch (header.alpha_type) {
            case 0:  return DecodedTexture::Format::DXT1;
            case 1:  return DecodedTexture::Format::DXT3;
            case 7:  return DecodedTexture::Format::DXT5;
            case 8:  return DecodedTexture::Format::RGBA8;
            default:

                if (header.alpha_depth <= 1) return DecodedTexture::Format::DXT1;
                if (header.alpha_depth == 4) return DecodedTexture::Format::DXT3;
                return DecodedTexture::Format::DXT5;
        }
    }
    return DecodedTexture::Format::RGBA8;
}

void BLPLoader::DecompressDXT1Block(const uint8_t* block, uint32_t* output, uint32_t stride) {
    const uint16_t c0 = ReadU16LE(block + 0);
    const uint16_t c1 = ReadU16LE(block + 2);
    const uint32_t indices =
        static_cast<uint32_t>(block[4]) |
        (static_cast<uint32_t>(block[5]) << 8) |
        (static_cast<uint32_t>(block[6]) << 16) |
        (static_cast<uint32_t>(block[7]) << 24);

    const auto col0 = DecodeRGB565(c0);
    const auto col1 = DecodeRGB565(c1);

    uint32_t colors[4];
    colors[0] = PackRGBA(col0.r, col0.g, col0.b, 255);
    colors[1] = PackRGBA(col1.r, col1.g, col1.b, 255);

    if (c0 > c1) {

        colors[2] = PackRGBA(
            static_cast<uint8_t>((2 * col0.r + col1.r) / 3),
            static_cast<uint8_t>((2 * col0.g + col1.g) / 3),
            static_cast<uint8_t>((2 * col0.b + col1.b) / 3), 255);
        colors[3] = PackRGBA(
            static_cast<uint8_t>((col0.r + 2 * col1.r) / 3),
            static_cast<uint8_t>((col0.g + 2 * col1.g) / 3),
            static_cast<uint8_t>((col0.b + 2 * col1.b) / 3), 255);
    } else {

        colors[2] = PackRGBA(
            static_cast<uint8_t>((col0.r + col1.r) / 2),
            static_cast<uint8_t>((col0.g + col1.g) / 2),
            static_cast<uint8_t>((col0.b + col1.b) / 2), 255);
        colors[3] = PackRGBA(0, 0, 0, 0);
    }

    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const uint32_t idx = (indices >> (2 * (y * 4 + x))) & 0x3;
            output[y * stride + x] = colors[idx];
        }
    }
}

void BLPLoader::DecompressDXT3Block(const uint8_t* block, uint32_t* output, uint32_t stride) {

    uint8_t alpha[16];
    for (int row = 0; row < 4; ++row) {
        const uint16_t bits = ReadU16LE(block + row * 2);
        for (int col = 0; col < 4; ++col) {
            const uint8_t a4 = static_cast<uint8_t>((bits >> (col * 4)) & 0x0F);
            alpha[row * 4 + col] = static_cast<uint8_t>((a4 << 4) | a4);
        }
    }

    DecompressDXT1Block(block + 8, output, stride);

    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            uint32_t& pixel = output[y * stride + x];
            pixel = (pixel & 0x00FFFFFF) | (static_cast<uint32_t>(alpha[y * 4 + x]) << 24);
        }
    }
}

void BLPLoader::DecompressDXT5Block(const uint8_t* block, uint32_t* output, uint32_t stride) {
    const uint64_t alpha_bits = blp::detail::ReadLittleEndian48(block + 2);
    const auto alpha_lut = blp::detail::BuildDxt5AlphaTable(block[0], block[1]);

    DecompressDXT1Block(block + 8, output, stride);

    for (int i = 0; i < 16; ++i) {
        const uint8_t sel = static_cast<uint8_t>((alpha_bits >> (3 * i)) & 0x7);
        const uint32_t y = static_cast<uint32_t>(i / 4);
        const uint32_t x = static_cast<uint32_t>(i % 4);
        uint32_t& pixel = output[y * stride + x];
        pixel = (pixel & 0x00FFFFFF) | (static_cast<uint32_t>(alpha_lut[sel]) << 24);
    }
}

bool BLPLoader::DecodePalettized(const BLPHeader& header, const uint8_t* data,
                                 size_t size, DecodedTexture& out) {
    out.format = DecodedTexture::Format::RGBA8;

    const uint32_t mip_count = CountMipLevels(header);
    if (mip_count == 0) return false;

    out.mip_levels = mip_count;
    out.mips.resize(mip_count);

    uint32_t w = header.width;
    uint32_t h = header.height;

    for (uint32_t m = 0; m < mip_count; ++m) {
        const uint32_t mip_off = header.mip_offsets[m];
        const uint32_t mip_sz = header.mip_sizes[m];

        if (static_cast<size_t>(mip_off) + mip_sz > size) return false;

        const uint8_t* mip_data = data + mip_off;
        const size_t pixel_count = static_cast<size_t>(w) * h;

        if (mip_sz < pixel_count) return false;

        auto& mip = out.mips[m];
        mip.width = w;
        mip.height = h;
        mip.data.resize(pixel_count * 4);

        const uint8_t* indices = mip_data;
        const uint8_t* alpha_ptr = mip_data + pixel_count;
        const size_t alpha_bytes = mip_sz - pixel_count;

        for (size_t i = 0; i < pixel_count; ++i) {
            const uint32_t pal = header.palette[indices[i]];

            const uint8_t b_val = static_cast<uint8_t>(pal & 0xFF);
            const uint8_t g_val = static_cast<uint8_t>((pal >> 8) & 0xFF);
            const uint8_t r_val = static_cast<uint8_t>((pal >> 16) & 0xFF);

            mip.data[4 * i + 0] = r_val;
            mip.data[4 * i + 1] = g_val;
            mip.data[4 * i + 2] = b_val;

            uint8_t alpha = 255;
            if (header.alpha_depth == 0) {
                alpha = 255;
            } else if (header.alpha_depth == 8) {
                if (alpha_bytes <= i) return false;
                alpha = alpha_ptr[i];
            } else if (header.alpha_depth == 4) {
                const size_t byte_idx = i / 2;
                if (alpha_bytes <= byte_idx) return false;
                const uint8_t nibble = (i % 2 == 0)
                    ? (alpha_ptr[byte_idx] & 0x0F)
                    : ((alpha_ptr[byte_idx] >> 4) & 0x0F);
                alpha = static_cast<uint8_t>((nibble << 4) | nibble);
            } else if (header.alpha_depth == 1) {
                const size_t byte_idx = i / 8;
                if (alpha_bytes <= byte_idx) return false;
                const uint8_t bit = static_cast<uint8_t>(
                    (alpha_ptr[byte_idx] >> (i % 8)) & 0x1);
                alpha = bit ? 255 : 0;
            }
            mip.data[4 * i + 3] = alpha;
        }

        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return true;
}

bool BLPLoader::DecodeDXT(const BLPHeader& header, const uint8_t* data,
                          size_t size, DecodedTexture& out) {
    out.format = DetermineFormat(header);

    const uint32_t mip_count = CountMipLevels(header);
    if (mip_count == 0) return false;

    const size_t block_size = DXTBlockSize(out.format);
    if (block_size == 0) return false;

    out.mip_levels = mip_count;
    out.mips.resize(mip_count);

    uint32_t w = header.width;
    uint32_t h = header.height;

    for (uint32_t m = 0; m < mip_count; ++m) {
        const uint32_t mip_off = header.mip_offsets[m];
        const uint32_t mip_sz = header.mip_sizes[m];

        if (static_cast<size_t>(mip_off) + mip_sz > size) return false;

        const uint32_t blocks_w = std::max(1u, (w + 3) / 4);
        const uint32_t blocks_h = std::max(1u, (h + 3) / 4);
        const size_t expected = static_cast<size_t>(blocks_w) * blocks_h * block_size;
        const size_t stored_size = std::min<size_t>(mip_sz, expected);
        if (stored_size == 0) return false;

        auto& mip = out.mips[m];
        mip.width = w;
        mip.height = h;

        mip.data.assign(data + mip_off, data + mip_off + stored_size);

        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return true;
}

bool BLPLoader::DecodeUncompressed(const BLPHeader& header, const uint8_t* data,
                                   size_t size, DecodedTexture& out) {
    out.format = DecodedTexture::Format::RGBA8;

    const uint32_t mip_count = CountMipLevels(header);
    if (mip_count == 0) return false;

    out.mip_levels = mip_count;
    out.mips.resize(mip_count);

    uint32_t w = header.width;
    uint32_t h = header.height;

    for (uint32_t m = 0; m < mip_count; ++m) {
        const uint32_t mip_off = header.mip_offsets[m];
        const uint32_t mip_sz = header.mip_sizes[m];

        if (static_cast<size_t>(mip_off) + mip_sz > size) return false;

        const size_t pixel_count = static_cast<size_t>(w) * h;
        const size_t expected = pixel_count * 4;
        if (mip_sz < expected) return false;

        auto& mip = out.mips[m];
        mip.width = w;
        mip.height = h;
        mip.data.resize(expected);

        const uint8_t* src = data + mip_off;
        for (size_t i = 0; i < pixel_count; ++i) {
            mip.data[4 * i + 0] = src[4 * i + 2];
            mip.data[4 * i + 1] = src[4 * i + 1];
            mip.data[4 * i + 2] = src[4 * i + 0];
            mip.data[4 * i + 3] = src[4 * i + 3];
        }

        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return true;
}

std::optional<DecodedTexture> BLPLoader::Load(const uint8_t* data, size_t size) {
    if (size < sizeof(BLPHeader)) return std::nullopt;

    BLPHeader header{};
    std::memcpy(&header, data, sizeof(BLPHeader));

    if (std::memcmp(&header.magic, "BLP2", 4) != 0) return std::nullopt;

    if (header.type != 1) return std::nullopt;

    if (header.width == 0 || header.height == 0) return std::nullopt;

    DecodedTexture out;
    out.width = header.width;
    out.height = header.height;

    bool ok = false;
    switch (header.compression) {
        case 1:
            ok = DecodePalettized(header, data, size, out);
            break;
        case 2:
            if (header.alpha_type == 8) {
                ok = DecodeUncompressed(header, data, size, out);
            } else {
                ok = DecodeDXT(header, data, size, out);
            }
            break;
        case 3:
            ok = DecodeUncompressed(header, data, size, out);
            break;
        default:
            return std::nullopt;
    }

    if (!ok) return std::nullopt;
    return out;
}

std::optional<DecodedTexture> BLPLoader::LoadFromVFS(
    const openwow::vfs::VirtualFileSystem& vfs, const std::string& path) {
    const auto bytes = vfs.ReadFileBytes(path);
    if (!bytes.has_value()) return std::nullopt;
    return Load(bytes->data(), bytes->size());
}

DecodedTexture BLPLoader::DecompressToRGBA8(const DecodedTexture& tex) {
    if (tex.format == DecodedTexture::Format::RGBA8) return tex;

    const size_t block_size = DXTBlockSize(tex.format);
    if (block_size == 0) return tex;

    DecodedTexture out;
    out.width = tex.width;
    out.height = tex.height;
    out.mip_levels = tex.mip_levels;
    out.format = DecodedTexture::Format::RGBA8;
    out.mips.resize(tex.mips.size());

    for (size_t m = 0; m < tex.mips.size(); ++m) {
        const auto& src_mip = tex.mips[m];
        auto& dst_mip = out.mips[m];
        dst_mip.width = src_mip.width;
        dst_mip.height = src_mip.height;

        const uint32_t w = src_mip.width;
        const uint32_t h = src_mip.height;
        std::vector<uint32_t> pixels(static_cast<size_t>(w) * h, 0);

        const uint32_t blocks_w = std::max(1u, (w + 3) / 4);
        const uint32_t blocks_h = std::max(1u, (h + 3) / 4);

        for (uint32_t by = 0; by < blocks_h; ++by) {
            for (uint32_t bx = 0; bx < blocks_w; ++bx) {
                const size_t block_idx = static_cast<size_t>(by) * blocks_w + bx;
                const size_t block_offset = block_idx * block_size;
                if (block_offset + block_size > src_mip.data.size()) {
                    continue;
                }
                const uint8_t* block = src_mip.data.data() + block_offset;

                uint32_t tile[16] = {};
                switch (tex.format) {
                    case DecodedTexture::Format::DXT1:
                        DecompressDXT1Block(block, tile, 4);
                        break;
                    case DecodedTexture::Format::DXT3:
                        DecompressDXT3Block(block, tile, 4);
                        break;
                    case DecodedTexture::Format::DXT5:
                        DecompressDXT5Block(block, tile, 4);
                        break;
                    default:
                        break;
                }

                for (uint32_t py = 0; py < 4; ++py) {
                    for (uint32_t px = 0; px < 4; ++px) {
                        const uint32_t x = bx * 4 + px;
                        const uint32_t y = by * 4 + py;
                        if (x < w && y < h) {
                            pixels[y * w + x] = tile[py * 4 + px];
                        }
                    }
                }
            }
        }

        dst_mip.data.resize(pixels.size() * 4);
        std::memcpy(dst_mip.data.data(), pixels.data(), dst_mip.data.size());
    }

    return out;
}

}

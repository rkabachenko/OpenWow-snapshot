#include "openwow/core/md5.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace openwow::core {
namespace {

constexpr std::array<std::uint32_t, 64> kMd5RoundConstants = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

constexpr std::array<std::uint32_t, 64> kMd5RotationAmounts = {
    7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,
    5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u,
    4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,
    6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u,
};

constexpr std::array<std::uint8_t, 64> kMd5Padding = {0x80u};

std::uint32_t RotateLeft(const std::uint32_t value,
                         const std::uint32_t shift) {
  return (value << shift) | (value >> (32u - shift));
}

std::uint32_t ReadLe32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

void WriteLe32(std::uint8_t* bytes, const std::uint32_t value) {
  bytes[0] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
  bytes[2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
  bytes[3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

void MD5_Transform(std::array<std::uint32_t, 4>& state,
                   const std::uint8_t block[64]) {
  std::array<std::uint32_t, 16> words{};
  for (std::size_t index = 0; index < words.size(); ++index) {
    words[index] = ReadLe32(block + index * sizeof(std::uint32_t));
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];

  for (std::uint32_t round = 0; round < 64; ++round) {
    std::uint32_t f = 0;
    std::uint32_t g = 0;
    if (round < 16) {
      f = (b & c) | (~b & d);
      g = round;
    } else if (round < 32) {
      f = (d & b) | (~d & c);
      g = (5u * round + 1u) & 0x0Fu;
    } else if (round < 48) {
      f = b ^ c ^ d;
      g = (3u * round + 5u) & 0x0Fu;
    } else {
      f = c ^ (b | ~d);
      g = (7u * round) & 0x0Fu;
    }

    const std::uint32_t next = d;
    d = c;
    c = b;
    b += RotateLeft(a + f + kMd5RoundConstants[round] + words[g],
                    kMd5RotationAmounts[round]);
    a = next;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

void MD5_AddBytes(MD5Context* ctx, const std::uint32_t size) {
  const std::uint32_t added_bits = size << 3;
  ctx->bit_count[0] += added_bits;
  ctx->bit_count[1] += size >> 29;
  if (ctx->bit_count[0] < added_bits) {
    ++ctx->bit_count[1];
  }
}

void MD5_UpdateChunk(MD5Context* ctx, const std::uint8_t* data,
                     const std::uint32_t size) {
  std::uint32_t buffered_bytes = (ctx->bit_count[0] >> 3) & 0x3Fu;
  MD5_AddBytes(ctx, size);

  std::uint32_t remaining = size;
  if (buffered_bytes != 0) {
    std::uint32_t prefix = remaining;
    if (buffered_bytes + prefix > 64u) {
      prefix = 64u - buffered_bytes;
    }

    std::memmove(ctx->buffer.data() + buffered_bytes, data, prefix);
    data += prefix;
    remaining -= prefix;

    if (buffered_bytes + prefix < 64u) {
      return;
    }

    MD5_Transform(ctx->state, ctx->buffer.data());
  }

  while (remaining >= 64u) {
    MD5_Transform(ctx->state, data);
    data += 64u;
    remaining -= 64u;
  }

  if (remaining != 0u) {
    std::memmove(ctx->buffer.data(), data, remaining);
  }
}

}

void MD5_Init(MD5Context* ctx) {
  ctx->bit_count = {0u, 0u};
  ctx->state = {
      0x67452301u,
      0xefcdab89u,
      0x98badcfeu,
      0x10325476u,
  };
  ctx->buffer.fill(0u);
}

void MD5_Update(MD5Context* ctx, const void* data, std::size_t size) {
  auto* cursor = static_cast<const std::uint8_t*>(data);
  while (size != 0u) {
    const std::size_t chunk_size = std::min<std::size_t>(
        size, std::numeric_limits<std::uint32_t>::max());
    MD5_UpdateChunk(ctx, cursor, static_cast<std::uint32_t>(chunk_size));
    cursor += chunk_size;
    size -= chunk_size;
  }
}

void MD5_Final(MD5Context* ctx, std::uint8_t digest[16]) {
  std::uint8_t bit_length[8];
  WriteLe32(bit_length, ctx->bit_count[0]);
  WriteLe32(bit_length + 4, ctx->bit_count[1]);

  const std::uint32_t buffered_bytes = (ctx->bit_count[0] >> 3) & 0x3Fu;
  const std::uint32_t padding_size =
      (buffered_bytes < 56u) ? (56u - buffered_bytes) : (120u - buffered_bytes);
  MD5_Update(ctx, kMd5Padding.data(), padding_size);
  MD5_Update(ctx, bit_length, sizeof(bit_length));

  for (std::size_t index = 0; index < ctx->state.size(); ++index) {
    WriteLe32(digest + index * sizeof(std::uint32_t), ctx->state[index]);
  }
}

std::array<std::uint8_t, 16> MD5_Digest(const void* data, std::size_t size) {
  MD5Context ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, data, size);

  std::array<std::uint8_t, 16> digest{};
  MD5_Final(&ctx, digest.data());
  return digest;
}

}

#include "openwow/foundation/hashing/retail_sha1.h"

#include <cstring>

namespace openwow::foundation::hashing {

std::uint32_t RotateLeft(const std::uint32_t value, const int amount) {
  return (value << amount) | (value >> (32 - amount));
}

std::uint32_t ReadBigEndian(const std::uint8_t* bytes) {
  return (std::uint32_t(bytes[0]) << 24u) |
         (std::uint32_t(bytes[1]) << 16u) |
         (std::uint32_t(bytes[2]) << 8u) | std::uint32_t(bytes[3]);
}

void WriteBigEndian(std::uint8_t* bytes, const std::uint32_t value) {
  bytes[0] = std::uint8_t(value >> 24u);
  bytes[1] = std::uint8_t(value >> 16u);
  bytes[2] = std::uint8_t(value >> 8u);
  bytes[3] = std::uint8_t(value);
}

void AddBytes(RetailSha1State& state, const std::uint32_t size) {
  const std::uint32_t previous = state.bit_count_low;
  state.bit_count_low += size << 3u;
  if (state.bit_count_low < previous) ++state.bit_count_high;
  state.bit_count_high += size >> 29u;
}

void InitializeRetailSha1(RetailSha1State& state) {
  state = {};
  state.digest[0] = 0x67452301u;
  state.digest[1] = 0xEFCDAB89u;
  state.digest[2] = 0x98BADCFEu;
  state.digest[3] = 0x10325476u;
  state.digest[4] = 0xC3D2E1F0u;
}

void TransformRetailSha1Block(std::uint32_t state[5],
                              const std::uint8_t block[64]) {
    std::uint32_t W[80];

    for (int i = 0; i < 16; ++i)
        W[i] = ReadBigEndian(block + 4 * i);
    for (int i = 16; i < 80; ++i)
        W[i] = RotateLeft(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        uint32_t temp = RotateLeft(a, 5) + f + e + k + W[i];
        e = d;
        d = c;
        c = RotateLeft(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void UpdateRetailSha1(RetailSha1State& state, const std::uint8_t* data,
                      std::uint32_t len) {
    uint32_t index = (state.bit_count_low >> 3) & 0x3F;
    AddBytes(state, len);

    if (index) {
        uint32_t partLen = 64 - index;
        if (len >= partLen) {
            std::memcpy(state.buffer + index, data, partLen);
            TransformRetailSha1Block(state.digest, state.buffer);
            data += partLen;
            len -= partLen;
            index = 0;
        } else {
            std::memcpy(state.buffer + index, data, len);
            return;
        }
    }

    while (len >= 64) {
        TransformRetailSha1Block(state.digest, data);
        data += 64;
        len -= 64;
    }

    if (len > 0) {
        std::memcpy(state.buffer, data, len);
    }
}

constexpr std::uint8_t kSha1Padding[64] = {0x80};

void FinalizeRetailSha1(RetailSha1State& state, std::uint8_t digest[20]) {

    uint8_t bits[8];
    WriteBigEndian(bits, state.bit_count_high);
    WriteBigEndian(bits + 4, state.bit_count_low);

    uint32_t index = (state.bit_count_low >> 3) & 0x3F;
    uint32_t padLen = (index < 56) ? (56 - index) : (120 - index);
    UpdateRetailSha1(state, kSha1Padding, padLen);

    UpdateRetailSha1(state, bits, 8);

    for (int i = 0; i < 5; ++i) {
        WriteBigEndian(digest + 4 * i, state.digest[i]);
    }
}

void UpdateRetailSha1(RetailSha1State& state, const char* text) {
  UpdateRetailSha1(state, reinterpret_cast<const std::uint8_t*>(text),
                   static_cast<std::uint32_t>(std::strlen(text)));
}

}

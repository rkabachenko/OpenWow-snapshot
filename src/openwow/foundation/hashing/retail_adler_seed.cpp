#include "openwow/foundation/hashing/retail_adler_seed.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace openwow::foundation::hashing {
namespace {

constexpr std::uint32_t kPackingDivisor = 47u;
constexpr std::uint32_t kResidueField0Modulus = 53u;
constexpr std::uint32_t kResidueField1Modulus = 59u;
constexpr std::uint32_t kResidueField2Modulus = 61u;
constexpr std::uint32_t kFloatMantissaMask = 0x007FFFFFu;
constexpr std::uint32_t kFloatOneBits = 0x3F800000u;

constexpr std::array<std::uint8_t, 256> kAdlerSeedTable = {
    0x8E, 0x14, 0x27, 0x99, 0xFD, 0xAA, 0xC7, 0x08, 0xD5, 0xE6, 0x3E, 0x1F,
    0xF6, 0xBB, 0x55, 0xDA, 0x75, 0xA0, 0x4A, 0x6A, 0xE8, 0xBD, 0x97, 0xFF,
    0xDE, 0x9B, 0xBC, 0x9F, 0x81, 0x8A, 0xA1, 0x46, 0x6E, 0x0B, 0xE3, 0x63,
    0x76, 0x7A, 0x6C, 0x5D, 0x88, 0xD3, 0x69, 0xCA, 0xC3, 0x47, 0xB9, 0x25,
    0x83, 0xAB, 0xA2, 0x3F, 0xA6, 0x41, 0x7C, 0xBA, 0xE5, 0xAC, 0x95, 0x01,
    0x7E, 0xCF, 0x09, 0xC1, 0xD9, 0x62, 0x70, 0x71, 0x8D, 0xDB, 0x05, 0x02,
    0x24, 0x87, 0xEF, 0x54, 0xC6, 0xD4, 0x37, 0x30, 0xD0, 0x1B, 0xCB, 0x7B,
    0xB8, 0xE4, 0xD8, 0xEC, 0x49, 0xCE, 0xAD, 0xDC, 0x13, 0xA9, 0x94, 0xC4,
    0x8F, 0x39, 0xAE, 0x0D, 0x18, 0x52, 0xDD, 0x0E, 0x78, 0xFA, 0xF5, 0x85,
    0x58, 0xD2, 0xAF, 0x6D, 0xA4, 0xB2, 0x53, 0x3B, 0x51, 0xA5, 0x50, 0xBE,
    0xFC, 0x2D, 0xF4, 0x11, 0x48, 0x98, 0x16, 0xF1, 0x86, 0xDF, 0x3D, 0x66,
    0x5E, 0x44, 0x2E, 0x2F, 0x36, 0x07, 0x6B, 0x17, 0x8B, 0x29, 0x4C, 0xB6,
    0xE2, 0x89, 0x5F, 0xE7, 0xCD, 0xA7, 0x21, 0xE1, 0x4D, 0xC9, 0x65, 0xED,
    0xFE, 0xEE, 0x9C, 0x23, 0x33, 0x7D, 0xB7, 0x04, 0x9E, 0x9A, 0x2A, 0x40,
    0xB3, 0x10, 0x5B, 0xF3, 0x82, 0x77, 0x1C, 0x92, 0x20, 0x4E, 0x1E, 0x57,
    0x22, 0x72, 0x06, 0x8C, 0x67, 0x2C, 0x73, 0xFB, 0x59, 0xC2, 0x0A, 0xBF,
    0x79, 0x5C, 0xF9, 0x0C, 0x28, 0x1A, 0x12, 0x68, 0x74, 0x34, 0x19, 0x42,
    0xB1, 0xC0, 0x84, 0xF8, 0x38, 0xF0, 0x15, 0x9D, 0x60, 0xF2, 0x3A, 0x6F,
    0xB4, 0x90, 0xEB, 0x91, 0x1D, 0x7F, 0x35, 0x61, 0x5A, 0x32, 0x03, 0x56,
    0xA3, 0xC5, 0x2B, 0x93, 0x80, 0x0F, 0x4B, 0x43, 0xF7, 0xA8, 0xE0, 0x3C,
    0x96, 0xD1, 0x64, 0x26, 0xD7, 0x45, 0xCC, 0x4F, 0xC8, 0xB0, 0xE9, 0xB5,
    0x00, 0xD6, 0x31, 0xEA,
};

[[nodiscard]] constexpr std::uint32_t ReadSeedWord(
    const std::size_t offset) {
  return static_cast<std::uint32_t>(kAdlerSeedTable[offset]) |
         (static_cast<std::uint32_t>(kAdlerSeedTable[offset + 1u]) << 8u) |
         (static_cast<std::uint32_t>(kAdlerSeedTable[offset + 2u]) << 16u) |
         (static_cast<std::uint32_t>(kAdlerSeedTable[offset + 3u]) << 24u);
}

[[nodiscard]] constexpr int WrapSeedIndex(const std::uint8_t value,
                                          const int decrement,
                                          const int period) {
  const int decremented = static_cast<int>(value) - decrement;
  return decremented < 0 ? decremented + period : decremented;
}

}

AdlerSeedState MakeAdlerSeedState(const std::uint32_t seed) {
  const std::uint32_t quotient = seed / kPackingDivisor;
  return {
      seed,
      ((quotient + seed + 16u * quotient) << 26u) |
          ((seed % kResidueField0Modulus) << 18u) |
          ((seed % kResidueField1Modulus) << 10u) |
          (4u * (seed % kResidueField2Modulus)),
  };
}

std::uint32_t AdvanceAdlerSeed(AdlerSeedState& state) {
  const auto byte0 = static_cast<std::uint8_t>(state.packed);
  const auto byte1 = static_cast<std::uint8_t>(state.packed >> 8u);
  const auto byte2 = static_cast<std::uint8_t>(state.packed >> 16u);
  const auto byte3 = static_cast<std::uint8_t>(state.packed >> 24u);

  const int index2 = WrapSeedIndex(byte2, 12, 212);
  const int index3 = WrapSeedIndex(byte3, 4, 188);
  const int index1 = WrapSeedIndex(byte1, 24, 236);
  const int index0 = WrapSeedIndex(byte0, 28, 244);

  const std::uint32_t next =
      state.value +
      (std::rotl(ReadSeedWord(static_cast<std::size_t>(index3)), 1) ^
       ReadSeedWord(static_cast<std::size_t>(index0)) ^
       std::rotl(ReadSeedWord(static_cast<std::size_t>(index2)), 2) ^
       std::rotl(ReadSeedWord(static_cast<std::size_t>(index1)), 3));

  state.packed = static_cast<std::uint32_t>(index0) |
                 (static_cast<std::uint32_t>(index1) << 8u) |
                 (static_cast<std::uint32_t>(index2) << 16u) |
                 (static_cast<std::uint32_t>(index3) << 24u);
  state.value = next;
  return next;
}

float AdlerSeedNextUnitFloat(AdlerSeedState& state) {
  const std::uint32_t bits =
      (AdvanceAdlerSeed(state) & kFloatMantissaMask) | kFloatOneBits;
  return std::bit_cast<float>(bits) - 1.0f;
}

float AdlerSeedNextSignedUnitFloat(AdlerSeedState& state) {
  const std::uint32_t next = AdvanceAdlerSeed(state);
  const std::uint32_t bits = (next & kFloatMantissaMask) | kFloatOneBits;
  const float sample = std::bit_cast<float>(bits);
  return static_cast<std::int32_t>(next) >= 0 ? sample - 2.0f
                                               : 2.0f - sample;
}

float AdlerSeedNextRangeFloat(const float lower_bound,
                              const float upper_bound,
                              AdlerSeedState& state) {
  return lower_bound +
         AdlerSeedNextUnitFloat(state) * (upper_bound - lower_bound);
}

std::uint32_t AdlerSeedNextBoundedValue(const std::uint32_t upper_bound,
                                        AdlerSeedState& state) {
  return static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(upper_bound) * AdvanceAdlerSeed(state)) >>
      32u);
}

AdlerSeedUnitCircleDirection
AdlerSeedNextUnitCircleDirection(AdlerSeedState& state) {

  constexpr float kTwoPi = 6.2831855f;
  const float angle = AdlerSeedNextUnitFloat(state) * kTwoPi;
  return {std::cos(angle), std::sin(angle)};
}

}

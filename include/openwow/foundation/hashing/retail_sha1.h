#pragma once

#include <cstdint>

namespace openwow::foundation::hashing {

struct RetailSha1State {
  std::uint32_t bit_count_low{};
  std::uint32_t bit_count_high{};
  std::uint32_t digest[5]{};
  std::uint8_t buffer[64]{};
};
static_assert(sizeof(RetailSha1State) == 92);

void TransformRetailSha1Block(std::uint32_t digest[5],
                              const std::uint8_t block[64]);

void InitializeRetailSha1(RetailSha1State& state);
void UpdateRetailSha1(RetailSha1State& state, const std::uint8_t* data,
                      std::uint32_t size);
void UpdateRetailSha1(RetailSha1State& state, const char* text);
void FinalizeRetailSha1(RetailSha1State& state, std::uint8_t digest[20]);

}

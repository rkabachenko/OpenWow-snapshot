#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace openwow::core {

struct MD5Context {
  std::array<std::uint32_t, 2> bit_count{};
  std::array<std::uint32_t, 4> state{};
  std::array<std::uint8_t, 64> buffer{};
};
static_assert(sizeof(MD5Context) == 88);

void MD5_Init(MD5Context* ctx);
void MD5_Update(MD5Context* ctx, const void* data, std::size_t size);
void MD5_Final(MD5Context* ctx, std::uint8_t digest[16]);
std::array<std::uint8_t, 16> MD5_Digest(const void* data, std::size_t size);

}

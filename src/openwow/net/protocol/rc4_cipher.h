#pragma once

#include "openwow/core/storm_error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace openwow::net {

struct RC4State {
  std::array<std::uint8_t, 256> S{};
  std::uint8_t i{0};
  std::uint8_t j{0};

  void Init(const std::uint8_t* key, const std::size_t key_len) {
    for (std::size_t index = 0; index < S.size(); ++index) {
      S[index] = static_cast<std::uint8_t>(index);
    }

    std::uint8_t j_value = 0;
    for (std::size_t index = 0; index < S.size(); ++index) {
      j_value = static_cast<std::uint8_t>(j_value + S[index] +
                                          key[index % key_len]);
      std::swap(S[index], S[static_cast<std::size_t>(j_value)]);
    }

    i = 0;
    j = 0;
  }

  void Process(std::uint8_t* data, const std::size_t len) {
    for (std::size_t index = 0; index < len; ++index) {
      i = static_cast<std::uint8_t>(i + 1);
      j = static_cast<std::uint8_t>(j + S[static_cast<std::size_t>(i)]);
      std::swap(S[static_cast<std::size_t>(i)],
                S[static_cast<std::size_t>(j)]);
      data[index] ^= S[static_cast<std::uint8_t>(
          S[static_cast<std::size_t>(i)] + S[static_cast<std::size_t>(j)])];
    }
  }

  void Drop(const std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
      i = static_cast<std::uint8_t>(i + 1);
      j = static_cast<std::uint8_t>(j + S[static_cast<std::size_t>(i)]);
      std::swap(S[static_cast<std::size_t>(i)],
                S[static_cast<std::size_t>(j)]);
    }
  }

  void Reset() {
    S.fill(0);
    i = 0;
    j = 0;
  }
};

static_assert(sizeof(RC4State) == 258,
              "RC4State must match the IDA 258-byte layout");

inline void RC4_Init(const std::uint8_t* key, const std::uint32_t key_len,
                     RC4State& state) {
  if (key == nullptr || key_len == 0) {
    openwow::core::SErrSetLastError(87);
    return;
  }

  state.Init(key, key_len);
}

inline RC4State* RC4_Process(std::uint8_t* data, const std::uint32_t data_len,
                             const RC4State& source_state,
                             RC4State& target_state) {
  if (&source_state != &target_state) {
    target_state = source_state;
  }

  target_state.Process(data, data_len);
  return &target_state;
}

inline RC4State* RC4_Process(std::uint8_t* data, const std::uint32_t data_len,
                             RC4State& state) {
  return RC4_Process(data, data_len, state, state);
}

}

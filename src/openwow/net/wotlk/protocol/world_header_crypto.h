#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::net::wotlk {

inline constexpr std::array<std::uint8_t, 16> kWorldHeaderRecvSeed = {
    0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA,
    0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57,
};

inline constexpr std::array<std::uint8_t, 16> kWorldHeaderSendSeed = {
    0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5,
    0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE,
};

std::array<std::uint8_t, 20> ComputeSha1PadHmac(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> first_chunk,
    std::span<const std::uint8_t> second_chunk = {});

std::array<std::uint8_t, 20> DeriveWorldHeaderKey(
    std::span<const std::uint8_t, 16> seed,
    const std::uint8_t* session_key,
    std::size_t session_key_len);

struct WorldHeaderKeyPair {
  std::array<std::uint8_t, 20> send;
  std::array<std::uint8_t, 20> receive;
};

WorldHeaderKeyPair DeriveWorldHeaderKeys(
    const std::uint8_t* session_key,
    std::size_t session_key_len);
WorldHeaderKeyPair DeriveWorldHeaderKeys(
    const std::uint8_t* session_key,
    std::size_t session_key_len,
    std::span<const std::uint8_t, 32> redirect_challenge);

}

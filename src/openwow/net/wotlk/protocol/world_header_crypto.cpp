#include "openwow/net/wotlk/protocol/world_header_crypto.h"

#include "openwow/foundation/hashing/retail_sha1.h"

#include <algorithm>
#include <array>

namespace openwow::net::wotlk {
namespace {

constexpr std::uint32_t kHmacBlockSize = 64u;

struct WorldHeaderHmacContext {
  foundation::hashing::RetailSha1State inner_hash{};
  std::array<std::uint8_t, kHmacBlockSize> inner_pad{};
  std::array<std::uint8_t, kHmacBlockSize> outer_pad{};
};

}

std::array<std::uint8_t, 20> ComputeSha1PadHmac(
    const std::span<const std::uint8_t> key,
    const std::span<const std::uint8_t> first_chunk,
    const std::span<const std::uint8_t> second_chunk) {
  WorldHeaderHmacContext ctx{};
  ctx.inner_pad.fill(0x36u);
  ctx.outer_pad.fill(0x5Cu);

  const auto key_bytes = std::min<std::size_t>(key.size(), kHmacBlockSize);
  for (std::size_t index = 0; index < key_bytes; ++index) {
    ctx.inner_pad[index] ^= key[index];
    ctx.outer_pad[index] ^= key[index];
  }

  foundation::hashing::InitializeRetailSha1(ctx.inner_hash);
  foundation::hashing::UpdateRetailSha1(ctx.inner_hash, ctx.inner_pad.data(),
                               kHmacBlockSize);
  foundation::hashing::UpdateRetailSha1(ctx.inner_hash, first_chunk.data(),
      static_cast<std::uint32_t>(first_chunk.size()));
  if (!second_chunk.empty()) {
    foundation::hashing::UpdateRetailSha1(ctx.inner_hash, second_chunk.data(),
        static_cast<std::uint32_t>(second_chunk.size()));
  }

  std::array<std::uint8_t, 20> inner_digest{};
  foundation::hashing::FinalizeRetailSha1(ctx.inner_hash, inner_digest.data());

  foundation::hashing::RetailSha1State outer_hash{};
  foundation::hashing::InitializeRetailSha1(outer_hash);
  foundation::hashing::UpdateRetailSha1(outer_hash, ctx.outer_pad.data(),
                               kHmacBlockSize);
  foundation::hashing::UpdateRetailSha1(outer_hash, inner_digest.data(),
                               static_cast<std::uint32_t>(inner_digest.size()));

  std::array<std::uint8_t, 20> digest{};
  foundation::hashing::FinalizeRetailSha1(outer_hash, digest.data());
  return digest;
}

std::array<std::uint8_t, 20> DeriveWorldHeaderKey(
    std::span<const std::uint8_t, 16> seed,
    const std::uint8_t* session_key,
    std::size_t session_key_len) {
  return ComputeSha1PadHmac(
      seed, std::span<const std::uint8_t>(session_key, session_key_len));
}

WorldHeaderKeyPair DeriveWorldHeaderKeys(
    const std::uint8_t* session_key,
    const std::size_t session_key_len) {
  return {
      .send = DeriveWorldHeaderKey(kWorldHeaderSendSeed, session_key,
                                   session_key_len),
      .receive = DeriveWorldHeaderKey(kWorldHeaderRecvSeed, session_key,
                                      session_key_len),
  };
}

WorldHeaderKeyPair DeriveWorldHeaderKeys(
    const std::uint8_t* session_key,
    const std::size_t session_key_len,
    const std::span<const std::uint8_t, 32> redirect_challenge) {
  const std::span<const std::uint8_t, 16> receive_seed(
      redirect_challenge.data(), 16);
  const std::span<const std::uint8_t, 16> send_seed(
      redirect_challenge.data() + 16, 16);
  return {
      .send = DeriveWorldHeaderKey(send_seed, session_key, session_key_len),
      .receive =
          DeriveWorldHeaderKey(receive_seed, session_key, session_key_len),
  };
}

}

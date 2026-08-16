#include "openwow/net/wotlk/proof_of_work.h"

#include "openwow/foundation/hashing/retail_sha1.h"

#include <openssl/sha.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <cstring>

namespace openwow::net::wotlk {
namespace {

std::uint32_t ReadU32Le(const std::span<const std::uint8_t> bytes,
                        const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::array<std::uint8_t, 4> U32Le(const std::uint32_t value) {
  return {
      static_cast<std::uint8_t>(value),
      static_cast<std::uint8_t>(value >> 8U),
      static_cast<std::uint8_t>(value >> 16U),
      static_cast<std::uint8_t>(value >> 24U),
  };
}

void UpdateU32Le(SHA_CTX& sha, const std::uint32_t value) {
  const auto bytes = U32Le(value);
  SHA1_Update(&sha, bytes.data(), bytes.size());
}

void UpdateU64Le(SHA_CTX& sha, const std::uint64_t value) {
  UpdateU32Le(sha, static_cast<std::uint32_t>(value));
  UpdateU32Le(sha, static_cast<std::uint32_t>(value >> 32U));
}

}

std::optional<RealmAuthChallenge> ParseRealmAuthChallenge(
    const std::span<const std::uint8_t> payload) {
  if (payload.size() < RealmAuthChallenge::kWireSize) {
    return std::nullopt;
  }

  RealmAuthChallenge challenge;
  challenge.proof_of_work_difficulty = ReadU32Le(payload, 0);
  challenge.auth_seed = ReadU32Le(payload, 4);
  for (std::size_t i = 0; i < challenge.proof_of_work_words.size(); ++i) {
    challenge.proof_of_work_words[i] = ReadU32Le(payload, 8 + i * 4);
  }
  return challenge;
}

std::uint8_t CountRealmProofOfWorkZeroBits(
    const std::span<const std::uint8_t, 20> digest) {
  std::uint8_t bits = 0;
  std::size_t pos = 0;
  while (pos < digest.size() && digest[pos] == 0) {
    bits = static_cast<std::uint8_t>(bits + 8);
    ++pos;
  }
  if (pos == digest.size()) {
    return bits;
  }

  std::uint8_t value = digest[pos];
  while ((value & 1U) == 0U) {
    ++bits;
    value >>= 1U;
  }
  return bits;
}

std::array<std::uint8_t, 20> ComputeRealmProofOfWorkDigest(
    const std::string_view account_name,
    const std::array<std::uint32_t, 8>& challenge_words,
    const std::uint64_t nonce) {
  SHA_CTX sha;
  SHA1_Init(&sha);
  SHA1_Update(&sha, account_name.data(), account_name.size());
  for (const std::uint32_t word : challenge_words) {
    UpdateU32Le(sha, word);
  }
  UpdateU64Le(sha, nonce);

  std::array<std::uint8_t, 20> digest{};
  SHA1_Final(digest.data(), &sha);
  return digest;
}

bool VerifyRealmProofOfWork(const std::string_view account_name,
                            const RealmAuthChallenge& challenge,
                            const std::uint64_t nonce) {
  if (challenge.proof_of_work_difficulty > 160) {
    return false;
  }
  const auto digest = ComputeRealmProofOfWorkDigest(
      account_name, challenge.proof_of_work_words, nonce);
  return CountRealmProofOfWorkZeroBits(digest) >=
         challenge.proof_of_work_difficulty;
}

std::optional<std::uint64_t> SolveRealmProofOfWork(
    const std::string_view account_name,
    const RealmAuthChallenge& challenge) {
  if (challenge.proof_of_work_difficulty > 160) {
    return std::nullopt;
  }

  SHA_CTX prefix;
  SHA1_Init(&prefix);
  SHA1_Update(&prefix, account_name.data(), account_name.size());
  for (const std::uint32_t word : challenge.proof_of_work_words) {
    UpdateU32Le(prefix, word);
  }

  for (std::uint64_t nonce = 0;; ++nonce) {
    SHA_CTX candidate = prefix;
    UpdateU64Le(candidate, nonce);
    std::array<std::uint8_t, 20> digest{};
    SHA1_Final(digest.data(), &candidate);
    if (CountRealmProofOfWorkZeroBits(digest) >=
        challenge.proof_of_work_difficulty) {
      return nonce;
    }
    if (nonce == UINT64_MAX) {
      return std::nullopt;
    }
  }
}

std::array<std::uint8_t, 20> ComputeRealmRedirectionAuthDigest(
    const std::string_view account_name,
    const std::span<const std::uint8_t, 40> session_key,
    const std::uint32_t auth_seed) {
  foundation::hashing::RetailSha1State sha;
  foundation::hashing::InitializeRetailSha1(sha);
  foundation::hashing::UpdateRetailSha1(
      sha, reinterpret_cast<const std::uint8_t*>(account_name.data()),
      static_cast<std::uint32_t>(account_name.size()));
  foundation::hashing::UpdateRetailSha1(
      sha, session_key.data(), static_cast<std::uint32_t>(session_key.size()));
  const auto seed_bytes = U32Le(auth_seed);
  foundation::hashing::UpdateRetailSha1(
      sha, seed_bytes.data(), static_cast<std::uint32_t>(seed_bytes.size()));

  std::array<std::uint8_t, 20> digest{};
  foundation::hashing::FinalizeRetailSha1(sha, digest.data());
  return digest;
}

std::uint8_t CountLeadingZeroBits(const std::uint8_t digest[20]) {
  return CountRealmProofOfWorkZeroBits(
      std::span<const std::uint8_t, 20>(digest, 20));
}

bool ProofOfWork(const void* sha1_state,
                 const std::uint32_t difficulty,
                 std::uint64_t* const out_nonce) {
  if (sha1_state == nullptr || out_nonce == nullptr || difficulty > 160) {
    return false;
  }

  const auto& prefix = *static_cast<const SHA_CTX*>(sha1_state);
  for (std::uint64_t nonce = 0;; ++nonce) {
    SHA_CTX candidate = prefix;
    UpdateU64Le(candidate, nonce);
    std::array<std::uint8_t, 20> digest{};
    SHA1_Final(digest.data(), &candidate);
    if (CountRealmProofOfWorkZeroBits(digest) >= difficulty) {
      *out_nonce = nonce;
      return true;
    }
    if (nonce == UINT64_MAX) {
      return false;
    }
  }
}

}

#pragma GCC diagnostic pop

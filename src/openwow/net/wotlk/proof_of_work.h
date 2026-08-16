#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace openwow::net::wotlk {

struct RealmAuthChallenge {
  static constexpr std::size_t kWireSize = 40;

  std::uint32_t proof_of_work_difficulty{0};
  std::uint32_t auth_seed{0};
  std::array<std::uint32_t, 8> proof_of_work_words{};
};

[[nodiscard]] std::optional<RealmAuthChallenge> ParseRealmAuthChallenge(
    std::span<const std::uint8_t> payload);

[[nodiscard]] std::uint8_t CountRealmProofOfWorkZeroBits(
    std::span<const std::uint8_t, 20> digest);

[[nodiscard]] std::array<std::uint8_t, 20> ComputeRealmProofOfWorkDigest(
    std::string_view account_name,
    const std::array<std::uint32_t, 8>& challenge_words,
    std::uint64_t nonce);

[[nodiscard]] bool VerifyRealmProofOfWork(
    std::string_view account_name,
    const RealmAuthChallenge& challenge,
    std::uint64_t nonce);

[[nodiscard]] std::optional<std::uint64_t> SolveRealmProofOfWork(
    std::string_view account_name,
    const RealmAuthChallenge& challenge);

[[nodiscard]] std::array<std::uint8_t, 20>
ComputeRealmRedirectionAuthDigest(
    std::string_view account_name,
    std::span<const std::uint8_t, 40> session_key,
    std::uint32_t auth_seed);

bool ProofOfWork(const void* sha1_state,
                 std::uint32_t difficulty,
                 std::uint64_t* out_nonce);

std::uint8_t CountLeadingZeroBits(const std::uint8_t digest[20]);

}

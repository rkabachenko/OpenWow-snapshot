#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace openwow::net {

inline constexpr std::uint8_t kAuthLogonChallengeOpcode = 0x00;
inline constexpr std::uint8_t kAuthLogonChallengeProtocolVersion = 0x08;

[[nodiscard]] constexpr std::int32_t ResolveRetailLogonChallengeTimezoneBiasMinutes(
    const std::int32_t system_bias_minutes,
    const bool query_succeeded) {
  return query_succeeded ? -system_bias_minutes : 0;
}

[[nodiscard]] std::int32_t QueryRetailLogonChallengeTimezoneBiasMinutes();

[[nodiscard]] std::vector<std::uint8_t> BuildRetailLogonChallengePacket(
    std::string_view username);

}

#include "openwow/net/auth/logon_challenge_packet.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <ctime>
#endif

namespace openwow::net {
namespace {

constexpr std::uint8_t kMajorVersion = 3;
constexpr std::uint8_t kMinorVersion = 3;
constexpr std::uint8_t kBugfixVersion = 5;
constexpr std::uint16_t kBuildNumber = 12340;

constexpr std::uint32_t kLoopbackIpv4Address = 0x0100007Fu;

void WriteLE16(std::vector<std::uint8_t>& buffer, const std::uint16_t value) {
  buffer.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void WriteLE32(std::vector<std::uint8_t>& buffer, const std::uint32_t value) {
  buffer.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

[[nodiscard]] std::string ToUpperAscii(std::string_view value) {
  std::string uppercased(value);
  std::transform(uppercased.begin(), uppercased.end(), uppercased.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return uppercased;
}

}

std::int32_t QueryRetailLogonChallengeTimezoneBiasMinutes() {
#if defined(_WIN32)
  TIME_ZONE_INFORMATION time_zone_information{};
  const DWORD query_result = ::GetTimeZoneInformation(&time_zone_information);
  if (query_result == TIME_ZONE_ID_INVALID) {
    return ResolveRetailLogonChallengeTimezoneBiasMinutes(0, false);
  }

  return ResolveRetailLogonChallengeTimezoneBiasMinutes(
      static_cast<std::int32_t>(time_zone_information.Bias), true);
#else
  ::tzset();

  return ResolveRetailLogonChallengeTimezoneBiasMinutes(
      static_cast<std::int32_t>(::timezone / 60), true);
#endif
}

std::vector<std::uint8_t> BuildRetailLogonChallengePacket(
    const std::string_view username) {
  const std::string uppercase_username = ToUpperAscii(username);
  const auto username_length =
      static_cast<std::uint8_t>(std::min<std::size_t>(uppercase_username.size(), 255));
  const auto payload_size = static_cast<std::uint16_t>(30u + username_length);

  std::vector<std::uint8_t> packet;
  packet.reserve(4u + payload_size);

  packet.push_back(kAuthLogonChallengeOpcode);
  packet.push_back(kAuthLogonChallengeProtocolVersion);
  WriteLE16(packet, payload_size);

  packet.push_back('W');
  packet.push_back('o');
  packet.push_back('W');
  packet.push_back(0x00);

  packet.push_back(kMajorVersion);
  packet.push_back(kMinorVersion);
  packet.push_back(kBugfixVersion);
  WriteLE16(packet, kBuildNumber);

  packet.push_back('6');
  packet.push_back('8');
  packet.push_back('x');
  packet.push_back(0x00);

  packet.push_back('n');
  packet.push_back('i');
  packet.push_back('W');
  packet.push_back(0x00);

  packet.push_back('S');
  packet.push_back('U');
  packet.push_back('n');
  packet.push_back('e');

  WriteLE32(packet, static_cast<std::uint32_t>(
                        QueryRetailLogonChallengeTimezoneBiasMinutes()));
  WriteLE32(packet, kLoopbackIpv4Address);

  packet.push_back(username_length);
  packet.insert(packet.end(), uppercase_username.begin(),
                uppercase_username.begin() + username_length);

  return packet;
}

}

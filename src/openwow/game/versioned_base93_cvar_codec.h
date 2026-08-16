#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game::detail {

inline constexpr char kVersionedBase93FormatMarker = 'v';
inline constexpr unsigned char kVersionedBase93FormatVersion = 1;
inline constexpr std::size_t kVersionedBase93HeaderSize = 2;
inline constexpr std::size_t kVersionedBase93EncodedWidth = 3;
inline constexpr std::uint32_t kVersionedBase93Base = 93;
inline constexpr std::uint32_t kVersionedBase93DigitOffset = 35;
inline constexpr std::uint32_t kVersionedBase93MaxEncodableValue = 0xC4604;

inline constexpr std::size_t kVersionedBase93AppendLengthLimit = 0xFD;
inline constexpr std::size_t kVersionedBase93MaxValueCount = 84;

inline bool VersionedBase93NeedsCanonicalRewrite(
    const std::string_view value) {
  return value.empty() || value.front() != kVersionedBase93FormatMarker ||
         value.size() < kVersionedBase93HeaderSize || value[1] == '\0';
}

inline std::string_view GetVersionedBase93Payload(
    const std::string_view value) {
  if (!value.empty() && value.front() == kVersionedBase93FormatMarker) {
    if (value.size() <= kVersionedBase93HeaderSize) {
      return {};
    }
    return value.substr(kVersionedBase93HeaderSize);
  }

  return value;
}

inline void AppendVersionedBase93Value(std::string& encoded,
                                       const std::uint32_t value) {
  if (value > kVersionedBase93MaxEncodableValue ||
      encoded.size() >= kVersionedBase93AppendLengthLimit) {
    return;
  }

  const std::uint32_t high =
      value / (kVersionedBase93Base * kVersionedBase93Base);
  const std::uint32_t remainder =
      value % (kVersionedBase93Base * kVersionedBase93Base);
  const std::uint32_t middle = remainder / kVersionedBase93Base;
  const std::uint32_t low = remainder % kVersionedBase93Base;

  encoded.push_back(static_cast<char>(high + kVersionedBase93DigitOffset));
  encoded.push_back(static_cast<char>(middle + kVersionedBase93DigitOffset));
  encoded.push_back(static_cast<char>(low + kVersionedBase93DigitOffset));
}

inline std::string EncodeVersionedBase93Values(
    const std::span<const std::uint32_t> values) {
  std::string encoded;
  encoded.reserve(
      kVersionedBase93HeaderSize +
      std::min(values.size(), kVersionedBase93MaxValueCount) *
          kVersionedBase93EncodedWidth);
  encoded.push_back(kVersionedBase93FormatMarker);
  encoded.push_back(static_cast<char>(kVersionedBase93FormatVersion));
  for (const auto value : values) {
    AppendVersionedBase93Value(encoded, value);
  }
  return encoded;
}

inline std::vector<std::uint32_t> DecodeVersionedBase93Payload(
    const std::string_view payload) {
  std::vector<std::uint32_t> values;
  values.reserve(payload.size() / kVersionedBase93EncodedWidth);

  for (std::size_t offset = 0;
       offset + (kVersionedBase93EncodedWidth - 1) < payload.size();
       offset += kVersionedBase93EncodedWidth) {
    const auto first = static_cast<unsigned char>(payload[offset]);
    const auto second = static_cast<unsigned char>(payload[offset + 1]);
    const auto third = static_cast<unsigned char>(payload[offset + 2]);
    if (first == 0 || second == 0 || third == 0) {
      break;
    }

    values.push_back(
        kVersionedBase93Base *
            (kVersionedBase93Base * (first - kVersionedBase93DigitOffset) +
             (second - kVersionedBase93DigitOffset)) +
        (third - kVersionedBase93DigitOffset));
  }

  return values;
}

}

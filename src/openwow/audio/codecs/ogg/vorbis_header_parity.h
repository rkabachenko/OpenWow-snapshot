
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::audio {

[[nodiscard]] bool LocateVorbisSetupFramingBit(std::span<const std::uint8_t> logical_stream,
                                               std::size_t *byte_offset, std::uint8_t *bit_mask);

[[nodiscard]] bool ValidateVorbisHeaderParity(std::span<const std::uint8_t> logical_stream);

[[nodiscard]] bool SanitizeVorbisAudioPacketParity(std::span<const std::uint8_t> logical_stream,
                                                   std::vector<std::uint8_t> *sanitized_stream);

}

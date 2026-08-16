
#pragma once

#include <cstdint>
#include <vector>

namespace openwow::audio {

bool OggVorbis_DecodeToWAV(const std::uint8_t* ogg_data,
                           std::uint32_t ogg_size,
                           std::vector<std::uint8_t>& wav_out);

}

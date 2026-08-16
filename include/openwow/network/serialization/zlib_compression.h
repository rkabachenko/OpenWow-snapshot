#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::network::serialization {

enum class ZlibCompressionLevel : int {
  kDefault = -1,
  kBestSpeed = 1,
  kBestCompression = 9,
};

enum class ZlibResult : int {
  kOk = 0,
  kNeedDictionary = 2,
  kStreamError = -2,
  kDataError = -3,
  kMemoryError = -4,
  kBufferError = -5,
  kVersionError = -6,
};

[[nodiscard]] std::vector<std::uint8_t> CompressZlib(
    const std::uint8_t* data, std::size_t size,
    ZlibCompressionLevel level = ZlibCompressionLevel::kDefault);

[[nodiscard]] ZlibResult DecompressZlib(std::uint8_t* output,
                                        std::size_t* output_size,
                                        const std::uint8_t* input,
                                        std::size_t input_size);

[[nodiscard]] std::vector<std::uint8_t> DecompressZlib(
    const std::uint8_t* data, std::size_t size,
    std::size_t max_output_size);

}

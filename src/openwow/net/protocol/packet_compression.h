#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::net {

class PacketCompression {
 public:

  [[nodiscard]] static std::vector<std::uint8_t> Decompress(
      const std::uint8_t* data, std::size_t size,
      std::uint32_t decompressed_size);

  [[nodiscard]] static std::vector<std::uint8_t> Compress(
      const std::uint8_t* data, std::size_t size);

};

}

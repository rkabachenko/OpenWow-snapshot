#include "openwow/net/protocol/packet_compression.h"
#include "openwow/network/serialization/zlib_compression.h"

namespace openwow::net {

std::vector<std::uint8_t> PacketCompression::Decompress(
    const std::uint8_t* data, std::size_t size,
    std::uint32_t decompressed_size) {
  if (!data || size == 0 || decompressed_size == 0) {
    return {};
  }

  return network::serialization::DecompressZlib(data, size,
                                                 decompressed_size);
}

std::vector<std::uint8_t> PacketCompression::Compress(
    const std::uint8_t* data, std::size_t size) {
  return network::serialization::CompressZlib(data, size);
}

}

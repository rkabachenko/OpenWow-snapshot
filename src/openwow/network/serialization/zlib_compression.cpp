#include "openwow/network/serialization/zlib_compression.h"

#include <limits>
#include <zlib.h>

namespace openwow::network::serialization {

std::vector<std::uint8_t> CompressZlib(const std::uint8_t* data,
                                       const std::size_t size,
                                       const ZlibCompressionLevel level) {
  if (data == nullptr || size == 0 ||
      size > static_cast<std::size_t>(std::numeric_limits<uLong>::max())) {
    return {};
  }

  const auto source_size = static_cast<uLong>(size);
  uLongf compressed_size = compressBound(source_size);
  std::vector<std::uint8_t> output(compressed_size);

  const int rc = compress2(output.data(), &compressed_size, data, source_size,
                           static_cast<int>(level));
  if (rc != Z_OK) {
    return {};
  }

  output.resize(compressed_size);
  return output;
}

ZlibResult DecompressZlib(std::uint8_t* output, std::size_t* output_size,
                          const std::uint8_t* input,
                          const std::size_t input_size) {
  if (output == nullptr || output_size == nullptr || input == nullptr ||
      input_size > static_cast<std::size_t>(std::numeric_limits<uInt>::max()) ||
      *output_size > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
    return ZlibResult::kStreamError;
  }

  z_stream stream{};
  stream.next_in = const_cast<Bytef*>(input);
  stream.avail_in = static_cast<uInt>(input_size);
  stream.next_out = output;
  stream.avail_out = static_cast<uInt>(*output_size);
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;

  int rc = inflateInit(&stream);
  if (rc != Z_OK) {
    return static_cast<ZlibResult>(rc);
  }

  rc = inflate(&stream, Z_FINISH);
  if (rc == Z_STREAM_END) {
    *output_size = stream.total_out;
    return static_cast<ZlibResult>(inflateEnd(&stream));
  }

  inflateEnd(&stream);

  if (rc == Z_STREAM_ERROR ||
      (rc == Z_BUF_ERROR && stream.avail_in == 0)) {
    return ZlibResult::kDataError;
  }
  return static_cast<ZlibResult>(rc);
}

std::vector<std::uint8_t> DecompressZlib(const std::uint8_t* data,
                                         const std::size_t size,
                                         const std::size_t max_output_size) {
  if (data == nullptr || size == 0 || max_output_size == 0) {
    return {};
  }

  std::vector<std::uint8_t> output(max_output_size);
  std::size_t actual_size = max_output_size;
  const ZlibResult result =
      DecompressZlib(output.data(), &actual_size, data, size);
  if (result != ZlibResult::kOk) {
    return {};
  }

  output.resize(actual_size);
  return output;
}

}

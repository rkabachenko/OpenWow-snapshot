#pragma once

#include <cstdint>
#include <string>

namespace openwow::vfs {

struct StreamingSourceTransportLayout {
  std::uint32_t checksum_block_size = 0;
  std::uint32_t split_size = 0;
};

struct StreamingSourceRangeRequest {
  std::string source_url;
  std::uint64_t logical_file_size = 0;
  std::uint64_t logical_offset = 0;
  std::uint32_t logical_size = 0;
  std::uint32_t max_retry_count = 5;
  StreamingSourceTransportLayout transport{};
};

bool ReadStreamingSourceRangeByScheme(const StreamingSourceRangeRequest &request,
                                      void *destination);

}

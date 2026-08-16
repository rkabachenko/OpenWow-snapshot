#pragma once

#include "openwow/vfs/retail/streaming/streaming_source_transport.h"

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::vfs {

class RuntimeFile;

struct StreamingDirectRequest {
  std::string source_url;
  std::uint32_t max_retry_count = 5;
  StreamingSourceTransportLayout transport{};
};

std::optional<StreamingDirectRequest>
BuildStreamingDirectRequestForLookupKey(const std::string &lookup_key,
                                        const char *source_path);
bool TryOpenStreamingPartBackingHandle(void *callback_table, const char *logical_path,
                                       std::uint32_t open_flags, int *out_handle);
bool ReadStreamingPartBackingAtOffsetLocked(RuntimeFile &runtime_file, void *buffer,
                                            std::uint64_t offset,
                                            std::uint32_t *inout_bytes_to_read,
                                            bool require_exact);

}

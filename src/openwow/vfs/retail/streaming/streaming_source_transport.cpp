#include "openwow/vfs/retail/streaming/streaming_source_transport.h"

#include "openwow/core/streaming_storage.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/net/os_url_download.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace openwow::vfs {
namespace {

constexpr std::string_view kFilePrefix = "file://";
constexpr std::string_view kHttpPrefix = "http://";
constexpr std::chrono::milliseconds kRetrySleep(10);
constexpr std::uint32_t kChecksumTrailerBytes = 32;

enum class FailureReason : std::uint32_t {
  None,
  DownloadStartFailed,
  DownloadedSmallerThanExpected,
  DownloadFailed,
  Md5CheckFailed,
  CopiedSmallerThanExpected,
};

struct HttpSegment {
  std::string url;
  std::uint64_t storage_begin = 0;
  std::uint64_t storage_end = 0;
  std::uint32_t output_offset = 0;
  std::uint32_t output_size = 0;
  std::uint32_t data_offset = 0;
};

bool StartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         openwow::text::EqualsIgnoreCaseAscii(value.substr(0, prefix.size()), prefix);
}

const char *FailureText(FailureReason reason, std::uint32_t completion_code) {
  switch (reason) {
  case FailureReason::DownloadStartFailed:
    return "DownloadURL failed with no error";
  case FailureReason::DownloadedSmallerThanExpected:
    return "Downloaded smaller than expected";
  case FailureReason::DownloadFailed:
    if (completion_code == static_cast<std::uint32_t>(
                               openwow::net::OsUrlDownloadCompletionCode::kTimeout)) {
      return "DownloadURL failed - Timed out";
    }
    if (completion_code == static_cast<std::uint32_t>(
                               openwow::net::OsUrlDownloadCompletionCode::kNotFound)) {
      return "DownloadURL failed - File not found";
    }
    return "DownloadURL failed";
  case FailureReason::Md5CheckFailed:
    return "MD5 check failed";
  case FailureReason::CopiedSmallerThanExpected:
    return "Copied smaller than expected";
  case FailureReason::None:
  default:
    return "Unknown error";
  }
}

void PushFailure(const StreamingSourceRangeRequest &request, FailureReason reason,
                 std::uint32_t completion_code) {
  std::string message(FailureText(reason, completion_code));
  if (!request.source_url.empty()) {
    message += " - ";
    message += request.source_url;
  }
  openwow::data::PushStreamingStatusMessage(std::move(message), 21,
                                            static_cast<int>(completion_code));
}

bool ReadFileRange(const StreamingSourceRangeRequest &request, void *destination) {
  if (request.logical_size == 0) {
    return true;
  }
  const std::string_view url(request.source_url);
  if (!StartsWithIgnoreCase(url, kFilePrefix)) {
    return false;
  }
  std::ifstream stream(std::string(url.substr(kFilePrefix.size())), std::ios::binary);
  if (!stream) {
    return false;
  }
  stream.seekg(static_cast<std::streamoff>(request.logical_offset), std::ios::beg);
  if (!stream) {
    return false;
  }
  stream.read(static_cast<char *>(destination),
              static_cast<std::streamsize>(request.logical_size));
  return stream.good() ||
         stream.gcount() == static_cast<std::streamsize>(request.logical_size);
}

std::vector<HttpSegment> BuildHttpSegments(const StreamingSourceRangeRequest &request) {
  std::vector<HttpSegment> segments;
  if (request.logical_size == 0) {
    return segments;
  }
  if (request.transport.checksum_block_size == 0) {
    segments.push_back({request.source_url, request.logical_offset,
                        request.logical_offset + request.logical_size - 1u, 0,
                        request.logical_size, 0});
    return segments;
  }

  const auto range = openwow::core::StreamingStorage::MapLogicalRangeToChecksummedStorageRange(
      request.logical_offset, request.logical_offset + request.logical_size - 1u,
      request.transport.checksum_block_size, request.logical_file_size);
  if (request.transport.split_size == 0) {
    segments.push_back({request.source_url, range.storageBegin, range.storageEnd, 0,
                        request.logical_size, range.dataOffset});
    return segments;
  }

  const std::uint64_t split_storage_span =
      static_cast<std::uint64_t>(request.transport.split_size) +
      static_cast<std::uint64_t>(kChecksumTrailerBytes) *
          (request.transport.split_size / request.transport.checksum_block_size);
  if (split_storage_span == 0) {
    return segments;
  }
  const std::uint64_t first_part = range.storageBegin / split_storage_span;
  const std::uint64_t last_part = range.storageEnd / split_storage_span;
  const std::uint32_t block_span =
      request.transport.checksum_block_size + kChecksumTrailerBytes;
  std::uint32_t copied = 0;
  for (std::uint64_t part = first_part; part <= last_part; ++part) {
    const std::uint64_t part_storage_begin = part * split_storage_span;
    const std::uint64_t begin =
        part == first_part ? range.storageBegin - part_storage_begin : 0;
    const std::uint64_t end = std::min<std::uint64_t>(
        range.storageEnd - part_storage_begin, split_storage_span - 1u);
    const std::uint64_t storage_size = end - begin + 1u;
    const std::uint64_t checksum_blocks = (storage_size + block_span - 1u) / block_span;
    std::uint64_t raw_size = storage_size - checksum_blocks * kChecksumTrailerBytes;
    if (part == first_part) {
      raw_size -= range.dataOffset;
    }
    const auto output_size = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(request.logical_size - copied, raw_size));
    if (output_size == 0) {
      break;
    }
    segments.push_back({request.source_url + "." + std::to_string(part), begin, end, copied,
                        output_size, part == first_part ? range.dataOffset : 0u});
    copied += output_size;
  }
  return segments;
}

bool CopyHttpSegment(const StreamingSourceRangeRequest &request, const HttpSegment &segment,
                     const std::string &body, void *destination, FailureReason *failure) {
  auto *output = static_cast<std::byte *>(destination);
  if (request.transport.checksum_block_size == 0) {
    if (body.size() < segment.output_size) {
      *failure = FailureReason::CopiedSmallerThanExpected;
      return false;
    }
    std::memmove(output + segment.output_offset, body.data(), segment.output_size);
    return true;
  }
  std::string verified;
  if (!openwow::core::StreamingStorage::VerifyDownloadChecksumBlocks(
          body, request.transport.checksum_block_size, verified)) {
    *failure = FailureReason::Md5CheckFailed;
    return false;
  }
  if (verified.size() < static_cast<std::size_t>(segment.data_offset + segment.output_size)) {
    *failure = FailureReason::CopiedSmallerThanExpected;
    return false;
  }
  std::memmove(output + segment.output_offset, verified.data() + segment.data_offset,
               segment.output_size);
  return true;
}

bool ReadHttpRange(const StreamingSourceRangeRequest &request, void *destination) {
  if (request.logical_size == 0) {
    return true;
  }
  std::uint32_t completion_code = 0;
  FailureReason failure = FailureReason::DownloadStartFailed;
  for (const auto &segment : BuildHttpSegments(request)) {
    const auto expected = static_cast<std::uint32_t>(segment.storage_end - segment.storage_begin + 1u);
    openwow::net::UrlDownloadRangeBuffer buffer(expected);
    buffer.SetInclusiveByteWindowParts(static_cast<std::uint32_t>(segment.storage_begin),
                                       static_cast<std::uint32_t>(segment.storage_begin >> 32),
                                       static_cast<std::uint32_t>(segment.storage_end),
                                       static_cast<std::uint32_t>(segment.storage_end >> 32));
    bool segment_ok = false;
    for (std::uint32_t attempt = 0; attempt < request.max_retry_count; ++attempt) {
      if (attempt != 0) {
        openwow::data::Streaming_RecordDownloadRetry();
      }
      buffer.ResetForDownload(expected);
      completion_code = 0;
      if (!openwow::net::OsURLDownload_Start(segment.url.c_str(),
              &openwow::net::UrlDownloadRangeBuffer::Callback, &buffer,
              static_cast<int>(buffer.timeout_ms()))) {
        failure = FailureReason::DownloadStartFailed;
        std::this_thread::sleep_for(kRetrySleep);
        continue;
      }
      buffer.WaitForCompletion();
      completion_code = buffer.completion_code();
      if (!buffer.success()) {
        failure = FailureReason::DownloadFailed;
        if (completion_code == static_cast<std::uint32_t>(
                                   openwow::net::OsUrlDownloadCompletionCode::kTimeout)) {
          buffer.SetTimeoutMs(buffer.timeout_ms() * 2u);
        }
        std::this_thread::sleep_for(kRetrySleep);
        continue;
      }
      if (buffer.body().size() != expected) {
        failure = FailureReason::DownloadedSmallerThanExpected;
        continue;
      }
      segment_ok = CopyHttpSegment(request, segment, buffer.body(), destination, &failure);
      if (segment_ok) {
        break;
      }
    }
    if (!segment_ok) {
      PushFailure(request, failure, completion_code);
      return false;
    }
  }
  return true;
}

}

bool ReadStreamingSourceRangeByScheme(const StreamingSourceRangeRequest &request,
                                      void *destination) {
  if (request.logical_size != 0 && destination == nullptr) {
    return false;
  }
  const std::string_view url(request.source_url);
  if (StartsWithIgnoreCase(url, kFilePrefix)) {
    return ReadFileRange(request, destination);
  }
  if (StartsWithIgnoreCase(url, kHttpPrefix)) {
    return ReadHttpRange(request, destination);
  }
  return false;
}

}

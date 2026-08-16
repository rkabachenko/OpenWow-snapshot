#pragma once

#include <cstdint>
#include <string>

namespace openwow::vfs {

struct SFileHandle {
  std::int32_t type{0};
  std::int32_t field_04{0};
  std::int32_t field_08{0};
  std::int32_t filename_intern{0};
  std::int32_t field_10{0};
  std::int32_t file_size{0};
  std::int32_t field_18{0};
  std::int32_t field_1C{0};
  std::int32_t field_20{0};
  std::int32_t field_24{0};
  std::int32_t field_28{0};
  void *critical_section{nullptr};
};

struct SArchiveHandle {
  std::int32_t type{0};
  std::uint32_t archive_token{0};
};
static_assert(sizeof(SArchiveHandle) == 8);

enum class SFileNativeObjectKind : std::uint32_t {
  kUnknown = 0,
  kFile = 1,
  kDirectory = 2,
};

struct RuntimeSFileHandleMetadata {
  std::string native_path;
  std::string archive_path;
  std::uint64_t size{0};
  std::uint32_t translated_attributes{0};
  std::int64_t creation_time_ns_since_2000{0};
  std::int64_t last_access_time_ns_since_2000{0};
  std::int64_t last_write_time_ns_since_2000{0};
  SFileNativeObjectKind object_kind{SFileNativeObjectKind::kUnknown};
  bool non_directory_hint{false};
};

enum class SFileArchiveLookupResult : std::int32_t {
  kMiss = 0,
  kArchive = 1,
  kDirectoryArchive = 2,
  kPatchMarker = 4,
};

struct SFileArchiveLookupInfo {
  std::uint32_t archive_token = 0;
  std::string resolved_path;
  std::string archive_path;
  std::uint64_t file_position = 0;
  std::uint32_t compressed_size = 0;
  std::uint32_t file_size = 0;
  std::uint32_t file_flags = 0;
};

}

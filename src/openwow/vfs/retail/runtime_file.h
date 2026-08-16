#pragma once

#include "openwow/core/streaming_storage.h"
#include "openwow/vfs/retail/sfile_types.h"
#include "openwow/vfs/retail/streaming/streaming_file_adapter.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::core {
class StormFileIO;
}

namespace openwow::platform {
class StormCriticalSection;
}

namespace openwow::vfs {

class ArchiveRegistry;

struct LooseFileMetadataSnapshot {
  std::uint16_t attribute_word = 0;
  std::uint64_t size = 0;
  std::int64_t creation_time_ns_since_2000 = 0;
  std::int64_t last_access_time_ns_since_2000 = 0;
  std::int64_t last_write_time_ns_since_2000 = 0;
};

class LooseFileMetadataStore {
public:
  static std::optional<LooseFileMetadataSnapshot> Query(const std::string &native_path);
  static void StoreAttributes(const std::filesystem::path &path,
                              std::uint16_t file_attributes);
  static void ResetForTests();
};

class RuntimeFile {
public:
  using StreamingReadDelegate =
      std::function<bool(void *, std::uint64_t, std::uint32_t *, bool)>;
  using DirectReadFallback =
      std::function<bool(std::uint64_t, void *, std::uint32_t *)>;
  struct StreamingPartBacking {
    openwow::core::StreamingEntry entry_metadata;
    openwow::core::StreamingPartFileState part_state;
    std::string lookup_key;
    std::optional<StreamingDirectRequest> direct_request;
  };

  explicit RuntimeFile(int type = 0);
  ~RuntimeFile();
  RuntimeFile(const RuntimeFile &) = delete;
  RuntimeFile &operator=(const RuntimeFile &) = delete;

  bool OpenLoose(const char *native_path, const char *mode);
  bool OpenArchive(ArchiveRegistry &archives, void *raw_archive, const char *filename);
  bool RefreshSize(std::uint64_t *out_size);
  void SyncSizeFields();
  void ResetAttributes();
  void UpdateTranslatedAttributes(std::uint32_t attributes);
  void SyncLooseMetadata();
  void SetCursor(std::int64_t new_position);
  [[nodiscard]] bool TryCursorOffset(std::uint64_t *out_offset) const;
  [[nodiscard]] std::int64_t SignedSize() const;
  [[nodiscard]] std::int64_t ApplyNativeCursorMove(std::int32_t offset,
                                                   std::uint32_t method);
  [[nodiscard]] std::int64_t ApplyArchiveCursorMove(std::int32_t offset,
                                                    std::uint32_t method);
  [[nodiscard]] std::int64_t ApplyBufferedCursorMove(std::int32_t offset,
                                                     std::uint32_t method) const;
  [[nodiscard]] openwow::platform::StormCriticalSection *critical_section() const;
  bool SetBackingCursor(std::uint64_t offset);
  bool ReadAtOffset(void *buffer, std::uint64_t offset, std::uint32_t *inout_bytes,
                    bool require_exact, bool preserve_count_on_failure,
                    const StreamingReadDelegate &streaming_read = {});
  bool ReadCurrent(void *buffer, std::uint32_t *inout_bytes, bool require_exact,
                   const StreamingReadDelegate &streaming_read = {});
  bool ReadArchiveCurrent(void *buffer, std::uint32_t requested_bytes,
                          std::uint32_t *out_bytes_read,
                          const StreamingReadDelegate &streaming_read,
                          const DirectReadFallback &direct_fallback);
  bool WriteCurrent(const void *buffer, std::uint32_t bytes_to_write);
  bool WriteAtOffset(const void *buffer, std::uint64_t offset,
                     std::uint32_t *inout_bytes_to_write);
  bool Flush();
  bool Resize(std::uint64_t new_size);
  bool Buffer(const StreamingReadDelegate &streaming_read = {});
  bool QueryNativePosition(std::int64_t *out_position);
  bool SetLastWriteTime(std::int64_t time_ns_since_2000);
  [[nodiscard]] RuntimeSFileHandleMetadata Metadata() const;

  static std::uint8_t *FatalReadFlag();
  static std::uint8_t *SetFatalReadFlag(std::uint8_t value);
  static void ResetFatalReadFlagForTests();

  SFileHandle handle{};
  int handle_id = 0;
  std::uint64_t size = 0;
  std::int64_t position = 0;
  std::string native_path;
  std::string archive_path;
  std::string logical_path;
  std::unique_ptr<openwow::core::StormFileIO> file_io;
  void *archive_file_handle = nullptr;
  std::shared_ptr<void> archive_access;
  bool buffered_source = false;
  std::vector<std::uint8_t> buffered_bytes;
  std::uint32_t translated_attributes = 0;
  std::int64_t creation_time_ns_since_2000 = 0;
  std::int64_t last_access_time_ns_since_2000 = 0;
  std::int64_t last_write_time_ns_since_2000 = 0;
  SFileNativeObjectKind object_kind = SFileNativeObjectKind::kUnknown;
  bool non_directory_hint = false;
  std::optional<StreamingPartBacking> streaming_part_backing;
};

}

#include "openwow/vfs/retail/file_stack/file_stack_abi.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/core/streaming_storage.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace openwow::vfs {

constexpr std::uint16_t kWin32FileAttributeReadOnly = 0x0001u;
constexpr std::uint16_t kWin32FileAttributeHidden = 0x0002u;
constexpr std::uint16_t kWin32FileAttributeSystem = 0x0004u;
constexpr std::uint16_t kWin32FileAttributeDirectory = 0x0010u;
constexpr std::uint16_t kWin32FileAttributeArchive = 0x0020u;
constexpr std::uint16_t kWin32FileAttributeNormal = 0x0080u;
constexpr std::uint16_t kWin32FileAttributeTemporary = 0x0100u;
constexpr std::uint32_t kFileStackAttributeReadOnly = 0x0001u;
constexpr std::uint32_t kFileStackAttributeHidden = 0x0002u;
constexpr std::uint32_t kFileStackAttributeSystem = 0x0004u;
constexpr std::uint32_t kFileStackAttributeArchive = 0x0008u;
constexpr std::uint32_t kFileStackAttributeTemporary = 0x0010u;
constexpr std::uint32_t kFileStackAttributeNonDirectory = 0x0020u;
constexpr std::uint32_t kFileStackAttributeDirectory = 0x0040u;
constexpr std::size_t kFileStackEnumerateDirectoryEntriesSlotOffset = 0x18u;
constexpr std::size_t kFileStackQueryPathAttributesSlotOffset = 0x1Cu;
constexpr std::size_t kFileStackQueryPathMetadataSlotOffset = 0x24u;
constexpr std::size_t kFileStackCommitFileHandleMetadataSlotOffset = 0x3Cu;
constexpr std::size_t kFileStackCreateDirectorySlotOffset = 0x40u;
constexpr std::size_t kFileStackMovePathSlotOffset = 0x44u;

namespace {

class UninitializedFileStackRequestRecord {
public:
  static constexpr std::size_t kSize = 0x14;

  ~UninitializedFileStackRequestRecord() {
    for (const std::uint32_t token : pointer_tokens_) {
      UnregisterFileStackCompatPointer(token);
    }
  }

  void *data() {
    return storage_.data();
  }

  template <typename T> void Write(std::size_t offset, const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    if constexpr (std::is_pointer_v<T>) {
      const std::uint32_t token = RegisterFileStackCompatPointer(value);
      if (token != 0) {
        pointer_tokens_.push_back(token);
      }
      std::memcpy(storage_.data() + offset, &token, sizeof(token));
    } else {
      std::memcpy(storage_.data() + offset, &value, sizeof(value));
    }
  }

private:
  std::array<std::byte, kSize> storage_;
  std::vector<std::uint32_t> pointer_tokens_;
};

template <std::size_t StorageSize>
class UninitializedFileStackPathQueryScratch {
public:
  static constexpr std::size_t kVisibleSize = sizeof(FileStackPathMetadata);
  static_assert(StorageSize >= kVisibleSize);

  void *data() {
    return storage_.data();
  }

  template <typename T> void Write(std::size_t offset, const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(storage_.data() + offset, &value, sizeof(value));
  }

  void CopyVisible(FileStackPathMetadata *out_metadata) const {
    std::memcpy(out_metadata, storage_.data(), kVisibleSize);
  }

  std::uint32_t object_kind() const {
    std::uint32_t kind = 0;
    std::memcpy(&kind, storage_.data() + 0x28u, sizeof(kind));
    return kind;
  }

private:
  std::array<std::byte, StorageSize> storage_;
};

bool DispatchFileStackSlotRaw(void *callback_table, std::size_t slot_offset, void *event_record) {
  if (!callback_table) {
    return false;
  }

  auto callback = ReadFileStackDispatchSlot(callback_table, slot_offset);
  return callback && callback(callback_table, event_record);
}

bool DispatchFileStackSlot(void *callback_table, std::size_t slot_offset,
                           FileStackEventRecord &event_record) {
  return DispatchFileStackSlotRaw(callback_table, slot_offset, event_record.data());
}

bool DispatchFileStackPathQuery(void *callback_table, const std::size_t slot_offset,
                                const std::uint32_t opcode, const char *path, void *query_output,
                                const std::optional<std::uint32_t> field_0c) {
  UninitializedFileStackRequestRecord request;
  request.Write(0x00u, opcode);
  request.Write(0x04u, path);
  if (field_0c.has_value()) {
    request.Write(0x0Cu, *field_0c);
  }
  request.Write(0x10u, query_output);
  return DispatchFileStackSlotRaw(callback_table, slot_offset, request.data());
}

}

bool FileStack_Streaming_ShutdownProviderAndDispatch(void *self, void *event_record) {
  openwow::core::StreamingStorage::Instance().Shutdown();

  const auto slot_offset =
      ReadFileStackField<std::uint32_t>(event_record, 0x00u);
  const auto *next_provider = ReadFileStackNextProvider(self);
  return DispatchFileStackSlotRaw(const_cast<void *>(next_provider), slot_offset, event_record);
}

bool FileStack_EnumerateDirectoryEntries(
    void *callback_table, const char *path,
    const std::function<bool(const FileSystemDirectoryEntry &)> &callback) {
  if (!callback_table || !path || !callback) {
    return false;
  }

  FileStackEventRecord event_record;
  FileStackDirectoryEnumerationBridge bridge{
      .callback = &callback,
  };
  event_record.Write(0x00u,
                     static_cast<std::uint32_t>(kFileStackEnumerateDirectoryEntriesSlotOffset));
  event_record.Write(0x04u, path);
  event_record.Write(0x80u, &bridge);
  return DispatchFileStackSlot(callback_table, kFileStackEnumerateDirectoryEntriesSlotOffset,
                               event_record);
}

bool FileStack_QueryPathMetadata(void *callback_table, const char *path,
                                 FileStackPathMetadata *out_metadata) {
  if (!callback_table) {
    return false;
  }

  UninitializedFileStackPathQueryScratch<0x38u> scratch;
  scratch.Write(0x30u, std::uint32_t{0});
  scratch.Write(0x34u, std::int32_t{-1});

  if (!DispatchFileStackPathQuery(callback_table, kFileStackQueryPathMetadataSlotOffset,
                                  static_cast<std::uint32_t>(
                                      kFileStackQueryPathMetadataSlotOffset),
                                  path, scratch.data(), std::uint32_t{0})) {
    return false;
  }

  scratch.CopyVisible(out_metadata);
  return true;
}

std::uint32_t FileStack_QueryPathAttributes(void *callback_table, const char *path) {
  if (!callback_table || !path) {
    return 0;
  }

  UninitializedFileStackPathQueryScratch<0x6Cu> scratch;
  if (!DispatchFileStackPathQuery(callback_table, kFileStackQueryPathAttributesSlotOffset,
                                  static_cast<std::uint32_t>(
                                      kFileStackQueryPathAttributesSlotOffset),
                                  path, scratch.data(), std::nullopt)) {
    return 0;
  }

  return scratch.object_kind();
}

bool FileStack_SetPathAttributes(void *callback_table, const char *path,
                                 const std::uint32_t file_attributes) {
  FileStackEventRecord event_record;
  std::array<std::byte, 0x60> scratch{};
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x64u));
  event_record.Write(0x04u, path);
  event_record.Write(0x10u, scratch.data());
  event_record.Write(0x28u, file_attributes);
  event_record.Write(0x50u, std::uint32_t{4});
  return DispatchFileStackSlot(callback_table, 0x64u, event_record);
}

bool IOUnitContainer_CopyPath(const char *source_path, const char *destination_path,
                              const bool overwrite_existing) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x48u));
  event_record.Write(0x04u, source_path);
  event_record.Write(0x08u, destination_path);
  event_record.Write(0x89u, static_cast<std::uint8_t>(overwrite_existing));
  return DispatchFileStackSlot(GetActiveFileStackCallbackTable(), 0x48u,
                               event_record);
}

bool IOUnitContainer_MovePath(const char *source_path, const char *destination_path) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(kFileStackMovePathSlotOffset));
  event_record.Write(0x04u, source_path);
  event_record.Write(0x08u, destination_path);
  return DispatchFileStackSlot(GetActiveFileStackCallbackTable(),
                               kFileStackMovePathSlotOffset, event_record);
}

bool IOUnitContainer_CreateDirectory(const char *path, const bool recursive) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(kFileStackCreateDirectorySlotOffset));
  event_record.Write(0x04u, path);
  event_record.Write(0x6Cu, static_cast<std::uint8_t>(recursive));
  return DispatchFileStackSlot(GetActiveFileStackCallbackTable(),
                               kFileStackCreateDirectorySlotOffset, event_record);
}

bool FileStack_GetFileHandlePointer(void *callback_table, void *file_handle,
                                    std::uint64_t *out_position) {
  if (!callback_table || !out_position) {
    return false;
  }

  FileStackEventRecord event_record;
  std::uint32_t cursor_low = 0;
  std::uint32_t cursor_high = 0;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x2Cu));
  event_record.Write(0x0Cu, file_handle);
  event_record.Write(0x68u, cursor_low);
  event_record.Write(0x6Cu, cursor_high);
  event_record.Write(0x70u, std::uint32_t{1});

  if (!DispatchFileStackSlot(callback_table, 0x2Cu, event_record)) {
    return false;
  }

  *out_position =
      static_cast<std::uint64_t>(cursor_low) | (static_cast<std::uint64_t>(cursor_high) << 32);
  return true;
}

bool FileStack_SetFileHandlePointer(void *callback_table, void *file_handle,
                                    const std::int64_t offset, const std::uint32_t move_method) {
  FileStackEventRecord event_record;
  const auto offset_low = static_cast<std::uint32_t>(offset & 0xFFFFFFFFll);
  const auto offset_high =
      static_cast<std::uint32_t>((static_cast<std::uint64_t>(offset) >> 32) & 0xFFFFFFFFull);

  event_record.Write(0x00u, static_cast<std::uint32_t>(0x68u));
  event_record.Write(0x0Cu, file_handle);
  event_record.Write(0x68u, offset_low);
  event_record.Write(0x6Cu, offset_high);
  event_record.Write(0x70u, move_method);
  return DispatchFileStackSlot(callback_table, 0x68u, event_record);
}

bool FileStack_CommitFileHandleMetadata(void *callback_table, const char *path, void *file_handle,
                                        FileStackPathMetadata *metadata,
                                        std::uint32_t *inout_dirty_flags) {
  if (!inout_dirty_flags) {
    return false;
  }

  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x3Cu));
  event_record.Write(0x04u, path);
  event_record.Write(0x0Cu, file_handle);
  event_record.Write(0x10u, metadata);
  event_record.Write(0x50u, *inout_dirty_flags);

  const bool ok = DispatchFileStackSlot(callback_table, kFileStackCommitFileHandleMetadataSlotOffset,
                                        event_record);
  *inout_dirty_flags = ReadFileStackField<std::uint32_t>(event_record.data(), 0x50u);
  return ok;
}

std::uint32_t FileStack_TranslateMetadataAttributesToWin32(const std::uint8_t file_attributes) {
  std::uint32_t translated = 0;

  if ((file_attributes & kFileStackAttributeReadOnly) != 0) {
    translated |= kWin32FileAttributeReadOnly;
  }
  if ((file_attributes & kFileStackAttributeHidden) != 0) {
    translated |= kWin32FileAttributeHidden;
  }
  if ((file_attributes & kFileStackAttributeSystem) != 0) {
    translated |= kWin32FileAttributeSystem;
  }
  if ((file_attributes & kFileStackAttributeArchive) != 0) {
    translated |= kWin32FileAttributeArchive;
  }
  if ((file_attributes & kFileStackAttributeTemporary) != 0) {
    translated |= kWin32FileAttributeTemporary;
  }
  if ((file_attributes & kFileStackAttributeNonDirectory) != 0) {
    translated |= kWin32FileAttributeNormal;
  }
  if ((file_attributes & kFileStackAttributeDirectory) != 0) {
    translated |= kWin32FileAttributeDirectory;
  }

  return translated;
}

std::uint32_t FileStack_TranslateWin32Attributes(const std::uint16_t file_attributes) {
  std::uint32_t translated = 0;

  if ((file_attributes & kWin32FileAttributeReadOnly) != 0) {
    translated |= kFileStackAttributeReadOnly;
  }
  if ((file_attributes & kWin32FileAttributeHidden) != 0) {
    translated |= kFileStackAttributeHidden;
  }
  if ((file_attributes & kWin32FileAttributeSystem) != 0) {
    translated |= kFileStackAttributeSystem;
  }
  if ((file_attributes & kWin32FileAttributeArchive) != 0) {
    translated |= kFileStackAttributeArchive;
  }
  if ((file_attributes & kWin32FileAttributeTemporary) != 0) {
    translated |= kFileStackAttributeTemporary;
  }
  if ((file_attributes & kWin32FileAttributeDirectory) != 0) {
    translated |= kFileStackAttributeDirectory;
  }
  if ((file_attributes & kWin32FileAttributeNormal) != 0 ||
      (file_attributes & kWin32FileAttributeDirectory) == 0) {
    translated |= kFileStackAttributeNonDirectory;
  }

  return translated;
}

void *FileStack_OpenFileHandle(void *callback_table, void *file_handle, const char *path,
                               std::uint32_t flags) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x4Cu));
  event_record.Write(0x04u, path);
  event_record.Write(0x0Cu, file_handle);
  event_record.Write(0x58u, flags);

  if (!DispatchFileStackSlot(callback_table, 0x4Cu, event_record)) {
    return nullptr;
  }

  return file_handle;
}

bool FileStack_CloseFileHandle(void *callback_table, void *file_handle) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x0Cu));
  event_record.Write(0x0Cu, file_handle);
  return DispatchFileStackSlot(callback_table, 0x0Cu, event_record);
}

}

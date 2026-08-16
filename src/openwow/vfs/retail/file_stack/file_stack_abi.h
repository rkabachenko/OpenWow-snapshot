#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace openwow::vfs {

struct FileSystemDirectoryEntry;

struct FileStackDirectoryEnumerationBridge {
  const std::function<bool(const FileSystemDirectoryEntry &)> *callback = nullptr;
};

struct FileStackPathMetadata {
  std::uint32_t field_00{0};
  std::uint32_t file_size_low{0};
  std::uint32_t file_size_high{0};
  std::uint32_t translated_attributes{0};
  std::int64_t creation_time_ns_since_2000{0};
  std::int64_t last_access_time_ns_since_2000{0};
  std::int64_t last_write_time_ns_since_2000{0};
  std::uint32_t object_kind{0};
  std::uint32_t provider_flags{0};
};
static_assert(sizeof(FileStackPathMetadata) == 0x30);
static_assert(offsetof(FileStackPathMetadata, file_size_low) == 0x04);
static_assert(offsetof(FileStackPathMetadata, file_size_high) == 0x08);
static_assert(offsetof(FileStackPathMetadata, translated_attributes) == 0x0C);
static_assert(offsetof(FileStackPathMetadata, creation_time_ns_since_2000) == 0x10);
static_assert(offsetof(FileStackPathMetadata, last_access_time_ns_since_2000) == 0x18);
static_assert(offsetof(FileStackPathMetadata, last_write_time_ns_since_2000) == 0x20);
static_assert(offsetof(FileStackPathMetadata, object_kind) == 0x28);
static_assert(offsetof(FileStackPathMetadata, provider_flags) == 0x2C);

bool FileStack_EnumerateDirectoryEntries(
    void *callback_table, const char *path,
    const std::function<bool(const FileSystemDirectoryEntry &)> &callback);
bool FileStack_QueryPathMetadata(void *callback_table, const char *path,
                                 FileStackPathMetadata *out_metadata);
std::uint32_t FileStack_QueryPathAttributes(void *callback_table, const char *path);
bool FileStack_SetPathAttributes(void *callback_table, const char *path,
                                 std::uint32_t file_attributes);
bool FileStack_GetFileHandlePointer(void *callback_table, void *file_handle,
                                    std::uint64_t *out_position);
bool FileStack_SetFileHandlePointer(void *callback_table, void *file_handle,
                                    std::int64_t offset, std::uint32_t move_method);
bool FileStack_CommitFileHandleMetadata(void *callback_table, const char *path,
                                        void *file_handle, FileStackPathMetadata *metadata,
                                        std::uint32_t *inout_dirty_flags);
void *FileStack_OpenFileHandle(void *callback_table, void *file_handle, const char *path,
                               std::uint32_t flags);
bool FileStack_CloseFileHandle(void *callback_table, void *file_handle);
bool IOUnitContainer_CopyPath(const char *source_path, const char *destination_path,
                              bool overwrite_existing);
bool IOUnitContainer_MovePath(const char *source_path, const char *destination_path);
bool IOUnitContainer_CreateDirectory(const char *path, bool recursive);
bool FileStack_Streaming_ShutdownProviderAndDispatch(void *self, void *event_record);
void IOUnitContainerFileStack_ShutdownProviderChain();
std::uint32_t FileStack_TranslateWin32Attributes(std::uint16_t file_attributes);
std::uint32_t FileStack_TranslateMetadataAttributesToWin32(std::uint8_t file_attributes);

}

#pragma once

#include "openwow/vfs/retail/sfile_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::vfs {

bool SFileOpenArchiveWrapped(const char *path, std::int32_t priority, std::uint32_t flags,
                             void **out_handle);
bool SFileCloseArchiveWrapped(void *archive);
bool CopyWrappedArchivePathBounded(const void *archive, char *output, int output_capacity);
bool SFileOpenPatchArchiveWrapped(void *base_archive, const char *path, std::int32_t priority,
                                  int flags, void **out_handle);
bool SFileAuthenticateArchive(void *archive, std::int32_t *out_result, void *modulus,
                              int modulus_size, void *exponent, int exponent_size);
bool SFileAuthenticateArchiveEx(void *archive, std::int32_t *out_result, void *modulus,
                                int modulus_size, void *exponent, int exponent_size,
                                const char *suffix);
bool SFileCloseArchiveRaw(void *archive);
bool SFileOpenArchiveRaw_SetLastError(const char *path, std::int32_t priority,
                                      std::uint32_t flags, void **out_handle);
SFileArchiveLookupResult LookupRegisteredArchiveFile(const void *archive_handle,
                                                      const char *filename,
                                                      SFileArchiveLookupInfo *out_info);
bool QueryWrappedArchiveFileMetadata(const char *filename, std::string *archive_path,
                                     std::uint64_t *block_offset,
                                     std::uint32_t *compressed_size,
                                     std::uint32_t *file_flags);
int SFileEnumListfile(void *archive, std::function<bool(const char *, int)> callback,
                      int user_data);
bool SFileArchiveHasFile_SetLastErrorOnHit(const char *filename);
int ClearDefaultArchiveLookupKey();

bool ReadRetailVfsFileBytes(const char *path, std::vector<std::uint8_t> *out_bytes);
bool ReadRetailArchiveListFile(const char *archive_name, std::string *out_contents);

}

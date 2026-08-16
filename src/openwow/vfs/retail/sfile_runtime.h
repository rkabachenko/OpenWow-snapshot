#pragma once

#include "openwow/vfs/retail/sfile_types.h"

#include <cstddef>
#include <cstdint>

namespace openwow::vfs {

SFileHandle *SFileHandle_Init(SFileHandle *handle, int type);
int SFileOpenFile(void *archive_handle, const char *filename, int flags, int *out_file);
int SFile_SetFilePointer(int handle, std::int32_t offset_low, std::int32_t offset_high,
                         std::uint32_t move_method);
int SFile_GetFileSize(int handle, std::uint32_t *out_high);
std::uint8_t *SFileReadFatalFlag_Get();
std::uint8_t *SFileReadFatalFlag_Set(std::uint8_t value);
int SFile_ReadFile(int handle, void *buffer, int size, std::uint32_t *out_bytes_read,
                   int decompress, int overlapped_flags);
int SFileOpenFileAndLoadData(void *archive, const char *filename, void **out_data,
                             std::size_t *out_size, std::size_t extra_padding,
                             int open_flags, int read_flags);
bool SFileReadFileToBuffer(void *archive, const char *filename, void **out_data, int *out_size,
                           std::size_t extra_padding, int open_flags);
bool SFileReadFileToBuffer_SetLastError(void *archive, const char *filename, void **out_data,
                                        int *out_size, std::size_t extra_padding,
                                        int open_flags, int unused_read_flags);
int SFileReadFileToBuffer_Wrapper(const char *filename, void **out_data,
                                  std::size_t *out_size, std::size_t extra_padding,
                                  int read_flags);
int SFileOpenFile_Wrapper(const char *filename, int *out_file);
int SFileFreeLoadedData(void *block);
bool SFileCanResolvePath(const char *filename, int flags);
int SFileHandle_CopyLogicalPathBounded(int file_handle, char *output, int output_capacity);
int AsyncFileRead_RequestDataPreloadPathAvailability(int file_handle, int queue_index,
                                                     char wait_for_completion);
bool OpenLooseFileHandle(const char *native_path, const char *mode, int *out_handle);
bool QueryRuntimeSFileHandleMetadata(int handle, RuntimeSFileHandleMetadata *out_metadata);

}

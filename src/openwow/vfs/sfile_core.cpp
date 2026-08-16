#include "openwow/vfs/retail/sfile_runtime.h"

extern "C" int SFileOpenFile(void *archive, const char *filename, int flags, int *out_file) {
  return openwow::vfs::SFileOpenFile(archive, filename, flags, out_file);
}

extern "C" int SFile_SetFilePointer(int handle, int low, int high, unsigned int method) {
  return openwow::vfs::SFile_SetFilePointer(handle, low, high, method);
}

extern "C" int SFile_GetFileSize(int handle, void *high) {
  return openwow::vfs::SFile_GetFileSize(handle, static_cast<std::uint32_t *>(high));
}

extern "C" int SFile_ReadFile(int handle, void *buffer, int size, void *out_bytes_read,
                              int decompress, int overlapped_flags) {
  return openwow::vfs::SFile_ReadFile(handle, buffer, size,
                                      static_cast<std::uint32_t *>(out_bytes_read), decompress,
                                      overlapped_flags);
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::vfs {

inline constexpr std::uint32_t kIOUnitContainerTag = 0x6D707120u;
inline constexpr std::uint32_t kIOUnitContainerReadOnlyTag = 0x6D707152u;
inline constexpr std::uint32_t kIOUnitContainerWritableTag = 0x6D707157u;
inline constexpr std::uint32_t kIOUnitContainerRangeWrapperTag = 0x72616E67u;
inline constexpr std::uint32_t kIOUnitContainerLockTag = 0x6C6F636Bu;

struct IOUnitContainerCompatAllocState {
  std::byte *cursor = nullptr;
  std::uint8_t owned = 0;
};

void IOUnitContainer_Alloc(IOUnitContainerCompatAllocState *alloc_state,
                           std::span<const std::uint32_t> reservation_sizes);

bool IOUnitContainer_SetFileHandleCursor(int handle, std::uint64_t offset);
std::uint64_t IOUnitContainer_GetFileSizeByHandle(int handle);
bool IOUnitContainer_CloseFileHandle(int handle);
bool IOUnitContainer_CreateFileHandle(const char *path, std::uint32_t flags, int *out_handle);
bool IOUnitContainer_LoadFileHandleData(int handle, void **out_data, int *out_size);
bool IOUnitContainer_LoadDirectFile(const char *path, void **out_data, int *out_size,
                                    int open_flags);
bool IOUnitContainer_ReadFileHandle(int handle, void *buffer,
                                    std::uint32_t *inout_bytes_to_read);
int IOUnitContainer_ReadFileHandle_Wrapper(int handle, void *buffer,
                                           std::uint32_t bytes_to_read,
                                           std::uint32_t *out_bytes_read);
bool IOUnitContainer_ReadFileHandleAtOffset(int handle, void *buffer, std::uint64_t offset,
                                            std::uint32_t *inout_bytes_to_read);
bool IOUnitContainer_WriteFileHandle(int handle, const void *buffer,
                                     std::uint32_t bytes_to_write);
int IOUnitContainer_WriteFileHandle_Wrapper(int handle, const void *buffer,
                                            std::uint32_t bytes_to_write,
                                            std::uint32_t *out_bytes_written);
bool IOUnitContainer_WriteFileHandleAtOffset(int handle, const void *buffer,
                                             std::uint64_t offset,
                                             std::uint32_t *inout_bytes_to_write);
bool IOUnitContainer_FlushFileHandle(int handle);
bool IOUnitContainer_SetFileHandleSize(int handle, std::uint64_t size, int mode);

std::uint32_t IOUnitContainerRangeWrapper_GetTypeTag();
std::uint32_t IOUnitContainer_GetTypeTag();
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetTypeTag();
std::uint32_t IOUnitContainer_CreateWritableFile_GetTypeTag();
std::uint32_t IOUnitContainerLock_GetTypeTag();

bool IOUnitContainer_MatchesAttributeDword(std::uintptr_t self, std::uint32_t value);
bool IOUnitContainer_SetAttributeDword(std::uintptr_t self, std::uint32_t value);

void *IOUnitContainerRangeWrapper_Ctor(void *self, std::uint64_t base_offset,
                                       std::uint64_t end_offset, std::uint8_t owned,
                                       void *inner);
void *IOUnitContainerRangeWrapper_Destroy(void *self, char free_self);
void *IOUnitContainerRangeWrapper_Create(IOUnitContainerCompatAllocState *alloc_state,
                                         std::uint64_t base_offset,
                                         std::uint64_t end_offset, void *inner);
void *IOUnitContainerLock_Alloc(IOUnitContainerCompatAllocState *alloc_state, int unused,
                                void *inner);
void *IOUnitContainerLock_AllocDefault(IOUnitContainerCompatAllocState *alloc_state,
                                       void *inner);
void IOUnitContainer_ForwardingWrapper_Ctor(void *self);
bool IOUnitContainerLock_WrapInner(void *lock_wrapper, void *replacement);

int IOUnitContainer_CreateReadOnlyFile_vf1(void *self, int a2, int a3, int a4, int a5,
                                           int a6);
int IOUnitContainer_CreateWritableFile_vf2(void *self, int a2, int a3, int a4, int a5);
int IOUnitContainer_GetFileSize_vf4(void *self);
int IOUnitContainer_ForwardInnerVf5(void *self, int a2, int a3);
std::uint8_t IOUnitContainer_ReadOnlySectorReader_vf4(void *self,
                                                      std::uint32_t *out_offset_parts);
int IOUnitContainer_CreateFileUnit_Flush_vf3(void *self);
int IOUnitContainer_CreateFileUnit_vf8(void *self);
int IOUnitContainer_CreateFileUnit_SetSize_vf5(void *self, std::uint32_t size_low,
                                               std::uint32_t size_high);
std::uint8_t IOUnitContainerLock_vf1(void *self, int a2, int a3, int a4, int a5, int a6);
std::uint8_t IOUnitContainerLock_vf2(void *self, int a2, int a3, int a4, int a5);
std::uint8_t IOUnitContainerLock_vf3(void *self);
std::uint8_t IOUnitContainerLock_vf4(void *self, std::uint32_t *out_offset_parts);
std::uint8_t IOUnitContainerLock_vf5(void *self, int a2, int a3);
int IOUnitContainerLock_vf6(void *self);
void *IOUnitContainerLock_Destroy(void *self, char free_self);

std::uint8_t IOUnitContainer_CreateFileUnit_vf19(void *self, int a2,
                                                 int relative_offset_low,
                                                 int relative_offset_high, int size,
                                                 int allow_truncate);
std::uint8_t IOUnitContainer_CreateFileUnit_vf20(void *self, int a2,
                                                 std::uint64_t relative_offset,
                                                 std::uint32_t size);
int IOUnitContainer_CreateFileUnit_vf21(void *self);
std::uint8_t IOUnitContainer_CreateFileUnit_vf22(void *self,
                                                 std::uint32_t *out_offset_parts);
bool IOUnitContainer_CreateFileUnit_vf23(void *self, std::uint64_t size);
int IOUnitContainer_CreateFileUnit_vf24(void *self);
bool IOUnitContainer_CreateFileUnit_vf1_Stub(int a1, int a2, int a3, int a4, int a5);
bool IOUnitContainer_ReadOnly_vf2_Stub(int a1, int a2, int a3, int a4);
std::uint8_t IOUnitContainer_ReadOnly_vf5_Stub(void *self, int a2, int a3);

void *IOUnitSourcePathBase_Destroy(void *self, char free_self);
void *IOUnitContainer_ArchiveFile_Destroy(void *self, char free_self);
void *IOUnitSourcePathBase_GetRetainedSource_vf9(void *self);
const char *IOUnitSourcePathBase_GetPath_vf10(void *self);
std::uint8_t IOUnitContainer_CreateReadOnlyFile_vf4(void *self,
                                                    std::uint32_t *out_offset_parts);
bool IOUnitContainer_CreateReadOnlyFile_vf12(void *self,
                                             std::uint32_t *out_offset_parts);
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetRetainedSourceField18_vf13(void *self);
std::uint64_t IOUnitContainer_CreateReadOnlyFile_vf14(void *self);
std::uint64_t IOUnitContainer_CreateReadOnlyFile_GetAttributeQword_vf15(void *self);
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetAttributeDword_vf16(void *self);
const std::uint8_t *IOUnitContainer_CreateReadOnlyFile_GetAttributeMd5Digest_vf17(void *self);
std::uint8_t IOUnitContainer_CreateReadOnlyFile_GetAttributeLookupFlag_vf18(void *self);
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetBlockFlags_vf20(void *self);
char IOUnitContainer_CreateReadOnlyFile_SetReadOnlyFlagAndUpdateBlockEntry_vf21(void *self);
std::uintptr_t IOUnitContainer_CreateWritableFile_CommitCachedBlockEntry(
    void *self, std::uint32_t committed_compressed_size);
bool IOUnitContainer_CreateReadOnlyFile_HasHighFlags_vf22(void *self);

namespace detail {

inline constexpr std::size_t kIOUnitContainerLockReservationSize = 44;
inline constexpr std::size_t kIOUnitContainerRangeWrapperReservationSize = 32;

void *AllocateIOUnitContainerCompatStorage(IOUnitContainerCompatAllocState *alloc_state,
                                           std::size_t reservation_size,
                                           std::size_t native_size,
                                           std::uint8_t *out_owned);
int ComparePackedSignedQwords(std::uint64_t lhs, std::uint64_t rhs);
int ForwardIOUnitVf5(void *inner, int a2, int a3);

}
}

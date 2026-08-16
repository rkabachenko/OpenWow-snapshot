#pragma once

#include "openwow/core/md5.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"

#include <cstddef>
#include <cstdint>

namespace openwow::vfs {

inline constexpr std::uint32_t kIOChecksumUnitTag = 0x63726320u;

struct IOChecksumUnitCommonCompat {
  std::uintptr_t inner = 0;
  std::uint8_t owned = 0;
  std::uint32_t refcount = 1;
  std::uint64_t processed_bytes = 0;
  std::uint64_t end_offset = 0;
  std::uintptr_t callback_context = 0;
  std::uint8_t drain_on_close = 0;
  std::uintptr_t finalize_callback = 0;
};

struct IOCrc32ChecksumUnitCompat {
  IOChecksumUnitCommonCompat common{};
  std::uint32_t crc32 = 0;
};

struct IOMd5ChecksumUnitCompat {
  IOChecksumUnitCommonCompat common{};
  openwow::core::MD5Context md5{};
};

std::uint32_t IOChecksumUnit_GetTypeTag();
std::uintptr_t IOChecksumUnit_SetCallbackContext(IOChecksumUnitCommonCompat *self,
                                                 std::uintptr_t callback_context);
bool IOChecksumUnit_SetSize_vf5(IOChecksumUnitCommonCompat *self, int size_low, int size_high);
bool IOChecksumUnit_Clamp(const IOChecksumUnitCommonCompat *self, std::int64_t offset,
                          std::uint32_t &size, bool allow_truncate);
IOCrc32ChecksumUnitCompat *IOChecksumUnit_InitCrc32(
    IOCrc32ChecksumUnitCompat *self, std::uint64_t end_offset,
    std::uintptr_t finalize_callback, std::uint8_t drain_on_close,
    IOCrc32ChecksumUnitCompat **out_self, std::uint8_t owned, std::uintptr_t inner);
void *IOChecksumUnit_WrapWithCrc32IfNeeded(IOUnitContainerCompatAllocState *alloc_state,
                                           std::uint64_t end_offset,
                                           std::uintptr_t finalize_callback,
                                           std::uint8_t drain_on_close,
                                           IOCrc32ChecksumUnitCompat **out_self, void *inner);
IOMd5ChecksumUnitCompat *IOChecksumUnit_InitMd5(
    IOMd5ChecksumUnitCompat *self, std::uint64_t end_offset,
    std::uintptr_t finalize_callback, std::uint8_t drain_on_close,
    IOMd5ChecksumUnitCompat **out_self, std::uint8_t owned, std::uintptr_t inner);
void *IOChecksumUnit_CreateMd5IfNeeded(std::uint64_t end_offset,
                                       std::uintptr_t finalize_callback,
                                       std::uint8_t drain_on_close,
                                       IOMd5ChecksumUnitCompat **out_self, void *inner);
void *IOChecksumUnit_WrapWithMd5IfNeeded(IOUnitContainerCompatAllocState *alloc_state,
                                         std::uint64_t end_offset,
                                         std::uintptr_t finalize_callback,
                                         std::uint8_t drain_on_close,
                                         IOMd5ChecksumUnitCompat **out_self, void *inner);
std::uint32_t IOChecksumUnit_Crc32_Update(IOCrc32ChecksumUnitCompat *self, const void *data,
                                         std::size_t size);
int IOChecksumUnit_Crc32_Finalize(IOCrc32ChecksumUnitCompat *self);
void IOChecksumUnit_Md5_Update(IOMd5ChecksumUnitCompat *self, const void *data,
                               std::size_t size);
int IOChecksumUnit_Md5_Finalize(IOMd5ChecksumUnitCompat *self);

using IOChecksumUnitUpdateSpanFn = void (*)(void *self, const void *data, std::uint32_t size);
using IOChecksumUnitFinalizeUnitFn = int (*)(void *self);
using IOChecksumUnitReadSpanFn = bool (*)(void *self, void *buffer, std::uint64_t offset,
                                          std::uint32_t size, std::uint32_t flags);

bool IOChecksumUnit_ClampProcessedSpan(const IOChecksumUnitCommonCompat *self, const void *data,
                                       std::uint64_t offset, std::uint32_t size,
                                       const void **out_data, std::uint32_t *out_size);
int IOChecksumUnit_ProcessProcessedSpan(IOChecksumUnitCommonCompat *self, void *checksum_unit,
                                        const void *data, std::uint64_t offset,
                                        std::uint32_t size,
                                        IOChecksumUnitUpdateSpanFn update_span,
                                        IOChecksumUnitFinalizeUnitFn finalize_unit);
bool IOChecksumUnit_Close(IOChecksumUnitCommonCompat *self, void *checksum_unit,
                          IOChecksumUnitReadSpanFn read_span);

}

#include "openwow/vfs/retail/io_unit/checksum_unit.h"

#include "openwow/data/streaming_init.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include <zlib.h>

namespace openwow::vfs {
namespace {

constexpr std::size_t kIOCrc32ChecksumUnitReservationSize = 48;
constexpr std::size_t kIOMd5ChecksumUnitReservationSize = 136;

void InitializeCommon(IOChecksumUnitCommonCompat *self, std::uint64_t end_offset,
                      std::uintptr_t finalize_callback, std::uint8_t drain_on_close,
                      std::uint8_t owned, std::uintptr_t inner) {
  if (!self) {
    return;
  }
  self->inner = inner;
  self->owned = owned;
  self->refcount = 1;
  self->processed_bytes = 0;
  self->end_offset = end_offset;
  self->callback_context = 0;
  self->drain_on_close = drain_on_close;
  self->finalize_callback = finalize_callback;
}

bool ClampProcessedSpan(const IOChecksumUnitCommonCompat &self, const void *data,
                        std::uint64_t offset, std::uint32_t size, const void **out_data,
                        std::uint32_t *out_size) {
  if (offset > self.processed_bytes) {
    return false;
  }
  const std::uint64_t range_end = offset + static_cast<std::uint64_t>(size);
  if (range_end <= self.processed_bytes) {
    return false;
  }
  std::uintptr_t clamped_data = reinterpret_cast<std::uintptr_t>(data);
  std::uint32_t clamped_size = size;
  if (offset < self.processed_bytes) {
    const auto delta = static_cast<std::uint32_t>(self.processed_bytes - offset);
    clamped_data += delta;
    clamped_size -= delta;
  }
  *out_data = reinterpret_cast<const void *>(clamped_data);
  *out_size = clamped_size;
  return true;
}

std::uint32_t UpdateStormCrc32(std::uint32_t seed, const std::uint8_t *bytes,
                               std::size_t size) {
  if (!bytes) {
    return 0;
  }
  std::uint32_t crc = seed;
  std::size_t remaining = size;
  auto *cursor = static_cast<const Bytef *>(bytes);
  while (remaining != 0u) {
    const auto chunk =
        static_cast<uInt>(std::min<std::size_t>(remaining, std::numeric_limits<uInt>::max()));
    crc = static_cast<std::uint32_t>(crc32(crc, cursor, chunk));
    cursor += chunk;
    remaining -= chunk;
  }
  return crc;
}

bool IsPackedSignedQwordAtMost(int lhs_low, int lhs_high, std::uint64_t rhs) {
  const std::uint64_t lhs = static_cast<std::uint32_t>(lhs_low) |
                            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lhs_high))
                             << 32);
  return detail::ComparePackedSignedQwords(lhs, rhs) <= 0;
}

template <typename ChecksumUnit, typename InitFn>
void *WrapIfNeeded(IOUnitContainerCompatAllocState *alloc_state, std::size_t reservation_size,
                   std::uint64_t end_offset, std::uintptr_t finalize_callback,
                   std::uint8_t drain_on_close, ChecksumUnit **out_self, void *inner,
                   InitFn init_fn) {
  if (inner == nullptr) {
    return nullptr;
  }
  if (finalize_callback == 0) {
    return inner;
  }
  std::uint8_t owned = 0;
  void *const storage = detail::AllocateIOUnitContainerCompatStorage(
      alloc_state, reservation_size, sizeof(ChecksumUnit), &owned);
  if (storage == nullptr) {
    return nullptr;
  }
  return init_fn(static_cast<ChecksumUnit *>(storage), end_offset, finalize_callback,
                 drain_on_close, out_self, owned, reinterpret_cast<std::uintptr_t>(inner));
}

}

std::uint32_t IOChecksumUnit_GetTypeTag() { return kIOChecksumUnitTag; }

std::uintptr_t IOChecksumUnit_SetCallbackContext(IOChecksumUnitCommonCompat *self,
                                                 const std::uintptr_t callback_context) {
  self->callback_context = callback_context;
  return callback_context;
}

bool IOChecksumUnit_SetSize_vf5(IOChecksumUnitCommonCompat *self, const int size_low,
                                const int size_high) {
  if (!IsPackedSignedQwordAtMost(size_low, size_high, self->end_offset)) {
    return false;
  }
  return detail::ForwardIOUnitVf5(reinterpret_cast<void *>(self->inner), size_low, size_high) != 0;
}

bool IOChecksumUnit_Clamp(const IOChecksumUnitCommonCompat *self, const std::int64_t offset,
                          std::uint32_t &size, const bool allow_truncate) {
  const std::uint64_t offset_bits = static_cast<std::uint64_t>(offset);
  if (detail::ComparePackedSignedQwords(offset_bits + static_cast<std::uint64_t>(size),
                                       self->end_offset) <= 0) {
    return true;
  }
  if (detail::ComparePackedSignedQwords(offset_bits, self->end_offset) >= 0) {
    openwow::data::PushStreamingStatusMessage("IOChecksumUnit::Clamp - Range Error", 1, 10);
    return false;
  }
  if (!allow_truncate) {
    return false;
  }
  size = static_cast<std::uint32_t>(self->end_offset - offset_bits);
  return true;
}

IOCrc32ChecksumUnitCompat *IOChecksumUnit_InitCrc32(
    IOCrc32ChecksumUnitCompat *self, const std::uint64_t end_offset,
    const std::uintptr_t finalize_callback, const std::uint8_t drain_on_close,
    IOCrc32ChecksumUnitCompat **out_self, const std::uint8_t owned,
    const std::uintptr_t inner) {
  InitializeCommon(&self->common, end_offset, finalize_callback, drain_on_close, owned, inner);
  self->crc32 = 0;
  if (out_self) {
    *out_self = self;
  }
  return self;
}

void *IOChecksumUnit_WrapWithCrc32IfNeeded(IOUnitContainerCompatAllocState *alloc_state,
                                           const std::uint64_t end_offset,
                                           const std::uintptr_t finalize_callback,
                                           const std::uint8_t drain_on_close,
                                           IOCrc32ChecksumUnitCompat **out_self, void *inner) {
  return WrapIfNeeded(alloc_state, kIOCrc32ChecksumUnitReservationSize, end_offset,
                      finalize_callback, drain_on_close, out_self, inner,
                      &IOChecksumUnit_InitCrc32);
}

IOMd5ChecksumUnitCompat *IOChecksumUnit_InitMd5(
    IOMd5ChecksumUnitCompat *self, const std::uint64_t end_offset,
    const std::uintptr_t finalize_callback, const std::uint8_t drain_on_close,
    IOMd5ChecksumUnitCompat **out_self, const std::uint8_t owned,
    const std::uintptr_t inner) {
  InitializeCommon(&self->common, end_offset, finalize_callback, drain_on_close, owned, inner);
  openwow::core::MD5_Init(&self->md5);
  if (out_self) {
    *out_self = self;
  }
  return self;
}

void *IOChecksumUnit_CreateMd5IfNeeded(const std::uint64_t end_offset,
                                       const std::uintptr_t finalize_callback,
                                       const std::uint8_t drain_on_close,
                                       IOMd5ChecksumUnitCompat **out_self, void *const inner) {
  if (inner == nullptr) {
    return nullptr;
  }
  if (finalize_callback == 0) {
    return inner;
  }
  void *const storage = ::operator new(kIOMd5ChecksumUnitReservationSize, std::nothrow);
  if (storage == nullptr) {
    return nullptr;
  }
  std::memset(storage, 0, kIOMd5ChecksumUnitReservationSize);
  return IOChecksumUnit_InitMd5(static_cast<IOMd5ChecksumUnitCompat *>(storage), end_offset,
                                finalize_callback, drain_on_close, out_self, 1u,
                                reinterpret_cast<std::uintptr_t>(inner));
}

void *IOChecksumUnit_WrapWithMd5IfNeeded(IOUnitContainerCompatAllocState *alloc_state,
                                         const std::uint64_t end_offset,
                                         const std::uintptr_t finalize_callback,
                                         const std::uint8_t drain_on_close,
                                         IOMd5ChecksumUnitCompat **out_self, void *inner) {
  return WrapIfNeeded(alloc_state, kIOMd5ChecksumUnitReservationSize, end_offset,
                      finalize_callback, drain_on_close, out_self, inner,
                      &IOChecksumUnit_InitMd5);
}

std::uint32_t IOChecksumUnit_Crc32_Update(IOCrc32ChecksumUnitCompat *self, const void *data,
                                         const std::size_t size) {
  self->crc32 = UpdateStormCrc32(self->crc32, static_cast<const std::uint8_t *>(data), size);
  return self->crc32;
}

int IOChecksumUnit_Crc32_Finalize(IOCrc32ChecksumUnitCompat *self) {
  using Callback = int (*)(std::uintptr_t, std::uint32_t);
  return reinterpret_cast<Callback>(self->common.finalize_callback)(self->common.callback_context,
                                                                    self->crc32);
}

void IOChecksumUnit_Md5_Update(IOMd5ChecksumUnitCompat *self, const void *data,
                               const std::size_t size) {
  openwow::core::MD5_Update(&self->md5, data, size);
}

int IOChecksumUnit_Md5_Finalize(IOMd5ChecksumUnitCompat *self) {
  std::uint8_t digest[16]{};
  openwow::core::MD5_Final(&self->md5, digest);
  using Callback = int (*)(std::uintptr_t, const std::uint8_t *);
  return reinterpret_cast<Callback>(self->common.finalize_callback)(self->common.callback_context,
                                                                    digest);
}

bool IOChecksumUnit_ClampProcessedSpan(const IOChecksumUnitCommonCompat *self, const void *data,
                                       const std::uint64_t offset, const std::uint32_t size,
                                       const void **out_data, std::uint32_t *out_size) {
  return ClampProcessedSpan(*self, data, offset, size, out_data, out_size);
}

int IOChecksumUnit_ProcessProcessedSpan(IOChecksumUnitCommonCompat *self, void *checksum_unit,
                                        const void *data, const std::uint64_t offset,
                                        const std::uint32_t size,
                                        const IOChecksumUnitUpdateSpanFn update_span,
                                        const IOChecksumUnitFinalizeUnitFn finalize_unit) {
  const void *clamped_data = nullptr;
  std::uint32_t clamped_size = 0;
  if (!ClampProcessedSpan(*self, data, offset, size, &clamped_data, &clamped_size)) {
    return 1;
  }
  update_span(checksum_unit, clamped_data, clamped_size);
  self->processed_bytes += static_cast<std::uint64_t>(clamped_size);
  return self->processed_bytes == self->end_offset ? finalize_unit(checksum_unit) : 1;
}

bool IOChecksumUnit_Close(IOChecksumUnitCommonCompat *self, void *checksum_unit,
                          const IOChecksumUnitReadSpanFn read_span) {
  if (!self->drain_on_close || self->processed_bytes >= self->end_offset) {
    return true;
  }
  const std::uint32_t remaining_size =
      static_cast<std::uint32_t>(self->end_offset - self->processed_bytes);
  void *scratch_buffer = remaining_size != 0 ? std::malloc(remaining_size) : nullptr;
  const bool result =
      read_span(checksum_unit, scratch_buffer, self->processed_bytes, remaining_size, 0);
  std::free(scratch_buffer);
  return result;
}

}

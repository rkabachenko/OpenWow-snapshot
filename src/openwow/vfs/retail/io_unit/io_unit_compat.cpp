#include "openwow/vfs/retail/io_unit/io_unit_compat.h"

#include "openwow/core/mpq_internals.h"
#include "openwow/data/streaming_init.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/file_stack/file_stack_abi.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"
#include "openwow/vfs/retail/runtime_file.h"
#include "openwow/vfs/retail/runtime_file_registry.h"
#include "openwow/vfs/retail/streaming/streaming_file_adapter.h"
#include "openwow/vfs/retail/sfile_runtime.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace openwow::vfs {
namespace {

using IOUnitForwardReadOnlyVf1Fn = int (*)(void *, int, int, int, int, int);
using IOUnitForwardWritableVf2Fn = int (*)(void *, int, int, int, int);
using IOUnitForwardNullaryIntFn = int (*)(void *);
using IOUnitForwardVf5Fn = int (*)(void *, int, int);
using IOUnitByteResult = std::uint8_t;
using IOUnitByteReadOnlyVf1Fn = IOUnitByteResult (*)(void *, int, int, int, int, int);
using IOUnitByteLowHighWritableVf2Fn = IOUnitByteResult (*)(void *, int, int, int, int);
using IOUnitByteWritableVf2Fn = IOUnitByteResult (*)(void *, int, std::uint64_t,
                                                     std::uint32_t);
using IOUnitByteNullaryFn = IOUnitByteResult (*)(void *);
using IOUnitByteVf5Fn = IOUnitByteResult (*)(void *, int, int);
using IOUnitRangeQueryVf4Fn = IOUnitByteResult (*)(void *, std::uint32_t *);
using IOUnitRangeCompareVf5Fn = bool (*)(void *, std::uint64_t);
using IOUnitGetTypeTagFn = std::uint32_t (*)();
using IOUnitNullaryNoArgIntFn = int (*)();
using IOUnitForwardDeleteFn = void *(*)(void *, char);
using IOUnitNullsubFn = void (*)();

struct IOUnitForwardingCompatVTable {
  IOUnitGetTypeTagFn vf0_type_tag = nullptr;
  IOUnitForwardReadOnlyVf1Fn vf1 = nullptr;
  IOUnitForwardWritableVf2Fn vf2 = nullptr;
  IOUnitForwardNullaryIntFn vf3 = nullptr;
  IOUnitForwardNullaryIntFn vf4 = nullptr;
  IOUnitForwardVf5Fn vf5 = nullptr;
  IOUnitForwardNullaryIntFn vf6 = nullptr;
};

struct IOUnitForwardingCompat {
  IOUnitForwardingCompatVTable *vtable = nullptr;
};

struct IOUnitBoundedCompatVTable {
  IOUnitGetTypeTagFn vf0_type_tag = nullptr;
  IOUnitForwardReadOnlyVf1Fn vf1 = nullptr;
  IOUnitForwardWritableVf2Fn vf2 = nullptr;
  IOUnitForwardNullaryIntFn vf3 = nullptr;
  IOUnitRangeQueryVf4Fn vf4 = nullptr;
  IOUnitByteVf5Fn vf5 = nullptr;
  IOUnitForwardNullaryIntFn vf6 = nullptr;
};

struct IOUnitBoundedCompat {
  IOUnitBoundedCompatVTable *vtable = nullptr;
};

struct IOUnitContainerLockVTable {
  IOUnitGetTypeTagFn vf0_type_tag = nullptr;
  IOUnitByteReadOnlyVf1Fn vf1 = nullptr;
  IOUnitByteLowHighWritableVf2Fn vf2 = nullptr;
  IOUnitByteNullaryFn vf3 = nullptr;
  IOUnitRangeQueryVf4Fn vf4 = nullptr;
  IOUnitByteVf5Fn vf5 = nullptr;
  IOUnitForwardNullaryIntFn vf6 = nullptr;
  IOUnitForwardDeleteFn vf7_destroy = nullptr;
};

struct IOUnitContainerBoundedFileWrapperVTable {
  IOUnitGetTypeTagFn vf0_type_tag = nullptr;
  IOUnitByteReadOnlyVf1Fn vf1 = nullptr;
  IOUnitByteWritableVf2Fn vf2 = nullptr;
  IOUnitForwardNullaryIntFn vf3 = nullptr;
  IOUnitRangeQueryVf4Fn vf4 = nullptr;
  IOUnitRangeCompareVf5Fn vf5 = nullptr;
  IOUnitForwardNullaryIntFn vf6 = nullptr;
  IOUnitForwardDeleteFn vf7_destroy = nullptr;
};

struct IOUnitContainerArchiveFileVTable {
  IOUnitGetTypeTagFn vf0_type_tag = nullptr;
  IOUnitForwardReadOnlyVf1Fn vf1 = nullptr;
  IOUnitForwardWritableVf2Fn vf2 = nullptr;
  IOUnitForwardNullaryIntFn vf3 = nullptr;
  IOUnitRangeQueryVf4Fn vf4 = nullptr;
  IOUnitByteVf5Fn vf5 = nullptr;
  IOUnitForwardNullaryIntFn vf6 = nullptr;
  IOUnitForwardDeleteFn vf7_destroy = nullptr;
};

struct IOUnitContainerDestroyVTable {
  void *slots[7] = {};
  IOUnitForwardDeleteFn vf7_destroy = nullptr;
};

struct IOUnitContainerForwardingWrapperCompat {
  void *vtable = nullptr;
  IOUnitForwardingCompat *inner = nullptr;
};

struct IOUnitContainerForwardingWrapperBaseVTable {
  IOUnitGetTypeTagFn vf0_type_tag = nullptr;
  IOUnitForwardReadOnlyVf1Fn vf1 = nullptr;
  IOUnitForwardWritableVf2Fn vf2 = nullptr;
  IOUnitForwardNullaryIntFn vf3 = nullptr;
  IOUnitForwardNullaryIntFn vf4 = nullptr;
  IOUnitForwardVf5Fn vf5 = nullptr;
  IOUnitForwardNullaryIntFn vf6 = nullptr;
  IOUnitForwardDeleteFn vf7_destroy = nullptr;
  IOUnitGetTypeTagFn vf8_return_one = nullptr;
  void *slot9 = nullptr;
  IOUnitForwardDeleteFn vf10_delete_thunk = nullptr;
  IOUnitNullaryNoArgIntFn vf11_return_zero = nullptr;
};

struct IOUnitContainerForwardingWrapperDeletedVTable {
  IOUnitForwardDeleteFn vf0_delete_thunk = nullptr;
  IOUnitNullaryNoArgIntFn vf1_return_zero = nullptr;
  IOUnitGetTypeTagFn vf2_return_one = nullptr;
  IOUnitNullsubFn vf3_nullsub = nullptr;
};

struct IOUnitContainerFileUnitCompat {
  void *vtable = nullptr;
  std::uint32_t reserved_04 = 0;
  std::uint8_t owned = 0;
  std::uint8_t padding[3] = {};
  std::uint32_t refcount = 1;
  std::int32_t handle = 0;
};
static_assert(sizeof(void *) != 4 || sizeof(IOUnitContainerFileUnitCompat) == 20);

struct IOUnitContainerLockWrapperCompat {
  void *vtable = nullptr;
  IOUnitBoundedCompat *inner = nullptr;
  std::uint8_t owned = 0;
  std::uint8_t padding[3] = {};
  std::uint32_t refcount = 1;
  openwow::platform::StormCriticalSection *critical_section = nullptr;
};
static_assert(sizeof(void *) != 4 || sizeof(IOUnitContainerLockWrapperCompat) == 20);

struct IOUnitContainerBoundedFileWrapperCompat {
  void *vtable = nullptr;
  IOUnitBoundedCompat *inner = nullptr;
  std::uint32_t field_08 = 0;
  std::uint32_t field_0c = 0;
  std::uint64_t base_offset = 0;
  std::uint64_t end_offset = 0;
};

struct IOUnitContainerReadOnlySectorReaderCompat {
  void *vtable = nullptr;
  void *inner = nullptr;
  std::uint8_t owned = 0;
  std::uint8_t padding_09[3] = {};
  std::uint32_t refcount = 1;
  std::uint32_t sector_table_count = 0;
  std::uint32_t *sector_table = nullptr;
  std::uint32_t inline_sector_table[10] = {};
  std::uint32_t current_sector_index = 0;
  std::uint32_t *sector_cursor = nullptr;
  std::uint8_t reserved_48[0x20] = {};
  std::uint32_t sector_size = 0;
  std::uint32_t tail_sector_size = 0;
  std::uint32_t field_70 = 0;
  std::uint32_t field_74 = 0;
  std::uint32_t logical_file_size = 0;
};
static_assert(sizeof(void *) != 4 || sizeof(IOUnitContainerReadOnlySectorReaderCompat) == 124);
static_assert(sizeof(void *) != 4 ||
              offsetof(IOUnitContainerReadOnlySectorReaderCompat, logical_file_size) == 0x78);

struct IOUnitSourcePathBaseCompat {
  void *vtable = nullptr;
  void *archive_handle = nullptr;
  std::uint8_t owned = 0;
  std::uint8_t padding_09[3] = {};
  std::uint32_t refcount = 1;
  std::uint32_t field_10 = 0;
  void *retained_source = nullptr;
  std::uint32_t path_length = 0;
  char *path_buffer = nullptr;
  char inline_path[0x104] = {};
};
static_assert(sizeof(void *) != 4 || sizeof(IOUnitSourcePathBaseCompat) == 0x124);
static_assert(sizeof(void *) != 4 || offsetof(IOUnitSourcePathBaseCompat, retained_source) == 0x14);
static_assert(sizeof(void *) != 4 || offsetof(IOUnitSourcePathBaseCompat, path_buffer) == 0x1C);

struct IOUnitRetainedSourceField18Compat {
  std::byte reserved_00[0x18] = {};
  std::uint32_t field_18 = 0;
};
static_assert(offsetof(IOUnitRetainedSourceField18Compat, field_18) == 0x18);

struct IOUnitContainerReadOnlyFileCompat {
  IOUnitSourcePathBaseCompat base{};
  std::uint32_t block_index = 0;
  openwow::core::BlockTableEntry cached_block_entry{};
  std::uint8_t trailing_padding[3] = {};
};
static_assert(sizeof(void *) != 4 || sizeof(IOUnitContainerReadOnlyFileCompat) == 348);
static_assert(sizeof(openwow::core::BlockTableEntry) == 0x31);
static_assert(sizeof(void *) != 4 ||
              offsetof(IOUnitContainerReadOnlyFileCompat, cached_block_entry) == 0x128);
static_assert(sizeof(void *) != 4 ||
              offsetof(IOUnitContainerReadOnlyFileCompat,
                       cached_block_entry.compressed_size_lo) == 0x130);
static_assert(sizeof(void *) != 4 ||
              offsetof(IOUnitContainerReadOnlyFileCompat, cached_block_entry.file_size_lo) ==
                  0x134);

class StormCriticalSectionScope {
public:
  explicit StormCriticalSectionScope(openwow::platform::StormCriticalSection *section)
      : section_(section) {
    section_->Enter();
  }
  ~StormCriticalSectionScope() { section_->Leave(); }

private:
  openwow::platform::StormCriticalSection *section_;
};

std::byte *ConsumeReservation(IOUnitContainerCompatAllocState *alloc_state,
                              std::size_t reservation_size, std::uint8_t *out_owned) {
  if (alloc_state != nullptr) {
    std::byte *const reservation = alloc_state->cursor;
    *out_owned = alloc_state->owned;
    if (reservation != nullptr) {
      const auto next = reinterpret_cast<std::uintptr_t>(reservation + reservation_size);
      alloc_state->cursor =
          reinterpret_cast<std::byte *>((next + 3u) & ~static_cast<std::uintptr_t>(3u));
    }
    alloc_state->owned = 0;
    return reservation;
  }
  *out_owned = 1;
  return nullptr;
}

std::uint32_t QueryIOUnitTypeTag(const void *self) {
  if (!self) {
    return 0;
  }
  const auto *tagged_self = static_cast<const IOUnitForwardingCompat *>(self);
  if (!tagged_self->vtable || !tagged_self->vtable->vf0_type_tag) {
    return 0;
  }
  return tagged_self->vtable->vf0_type_tag();
}

int ForwardInnerReadOnlyVf1(IOUnitForwardingCompat *inner, int a2, int a3, int a4, int a5,
                            int a6) {
  return inner->vtable->vf1(inner, a2, a3, a4, a5, a6);
}
IOUnitByteResult ForwardInnerReadOnlyVf1(IOUnitBoundedCompat *inner, int a2, int a3, int a4,
                                         int a5, int a6) {
  return static_cast<IOUnitByteResult>(inner->vtable->vf1(inner, a2, a3, a4, a5, a6));
}
int ForwardInnerWritableVf2(IOUnitForwardingCompat *inner, int a2, int a3, int a4, int a5) {
  return inner->vtable->vf2(inner, a2, a3, a4, a5);
}
IOUnitByteResult ForwardInnerWritableVf2(IOUnitBoundedCompat *inner, int a2, int a3, int a4,
                                         int a5) {
  return static_cast<IOUnitByteResult>(inner->vtable->vf2(inner, a2, a3, a4, a5));
}
int ForwardInnerFileSize(IOUnitForwardingCompat *inner) { return inner->vtable->vf4(inner); }
int ForwardInnerVf5(IOUnitForwardingCompat *inner, int a2, int a3) {
  return inner->vtable->vf5(inner, a2, a3);
}
int ForwardInnerVf3(IOUnitForwardingCompat *inner) { return inner->vtable->vf3(inner); }
int ForwardInnerVf6(IOUnitForwardingCompat *inner) { return inner->vtable->vf6(inner); }
int ForwardInnerNullary(IOUnitBoundedCompat *inner) { return inner->vtable->vf6(inner); }
IOUnitByteResult ForwardInnerVf3(IOUnitBoundedCompat *inner) {
  return static_cast<IOUnitByteResult>(inner->vtable->vf3(inner));
}
IOUnitByteResult ForwardInnerVf4(IOUnitBoundedCompat *inner, std::uint32_t *out_parts) {
  return inner->vtable->vf4(inner, out_parts);
}
IOUnitByteResult ForwardInnerVf5(IOUnitBoundedCompat *inner, int a2, int a3) {
  return inner->vtable->vf5(inner, a2, a3);
}

int IOUnitContainer_ForwardingWrapper_vf3(void *self) {
  return ForwardInnerVf3(static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->inner);
}
int IOUnitContainer_ForwardingWrapper_vf6(void *self) {
  return ForwardInnerVf6(static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->inner);
}
std::uint32_t IOUnitContainer_ForwardingWrapper_ReturnOne() { return 1; }
int IOUnitContainer_ForwardingWrapper_ReturnZero() { return 0; }
void IOUnitContainer_ForwardingWrapper_Nullsub() {}

IOUnitContainerForwardingWrapperBaseVTable &ForwardingWrapperBaseVTable();
IOUnitContainerLockVTable &LockVTable();
IOUnitContainerBoundedFileWrapperVTable &RangeWrapperVTable();
IOUnitContainerArchiveFileVTable &ArchiveFileVTable();
IOUnitContainerForwardingWrapperDeletedVTable &ForwardingWrapperDeletedVTable();

void *IOUnitContainer_ForwardingWrapper_Destroy(void *self, char free_self) {
  static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->vtable =
      &ForwardingWrapperBaseVTable();
  if ((free_self & 1) != 0) {
    ::operator delete(self);
  }
  return self;
}
void *IOUnitContainer_ForwardingWrapper_DeleteThunk(void *self, char free_self) {
  static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->vtable =
      &ForwardingWrapperDeletedVTable();
  if ((free_self & 1) != 0) {
    ::operator delete(self);
  }
  return self;
}

IOUnitContainerForwardingWrapperBaseVTable &ForwardingWrapperBaseVTable() {
  static IOUnitContainerForwardingWrapperBaseVTable table{
      &IOUnitContainer_GetTypeTag,
      &IOUnitContainer_CreateReadOnlyFile_vf1,
      &IOUnitContainer_CreateWritableFile_vf2,
      &IOUnitContainer_ForwardingWrapper_vf3,
      &IOUnitContainer_GetFileSize_vf4,
      &IOUnitContainer_ForwardInnerVf5,
      &IOUnitContainer_ForwardingWrapper_vf6,
      &IOUnitContainer_ForwardingWrapper_Destroy,
      &IOUnitContainer_ForwardingWrapper_ReturnOne,
      nullptr,
      &IOUnitContainer_ForwardingWrapper_DeleteThunk,
      &IOUnitContainer_ForwardingWrapper_ReturnZero,
  };
  return table;
}
IOUnitContainerArchiveFileVTable &ArchiveFileVTable() {
  static IOUnitContainerArchiveFileVTable table{
      &IOUnitContainer_GetTypeTag,
      &IOUnitContainer_CreateReadOnlyFile_vf1,
      &IOUnitContainer_CreateWritableFile_vf2,
      &IOUnitContainer_ForwardingWrapper_vf3,
      &IOUnitContainer_CreateReadOnlyFile_vf4,
      &IOUnitContainer_ReadOnly_vf5_Stub,
      &IOUnitContainer_ForwardingWrapper_vf6,
      &IOUnitContainer_ArchiveFile_Destroy,
  };
  return table;
}
IOUnitContainerForwardingWrapperDeletedVTable &ForwardingWrapperDeletedVTable() {
  static IOUnitContainerForwardingWrapperDeletedVTable table{
      &IOUnitContainer_ForwardingWrapper_DeleteThunk,
      &IOUnitContainer_ForwardingWrapper_ReturnZero,
      &IOUnitContainer_ForwardingWrapper_ReturnOne,
      &IOUnitContainer_ForwardingWrapper_Nullsub,
  };
  return table;
}
IOUnitContainerLockVTable &LockVTable() {
  static IOUnitContainerLockVTable table{
      &IOUnitContainerLock_GetTypeTag, &IOUnitContainerLock_vf1,     &IOUnitContainerLock_vf2,
      &IOUnitContainerLock_vf3,        &IOUnitContainerLock_vf4,     &IOUnitContainerLock_vf5,
      &IOUnitContainerLock_vf6,        &IOUnitContainerLock_Destroy,
  };
  return table;
}
IOUnitContainerBoundedFileWrapperVTable &RangeWrapperVTable() {
  static IOUnitContainerBoundedFileWrapperVTable table{
      &IOUnitContainerRangeWrapper_GetTypeTag, &IOUnitContainer_CreateFileUnit_vf19,
      &IOUnitContainer_CreateFileUnit_vf20,    &IOUnitContainer_CreateFileUnit_vf21,
      &IOUnitContainer_CreateFileUnit_vf22,    &IOUnitContainer_CreateFileUnit_vf23,
      &IOUnitContainer_CreateFileUnit_vf24,    &IOUnitContainerRangeWrapper_Destroy,
  };
  return table;
}

void *ReleaseNext(std::uint32_t type_tag, void *self) {
  switch (type_tag) {
  case kIOUnitContainerRangeWrapperTag:
    return static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self)->inner;
  case kIOUnitContainerLockTag:
    return static_cast<IOUnitContainerLockWrapperCompat *>(self)->inner;
  default:
    return nullptr;
  }
}

bool ReleaseCompatObject(void *self) {
  while (self != nullptr) {
    const auto type_tag = QueryIOUnitTypeTag(self);
    void *const next = ReleaseNext(type_tag, self);
    std::uint8_t owned = 0;
    std::uint32_t *refcount = nullptr;
    switch (type_tag) {
    case kIOUnitContainerRangeWrapperTag: {
      auto *wrapper = static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self);
      owned = static_cast<std::uint8_t>(wrapper->field_08);
      refcount = &wrapper->field_0c;
      break;
    }
    case kIOUnitContainerReadOnlyTag:
    case kIOUnitContainerWritableTag: {
      auto *wrapper = static_cast<IOUnitContainerReadOnlyFileCompat *>(self);
      owned = wrapper->base.owned;
      refcount = &wrapper->base.refcount;
      break;
    }
    case kIOUnitContainerLockTag: {
      auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
      owned = wrapper->owned;
      refcount = &wrapper->refcount;
      break;
    }
    default:
      return false;
    }
    if (*refcount > 1u) {
      --(*refcount);
      return true;
    }
    *refcount = 0;
    auto *const vtable = reinterpret_cast<IOUnitContainerDestroyVTable *>(
        static_cast<IOUnitForwardingCompat *>(self)->vtable);
    if (vtable == nullptr || vtable->vf7_destroy == nullptr) {
      return false;
    }
    vtable->vf7_destroy(self, 0);
    if (owned != 0) {
      ::operator delete(self);
    }
    self = next;
  }
  return true;
}

openwow::core::detail::ReadOnlyArchiveHandleCompat *GetReadOnlyArchiveHandleCompat(
    IOUnitContainerReadOnlyFileCompat *file) {
  return static_cast<openwow::core::detail::ReadOnlyArchiveHandleCompat *>(
      file->base.archive_handle);
}

}

void IOUnitContainer_Alloc(IOUnitContainerCompatAllocState *const alloc_state,
                           const std::span<const std::uint32_t> reservation_sizes) {
  if (alloc_state == nullptr) {
    return;
  }
  std::size_t total_size = 0;
  for (const std::uint32_t reservation_size : reservation_sizes) {
    total_size = (total_size + static_cast<std::size_t>(reservation_size) + 3u) & ~std::size_t{3u};
  }
  void *storage = nullptr;
  if (total_size != 0) {
    storage = ::operator new(total_size, std::nothrow);
    if (storage != nullptr) {
      std::memset(storage, 0, total_size);
    }
  }
  alloc_state->cursor = static_cast<std::byte *>(storage);
  alloc_state->owned = 1;
}

namespace detail {

void *AllocateIOUnitContainerCompatStorage(IOUnitContainerCompatAllocState *const alloc_state,
                                           const std::size_t reservation_size,
                                           const std::size_t native_size,
                                           std::uint8_t *const out_owned) {
  if (alloc_state != nullptr) {
    std::uint8_t reservation_owned = 0;
    std::byte *const reservation = ConsumeReservation(alloc_state, reservation_size,
                                                      &reservation_owned);
    if (reservation != nullptr && native_size <= reservation_size) {
      *out_owned = reservation_owned;
      return reservation;
    }
  }
  *out_owned = 1;
  void *const storage = ::operator new(native_size, std::nothrow);
  if (storage != nullptr) {
    std::memset(storage, 0, native_size);
  }
  return storage;
}

int ComparePackedSignedQwords(const std::uint64_t lhs, const std::uint64_t rhs) {
  const auto lhs_low = static_cast<std::uint32_t>(lhs);
  const auto lhs_high = static_cast<std::uint32_t>(lhs >> 32) ^ 0x80000000u;
  const auto rhs_low = static_cast<std::uint32_t>(rhs);
  const auto rhs_high = static_cast<std::uint32_t>(rhs >> 32) ^ 0x80000000u;
  if (lhs_high != rhs_high) {
    return lhs_high < rhs_high ? -1 : 1;
  }
  if (lhs_low != rhs_low) {
    return lhs_low < rhs_low ? -1 : 1;
  }
  return 0;
}

int ForwardIOUnitVf5(void *inner, int a2, int a3) {
  return ForwardInnerVf5(static_cast<IOUnitForwardingCompat *>(inner), a2, a3);
}

}

bool IOUnitContainer_SetFileHandleCursor(int handle, std::uint64_t offset) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  return runtime_handle->SetBackingCursor(offset);
}

std::uint64_t IOUnitContainer_GetFileSizeByHandle(int handle) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  return runtime_handle ? runtime_handle->Metadata().size : 0x00000000FFFFFFFFull;
}

bool IOUnitContainer_LoadFileHandleData(int handle, void **out_data, int *out_size) {
  if (out_data == nullptr) {
    return false;
  }
  std::uint32_t file_size_high = 0;
  const auto file_size_low = static_cast<std::uint32_t>(SFile_GetFileSize(handle, &file_size_high));
  const auto file_size_high_signed = static_cast<std::int32_t>(file_size_high);
  if (file_size_high_signed < 0 ||
      (file_size_high_signed == 0 && static_cast<std::int32_t>(file_size_low) <= 0) ||
      static_cast<std::size_t>(file_size_low) == std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  std::unique_ptr<char, decltype(&std::free)> buffer(
      static_cast<char *>(std::malloc(static_cast<std::size_t>(file_size_low) + 1)), &std::free);
  if (!buffer) {
    return false;
  }
  std::uint32_t bytes_to_read = file_size_low;
  if (IOUnitContainer_ReadFileHandleAtOffset(handle, buffer.get(), 0, &bytes_to_read) &&
      bytes_to_read == file_size_low) {
    buffer.get()[file_size_low] = '\0';
    if (out_size) {
      *out_size = static_cast<std::int32_t>(file_size_low);
    }
    *out_data = buffer.release();
    return true;
  }
  return false;
}

bool IOUnitContainer_LoadDirectFile(const char *path, void **out_data, int *out_size,
                                    int open_flags) {
  if (!path || !out_data) {
    return false;
  }
  char resolved_path[260] = {};
  if (!ResolveExistingPathAbsolute(path, resolved_path, static_cast<int>(sizeof(resolved_path)),
                                   ExistingPathRequirement::kFileOnly)) {
    return false;
  }
  int handle = 0;
  if (!SFileOpenFile(nullptr, resolved_path, open_flags | 0x1001, &handle)) {
    return false;
  }
  const bool loaded = IOUnitContainer_LoadFileHandleData(handle, out_data, out_size);
  (void)IOUnitContainer_CloseFileHandle(handle);
  return loaded;
}

bool IOUnitContainer_CloseFileHandle(int handle) {
  const auto file_handle =
      reinterpret_cast<void *>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(handle)));
  return FileStack_CloseFileHandle(GetActiveFileStackCallbackTable(), file_handle);
}

bool IOUnitContainer_CreateFileHandle(const char *path, const std::uint32_t flags,
                                      int *out_handle) {
  if (out_handle == nullptr) {
    return false;
  }
  *out_handle = 0;
  if (path == nullptr) {
    return false;
  }
  FileStackEventRecord event_record;
  event_record.Write(0x00u, std::uint32_t{0x4Cu});
  event_record.Write(0x04u, path);
  event_record.Write(0x0Cu, std::uint32_t{0});
  event_record.Write(0x58u, flags);
  if (!DispatchFileStackEvent(GetActiveFileStackCallbackTable(), 0x4Cu, event_record)) {
    return false;
  }
  std::uint32_t handle_value = 0;
  std::memcpy(&handle_value, static_cast<const std::byte *>(event_record.data()) + 0x0Cu,
              sizeof(handle_value));
  *out_handle = static_cast<int>(handle_value);
  return true;
}

bool IOUnitContainer_ReadFileHandle(int handle, void *buffer,
                                    std::uint32_t *inout_bytes_to_read) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  const bool use_current_offset_short_read =
      runtime_handle->file_io != nullptr && !runtime_handle->buffered_source;
  return runtime_handle->ReadCurrent(
      buffer, inout_bytes_to_read, !use_current_offset_short_read,
      [&runtime_handle](void *stream_buffer, const std::uint64_t offset, std::uint32_t *bytes,
                        const bool exact) {
        return ReadStreamingPartBackingAtOffsetLocked(*runtime_handle, stream_buffer, offset,
                                                      bytes, exact);
      });
}

int IOUnitContainer_ReadFileHandle_Wrapper(int handle, void *buffer,
                                           std::uint32_t bytes_to_read,
                                           std::uint32_t *out_bytes_read) {
  if (buffer == nullptr || out_bytes_read == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return 0;
  }
  *out_bytes_read = bytes_to_read;
  if (!IOUnitContainer_ReadFileHandle(handle, buffer, out_bytes_read)) {
    *out_bytes_read = 0;
    return 0;
  }
  return 1;
}

bool IOUnitContainer_ReadFileHandleAtOffset(int handle, void *buffer, std::uint64_t offset,
                                            std::uint32_t *inout_bytes_to_read) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    if (inout_bytes_to_read) {
      *inout_bytes_to_read = 0;
    }
    return false;
  }
  return runtime_handle->ReadAtOffset(
      buffer, offset, inout_bytes_to_read, false, false,
      [&runtime_handle](void *stream_buffer, const std::uint64_t stream_offset,
                        std::uint32_t *bytes, const bool exact) {
        return ReadStreamingPartBackingAtOffsetLocked(*runtime_handle, stream_buffer,
                                                      stream_offset, bytes, exact);
      });
}

bool IOUnitContainer_WriteFileHandle(int handle, const void *buffer,
                                     std::uint32_t bytes_to_write) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  return runtime_handle->WriteCurrent(buffer, bytes_to_write);
}

int IOUnitContainer_WriteFileHandle_Wrapper(int handle, const void *buffer,
                                            std::uint32_t bytes_to_write,
                                            std::uint32_t *out_bytes_written) {
  if (buffer == nullptr || out_bytes_written == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return 0;
  }
  *out_bytes_written = bytes_to_write;
  return IOUnitContainer_WriteFileHandle(handle, buffer, bytes_to_write) ? 1 : 0;
}

bool IOUnitContainer_WriteFileHandleAtOffset(int handle, const void *buffer,
                                             std::uint64_t offset,
                                             std::uint32_t *inout_bytes_to_write) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  return runtime_handle->WriteAtOffset(buffer, offset, inout_bytes_to_write);
}

bool IOUnitContainer_FlushFileHandle(int handle) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section || !runtime_handle->file_io ||
      !runtime_handle->Flush()) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  return true;
}

bool IOUnitContainer_SetFileHandleSize(int handle, std::uint64_t size, int ) {
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!runtime_handle || !runtime_handle->handle.critical_section || !runtime_handle->Resize(size)) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  return true;
}

std::uint32_t IOUnitContainerRangeWrapper_GetTypeTag() {
  return kIOUnitContainerRangeWrapperTag;
}
std::uint32_t IOUnitContainer_GetTypeTag() { return kIOUnitContainerTag; }
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetTypeTag() {
  return kIOUnitContainerReadOnlyTag;
}
std::uint32_t IOUnitContainer_CreateWritableFile_GetTypeTag() {
  return kIOUnitContainerWritableTag;
}
std::uint32_t IOUnitContainerLock_GetTypeTag() { return kIOUnitContainerLockTag; }

bool IOUnitContainer_MatchesAttributeDword(const std::uintptr_t self,
                                           const std::uint32_t value) {
  std::uint32_t stored = 0;
  std::memcpy(&stored, reinterpret_cast<const std::byte *>(self) + 0x144, sizeof(stored));
  return stored == value;
}
bool IOUnitContainer_SetAttributeDword(const std::uintptr_t self, const std::uint32_t value) {
  std::memcpy(reinterpret_cast<std::byte *>(self) + 0x144, &value, sizeof(value));
  return true;
}

void *IOUnitContainerRangeWrapper_Ctor(void *self, const std::uint64_t base_offset,
                                       const std::uint64_t end_offset,
                                       const std::uint8_t owned, void *inner) {
  auto *wrapper = static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self);
  wrapper->vtable = &RangeWrapperVTable();
  wrapper->inner = static_cast<IOUnitBoundedCompat *>(inner);
  wrapper->field_08 = owned;
  wrapper->field_0c = 1;
  wrapper->base_offset = base_offset;
  wrapper->end_offset = end_offset;
  return wrapper;
}

void *IOUnitContainerRangeWrapper_Destroy(void *self, char free_self) {
  static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self)->vtable =
      &ForwardingWrapperDeletedVTable();
  if ((free_self & 1) != 0) {
    ::operator delete(self);
  }
  return self;
}

void *IOUnitContainerRangeWrapper_Create(IOUnitContainerCompatAllocState *alloc_state,
                                         const std::uint64_t base_offset,
                                         const std::uint64_t end_offset, void *inner) {
  if (inner == nullptr) {
    return nullptr;
  }
  if (base_offset > end_offset) {
    ReleaseCompatObject(inner);
    return nullptr;
  }
  std::uint8_t owned = 0;
  void *const storage = detail::AllocateIOUnitContainerCompatStorage(
      alloc_state, detail::kIOUnitContainerRangeWrapperReservationSize,
      sizeof(IOUnitContainerBoundedFileWrapperCompat), &owned);
  return storage ? IOUnitContainerRangeWrapper_Ctor(storage, base_offset, end_offset, owned, inner)
                 : nullptr;
}

void *IOUnitContainerLock_Alloc(IOUnitContainerCompatAllocState *alloc_state, int ,
                                void *inner) {
  if (inner == nullptr) {
    return nullptr;
  }
  std::uint8_t owned = 0;
  void *const storage = detail::AllocateIOUnitContainerCompatStorage(
      alloc_state, detail::kIOUnitContainerLockReservationSize,
      sizeof(IOUnitContainerLockWrapperCompat), &owned);
  if (storage == nullptr) {
    return nullptr;
  }
  auto *critical_section = new (std::nothrow) openwow::platform::StormCriticalSection();
  if (critical_section == nullptr) {
    if (owned != 0) {
      ::operator delete(storage);
    }
    return nullptr;
  }
  critical_section->Initialize();
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(storage);
  std::memset(wrapper, 0, sizeof(*wrapper));
  wrapper->vtable = &LockVTable();
  wrapper->inner = static_cast<IOUnitBoundedCompat *>(inner);
  wrapper->owned = owned;
  wrapper->refcount = 1;
  wrapper->critical_section = critical_section;
  return wrapper;
}

void *IOUnitContainerLock_AllocDefault(IOUnitContainerCompatAllocState *alloc_state,
                                       void *inner) {
  return IOUnitContainerLock_Alloc(alloc_state, 0, inner);
}
void IOUnitContainer_ForwardingWrapper_Ctor(void *self) {
  static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->vtable =
      &ForwardingWrapperBaseVTable();
}
bool IOUnitContainerLock_WrapInner(void *lock_wrapper, void *replacement) {
  auto *lock = static_cast<IOUnitContainerLockWrapperCompat *>(lock_wrapper);
  auto *wrapper = static_cast<IOUnitContainerForwardingWrapperCompat *>(replacement);
  if (!lock || !wrapper || QueryIOUnitTypeTag(lock) != kIOUnitContainerLockTag ||
      reinterpret_cast<void *>(wrapper->inner) != reinterpret_cast<void *>(lock->inner)) {
    return false;
  }
  lock->inner = reinterpret_cast<IOUnitBoundedCompat *>(replacement);
  return true;
}

int IOUnitContainer_CreateReadOnlyFile_vf1(void *self, int a2, int a3, int a4, int a5,
                                           int a6) {
  return ForwardInnerReadOnlyVf1(
      static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->inner, a2, a3, a4, a5, a6);
}
int IOUnitContainer_CreateWritableFile_vf2(void *self, int a2, int a3, int a4, int a5) {
  return ForwardInnerWritableVf2(
      static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->inner, a2, a3, a4, a5);
}
int IOUnitContainer_GetFileSize_vf4(void *self) {
  return ForwardInnerFileSize(static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->inner);
}
int IOUnitContainer_ForwardInnerVf5(void *self, int a2, int a3) {
  return ForwardInnerVf5(static_cast<IOUnitContainerForwardingWrapperCompat *>(self)->inner, a2,
                         a3);
}
std::uint8_t IOUnitContainer_ReadOnlySectorReader_vf4(void *self,
                                                      std::uint32_t *out_offset_parts) {
  auto *reader = static_cast<IOUnitContainerReadOnlySectorReaderCompat *>(self);
  out_offset_parts[0] = reader->logical_file_size;
  out_offset_parts[1] = 0;
  return true;
}
int IOUnitContainer_CreateFileUnit_Flush_vf3(void *self) {
  return IOUnitContainer_FlushFileHandle(static_cast<IOUnitContainerFileUnitCompat *>(self)->handle);
}
int IOUnitContainer_CreateFileUnit_vf8(void *self) {
  return IOUnitContainer_CloseFileHandle(static_cast<IOUnitContainerFileUnitCompat *>(self)->handle);
}
int IOUnitContainer_CreateFileUnit_SetSize_vf5(void *self, std::uint32_t size_low,
                                               std::uint32_t size_high) {
  const std::uint64_t size =
      static_cast<std::uint64_t>(size_low) | (static_cast<std::uint64_t>(size_high) << 32);
  return IOUnitContainer_SetFileHandleSize(
      static_cast<IOUnitContainerFileUnitCompat *>(self)->handle, size, 0);
}

std::uint8_t IOUnitContainerLock_vf1(void *self, int a2, int a3, int a4, int a5, int a6) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  StormCriticalSectionScope lock(wrapper->critical_section);
  return ForwardInnerReadOnlyVf1(wrapper->inner, a2, a3, a4, a5, a6);
}
std::uint8_t IOUnitContainerLock_vf2(void *self, int a2, int a3, int a4, int a5) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  StormCriticalSectionScope lock(wrapper->critical_section);
  return ForwardInnerWritableVf2(wrapper->inner, a2, a3, a4, a5);
}
std::uint8_t IOUnitContainerLock_vf3(void *self) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  StormCriticalSectionScope lock(wrapper->critical_section);
  return ForwardInnerVf3(wrapper->inner);
}
std::uint8_t IOUnitContainerLock_vf4(void *self, std::uint32_t *out_offset_parts) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  StormCriticalSectionScope lock(wrapper->critical_section);
  return wrapper->inner->vtable->vf4(wrapper->inner, out_offset_parts);
}
std::uint8_t IOUnitContainerLock_vf5(void *self, int a2, int a3) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  StormCriticalSectionScope lock(wrapper->critical_section);
  return ForwardInnerVf5(wrapper->inner, a2, a3);
}
int IOUnitContainerLock_vf6(void *self) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  StormCriticalSectionScope lock(wrapper->critical_section);
  return wrapper->inner->vtable->vf6(wrapper->inner);
}
void *IOUnitContainerLock_Destroy(void *self, char free_self) {
  auto *wrapper = static_cast<IOUnitContainerLockWrapperCompat *>(self);
  wrapper->vtable = &LockVTable();
  if (wrapper->critical_section != nullptr) {
    wrapper->critical_section->Delete();
    delete wrapper->critical_section;
  }
  wrapper->critical_section = nullptr;
  wrapper->vtable = &ForwardingWrapperBaseVTable();
  if ((free_self & 1) != 0) {
    ::operator delete(self);
  }
  return self;
}

std::uint8_t IOUnitContainer_CreateFileUnit_vf19(void *self, int a2,
                                                 int relative_offset_low,
                                                 int relative_offset_high, int size,
                                                 int allow_truncate) {
  auto *wrapper = static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self);
  return ForwardInnerReadOnlyVf1(wrapper->inner, a2, relative_offset_low, relative_offset_high,
                                 size, allow_truncate);
}
std::uint8_t IOUnitContainer_CreateFileUnit_vf20(void *self, int a2,
                                                 std::uint64_t relative_offset,
                                                 std::uint32_t size) {
  auto *wrapper = static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self);
  return ForwardInnerWritableVf2(wrapper->inner, a2,
                                 static_cast<int>(relative_offset & 0xFFFFFFFFu),
                                 static_cast<int>(relative_offset >> 32), size);
}
int IOUnitContainer_CreateFileUnit_vf21(void *self) {
  return ForwardInnerVf3(static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self)->inner);
}
std::uint8_t IOUnitContainer_CreateFileUnit_vf22(void *self,
                                                 std::uint32_t *out_offset_parts) {
  auto *wrapper = static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self);
  if (!ForwardInnerVf4(wrapper->inner, out_offset_parts)) {
    return false;
  }
  const std::uint64_t absolute_end = static_cast<std::uint64_t>(out_offset_parts[0]) |
                                     (static_cast<std::uint64_t>(out_offset_parts[1]) << 32);
  if (absolute_end <= wrapper->base_offset) {
    out_offset_parts[0] = 0;
    out_offset_parts[1] = 0;
    return true;
  }
  const std::uint64_t rebased_end =
      std::min(absolute_end, wrapper->end_offset) - wrapper->base_offset;
  out_offset_parts[0] = static_cast<std::uint32_t>(rebased_end);
  out_offset_parts[1] = static_cast<std::uint32_t>(rebased_end >> 32);
  return true;
}
bool IOUnitContainer_CreateFileUnit_vf23(void *self, std::uint64_t size) {
  auto *wrapper = static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self);
  if (detail::ComparePackedSignedQwords(size, wrapper->end_offset) > 0) {
    return false;
  }
  return ForwardInnerVf5(wrapper->inner, static_cast<int>(size & 0xFFFFFFFFu),
                         static_cast<int>(size >> 32));
}
int IOUnitContainer_CreateFileUnit_vf24(void *self) {
  return ForwardInnerNullary(static_cast<IOUnitContainerBoundedFileWrapperCompat *>(self)->inner);
}
bool IOUnitContainer_CreateFileUnit_vf1_Stub(int, int, int, int, int) { return false; }
bool IOUnitContainer_ReadOnly_vf2_Stub(int, int, int, int) { return false; }
std::uint8_t IOUnitContainer_ReadOnly_vf5_Stub(void *, int, int) { return 0; }

void *IOUnitSourcePathBase_Destroy(void *self, char free_self) {
  auto *source_path_base = static_cast<IOUnitSourcePathBaseCompat *>(self);
  if (source_path_base->path_buffer != source_path_base->inline_path) {
    ::operator delete[](source_path_base->path_buffer);
  }
  source_path_base->vtable = &ForwardingWrapperBaseVTable();
  if ((free_self & 1) != 0) {
    ::operator delete(self);
  }
  return self;
}
void *IOUnitContainer_ArchiveFile_Destroy(void *self, char free_self) {
  static_cast<IOUnitSourcePathBaseCompat *>(self)->vtable = &ArchiveFileVTable();
  return IOUnitSourcePathBase_Destroy(self, free_self);
}
void *IOUnitSourcePathBase_GetRetainedSource_vf9(void *self) {
  return static_cast<IOUnitSourcePathBaseCompat *>(self)->retained_source;
}
const char *IOUnitSourcePathBase_GetPath_vf10(void *self) {
  return static_cast<IOUnitSourcePathBaseCompat *>(self)->path_buffer;
}
std::uint8_t IOUnitContainer_CreateReadOnlyFile_vf4(void *self,
                                                    std::uint32_t *out_offset_parts) {
  auto *file = static_cast<IOUnitContainerReadOnlyFileCompat *>(self);
  out_offset_parts[0] = file->cached_block_entry.file_size_lo;
  out_offset_parts[1] = 0;
  return true;
}
bool IOUnitContainer_CreateReadOnlyFile_vf12(void *self,
                                             std::uint32_t *out_offset_parts) {
  auto *file = static_cast<IOUnitContainerReadOnlyFileCompat *>(self);
  out_offset_parts[0] = file->cached_block_entry.compressed_size_lo;
  out_offset_parts[1] = 0;
  return true;
}
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetRetainedSourceField18_vf13(void *self) {
  auto *source = static_cast<IOUnitRetainedSourceField18Compat *>(
      static_cast<IOUnitSourcePathBaseCompat *>(self)->retained_source);
  return source->field_18;
}
std::uint64_t IOUnitContainer_CreateReadOnlyFile_vf14(void *self) {
  auto *file = static_cast<IOUnitContainerReadOnlyFileCompat *>(self);
  return static_cast<std::uint64_t>(file->cached_block_entry.file_offset_low) |
         (static_cast<std::uint64_t>(file->cached_block_entry.file_offset_high) << 32);
}
std::uint64_t IOUnitContainer_CreateReadOnlyFile_GetAttributeQword_vf15(void *self) {
  return static_cast<IOUnitContainerReadOnlyFileCompat *>(self)->cached_block_entry.attribute_qword;
}
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetAttributeDword_vf16(void *self) {
  return static_cast<IOUnitContainerReadOnlyFileCompat *>(self)->cached_block_entry.attribute_dword;
}
const std::uint8_t *IOUnitContainer_CreateReadOnlyFile_GetAttributeMd5Digest_vf17(void *self) {
  return static_cast<IOUnitContainerReadOnlyFileCompat *>(self)->cached_block_entry.attribute_md5;
}
std::uint8_t IOUnitContainer_CreateReadOnlyFile_GetAttributeLookupFlag_vf18(void *self) {
  return static_cast<IOUnitContainerReadOnlyFileCompat *>(self)
      ->cached_block_entry.attribute_lookup_flag;
}
std::uint32_t IOUnitContainer_CreateReadOnlyFile_GetBlockFlags_vf20(void *self) {
  return static_cast<IOUnitContainerReadOnlyFileCompat *>(self)->cached_block_entry.flags;
}
char IOUnitContainer_CreateReadOnlyFile_SetReadOnlyFlagAndUpdateBlockEntry_vf21(void *self) {
  auto *file = static_cast<IOUnitContainerReadOnlyFileCompat *>(self);
  file->cached_block_entry.flags |= 0x02000000u;
  auto *archive_handle = GetReadOnlyArchiveHandleCompat(file);
  openwow::core::UpdateBlockTableEntry(archive_handle->block_table, file->block_index,
                                       file->cached_block_entry);
  return true;
}
std::uintptr_t IOUnitContainer_CreateWritableFile_CommitCachedBlockEntry(
    void *self, const std::uint32_t committed_compressed_size) {
  auto *file = static_cast<IOUnitContainerReadOnlyFileCompat *>(self);
  file->cached_block_entry.compressed_size_lo = committed_compressed_size;
  auto *archive_handle = GetReadOnlyArchiveHandleCompat(file);
  openwow::core::UpdateBlockTableEntry(archive_handle->block_table, file->block_index,
                                       file->cached_block_entry);
  archive_handle->AdvanceNextWriteOffset(committed_compressed_size);
  return reinterpret_cast<std::uintptr_t>(&archive_handle->block_table[file->block_index]);
}
bool IOUnitContainer_CreateReadOnlyFile_HasHighFlags_vf22(void *self) {
  return (static_cast<IOUnitContainerReadOnlyFileCompat *>(self)->cached_block_entry.flags &
          0x000F0000u) != 0;
}

}

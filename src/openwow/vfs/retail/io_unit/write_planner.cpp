#include "openwow/vfs/retail/io_unit/write_planner.h"

#include <algorithm>
#include <new>

namespace openwow::vfs {
namespace {

constexpr std::size_t kSFileCreateFileWritePlannerReservationSize = 144;

void InitializeCommon(SFileCreateFileWritePlannerCompat *self,
                      std::span<const std::uint16_t> utf16_path,
                      std::uint32_t logical_file_size, std::uint32_t block_size,
                      std::uintptr_t finalize_callback, std::uint8_t owned,
                      std::uintptr_t inner) {
  if (!self) {
    return;
  }
  self->inner = inner;
  self->owned = owned;
  self->refcount = 1;
  self->logical_file_size = logical_file_size;
  self->packed_sector_sizes.clear();
  self->compress_sector_table = false;
  self->sector_table.clear();
  self->block_size = block_size;
  self->current_sector_index = 0;
  self->utf16_path.assign(utf16_path.begin(), utf16_path.end());
  self->finalize_callback = finalize_callback;
  self->callback_context = 0;
}

}

void *SFileCreateFile_CreateSectorWritePlanner(
    IOUnitContainerCompatAllocState *const alloc_state,
    const std::span<const std::uint16_t> utf16_path,
    const std::uint32_t logical_file_size, const std::uint32_t block_size,
    const std::uintptr_t finalize_callback,
    SFileCreateFileWritePlannerCompat **const out_self, void *const inner) {
  if (inner == nullptr) {
    return nullptr;
  }
  if (logical_file_size == 0) {
    return inner;
  }
  std::uint8_t owned = 0;
  void *const storage = detail::AllocateIOUnitContainerCompatStorage(
      alloc_state, kSFileCreateFileWritePlannerReservationSize,
      sizeof(SFileCreateFileWritePlannerCompat), &owned);
  if (storage == nullptr) {
    return nullptr;
  }
  const std::uint32_t sector_count = (logical_file_size + block_size - 1u) / block_size;
  return SFileCreateFile_InitSectorWritePlanner(
      static_cast<SFileCreateFileWritePlannerCompat *>(storage), utf16_path, logical_file_size,
      sector_count, block_size, finalize_callback, out_self, owned,
      reinterpret_cast<std::uintptr_t>(inner));
}

SFileCreateFileWritePlannerCompat *SFileCreateFile_InitSectorWritePlanner(
    SFileCreateFileWritePlannerCompat *const self,
    const std::span<const std::uint16_t> utf16_path,
    const std::uint32_t logical_file_size, const std::uint32_t sector_count,
    const std::uint32_t block_size, const std::uintptr_t finalize_callback,
    SFileCreateFileWritePlannerCompat **const out_self, const std::uint8_t owned,
    const std::uintptr_t inner) {
  InitializeCommon(self, utf16_path, logical_file_size, block_size, finalize_callback, owned,
                   inner);
  if (self) {
    self->packed_sector_sizes.assign(sector_count, 0u);
    self->sector_table.assign(static_cast<std::size_t>(sector_count) + 2u, 0u);
    if (!self->sector_table.empty()) {
      self->sector_table.front() = 4u * (sector_count + 2u);
    }
  }
  if (out_self) {
    *out_self = self;
  }
  return self;
}

void *SFileCreateFile_CreateSingleUnitWritePlanner(
    IOUnitContainerCompatAllocState *const alloc_state,
    const std::span<const std::uint16_t> utf16_path,
    const std::uint32_t logical_file_size, const std::uintptr_t finalize_callback,
    SFileCreateFileWritePlannerCompat **const out_self, void *const inner) {
  if (inner == nullptr) {
    return nullptr;
  }
  if (logical_file_size == 0 || utf16_path.empty()) {
    return inner;
  }
  std::uint8_t owned = 0;
  void *const storage = detail::AllocateIOUnitContainerCompatStorage(
      alloc_state, kSFileCreateFileWritePlannerReservationSize,
      sizeof(SFileCreateFileWritePlannerCompat), &owned);
  if (storage == nullptr) {
    return nullptr;
  }
  return SFileCreateFile_InitSingleUnitWritePlanner(
      static_cast<SFileCreateFileWritePlannerCompat *>(storage), utf16_path, logical_file_size,
      finalize_callback, out_self, owned, reinterpret_cast<std::uintptr_t>(inner));
}

SFileCreateFileWritePlannerCompat *SFileCreateFile_InitSingleUnitWritePlanner(
    SFileCreateFileWritePlannerCompat *const self,
    const std::span<const std::uint16_t> utf16_path,
    const std::uint32_t logical_file_size, const std::uintptr_t finalize_callback,
    SFileCreateFileWritePlannerCompat **const out_self, const std::uint8_t owned,
    const std::uintptr_t inner) {
  InitializeCommon(self, utf16_path, logical_file_size, logical_file_size, finalize_callback,
                   owned, inner);
  if (out_self) {
    *out_self = self;
  }
  return self;
}

void SFileCreateFile_DestroyWritePlanner(SFileCreateFileWritePlannerCompat *const self) {
  if (!self) {
    return;
  }
  self->packed_sector_sizes.clear();
  self->sector_table.clear();
  self->utf16_path.clear();
  self->inner = 0;
  self->owned = 0;
  self->refcount = 0;
  self->logical_file_size = 0;
  self->compress_sector_table = false;
  self->block_size = 0;
  self->current_sector_index = 0;
  self->finalize_callback = 0;
  self->callback_context = 0;
}

SFileCreateFileWritePlannerCompat *SFileCreateFile_DeleteWritePlanner(
    SFileCreateFileWritePlannerCompat *const self, const std::uint8_t free_self) {
  SFileCreateFile_DestroyWritePlanner(self);
  if ((free_self & 1u) != 0u) {
    ::operator delete(self);
  }
  return self;
}

std::uint64_t SFileCreateFile_GetWritePlannerLogicalWrittenSize(
    const SFileCreateFileWritePlannerCompat &self) {
  const auto committed_size =
      static_cast<std::uint64_t>(self.block_size) * self.current_sector_index;
  return std::min<std::uint64_t>(self.logical_file_size, committed_size);
}

}

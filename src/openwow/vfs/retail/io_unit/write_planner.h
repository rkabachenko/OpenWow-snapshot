#pragma once

#include "openwow/vfs/retail/io_unit/io_unit_compat.h"

#include <cstdint>
#include <span>
#include <vector>

namespace openwow::vfs {

struct SFileCreateFileWritePlannerCompat {
  std::uintptr_t inner = 0;
  std::uint8_t owned = 0;
  std::uint32_t refcount = 1;
  std::uint32_t logical_file_size = 0;
  std::vector<std::uint32_t> packed_sector_sizes;
  bool compress_sector_table = false;
  std::vector<std::uint32_t> sector_table;
  std::uint32_t block_size = 0;
  std::uint32_t current_sector_index = 0;
  std::vector<std::uint16_t> utf16_path;
  std::uintptr_t finalize_callback = 0;
  std::uintptr_t callback_context = 0;
};

void *SFileCreateFile_CreateSectorWritePlanner(
    IOUnitContainerCompatAllocState *alloc_state, std::span<const std::uint16_t> utf16_path,
    std::uint32_t logical_file_size, std::uint32_t block_size,
    std::uintptr_t finalize_callback, SFileCreateFileWritePlannerCompat **out_self, void *inner);
SFileCreateFileWritePlannerCompat *SFileCreateFile_InitSectorWritePlanner(
    SFileCreateFileWritePlannerCompat *self, std::span<const std::uint16_t> utf16_path,
    std::uint32_t logical_file_size, std::uint32_t sector_count, std::uint32_t block_size,
    std::uintptr_t finalize_callback, SFileCreateFileWritePlannerCompat **out_self,
    std::uint8_t owned, std::uintptr_t inner);
void *SFileCreateFile_CreateSingleUnitWritePlanner(
    IOUnitContainerCompatAllocState *alloc_state, std::span<const std::uint16_t> utf16_path,
    std::uint32_t logical_file_size, std::uintptr_t finalize_callback,
    SFileCreateFileWritePlannerCompat **out_self, void *inner);
SFileCreateFileWritePlannerCompat *SFileCreateFile_InitSingleUnitWritePlanner(
    SFileCreateFileWritePlannerCompat *self, std::span<const std::uint16_t> utf16_path,
    std::uint32_t logical_file_size, std::uintptr_t finalize_callback,
    SFileCreateFileWritePlannerCompat **out_self, std::uint8_t owned, std::uintptr_t inner);
void SFileCreateFile_DestroyWritePlanner(SFileCreateFileWritePlannerCompat *self);
SFileCreateFileWritePlannerCompat *SFileCreateFile_DeleteWritePlanner(
    SFileCreateFileWritePlannerCompat *self, std::uint8_t free_self);
std::uint64_t SFileCreateFile_GetWritePlannerLogicalWrittenSize(
    const SFileCreateFileWritePlannerCompat &self);

}

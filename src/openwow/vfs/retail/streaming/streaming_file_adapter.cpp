#include "openwow/vfs/retail/streaming/streaming_file_adapter.h"

#include "openwow/core/io_unit_container.h"
#include "openwow/core/storm_file_io.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/streaming_init.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/file_stack/file_stack_abi.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"
#include "openwow/vfs/retail/runtime_file.h"
#include "openwow/vfs/retail/runtime_file_registry.h"
#include "openwow/vfs/retail/streaming/streaming_dispatch_lifetime.h"
#include "openwow/vfs/retail/streaming/streaming_read_plan.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace openwow::vfs {
namespace {

constexpr std::uint32_t kFileStackAttributeArchive = 0x0008u;
constexpr std::uint32_t kFileStackAttributeNonDirectory = 0x0020u;

std::uint64_t CombineDwords(std::uint32_t low, std::uint32_t high) {
  return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32u);
}

void SplitDwords(std::uint64_t value, std::uint32_t &low, std::uint32_t &high) {
  low = static_cast<std::uint32_t>(value);
  high = static_cast<std::uint32_t>(value >> 32u);
}

std::optional<std::string> BuildLookupKey(const char *path) {
  if (!path || path[0] == '\0') {
    return std::nullopt;
  }
  char absolute[260]{};
  if (!FileSystem_MakeAbsolutePath(path, absolute, static_cast<int>(sizeof(absolute)))) {
    return std::nullopt;
  }
  std::string key;
  key.reserve(sizeof(absolute) - 1u);
  for (std::size_t index = 0; absolute[index] != '\0'; ++index) {
    char value = absolute[index] == '/' ? '\\' : absolute[index];
    key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
  }
  return key;
}

bool OpenPartFile(void *callback_table, const char *logical_path, std::uint32_t open_flags,
                  int *out_handle) {
  if (!callback_table || !logical_path || !out_handle) {
    return false;
  }
  *out_handle = 0;
  auto &storage = openwow::core::StreamingStorage::Instance();
  const bool simple_mode = storage.IsSimpleMode();
  const std::uint32_t attempt_limit = simple_mode ? 25u : 1u;
  const std::uint32_t part_open_flags = (open_flags & 0xfffffff0u) | 3u;
  StormArchiveEventDispatcherCompat provider{};
  provider.callback_table = callback_table;
  openwow::core::IOUnitContainerPartFileInfo part_info{};
  part_info.path = logical_path;
  for (std::uint32_t attempt = 0; attempt < attempt_limit; ++attempt) {
    std::array<char, 260> part_path{};
    storage.BuildVariantPath(part_path.data(), static_cast<int>(part_path.size()), logical_path,
                             "part");
    const bool exists = FileStack_QueryPathAttributes(callback_table, part_path.data()) != 0;
    if (!exists && !openwow::core::IOUnitContainer_DeletePartFile(&provider, &part_info)) {
      if (!simple_mode || attempt + 1u >= attempt_limit) {
        return false;
      }
      storage.IncrementVariantPathRetryOrdinal();
      continue;
    }
    int handle = 0;
    if (OpenRuntimeFileFromFileStackOpen(part_path.data(), part_open_flags, &handle)) {
      *out_handle = handle;
      return true;
    }
    if (!simple_mode || attempt + 1u >= attempt_limit) {
      return false;
    }
    storage.IncrementVariantPathRetryOrdinal();
  }
  return false;
}

openwow::core::StreamingEntry BuildEntryMetadata(
    const RuntimeFile::StreamingPartBacking &backing) {
  openwow::core::StreamingEntry entry = backing.entry_metadata;
  entry.storageBytesUsed = backing.part_state.storageEnd;
  const auto &runtime_blocks = backing.part_state.entry.GetBlocks();
  entry.blocks.resize(runtime_blocks.size());
  for (std::size_t index = 0; index < runtime_blocks.size(); ++index) {
    auto &block = entry.blocks[index];
    const auto &runtime_block = runtime_blocks[index];
    block.key = static_cast<std::uint32_t>(runtime_block.state);
    SplitDwords(runtime_block.partFileOffset, block.size_lo, block.size_hi);
    SplitDwords(runtime_block.auxiliaryValue, block.dup_size_lo, block.dup_size_hi);
    block.field14 = runtime_block.blockSize;
  }
  return entry;
}

std::optional<StreamingDirectRequest> ResolveDirectRequest(
    const RuntimeFile::StreamingPartBacking &backing) {
  if (!backing.lookup_key.empty()) {
    if (auto request = BuildStreamingDirectRequestForLookupKey(
            backing.lookup_key, backing.entry_metadata.filename.c_str())) {
      return request;
    }
  }
  return backing.direct_request;
}

openwow::core::StreamingPartBlockState RestoreState(
    const SFileReadPlanAvailabilitySpan &span) {
  return static_cast<openwow::core::StreamingPartBlockState>(span.restore_state);
}

bool QueueDeferredSpan(RuntimeFile &file, const SFileReadPlanAvailabilitySpan &span) {
  if (!file.streaming_part_backing || !file.file_io || file.handle_id == 0) {
    return false;
  }
  auto &backing = *file.streaming_part_backing;
  if (backing.part_state.blockSize == 0) {
    return false;
  }
  const auto direct = ResolveDirectRequest(backing);
  if (!direct) {
    return false;
  }
  auto &storage = openwow::core::StreamingStorage::Instance();
  if (!storage.TrySetPartEntryBlockStateRange(backing.part_state.entry, span.storage_offset,
                                              span.storage_size,
                                              openwow::core::StreamingPartBlockState::Busy)) {
    return false;
  }
  std::vector<std::uint8_t> bytes(span.storage_size);
  const StreamingSourceRangeRequest request{direct->source_url, backing.entry_metadata.fileSize,
                                            span.storage_offset, span.storage_size,
                                            direct->max_retry_count, direct->transport};
  if (!ReadStreamingSourceRangeByScheme(request, bytes.data())) {
    storage.SetPartEntryBlockStateRange(backing.part_state.entry, span.storage_offset,
                                        span.storage_size, RestoreState(span));
    if (!backing.lookup_key.empty()) {
      openwow::data::ClearFileManifestPrefixActiveOnDeferredReadFailure(
          backing.lookup_key, backing.entry_metadata.filename);
    }
    return false;
  }
  for (std::size_t offset = 0; offset < bytes.size(); offset += 0x4000u) {
    const auto size = std::min<std::size_t>(0x4000u, bytes.size() - offset);
    openwow::core::StreamingPartWriteTask task;
    task.metadataMode = openwow::core::StreamingPartWriteMetadataMode::PartFileBlockTable;
    task.partFileHandle = file.handle_id;
    task.partFileState = &backing.part_state;
    task.pendingWrite.logicalBlockOffset = span.storage_offset + offset;
    task.pendingWrite.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
    if (!storage.QueuePendingPartWriteTask(std::move(task))) {
      storage.SetPartEntryBlockStateRange(backing.part_state.entry, span.storage_offset,
                                          span.storage_size, RestoreState(span));
      return false;
    }
  }
  return true;
}

}

std::optional<StreamingDirectRequest>
BuildStreamingDirectRequestForLookupKey(const std::string &lookup_key,
                                        const char *source_path) {
  if (const auto request = openwow::data::BuildFileManifestDirectFileRequest(lookup_key)) {
    StreamingDirectRequest direct;
    direct.source_url = request->source_url;
    direct.max_retry_count = static_cast<std::uint32_t>(std::max(request->max_retry, 0));
    if (request->resolved_transport_section) {
      direct.transport.checksum_block_size = static_cast<std::uint32_t>(
          std::max(request->resolved_transport_section->md5_size, 0));
      direct.transport.split_size = static_cast<std::uint32_t>(
          std::max(request->resolved_transport_section->split_size, 0));
    }
    return direct;
  }
  const auto absolute = BuildAbsoluteFilesystemPath(source_path);
  if (!std::filesystem::is_regular_file(absolute)) {
    return std::nullopt;
  }
  StreamingDirectRequest direct;
  direct.source_url = "file://" + ToUtf8FilesystemPathString(absolute);
  return direct;
}

bool TryOpenStreamingPartBackingHandle(void *callback_table, const char *logical_path,
                                       std::uint32_t open_flags, int *out_handle) {
  if (!out_handle) {
    return false;
  }
  *out_handle = 0;
  const auto lookup_key = BuildLookupKey(logical_path);
  if (!lookup_key) {
    return false;
  }
  auto &storage = openwow::core::StreamingStorage::Instance();
  auto *entry = storage.LookupStreamingEntry(nullptr, &*lookup_key, nullptr, 0, nullptr, 0);
  if (!entry) {
    return false;
  }
  if (FileStack_QueryPathAttributes(callback_table, logical_path) != 0) {
    FileStackPathMetadata metadata{};
    if (FileStack_QueryPathMetadata(callback_table, logical_path, &metadata) &&
        CombineDwords(metadata.file_size_low, metadata.file_size_high) == entry->fileSize) {
      return false;
    }
  }
  auto direct = BuildStreamingDirectRequestForLookupKey(*lookup_key, logical_path);
  if ((entry->flags & 4u) != 0 && !direct) {
    return false;
  }
  if ((entry->flags & 4u) != 0) {
    auto file = std::make_shared<RuntimeFile>();
    file->handle.type = 0;
    file->logical_path = logical_path ? logical_path : "";
    file->size = entry->fileSize;
    file->object_kind = SFileNativeObjectKind::kFile;
    file->non_directory_hint = true;
    file->translated_attributes = kFileStackAttributeArchive | kFileStackAttributeNonDirectory;
    file->SyncSizeFields();
    file->streaming_part_backing.emplace();
    file->streaming_part_backing->entry_metadata = *entry;
    file->streaming_part_backing->lookup_key = *lookup_key;
    file->streaming_part_backing->direct_request = std::move(direct);
    *out_handle = RetailRuntimeFileRegistry().Store(std::move(file));
    return true;
  }

  int handle = 0;
  if (!OpenPartFile(callback_table, logical_path, open_flags, &handle)) {
    return false;
  }
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!file) {
    return false;
  }
  file->logical_path = logical_path ? logical_path : "";
  file->object_kind = SFileNativeObjectKind::kFile;
  file->non_directory_hint = true;
  RuntimeFile::StreamingPartBacking backing;
  backing.entry_metadata = *entry;
  backing.lookup_key = *lookup_key;
  backing.direct_request = std::move(direct);
  backing.part_state.headerPath = file->logical_path;
  backing.part_state.logicalFileSize = entry->fileSize;
  backing.part_state.blockSize = entry->blockSize;
  if (!storage.OpenPartFile(handle, &backing.part_state)) {
    (void)RetailRuntimeFileRegistry().Remove(handle);
    return false;
  }
  backing.entry_metadata.storageBytesUsed = backing.part_state.storageEnd;
  const auto &blocks = backing.part_state.entry.GetBlocks();
  if (blocks.size() == backing.entry_metadata.blocks.size()) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      auto &block = backing.entry_metadata.blocks[index];
      block.key = blocks[index].state == openwow::core::StreamingPartBlockState::Available ? 3u : 0u;
      SplitDwords(blocks[index].partFileOffset, block.size_lo, block.size_hi);
      SplitDwords(blocks[index].auxiliaryValue, block.dup_size_lo, block.dup_size_hi);
      block.field14 = blocks[index].blockSize;
    }
  }
  file->streaming_part_backing = std::move(backing);
  file->size = entry->fileSize;
  file->SyncSizeFields();
  *out_handle = handle;
  return true;
}

bool ReadStreamingPartBackingAtOffsetLocked(RuntimeFile &file, void *buffer,
                                            std::uint64_t offset,
                                            std::uint32_t *inout_bytes_to_read,
                                            bool require_exact) {
  if (!inout_bytes_to_read || !file.streaming_part_backing) {
    return false;
  }
  auto &backing = *file.streaming_part_backing;
  const std::uint32_t requested = *inout_bytes_to_read;
  *inout_bytes_to_read = 0;
  if (requested == 0) {
    return true;
  }
  if (!file.file_io || backing.part_state.blockSize == 0) {
    const auto direct = ResolveDirectRequest(backing);
    if (!direct) {
      return false;
    }
    const StreamingSourceRangeRequest request{direct->source_url, backing.entry_metadata.fileSize,
                                              offset, requested, direct->max_retry_count,
                                              direct->transport};
    if (!ReadStreamingSourceRangeByScheme(request, buffer)) {
      return false;
    }
    openwow::data::Streaming_RecordDeferredDownloadBytes(requested);
    *inout_bytes_to_read = requested;
    return true;
  }
  const auto descriptors = BuildSFileReadPlanDescriptors(
      BuildEntryMetadata(backing), static_cast<std::int64_t>(offset),
      static_cast<std::int32_t>(requested));
  if (descriptors.empty()) {
    return !require_exact;
  }
  std::uint64_t deferred_bytes = 0;
  for (const auto &descriptor : descriptors) {
    if (descriptor.kind == SFileReadPlanDescriptorKind::kMissingCompressed ||
        descriptor.kind == SFileReadPlanDescriptorKind::kPartialCompressed ||
        descriptor.kind == SFileReadPlanDescriptorKind::kDependency) {
      deferred_bytes += descriptor.logical_size;
    }
  }
  const auto spans = BuildBlockingSFileReadPlanAvailabilitySpans(
      descriptors, backing.part_state.blockSize);
  if (!DispatchBlockingSFileReadPlanAvailabilitySpans(
          spans, [&file](const SFileReadPlanAvailabilitySpan &span) {
            return QueueDeferredSpan(file, span);
          })) {
    return false;
  }
  openwow::data::Streaming_RecordDeferredDownloadBytes(deferred_bytes);
  auto &storage = openwow::core::StreamingStorage::Instance();
  auto *output = static_cast<std::byte *>(buffer);
  std::uint32_t bytes_read = 0;
  for (const auto &descriptor : descriptors) {
    if (descriptor.logical_size == 0) {
      continue;
    }
    const bool ok = storage.ReadPartEntryLogicalSpan(
        backing.part_state.entry, output, descriptor.logical_offset, descriptor.logical_size,
        [&](std::uint64_t part_offset) {
          std::uint32_t local = descriptor.logical_size;
          return file.file_io->ReadAllowShort(output, static_cast<std::int64_t>(part_offset),
                                              descriptor.logical_size, &local) &&
                 local == descriptor.logical_size;
        });
    if (!ok) {
      return false;
    }
    if (descriptor.kind == SFileReadPlanDescriptorKind::kResidentRaw) {
      openwow::data::Streaming_RecordLocalReadBytes(descriptor.logical_size);
    }
    output += descriptor.logical_size;
    bytes_read += descriptor.logical_size;
  }
  *inout_bytes_to_read = bytes_read;
  return !require_exact || bytes_read == requested;
}

}

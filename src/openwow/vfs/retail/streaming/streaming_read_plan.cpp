#include "openwow/vfs/retail/streaming/streaming_read_plan.h"

#include "openwow/core/streaming_storage.h"
#include "openwow/data/streaming_init.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <exception>

namespace openwow::vfs {
namespace {

struct NormalizedRange {
  std::uint64_t logical_offset = 0;
  std::uint32_t span_size = 0;
};

[[noreturn]] void FailParameterContract() { std::terminate(); }

NormalizedRange NormalizeRange(std::uint64_t logical_file_size, std::int64_t logical_offset,
                               std::int32_t requested_size) {
  const auto capped = std::min<std::uint64_t>(
      logical_file_size, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
  const auto file_size = static_cast<std::int64_t>(capped);
  const auto offset = std::clamp<std::int64_t>(logical_offset, 0, file_size);
  std::int64_t end = offset;
  if (requested_size > 0 && offset > std::numeric_limits<std::int64_t>::max() - requested_size) {
    end = std::numeric_limits<std::int64_t>::max();
  } else if (requested_size < 0 &&
             offset < std::numeric_limits<std::int64_t>::min() - requested_size) {
    end = std::numeric_limits<std::int64_t>::min();
  } else {
    end += requested_size;
  }
  end = std::clamp<std::int64_t>(end, 0, file_size);
  return {static_cast<std::uint64_t>(offset),
          end > offset ? static_cast<std::uint32_t>(end - offset) : 0u};
}

std::uint32_t LogicalSectorSize(std::uint64_t file_size, std::uint32_t block_size,
                                std::uint32_t sector) {
  const auto start = static_cast<std::uint64_t>(sector) * block_size;
  const auto end = start + block_size;
  return file_size <= end ? static_cast<std::uint32_t>(file_size - start) : block_size;
}

const openwow::core::StreamingEntry::BlockMeta &BlockMeta(
    const openwow::core::StreamingEntry &entry, std::uint32_t sector) {
  if (sector >= entry.blocks.size()) {
    FailParameterContract();
  }
  return entry.blocks[sector];
}

void AppendBaseDescriptors(const openwow::core::StreamingEntry &entry,
                           std::uint64_t logical_offset, std::uint32_t span_size,
                           std::vector<SFileReadPlanDescriptor> *out) {
  if (!out || entry.blockSize == 0 || entry.blocks.empty()) {
    FailParameterContract();
  }
  std::uint64_t cursor = logical_offset;
  std::uint32_t remaining = span_size;
  std::uint32_t sector = static_cast<std::uint32_t>(logical_offset / entry.blockSize);
  while (remaining != 0) {
    const auto sector_base = static_cast<std::uint64_t>(sector) * entry.blockSize;
    const auto consumed = static_cast<std::uint32_t>(cursor - sector_base);
    const auto sector_size = LogicalSectorSize(entry.fileSize, entry.blockSize, sector);
    const auto size = std::min<std::uint32_t>(remaining, sector_size - consumed);
    const auto &block = BlockMeta(entry, sector);
    switch (block.key) {
    case 0:
    case 1:
      out->push_back({cursor, size, 0, sector_base, sector_size,
                      block.key == 1 ? SFileReadPlanDescriptorKind::kPartialCompressed
                                     : SFileReadPlanDescriptorKind::kMissingCompressed});
      break;
    case 2:
    case 3:
    case 4:
      out->push_back({cursor, size, 0, 0, 0,
                      block.key == 4 ? SFileReadPlanDescriptorKind::kPartialRaw
                                     : SFileReadPlanDescriptorKind::kResidentRaw});
      break;
    default:
      break;
    }
    cursor += size;
    remaining -= size;
    ++sector;
  }
}

void AppendDependencies(const openwow::core::StreamingEntry &entry, std::uint32_t begin,
                        std::uint32_t end, std::vector<SFileReadPlanDescriptor> *out) {
  if (!out || entry.blockSize == 0) {
    FailParameterContract();
  }
  for (std::uint32_t sector = begin; sector < end; ++sector) {
    out->push_back({0, 0, 0, static_cast<std::uint64_t>(sector) * entry.blockSize,
                    LogicalSectorSize(entry.fileSize, entry.blockSize, sector),
                    SFileReadPlanDescriptorKind::kDependency});
  }
}

bool IsAvailabilityKind(SFileReadPlanDescriptorKind kind) {
  return kind == SFileReadPlanDescriptorKind::kMissingCompressed ||
         kind == SFileReadPlanDescriptorKind::kPartialCompressed ||
         kind == SFileReadPlanDescriptorKind::kDependency;
}

std::uint32_t RestoreState(SFileReadPlanDescriptorKind kind) {
  return static_cast<std::uint32_t>(
      kind == SFileReadPlanDescriptorKind::kPartialCompressed
          ? openwow::core::StreamingPartBlockState::Reserved
          : openwow::core::StreamingPartBlockState::Missing);
}

void FlushRun(std::uint64_t offset, std::uint32_t size, std::uint32_t count,
              std::uint32_t block_size, std::uint32_t restore_state, bool already_dispatched,
              std::vector<SFileReadPlanAvailabilitySpan> *out) {
  if (!out || count == 0) {
    return;
  }
  if (!already_dispatched && count >= 4) {
    const auto leading_count = count - count / 2u;
    const auto leading_size = block_size * leading_count;
    out->push_back({offset, leading_size, restore_state});
    out->push_back({offset + leading_size, size - leading_size, restore_state});
    return;
  }
  out->push_back({offset, size, restore_state});
}

struct AvailabilityContext {
  const SFileReadPlanAvailabilityDispatch *dispatch = nullptr;
  SFileReadPlanAvailabilitySpan span{};
  bool ok = false;
  bool completed = false;
  std::uint64_t source_id = 0;
  std::mutex mutex;
  std::condition_variable completed_cv;
};

struct ExecutionResult {
  bool ok = false;
  std::uint32_t bytes_read = 0;
};

ExecutionResult ExecuteReadPlan(
    void *callback_table, void *file_context,
    const openwow::core::StreamingPartEntryRuntime &entry_state,
    const std::vector<SFileReadPlanDescriptor> &descriptors, void *buffer,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch,
    std::uint32_t grouped_block_size) {
  std::uint64_t deferred_bytes = 0;
  for (const auto &descriptor : descriptors) {
    if (IsAvailabilityKind(descriptor.kind)) {
      deferred_bytes += descriptor.logical_size;
    }
  }
  const auto spans = BuildBlockingSFileReadPlanAvailabilitySpans(
      descriptors, grouped_block_size != 0 ? grouped_block_size : 0x4000u);
  if (!DispatchBlockingSFileReadPlanAvailabilitySpans(spans, availability_dispatch)) {
    return {};
  }
  openwow::data::Streaming_RecordDeferredDownloadBytes(deferred_bytes);

  ExecutionResult result{true, 0};
  auto *output = static_cast<std::byte *>(buffer);
  for (const auto &descriptor : descriptors) {
    if (descriptor.logical_size == 0) {
      continue;
    }
    if (!FileStack_Streaming_ReadPlannedBlockSpan(callback_table, file_context, entry_state,
                                                  output, descriptor.logical_offset,
                                                  descriptor.logical_size)) {
      result.ok = false;
      return result;
    }
    if (descriptor.kind == SFileReadPlanDescriptorKind::kResidentRaw) {
      openwow::data::Streaming_RecordLocalReadBytes(descriptor.logical_size);
    }
    output += descriptor.logical_size;
    result.bytes_read += descriptor.logical_size;
  }
  return result;
}

}

std::vector<SFileReadPlanDescriptor>
BuildSFileReadPlanDescriptors(const openwow::core::StreamingEntry &entry,
                              std::int64_t logical_offset, std::int32_t requested_size) {
  const auto range = NormalizeRange(entry.fileSize, logical_offset, requested_size);
  if (range.span_size == 0) {
    return {};
  }
  if (entry.blockSize == 0 || entry.blocks.empty()) {
    FailParameterContract();
  }
  std::vector<SFileReadPlanDescriptor> descriptors;
  if ((entry.flags & 2u) != 0) {
    descriptors.reserve(static_cast<std::size_t>(entry.fileSize / entry.blockSize + 4u));
    if (entry.blocks.front().key == 2 || entry.blocks.front().key == 3) {
      AppendBaseDescriptors(entry, range.logical_offset, range.span_size, &descriptors);
      return descriptors;
    }
    const auto first = static_cast<std::uint32_t>(range.logical_offset / entry.blockSize);
    AppendDependencies(entry, 0, first, &descriptors);
    AppendBaseDescriptors(entry, range.logical_offset, range.span_size, &descriptors);
    const auto last_offset = range.logical_offset + range.span_size - 1u;
    const auto post = static_cast<std::uint32_t>(last_offset / entry.blockSize) + 1u;
    AppendDependencies(entry, post,
                       static_cast<std::uint32_t>(entry.fileSize / entry.blockSize) + 1u,
                       &descriptors);
    return descriptors;
  }
  const auto dependency_start = (entry.flags & 1u) != 0
                                    ? std::min<std::uint64_t>(range.logical_offset,
                                                              entry.storageBytesUsed)
                                    : range.logical_offset;
  const auto first_block = dependency_start / entry.blockSize;
  const auto end_block = (range.logical_offset + range.span_size) / entry.blockSize;
  descriptors.reserve(static_cast<std::size_t>(end_block - first_block + 4u));
  if ((entry.flags & 1u) != 0) {
    const auto aligned = (range.logical_offset / entry.blockSize) * entry.blockSize;
    if (dependency_start / entry.blockSize < range.logical_offset / entry.blockSize) {
      const auto start = descriptors.size();
      AppendBaseDescriptors(entry, dependency_start,
                            static_cast<std::uint32_t>(aligned - dependency_start),
                            &descriptors);
      for (std::size_t index = start; index < descriptors.size(); ++index) {
        descriptors[index].kind = SFileReadPlanDescriptorKind::kDependency;
      }
    }
  }
  AppendBaseDescriptors(entry, range.logical_offset, range.span_size, &descriptors);
  return descriptors;
}

std::vector<SFileReadPlanAvailabilitySpan> BuildBlockingSFileReadPlanAvailabilitySpans(
    const std::vector<SFileReadPlanDescriptor> &descriptors, std::uint32_t grouped_block_size) {
  std::vector<SFileReadPlanAvailabilitySpan> spans;
  std::uint64_t offset = 0;
  std::uint32_t size = 0;
  std::uint32_t count = 0;
  std::uint32_t restore_state = 0;
  const auto flush = [&] {
    FlushRun(offset, size, count, grouped_block_size, restore_state, !spans.empty(), &spans);
    offset = 0;
    size = 0;
    count = 0;
    restore_state = 0;
  };
  for (const auto &descriptor : descriptors) {
    if (!IsAvailabilityKind(descriptor.kind)) {
      flush();
      continue;
    }
    const auto descriptor_restore = RestoreState(descriptor.kind);
    if (count == 0 || offset + size != descriptor.storage_offset ||
        restore_state != descriptor_restore) {
      if (count != 0) {
        flush();
      }
      offset = descriptor.storage_offset;
      size = descriptor.storage_size;
      count = 1;
      restore_state = descriptor_restore;
      continue;
    }
    size += descriptor.storage_size;
    ++count;
  }
  flush();
  return spans;
}

bool DispatchBlockingSFileReadPlanAvailabilitySpans(
    const std::vector<SFileReadPlanAvailabilitySpan> &spans,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch) {
  if (spans.empty()) {
    return true;
  }
  if (!availability_dispatch) {
    return false;
  }
  auto &storage = openwow::core::StreamingStorage::Instance();
  storage.InitializeBackgroundDownloadWorkers();
  std::vector<std::shared_ptr<AvailabilityContext>> contexts;
  contexts.reserve(spans.size());
  for (const auto &span : spans) {
    auto context = std::make_shared<AvailabilityContext>();
    context->dispatch = &availability_dispatch;
    context->span = span;
    context->source_id = storage.CreateBackgroundDownloadSource();
    storage.SetBackgroundDownloadSourceDispatchCallback(context->source_id, [context] {
      context->ok = context->dispatch && static_cast<bool>(*context->dispatch) &&
                    (*context->dispatch)(context->span);
      {
        std::lock_guard lock(context->mutex);
        context->completed = true;
      }
      context->completed_cv.notify_all();
      return openwow::core::StreamingStorage::BackgroundDownloadTaskDispatchResult::kComplete;
    });
    contexts.push_back(std::move(context));
  }
  for (const auto &context : contexts) {
    if (!storage.QueueBackgroundDownloadTask(0, context->source_id)) {
      for (const auto &queued : contexts) {
        if (queued->source_id != 0) {
          storage.CloseBackgroundDownloadSource(queued->source_id);
        }
      }
      return false;
    }
  }
  bool ok = true;
  for (const auto &context : contexts) {
    std::unique_lock lock(context->mutex);
    context->completed_cv.wait(lock, [&] { return context->completed; });
    lock.unlock();
    storage.CloseBackgroundDownloadSource(context->source_id);
    ok = ok && context->ok;
  }
  return ok;
}

bool FileStack_Streaming_ReadPartFileAtOffset(void *callback_table, void *file_context,
                                              void *buffer, std::uint64_t offset,
                                              std::uint32_t *inout_bytes_to_read) {
  if (!callback_table || !inout_bytes_to_read) {
    return false;
  }
  std::array<std::byte, 0x90> event{};
  const std::uint32_t opcode = 0x54u;
  std::uint32_t size = *inout_bytes_to_read;
  const auto low = static_cast<std::uint32_t>(offset);
  const auto high = static_cast<std::uint32_t>(offset >> 32);
  std::memcpy(event.data(), &opcode, sizeof(opcode));
  std::memcpy(event.data() + 0x0c, &file_context, sizeof(file_context));
  std::memcpy(event.data() + 0x5c, &buffer, sizeof(buffer));
  std::memcpy(event.data() + 0x60, &size, sizeof(size));
  std::memcpy(event.data() + 0x68, &low, sizeof(low));
  std::memcpy(event.data() + 0x6c, &high, sizeof(high));
  const auto callback = ReadFileStackDispatchSlot(callback_table, 0x54u);
  if (!callback || !callback(callback_table, event.data())) {
    return false;
  }
  std::memcpy(&size, event.data() + 0x60, sizeof(size));
  *inout_bytes_to_read = size;
  return true;
}

bool FileStack_Streaming_ReadPlannedBlockSpan(
    void *callback_table, void *file_context,
    const openwow::core::StreamingPartEntryRuntime &entry_state, void *buffer,
    std::uint64_t logical_offset, std::uint32_t size) {
  return openwow::core::StreamingStorage::Instance().ReadPartEntryLogicalSpan(
      entry_state, buffer, logical_offset, size, [&](std::uint64_t part_offset) {
        std::uint32_t dispatched_size = size;
        return FileStack_Streaming_ReadPartFileAtOffset(callback_table, file_context, buffer,
                                                        part_offset, &dispatched_size);
      });
}

bool FileStack_Streaming_ExecuteReadPlan(
    void *callback_table, void *file_context,
    const openwow::core::StreamingPartEntryRuntime &entry_state,
    const std::vector<SFileReadPlanDescriptor> &descriptors, void *buffer,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch,
    std::uint32_t grouped_block_size) {
  return ExecuteReadPlan(callback_table, file_context, entry_state, descriptors, buffer,
                         availability_dispatch, grouped_block_size).ok;
}

bool FileStack_Streaming_ReadFileHandleAtOffset(
    const openwow::core::StreamingEntry *entry_metadata,
    const openwow::core::StreamingPartEntryRuntime *entry_state,
    void *callback_table, void *file_context, void *buffer, std::uint64_t offset,
    std::uint32_t *inout_bytes_to_read,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch,
    const std::function<bool()> &fallback_read, std::uint32_t grouped_block_size) {
  if (!entry_metadata) {
    return fallback_read ? fallback_read() : false;
  }
  if (!inout_bytes_to_read || !entry_state) {
    if (inout_bytes_to_read) {
      *inout_bytes_to_read = 0;
    }
    return false;
  }
  const auto descriptors = BuildSFileReadPlanDescriptors(
      *entry_metadata, static_cast<std::int64_t>(offset),
      static_cast<std::int32_t>(*inout_bytes_to_read));
  if (descriptors.empty()) {
    *inout_bytes_to_read = 0;
    return false;
  }
  const auto result = ExecuteReadPlan(callback_table, file_context, *entry_state, descriptors,
                                      buffer, availability_dispatch, grouped_block_size);
  *inout_bytes_to_read = result.bytes_read;
  return result.ok;
}

}

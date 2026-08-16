#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace openwow::core {
struct StreamingEntry;
class StreamingPartEntryRuntime;
}

namespace openwow::vfs {

enum class SFileReadPlanDescriptorKind : std::uint32_t {
  kResidentRaw = 0,
  kPartialRaw = 1,
  kMissingCompressed = 2,
  kPartialCompressed = 3,
  kDependency = 4,
};

struct SFileReadPlanDescriptor {
  std::uint64_t logical_offset = 0;
  std::uint32_t logical_size = 0;
  std::uint32_t reserved_0c = 0;
  std::uint64_t storage_offset = 0;
  std::uint32_t storage_size = 0;
  SFileReadPlanDescriptorKind kind = SFileReadPlanDescriptorKind::kResidentRaw;
};
static_assert(sizeof(SFileReadPlanDescriptor) == 32);

struct SFileReadPlanAvailabilitySpan {
  std::uint64_t storage_offset = 0;
  std::uint32_t storage_size = 0;
  std::uint32_t restore_state = 0;
};

using SFileReadPlanAvailabilityDispatch =
    std::function<bool(const SFileReadPlanAvailabilitySpan &)>;

std::vector<SFileReadPlanDescriptor>
BuildSFileReadPlanDescriptors(const openwow::core::StreamingEntry &entry,
                              std::int64_t logical_offset, std::int32_t requested_size);

std::vector<SFileReadPlanAvailabilitySpan> BuildBlockingSFileReadPlanAvailabilitySpans(
    const std::vector<SFileReadPlanDescriptor> &descriptors,
    std::uint32_t grouped_block_size);

bool DispatchBlockingSFileReadPlanAvailabilitySpans(
    const std::vector<SFileReadPlanAvailabilitySpan> &spans,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch);

bool FileStack_Streaming_ReadPartFileAtOffset(void *callback_table, void *file_context,
                                              void *buffer, std::uint64_t offset,
                                              std::uint32_t *inout_bytes_to_read);
bool FileStack_Streaming_ReadPlannedBlockSpan(
    void *callback_table, void *file_context,
    const openwow::core::StreamingPartEntryRuntime &entry_state, void *buffer,
    std::uint64_t logical_offset, std::uint32_t size);
bool FileStack_Streaming_ExecuteReadPlan(
    void *callback_table, void *file_context,
    const openwow::core::StreamingPartEntryRuntime &entry_state,
    const std::vector<SFileReadPlanDescriptor> &descriptors, void *buffer,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch,
    std::uint32_t grouped_block_size = 0x4000u);
bool FileStack_Streaming_ReadFileHandleAtOffset(
    const openwow::core::StreamingEntry *entry_metadata,
    const openwow::core::StreamingPartEntryRuntime *entry_state,
    void *callback_table, void *file_context, void *buffer, std::uint64_t offset,
    std::uint32_t *inout_bytes_to_read,
    const SFileReadPlanAvailabilityDispatch &availability_dispatch,
    const std::function<bool()> &fallback_read = {},
    std::uint32_t grouped_block_size = 0x4000u);

}

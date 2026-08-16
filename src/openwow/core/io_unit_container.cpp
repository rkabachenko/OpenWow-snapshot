
#include "io_unit_container.h"

#include "client_init_internal.h"
#include "mpq_internals.h"
#include "streaming_storage.h"
#include "storm_alloc.h"
#include "storm_path.h"
#include "storm_string.h"
#include "storm_utils.h"
#include "openwow/data/streaming_init.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"
#include "openwow/vfs/retail/file_stack/file_stack_abi.h"
#include "openwow/vfs/retail/sfile_runtime.h"
#include "openwow/vfs/retail/streaming/data_preload_controller.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

#if !defined(_WIN32)
#include <sys/statvfs.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openwow::core {

namespace {

using AsyncFileUnitSectorSizeResolver =
    std::function<std::optional<std::uint32_t>(const std::string&)>;
using StreamUnitSourceLifetimeResolver =
    std::function<IOStreamUnitSourceLifetimeHooks(void*)>;

AsyncFileUnitSectorSizeResolver& MutableAsyncFileUnitSectorSizeResolverForTests() {
    static AsyncFileUnitSectorSizeResolver resolver;
    return resolver;
}

StreamUnitSourceLifetimeResolver&
MutableIOStreamUnitSourceLifetimeResolverForTests() {
    static StreamUnitSourceLifetimeResolver resolver;
    return resolver;
}

template <typename T>
T ReadCompatValueAtOffset(const void* source, const std::size_t offset) {
    T value{};
    if (!source) {
        return value;
    }

    std::memcpy(&value, static_cast<const std::byte*>(source) + offset,
                sizeof(value));
    return value;
}

template <typename T>
void WriteCompatValueAtOffset(std::byte* destination, const std::size_t offset,
                              const T& value) {
    std::memcpy(destination + offset, &value, sizeof(value));
}

bool FileStack_OpenDeletePartHandle(void* callback_table, const char* path,
                                    const std::uint32_t flags,
                                    void** out_handle) {
    using FileStackDispatchFn = bool (*)(void*, void*);

    if (out_handle) {
        *out_handle = nullptr;
    }
    if (!callback_table) {
        return false;
    }

    const auto callback =
        ReadCompatValueAtOffset<FileStackDispatchFn>(callback_table, 0x4Cu);
    if (!callback) {
        return false;
    }

    std::array<std::byte, 0x90> event_record{};
    void* opened_handle = nullptr;
    WriteCompatValueAtOffset(event_record.data(), 0x00u, std::uint32_t{0x4Cu});
    WriteCompatValueAtOffset(event_record.data(), 0x04u, path);
    WriteCompatValueAtOffset(event_record.data(), 0x0Cu, opened_handle);
    WriteCompatValueAtOffset(event_record.data(), 0x58u, flags);

    if (!callback(callback_table, event_record.data())) {
        return false;
    }

    opened_handle = ReadCompatValueAtOffset<void*>(event_record.data(), 0x0Cu);
    if (out_handle) {
        *out_handle = opened_handle;
    }
    return opened_handle != nullptr;
}

void RetainIOUnitSourceHandle(void* source_handle) {
    if (source_handle == nullptr) {
        return;
    }

    if (auto& resolver = MutableIOStreamUnitSourceLifetimeResolverForTests();
        static_cast<bool>(resolver)) {
        auto hooks = resolver(source_handle);
        if (hooks.retain) {
            hooks.retain(source_handle);
        }
    }
}

void ReleaseIOUnitSourceHandle(void* source_handle) {
    if (source_handle == nullptr) {
        return;
    }

    if (auto& resolver = MutableIOStreamUnitSourceLifetimeResolverForTests();
        static_cast<bool>(resolver)) {
        auto hooks = resolver(source_handle);
        if (hooks.release) {
            hooks.release(source_handle);
        }
    }
}

std::int32_t IOAlignSlotOffsetHighDword(const IOAlignSlot& slot) {
    return static_cast<std::int32_t>(slot.tag1);
}

bool IOAlignSlotOffsetLess(const IOAlignSlot& lhs, const IOAlignSlot& rhs) {
    const auto lhs_high = IOAlignSlotOffsetHighDword(lhs);
    const auto rhs_high = IOAlignSlotOffsetHighDword(rhs);
    if (lhs_high != rhs_high) {
        return lhs_high < rhs_high;
    }
    return lhs.tag0 < rhs.tag0;
}

std::size_t ClampIOAlignActiveSlotCount(const IOAlignUnit& unit) {
    return std::min<std::size_t>(static_cast<std::size_t>(unit.slot_count),
                                 unit.slots.size());
}

void IOAlignSlotInsertionSortByOffset(IOAlignSlot* const begin,
                                      IOAlignSlot* const end) {
    if (begin == nullptr || end == nullptr || begin >= end) {
        return;
    }

    for (auto* current = begin + 1; current != end; ++current) {
        const IOAlignSlot preserved = *current;
        if (IOAlignSlotOffsetLess(preserved, *begin)) {
            std::move_backward(begin, current, current + 1);
            *begin = preserved;
            continue;
        }

        auto* insert_at = current;
        while (insert_at != begin
               && IOAlignSlotOffsetLess(preserved, *(insert_at - 1))) {
            *insert_at = *(insert_at - 1);
            --insert_at;
        }
        *insert_at = preserved;
    }
}

std::uint64_t ComposeOffset64(const std::uint32_t low,
                              const std::uint32_t high) {
    return static_cast<std::uint64_t>(low)
           | (static_cast<std::uint64_t>(high) << 32);
}

std::uint32_t OffsetLow32(const std::uint64_t value) {
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFu);
}

std::uint32_t OffsetHigh32(const std::uint64_t value) {
    return static_cast<std::uint32_t>(value >> 32);
}

void PushIOAlignReadRangeError() {
    openwow::data::PushStreamingStatusMessage(
        "IOAlignUnit::Read - Range Error", 1, 10);
}

std::uint32_t ComputeLogicalSectorSize(const std::uint64_t logical_file_size,
                                       const std::uint32_t sector_size,
                                       const std::uint32_t sector_index) {
    const auto sector_start =
        static_cast<std::uint64_t>(sector_index) * sector_size;
    const auto sector_end = sector_start + sector_size;
    if (logical_file_size <= sector_end) {
        return static_cast<std::uint32_t>(logical_file_size)
               - static_cast<std::uint32_t>(sector_start);
    }
    return sector_size;
}

std::uint32_t ComputeMaxReadPlanDescriptorCount(
    const std::uint64_t logical_offset, const std::uint64_t span_size,
    const std::uint32_t sector_size) {
    const auto first_sector = logical_offset / sector_size;
    const auto end_sector = (logical_offset + span_size) / sector_size;
    return static_cast<std::uint32_t>(end_sector - first_sector + 4u);
}

std::string ExtractRootPathForDiskGeometry(const std::string& native_path) {
    if (native_path.empty()) {
        return {};
    }

    const int storm_root_length = GetStormRootPathLength(native_path.c_str());
    if (storm_root_length > 0) {
        const auto prefix_length = std::min<std::size_t>(
            native_path.size(), static_cast<std::size_t>(storm_root_length));
        return native_path.substr(0, prefix_length);
    }

#if !defined(_WIN32)

    const auto root_path = std::filesystem::path(native_path).root_path();
    return root_path.empty() ? std::string() : root_path.string();
#else
    return {};
#endif
}

std::optional<std::uint32_t> QueryBytesPerSectorForRootPath(
    const std::string& root_path) {
    if (root_path.empty()) {
        return std::nullopt;
    }

    if (auto& resolver = MutableAsyncFileUnitSectorSizeResolverForTests();
        static_cast<bool>(resolver)) {
        return resolver(root_path);
    }

#if defined(_WIN32)
    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector = 0;
    DWORD free_clusters = 0;
    DWORD total_clusters = 0;
    if (!::GetDiskFreeSpaceA(root_path.c_str(), &sectors_per_cluster,
                             &bytes_per_sector, &free_clusters,
                             &total_clusters)) {
        return std::nullopt;
    }
    return bytes_per_sector;
#else
    struct statvfs stats {};
    if (::statvfs(root_path.c_str(), &stats) != 0) {
        return std::nullopt;
    }

    const std::uint64_t bytes_per_sector =
        stats.f_bsize != 0 ? static_cast<std::uint64_t>(stats.f_bsize)
                           : static_cast<std::uint64_t>(stats.f_frsize);
    if (bytes_per_sector == 0
        || bytes_per_sector > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(bytes_per_sector);
#endif
}

std::uint32_t ComputeSectorMask(std::optional<std::uint32_t> bytes_per_sector) {
    if (!bytes_per_sector || *bytes_per_sector == 0) {
        return 0xFFFFu;
    }

    const auto value = *bytes_per_sector;
    return ((value - 1u) & value) == 0 ? value - 1u : 0xFFFFu;
}

std::uint64_t RoundUpAsyncFileUnitPhysicalSize(
    const std::uint64_t logical_size, const std::uint32_t sector_mask) {
    const auto mask64 = static_cast<std::uint64_t>(sector_mask);
    return (logical_size + mask64) & ~mask64;
}

IOMpqSectorReaderUnit* AllocateMpqSectorReaderUnit(IOUnitAllocator* alloc,
                                                   std::uint8_t* out_owned) {
    const auto unit_size =
        static_cast<std::uint32_t>(sizeof(IOMpqSectorReaderUnit));

    if (alloc) {
        void* memory = alloc->AllocEntry(unit_size, out_owned);
        if (!memory) {
            return nullptr;
        }
        return new (memory) IOMpqSectorReaderUnit();
    }

    *out_owned = 1;
    void* memory = std::calloc(1, unit_size);
    if (!memory) {
        return nullptr;
    }
    return new (memory) IOMpqSectorReaderUnit();
}

bool OpenRuntimeFileHandleForPath(const char* path, const int flags,
                                  int* out_handle) {
    if (out_handle == nullptr) {
        return false;
    }

    *out_handle = 0;

    if (path == nullptr) {
        return false;
    }

    if (flags == 3079) {

        std::string native_path = path;
#if !defined(_WIN32)
        std::replace(native_path.begin(), native_path.end(), '\\', '/');
#endif
        const std::filesystem::path absolute_path =
            std::filesystem::absolute(std::filesystem::path(native_path));
        return openwow::vfs::OpenLooseFileHandle(
            absolute_path.string().c_str(), "w+b", out_handle);
    }

    return openwow::vfs::SFileOpenFile(nullptr, path, flags, out_handle) != 0;
}

void* AllocateFileUnitStorage(IOUnitAllocator* const alloc,
                              const std::uint32_t unit_size,
                              std::uint8_t* const out_owned) {
    if (alloc != nullptr) {
        return alloc->AllocEntry(unit_size, out_owned);
    }

    *out_owned = 1;
    return SMemAlloc(unit_size, ".\\IOUnitContainer.cpp", 0x44, 0x8);
}

void ReleaseFileUnitStorageOnOpenFailure(void* const storage,
                                         const std::uint8_t owned) {
    if (owned != 0 && storage != nullptr) {
        SMemFree(storage, ".\\IOUnitContainer.cpp", 58, 0);
    }
}

thread_local std::uint8_t g_io_file_unit_direct_read_retry_flag = 0;

std::uint8_t* IOFileUnitDirectReadRetryFlagStorage() {
    return &g_io_file_unit_direct_read_retry_flag;
}

bool TryDirectLooseFileUnitRead(const int handle, void* const dest,
                                const std::uint64_t offset,
                                const std::uint32_t size,
                                std::uint32_t* const out_bytes_read) {
    if (*IOFileUnitDirectReadRetryFlagStorage() == 0) {
        return false;
    }

    openwow::vfs::RuntimeSFileHandleMetadata handle_metadata;
    if (!openwow::vfs::QueryRuntimeSFileHandleMetadata(handle, &handle_metadata)
        || handle_metadata.native_path.empty()) {
        return false;
    }

    std::array<char, 260> logical_path{};
    const int copied_logical_path =
        openwow::vfs::SFileHandle_CopyLogicalPathBounded(
            handle, logical_path.data(),
            static_cast<int>(logical_path.size()));
    const char* const ready_state_path =
        (copied_logical_path != 0 && logical_path[0] != '\0')
            ? logical_path.data()
            : handle_metadata.native_path.c_str();

    if (openwow::vfs::QueryDataPreloadPathReadyState(ready_state_path)
        != openwow::vfs::DataPreloadPathReadyState::kReady) {
        return false;
    }

    return IOFileUnit_DirectRead(static_cast<std::uint32_t>(offset),
                                 handle_metadata.native_path.c_str(), dest,
                                 size, out_bytes_read);
}

bool ReadLooseFileUnitAtOffset(const int handle, void* const dest,
                               const std::uint64_t offset,
                               const std::uint32_t size,
                               std::uint32_t* const out_bytes_read) {
    if (TryDirectLooseFileUnitRead(handle, dest, offset, size,
                                   out_bytes_read)) {
        return true;
    }

    if (out_bytes_read != nullptr) {
        *out_bytes_read = size;
        const bool read_ok = openwow::vfs::IOUnitContainer_ReadFileHandleAtOffset(
            handle, dest, offset, out_bytes_read);
        return read_ok && *out_bytes_read != 0u;
    }

    std::uint32_t exact_bytes_read = size;
    return openwow::vfs::IOUnitContainer_ReadFileHandleAtOffset(
               handle, dest, offset, &exact_bytes_read)
        && exact_bytes_read == size;
}

bool SeekLooseFileUnitCursor(IOLooseFileUnit& unit,
                             const std::uint64_t desired_offset) {
    if (unit.cached_cursor_offset == desired_offset) {
        return true;
    }

    if (!openwow::vfs::IOUnitContainer_SetFileHandleCursor(
            static_cast<int>(unit.file_handle_index), desired_offset)) {
        unit.cached_cursor_offset = IOLooseFileUnit::kInvalidCursorOffset;
        return false;
    }

    unit.cached_cursor_offset = desired_offset;
    return true;
}

void InvalidateLooseFileUnitCursor(IOLooseFileUnit& unit) {
    unit.cached_cursor_offset = IOLooseFileUnit::kInvalidCursorOffset;
}

}

IOUnitAllocator::~IOUnitAllocator() {
    if (block_) {
        std::free(block_);
        block_ = nullptr;
    }
    cursor_ = nullptr;
    block_end_ = nullptr;
}

void IOUnitAllocator::Alloc(uint32_t baseSize, const uint32_t* sizes) {

    uint32_t total = (baseSize + 3) & ~3u;
    if (sizes) {
        const uint32_t* p = sizes;
        while (*p) {
            total = (total + *p + 3) & ~3u;
            ++p;
        }
    }
    block_ = std::calloc(1, total);
    cursor_ = static_cast<uint8_t*>(block_);
    block_end_ = cursor_ ? cursor_ + total : nullptr;
    first_alloc_ = true;
}

void* IOUnitAllocator::AllocEntry(uint32_t size, uint8_t* out_owned) {
    if (block_ && cursor_ && block_end_) {
        const auto available =
            static_cast<std::size_t>(block_end_ - cursor_);
        if (size <= available) {
            void* const result = cursor_;
            auto* next_cursor = cursor_ + size;
            const auto aligned = (reinterpret_cast<std::uintptr_t>(next_cursor) + 3u)
                                 & ~static_cast<std::uintptr_t>(3u);
            cursor_ = reinterpret_cast<uint8_t*>(
                std::min(aligned,
                         reinterpret_cast<std::uintptr_t>(block_end_)));
            *out_owned = first_alloc_ ? 1 : 0;
            first_alloc_ = false;
            return result;
        }
    }

    *out_owned = 1;
    return std::calloc(1, size);
}

void IOInlinePathStorage::Assign(const char* source) {
    const auto source_length = static_cast<std::uint32_t>(
        source != nullptr ? std::strlen(source) : 0u);
    const auto required_length = source_length + 1u;

    if (required_length > kInlineCapacity) {
        char* new_buffer = heap_buffer_;
        if (new_buffer == nullptr || length_ != required_length) {
            new_buffer = static_cast<char*>(
                SMemAlloc(required_length, ".\\IOUnitContainer.cpp", 0, 0));
        }
        if (new_buffer == nullptr) {
            Reset();
            return;
        }
        if (new_buffer != heap_buffer_) {
            if (heap_buffer_ != nullptr) {
                SMemFree(heap_buffer_, ".\\IOUnitContainer.cpp", 0, 0);
            }
            heap_buffer_ = new_buffer;
        }
    } else if (heap_buffer_ != nullptr) {
        SMemFree(heap_buffer_, ".\\IOUnitContainer.cpp", 0, 0);
        heap_buffer_ = nullptr;
    }

    length_ = required_length;
    (void)SStrCopy(data(), source != nullptr ? source : "", length_);
}

void IOInlinePathStorage::Reset() {
    if (heap_buffer_ != nullptr) {
        SMemFree(heap_buffer_, ".\\IOUnitContainer.cpp", 0, 0);
        heap_buffer_ = nullptr;
    }
    length_ = 0;
    inline_buffer_[0] = '\0';
}

void IOStreamUnit::Init(void* source_handle, const char* archive_path,
                        const char* file_path, uint8_t owned_flag,
                        IOUnit* chain_next) {
    next = chain_next;
    owned = owned_flag;
    refcount.store(1);
    reserved_10_ = 0;
    source_handle_ = source_handle;
    archive_path_.Assign(archive_path);
    file_path_.Assign(file_path);
    RetainIOUnitSourceHandle(source_handle_);
}

void IOStreamUnit::Destroy(bool free_self) {
    file_path_.Reset();
    archive_path_.Reset();
    source_handle_ = nullptr;
    reserved_10_ = 0;
    next = nullptr;
    if (free_self) {
        std::free(this);
    }
}

bool IOStreamUnit::Close() {
    if (source_handle_ != nullptr) {
        ReleaseIOUnitSourceHandle(source_handle_);
    }
    return true;
}

std::uint32_t IOStreamUnit::GetField5() const {
    return TruncatePointer(source_handle_);
}

std::uint32_t IOStreamUnit::GetField7() const {
    return TruncatePointer(archive_path_.data());
}

std::uint32_t IOStreamUnit::DispatchField11() {
    return TruncatePointer(file_path_.data());
}

std::uint32_t IOFileReadOnlyUnit::GetTypeTag() const {
    return openwow::vfs::IOUnitContainer_CreateReadOnlyFile_GetTypeTag();
}

bool IOFileReadOnlyUnit::Read(void* dest, const std::uint64_t offset_lo,
                              const std::uint64_t offset_hi,
                              const std::uint32_t size,
                              std::uint32_t* const out_bytes_read) {
    return next != nullptr
           && next->Read(dest, offset_lo, offset_hi, size, out_bytes_read);
}

void IOFileReadOnlyUnit::Init(void* archive_handle, const char* path,
                              const std::uint32_t block_index_value,
                              const BlockTableEntry& cached_block_entry,
                              const std::uint8_t owned_flag,
                              IOUnit* next_unit) {
    InitArchiveFileState(archive_handle, path, block_index_value,
                         cached_block_entry, owned_flag, next_unit);
}

void IOFileReadOnlyUnit::InitArchiveFileState(
    void* archive_handle, const char* path,
    const std::uint32_t block_index_value,
    const BlockTableEntry& cached_block_entry, const std::uint8_t owned_flag,
    IOUnit* next_unit) {
    next = next_unit;
    owned = owned_flag;
    refcount.store(1);
    reserved_10 = 0;
    archive = archive_handle;
    block_index = block_index_value;
    block_entry = cached_block_entry;

    const auto source_length = static_cast<std::uint32_t>(
        path != nullptr ? std::strlen(path) : 0u);
    path_length = source_length + 1u;
    path_ptr = inline_path;

    if (path_length > sizeof(inline_path)) {
        path_ptr = static_cast<char*>(
            SMemAlloc(path_length, ".\\IOUnitContainer.cpp", 0x44, 0));
        if (path_ptr == nullptr) {
            path_ptr = inline_path;
            inline_path[0] = '\0';
            path_length = 1;
        }
    }

    SStrCopy(path_ptr, path != nullptr ? path : "", path_length);

    if (block_entry.attribute_lookup_flag != 0
        || detail::ShouldResolveArchiveIntegrityDigestForPath(path_ptr)) {
        (void)detail::LookupArchiveIntegrityDigestForPath(
            path_ptr, block_entry.attribute_md5);
    }

    RetainIOUnitSourceHandle(archive);
}

void IOFileReadOnlyUnit::Destroy(const bool free_self) {
    DestroyArchiveFileState(free_self);
}

void IOFileReadOnlyUnit::DestroyArchiveFileState(const bool free_self) {
    if (path_ptr != inline_path && path_ptr != nullptr) {
        SMemFree(path_ptr, ".\\IOUnitContainer.cpp", 0x44, 0);
    }
    archive = nullptr;
    path_ptr = inline_path;
    inline_path[0] = '\0';
    path_length = 0;
    block_index = 0;
    next = nullptr;

    if (free_self) {
        std::free(this);
    }
}

bool IOFileReadOnlyUnit::Close() {
    if (archive != nullptr) {
        ReleaseIOUnitSourceHandle(archive);
    }
    return true;
}

std::uint32_t IOFileReadOnlyUnit::GetArchiveField24() const {
    const auto* archive_handle =
        static_cast<const detail::ReadOnlyArchiveHandleCompat*>(archive);
    return archive_handle->field_18;
}

void IOFileReadOnlyUnit::ClearAttributeLookupFlag() {
    auto* archive_handle = static_cast<detail::ReadOnlyArchiveHandleCompat*>(archive);
    ReadArchiveTables_SetAttributeLookupFlag(archive_handle->block_table,
                                            block_index, 0);
    block_entry.attribute_lookup_flag = 0;
}

void IOFileReadOnlyUnit::MarkBlockEntryDirty() {
    block_entry.flags |= 0x02000000u;

    auto* archive_handle = static_cast<detail::ReadOnlyArchiveHandleCompat*>(archive);
    UpdateBlockTableEntry(archive_handle->block_table, block_index, block_entry);
}

std::uint32_t IOFileWritableUnit::GetTypeTag() const {
    return openwow::vfs::IOUnitContainer_CreateWritableFile_GetTypeTag();
}

bool IOFileWritableUnit::Read(void* , const std::uint64_t ,
                              const std::uint64_t ,
                              const std::uint32_t ,
                              std::uint32_t* const ) {
    return false;
}

bool IOFileWritableUnit::Write(const void* source,
                               const std::uint32_t offset_lo,
                               const std::uint32_t offset_hi,
                               const std::uint32_t size) {
    return next != nullptr && next->Write(source, offset_lo, offset_hi, size);
}

void IOLooseFileUnit::Init(const std::uint32_t handle_index,
                           const std::uint8_t owned_flag) {
    next = nullptr;
    owned = owned_flag;
    refcount.store(1);
    file_handle_index = handle_index;
    cached_cursor_offset = kInvalidCursorOffset;
    if (file_handle_index != 0u) {
        (void)SeekLooseFileUnitCursor(*this, 0);
    }
}

bool IOLooseFileUnit::Read(void* const dest, const std::uint64_t offset_lo,
                           const std::uint64_t offset_hi,
                           const std::uint32_t size,
                           std::uint32_t* const out_bytes_read) {
    return ReadWithCount(dest, static_cast<std::uint32_t>(offset_lo),
                         static_cast<std::uint32_t>(offset_hi), size,
                         out_bytes_read);
}

bool IOLooseFileUnit::ReadWithCount(void* const dest,
                                    const std::uint32_t offset_lo,
                                    const std::uint32_t offset_hi,
                                    const std::uint32_t size,
                                    std::uint32_t* const out_bytes_read) {
    const auto desired_offset = ComposeOffset64(offset_lo, offset_hi);
    std::uint32_t bytes_read = 0;
    const std::uint32_t minimum_success_bytes =
        (out_bytes_read != nullptr) ? 1u : size;

    if (out_bytes_read != nullptr) {
        *out_bytes_read = 0;
    }

    if (!SeekLooseFileUnitCursor(*this, desired_offset)) {
        return false;
    }

    bytes_read = size;
    const bool read_ok = openwow::vfs::IOUnitContainer_ReadFileHandle(
        static_cast<int>(file_handle_index), dest, &bytes_read);
    cached_cursor_offset = desired_offset + bytes_read;
    if (!read_ok) {
        InvalidateLooseFileUnitCursor(*this);
        return false;
    }

    if (out_bytes_read != nullptr) {
        *out_bytes_read = bytes_read;
    }
    return bytes_read >= minimum_success_bytes;
}

bool IOLooseFileUnit::Write(const void* source, const std::uint32_t offset_lo,
                            const std::uint32_t offset_hi,
                            const std::uint32_t size) {
    const auto desired_offset = ComposeOffset64(offset_lo, offset_hi);
    if (!SeekLooseFileUnitCursor(*this, desired_offset)) {
        return false;
    }

    const bool write_ok = openwow::vfs::IOUnitContainer_WriteFileHandle(
        static_cast<int>(file_handle_index), source, size);
    if (!write_ok) {
        InvalidateLooseFileUnitCursor(*this);
        return false;
    }

    cached_cursor_offset = desired_offset + size;
    return true;
}

bool IOLooseFileUnit::Flush() {
    return openwow::vfs::IOUnitContainer_FlushFileHandle(
        static_cast<int>(file_handle_index));
}

bool IOLooseFileUnit::GetSize(std::uint32_t* out_lo, std::uint32_t* out_hi) {
    const std::uint64_t size = openwow::vfs::IOUnitContainer_GetFileSizeByHandle(
        static_cast<int>(file_handle_index));

    if (out_lo) {
        *out_lo = static_cast<std::uint32_t>(size & 0xFFFFFFFFu);
    }
    if (out_hi) {
        *out_hi = static_cast<std::uint32_t>(size >> 32);
    }
    return true;
}

bool IOLooseFileUnit::SetSize(const std::uint32_t size_lo,
                              const std::uint32_t size_hi) {
    const auto desired_size = ComposeOffset64(size_lo, size_hi);
    if (!SeekLooseFileUnitCursor(*this, desired_size)) {
        return false;
    }

    const bool resized = openwow::vfs::IOUnitContainer_SetFileHandleSize(
        static_cast<int>(file_handle_index), desired_size, 0);
    if (!resized) {
        InvalidateLooseFileUnitCursor(*this);
        return false;
    }

    cached_cursor_offset = desired_size;
    return true;
}

void IOLooseFileUnit::Destroy(const bool free_self) {
    file_handle_index = 0;
    cached_cursor_offset = kInvalidCursorOffset;
    next = nullptr;
    if (free_self) {
        std::free(this);
    }
}

bool IOLooseFileUnit::Close() {
    if (file_handle_index == 0) {
        return false;
    }

    if (!openwow::vfs::IOUnitContainer_CloseFileHandle(
            static_cast<int>(file_handle_index))) {
        return false;
    }

    file_handle_index = 0;
    cached_cursor_offset = kInvalidCursorOffset;
    return true;
}

std::uint32_t IOFileUnit_GetTypeTag() {
    return kIOLooseFileUnitTag;
}

std::uint32_t IOAlignUnit_GetTypeTag() {
    return kIOAlignUnitTag;
}

std::uint32_t IOAlignUnit::GetTypeTag() const {
    return IOAlignUnit_GetTypeTag();
}

void IOAlignUnit::Init(const std::uint64_t requested_offset,
                       const std::uint32_t slot_size_value,
                       const std::uint32_t slot_count_value,
                       const bool coalesce_full_span_reads_value,
                       const bool preserve_exact_window_end_value,
                       const std::uint32_t alignment_value,
                       const std::uint8_t mode_byte_value,
                       const std::uint64_t backing_end_offset_value,
                       const std::uint8_t owned_flag, IOUnit* const next_unit) {
    next = next_unit;
    owned = owned_flag;
    refcount.store(1);

    slot_size = slot_size_value;
    slot_count = slot_count_value;
    alignment = alignment_value;
    coalesce_full_span_reads = coalesce_full_span_reads_value;
    preserve_exact_window_end = preserve_exact_window_end_value;
    mode_byte = mode_byte_value;
    raw_buffer = nullptr;
    aligned_base = nullptr;
    backing_end_offset = backing_end_offset_value;
    window_end_offset = backing_end_offset_value;

    if (!preserve_exact_window_end && slot_size != 0) {
        const auto padded_window_end =
            ((window_end_offset + slot_size - 1u) / slot_size) * slot_size;
        window_end_offset = padded_window_end;
    }

    range_start_offset = requested_offset;
    if (slot_size != 0 && requested_offset >= slot_size) {
        range_start_offset = requested_offset % slot_size;
    }

    slots.clear();
    slots.resize(slot_count);
}

void IOAlignUnit::AllocBuffer() {
    if (raw_buffer) return;

    size_t raw_size = static_cast<size_t>(slot_count) * slot_size + alignment - 1;
    raw_buffer = SMemAlloc(raw_size, ".\\IOAlignUnit.cpp", 0x126, 0x8);
    if (!raw_buffer) return;

    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_buffer);
    uintptr_t aligned_addr = (raw_addr + alignment - 1) & ~(uintptr_t(alignment) - 1);
    aligned_base = reinterpret_cast<uint8_t*>(aligned_addr);

    slots.resize(slot_count);
    for (uint32_t i = 0; i < slot_count; ++i) {
        slots[i].Reset(aligned_base + i * slot_size);
    }
}

void IOAlignUnit::SortSlotsByOffset() {
    const auto count = ClampIOAlignActiveSlotCount(*this);
    if (count < 2) {
        return;
    }

    if (count <= 32) {
        IOAlignSlotInsertionSortByOffset(slots.data(), slots.data() + count);
        return;
    }

    std::sort(slots.begin(), slots.begin() + count, IOAlignSlotOffsetLess);
}

void IOAlignUnit::MoveSlotToBack(const std::size_t slot_index) {
    const auto count = ClampIOAlignActiveSlotCount(*this);
    if (count == 0 || slot_index >= count) {
        return;
    }

    const auto last_index = count - 1;
    if (slot_index == last_index) {
        return;
    }

    const auto preserved_slot = slots[slot_index];
    std::memmove(&slots[slot_index], &slots[slot_index + 1],
                 (last_index - slot_index) * sizeof(IOAlignSlot));
    slots[last_index] = preserved_slot;
}

void* IOAlignUnit::AcquireSlotBuffer(const std::uint64_t aligned_offset,
                                     const std::uint8_t flags) {
    AllocBuffer();

    const auto count = ClampIOAlignActiveSlotCount(*this);
    if (aligned_base == nullptr || count == 0) {
        return nullptr;
    }

    std::size_t slot_index = 0;
    bool found = false;
    for (; slot_index < count; ++slot_index) {
        if (slots[slot_index].Offset() == aligned_offset) {
            found = true;
            break;
        }
    }

    if (!found) {
        slot_index = count - 1;
        if (!FlushSlot(slot_index)) {
            return nullptr;
        }

        auto& slot = slots[slot_index];
        slot.tag0 = 0xFFFFFFFFu;
        slot.tag1 = 0xFFFFFFFFu;
        slot.SetDirty(false);

        if (aligned_offset + static_cast<std::uint64_t>(slot_size)
            > backing_end_offset) {
            if (aligned_offset >= backing_end_offset) {
                std::memset(slot.buffer, 0, slot_size);
            } else {
                const auto readable_bytes = static_cast<std::uint32_t>(
                    backing_end_offset - aligned_offset);
                if (next == nullptr
                    || !next->Read(slot.buffer, OffsetLow32(aligned_offset),
                                   OffsetHigh32(aligned_offset), readable_bytes,
                                   0)) {
                    return nullptr;
                }
                std::memset(static_cast<std::uint8_t*>(slot.buffer)
                                + readable_bytes,
                            0, slot_size - readable_bytes);
            }
        } else if (next == nullptr
                   || !next->Read(slot.buffer, OffsetLow32(aligned_offset),
                                  OffsetHigh32(aligned_offset), slot_size, 0)) {
            return nullptr;
        }

        slot.SetOffset(aligned_offset);
    }

    if (slot_index != 0) {
        const auto preserved_slot = slots[slot_index];
        std::memmove(&slots[1], &slots[0], slot_index * sizeof(IOAlignSlot));
        slots[0] = preserved_slot;
    }

    slots[0].flags |= flags;
    return slots[0].buffer;
}

bool IOAlignUnit::FlushSlot(const std::size_t slot_index) {
    auto& slot = slots[slot_index];
    if (!slot.IsDirty()) {
        return true;
    }

    const std::uint64_t slot_offset = slot.Offset();
    const std::uint64_t slot_end =
        slot_offset + static_cast<std::uint64_t>(slot_size);
    const std::uint64_t flush_end = std::min(window_end_offset, slot_end);
    std::uint32_t bytes_to_write = 0;
    if (flush_end > slot_offset) {
        bytes_to_write = static_cast<std::uint32_t>(flush_end - slot_offset);
    }
    if (bytes_to_write > slot_size) {
        bytes_to_write = slot_size;
    }

    if (!preserve_exact_window_end) {
        std::memset(static_cast<std::uint8_t*>(slot.buffer) + bytes_to_write, 0,
                    slot_size - bytes_to_write);
        bytes_to_write = slot_size;
    }

    if (next == nullptr
        || !next->Write(slot.buffer, static_cast<std::uint32_t>(slot_offset),
                        static_cast<std::uint32_t>(slot_offset >> 32),
                        bytes_to_write)) {
        return false;
    }

    backing_end_offset = std::max(
        backing_end_offset,
        slot_offset + static_cast<std::uint64_t>(bytes_to_write));
    slot.SetDirty(false);
    return true;
}

bool IOAlignUnit::FlushDirtySlots() {
    if (!raw_buffer) {
        return true;
    }

    SortSlotsByOffset();

    const auto count = ClampIOAlignActiveSlotCount(*this);

    bool success = true;
    for (std::size_t slot_index = 0; slot_index < count; ++slot_index) {
        if (!FlushSlot(slot_index)) {
            success = false;
        }
    }
    return success;
}

bool IOAlignUnit::Read(void* const dest, const std::uint64_t offset_lo,
                       const std::uint64_t offset_hi,
                       const std::uint32_t size,
                       std::uint32_t* out_bytes_read) {
    std::uint32_t ignored_bytes_read = 0;
    if (out_bytes_read == nullptr) {
        out_bytes_read = &ignored_bytes_read;
    }

    const auto requested_offset = ComposeOffset64(
        static_cast<std::uint32_t>(offset_lo),
        static_cast<std::uint32_t>(offset_hi));
    if (requested_offset < range_start_offset
        || requested_offset > window_end_offset) {
        PushIOAlignReadRangeError();
        return false;
    }

    if (requested_offset == window_end_offset || size == 0u) {
        *out_bytes_read = 0;
        return true;
    }

    const auto window_remaining = window_end_offset - requested_offset;
    const auto bytes_to_copy = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(window_remaining, size));

    auto* dest_bytes = static_cast<std::uint8_t*>(dest);
    auto dispatch_offset =
        range_start_offset
        + ((requested_offset - range_start_offset) / slot_size) * slot_size;
    std::uint32_t remaining = bytes_to_copy;

    if (dispatch_offset != requested_offset) {
        const auto intra_slot_offset = static_cast<std::uint32_t>(
            requested_offset - dispatch_offset);
        const auto head_bytes =
            std::min(slot_size - intra_slot_offset, remaining);
        auto* slot_buffer = static_cast<std::uint8_t*>(
            AcquireSlotBuffer(dispatch_offset, 0));
        if (slot_buffer == nullptr) {
            return false;
        }

        std::memcpy(dest_bytes, slot_buffer + intra_slot_offset, head_bytes);
        dest_bytes += head_bytes;
        remaining -= head_bytes;
        dispatch_offset += slot_size;
    }

    if (!(preserve_exact_window_end && coalesce_full_span_reads
          && dispatch_offset + remaining == window_end_offset)
        && remaining >= slot_size) {
        const auto full_span_bytes = slot_size * (remaining / slot_size);
        const auto direct_read_span_end = dispatch_offset + full_span_bytes;
        const auto count = ClampIOAlignActiveSlotCount(*this);

        for (std::size_t slot_index = 0; slot_index < count; ++slot_index) {
            const auto slot_offset = slots[slot_index].Offset();
            if (slot_offset < dispatch_offset
                || slot_offset >= direct_read_span_end) {
                continue;
            }

            if (!FlushSlot(slot_index)) {
                return false;
            }
        }

        const auto dispatched_chunk =
            coalesce_full_span_reads ? full_span_bytes : slot_size;
        while (remaining >= dispatched_chunk) {
            if (next == nullptr
                || !next->Read(dest_bytes, OffsetLow32(dispatch_offset),
                               OffsetHigh32(dispatch_offset), dispatched_chunk,
                               nullptr)) {
                return false;
            }

            dest_bytes += dispatched_chunk;
            remaining -= dispatched_chunk;
            dispatch_offset += dispatched_chunk;
        }
    }

    if (preserve_exact_window_end
        && dispatch_offset + remaining == window_end_offset) {
        if (next == nullptr
            || !next->Read(dest_bytes, OffsetLow32(dispatch_offset),
                           OffsetHigh32(dispatch_offset), remaining, nullptr)) {
            return false;
        }
    } else if (remaining != 0u) {
        const auto* slot_buffer = static_cast<const std::uint8_t*>(
            AcquireSlotBuffer(dispatch_offset, 0));
        if (slot_buffer == nullptr) {
            return false;
        }

        std::memcpy(dest_bytes, slot_buffer, remaining);
    }

    *out_bytes_read = bytes_to_copy;
    return true;
}

bool IOAlignUnit::Write(const void* source, const std::uint32_t offset_lo,
                        const std::uint32_t offset_hi,
                        const std::uint32_t size) {
    const auto requested_offset = ComposeOffset64(offset_lo, offset_hi);
    if (requested_offset < range_start_offset || slot_size == 0) {
        return false;
    }

    auto aligned_offset =
        range_start_offset
        + ((requested_offset - range_start_offset) / slot_size) * slot_size;
    auto current_offset = requested_offset;
    auto remaining = size;
    auto* source_bytes = static_cast<const std::uint8_t*>(source);

    if (aligned_offset != requested_offset) {
        const auto intra_slot_offset =
            static_cast<std::uint32_t>(requested_offset - aligned_offset);
        const auto head_bytes =
            std::min(slot_size - intra_slot_offset, remaining);
        auto* slot_buffer = static_cast<std::uint8_t*>(
            AcquireSlotBuffer(aligned_offset, 1));
        if (slot_buffer == nullptr) {
            return false;
        }

        std::memcpy(slot_buffer + intra_slot_offset, source_bytes, head_bytes);
        source_bytes += head_bytes;
        remaining -= head_bytes;
        current_offset += head_bytes;
        aligned_offset += slot_size;
        window_end_offset = std::max(window_end_offset, current_offset);
    }

    if (remaining >= slot_size) {
        const auto full_span_bytes = slot_size * (remaining / slot_size);
        const auto full_span_end = aligned_offset + full_span_bytes;
        const auto count = ClampIOAlignActiveSlotCount(*this);

        for (std::size_t slot_index = count; slot_index > 0; --slot_index) {
            auto& slot = slots[slot_index - 1];
            const auto slot_offset = slot.Offset();
            if (slot_offset < aligned_offset || slot_offset >= full_span_end) {
                continue;
            }

            slot.tag0 = 0xFFFFFFFFu;
            slot.tag1 = 0xFFFFFFFFu;
            slot.flags = 0;
            MoveSlotToBack(slot_index - 1);
        }

        if (!mode_byte && !FlushDirtySlots()) {
            return false;
        }

        auto dispatched_chunk = coalesce_full_span_reads ? full_span_bytes : slot_size;
        while (remaining >= dispatched_chunk) {
            if (next == nullptr
                || !next->Write(source_bytes, OffsetLow32(aligned_offset),
                                OffsetHigh32(aligned_offset),
                                dispatched_chunk)) {
                return false;
            }

            source_bytes += dispatched_chunk;
            remaining -= dispatched_chunk;
            aligned_offset += dispatched_chunk;
        }

        window_end_offset = std::max(window_end_offset, aligned_offset);
        backing_end_offset = std::max(backing_end_offset, aligned_offset);
        current_offset = aligned_offset;
    }

    if (remaining != 0) {
        auto* slot_buffer = static_cast<std::uint8_t*>(
            AcquireSlotBuffer(aligned_offset, 1));
        if (slot_buffer == nullptr) {
            return false;
        }

        std::memcpy(slot_buffer, source_bytes, remaining);
        current_offset = aligned_offset + remaining;
        window_end_offset = std::max(window_end_offset, current_offset);
    }

    return true;
}

bool IOAlignUnit::Flush() {
    const bool flushed_dirty_slots = FlushDirtySlots();
    if (next == nullptr || !next->Flush()) {
        return false;
    }
    return flushed_dirty_slots;
}

bool IOAlignUnit::GetSize(uint32_t* out_lo, uint32_t* out_hi) {
    const auto window_end = window_end_offset;
    *out_lo = static_cast<uint32_t>(window_end & 0xFFFFFFFFu);
    *out_hi = static_cast<uint32_t>(window_end >> 32);
    return true;
}

bool IOAlignUnit::SetSize(const std::uint32_t size_lo,
                          const std::uint32_t size_hi) {
    const auto requested_size = ComposeOffset64(size_lo, size_hi);
    if (raw_buffer != nullptr && requested_size < window_end_offset
        && slot_size != 0) {
        const auto truncated_slot_offset =
            range_start_offset
            + ((static_cast<std::int64_t>(requested_size)
                - static_cast<std::int64_t>(range_start_offset) - 1)
               / static_cast<std::int64_t>(slot_size))
                  * static_cast<std::int64_t>(slot_size);
        const auto slot_base =
            static_cast<std::uint64_t>(truncated_slot_offset);
        const auto preserved_bytes =
            static_cast<std::uint32_t>(requested_size - slot_base);
        const auto count = ClampIOAlignActiveSlotCount(*this);

        for (std::size_t slot_index = count; slot_index > 0; --slot_index) {
            auto& slot = slots[slot_index - 1];
            const auto slot_offset = slot.Offset();
            if (slot_offset < slot_base) {
                continue;
            }

            if (slot_offset == slot_base) {
                if (preserved_bytes <= slot_size) {
                    std::memset(static_cast<std::uint8_t*>(slot.buffer)
                                    + preserved_bytes,
                                0, slot_size - preserved_bytes);
                }
                continue;
            }

            slot.tag0 = 0xFFFFFFFFu;
            slot.tag1 = 0xFFFFFFFFu;
            slot.flags = 0;
            MoveSlotToBack(slot_index - 1);
        }
    }

    window_end_offset = requested_size;
    return next != nullptr && next->SetSize(size_lo, size_hi);
}

void IOAlignUnit::Destroy(bool free_self) {

    this->~IOAlignUnit();
    if (free_self) {
        FrameXML_OperatorDelete(this);
    }
}

bool IOAlignUnit::Close() {
    const bool flushed = FlushDirtySlots();

    SMemFree(raw_buffer, ".\\IOAlignUnit.cpp", 0x116, 0);
    raw_buffer = nullptr;
    aligned_base = nullptr;
    return flushed;
}

IOUnit* IOUnitContainer_CreateAlignUnit(
    IOUnitAllocator* const alloc, const std::uint64_t requested_offset,
    std::uint32_t slot_size, const std::uint32_t slot_count,
    const bool coalesce_full_span_reads,
    const bool preserve_exact_window_end, const std::uint32_t alignment,
    const std::uint8_t mode_byte, IOUnit* const chain) {
    if (chain == nullptr) {
        return nullptr;
    }

    std::uint32_t backing_end_lo = 0;
    std::uint32_t backing_end_hi = 0;
    if (!chain->GetSize(&backing_end_lo, &backing_end_hi)) {
        IOUnitContainer_Release(chain);
        return nullptr;
    }

    if (slot_size == 0) {
        slot_size = 1;
    }

    std::uint8_t owned = 1;
    void* memory = nullptr;
    const auto unit_size = static_cast<std::uint32_t>(sizeof(IOAlignUnit));
    if (alloc != nullptr) {
        memory = alloc->AllocEntry(unit_size, &owned);
    } else {
        memory = std::calloc(1, unit_size);
    }

    if (memory == nullptr) {
        return nullptr;
    }

    auto* unit = new (memory) IOAlignUnit();
    const auto backing_end_offset =
        (static_cast<std::uint64_t>(backing_end_hi) << 32) | backing_end_lo;
    unit->Init(requested_offset,
               slot_size,
               slot_count,
               coalesce_full_span_reads,
               preserve_exact_window_end,
               alignment,
               mode_byte,
               backing_end_offset,
               owned,
               chain);
    return unit;
}

IOAsyncFileUnit::~IOAsyncFileUnit() {
    if (raw_buffer) {
        SMemFree(raw_buffer, ".\\IOUnitContainer.cpp", 0x40, 0);
        raw_buffer = nullptr;
        aligned_buffer = nullptr;
    }
}

void IOAsyncFileUnit::Init(std::uint32_t handle_index, std::uint8_t owned_flag) {
    if (raw_buffer) {
        SMemFree(raw_buffer, ".\\IOUnitContainer.cpp", 0x40, 0);
        raw_buffer = nullptr;
        aligned_buffer = nullptr;
    }

    owned = owned_flag;
    file_handle_index = handle_index;
    refcount.store(1);
    next = nullptr;
    cached_end_position = 0;

    openwow::vfs::RuntimeSFileHandleMetadata handle_metadata;
    std::string root_path;
    if (openwow::vfs::QueryRuntimeSFileHandleMetadata(
            static_cast<int>(handle_index), &handle_metadata)) {
        cached_end_position = handle_metadata.size;
        root_path = ExtractRootPathForDiskGeometry(handle_metadata.native_path);
    }

    sector_mask = ComputeSectorMask(QueryBytesPerSectorForRootPath(root_path));

    raw_buffer = SMemAlloc(static_cast<std::size_t>(sector_mask) + 0x100000u,
                           ".\\IOUnitContainer.cpp", 0x40, 0);
    if (raw_buffer) {
        const auto raw = reinterpret_cast<std::uintptr_t>(raw_buffer);
        const auto aligned =
            (~static_cast<std::uintptr_t>(sector_mask))
            & (raw + static_cast<std::uintptr_t>(sector_mask));
        aligned_buffer = reinterpret_cast<std::uint8_t*>(aligned);
    }
}

bool IOAsyncFileUnit::Read(void* const dest, const std::uint64_t offset_lo,
                           const std::uint64_t offset_hi,
                           const std::uint32_t size,
                           std::uint32_t* const out_bytes_read) {
    return ReadWithCount(dest, static_cast<std::uint32_t>(offset_lo),
                         static_cast<std::uint32_t>(offset_hi), size,
                         out_bytes_read);
}

bool IOAsyncFileUnit::ReadWithCount(void* const dest,
                                    const std::uint32_t offset_lo,
                                    const std::uint32_t offset_hi,
                                    const std::uint32_t size,
                                    std::uint32_t* const out_bytes_read) {
    if (((sector_mask & reinterpret_cast<std::uintptr_t>(dest)) == 0u)
        && ((sector_mask & size) == 0u)) {
        return ReadLooseFileUnitAtOffset(
            static_cast<int>(file_handle_index), dest,
            ComposeOffset64(offset_lo, offset_hi), size, out_bytes_read);
    }

    if (aligned_buffer == nullptr) {
        return false;
    }

    auto* dest_bytes = static_cast<std::uint8_t*>(dest);
    std::uint64_t current_offset = ComposeOffset64(offset_lo, offset_hi);
    std::uint32_t remaining = size;
    std::uint32_t total_copied = 0;
    bool result = true;

    while (remaining != 0u) {
        std::uint32_t dispatched_size = std::min(remaining, 0x100000u);
        const std::uint32_t requested_size = dispatched_size;
        if ((sector_mask & dispatched_size) != 0u) {
            dispatched_size =
                static_cast<std::uint32_t>((~sector_mask)
                                           & (sector_mask + dispatched_size));
        }

        std::uint32_t chunk_bytes_read = 0;
        result = ReadLooseFileUnitAtOffset(
            static_cast<int>(file_handle_index), aligned_buffer, current_offset,
            dispatched_size, &chunk_bytes_read);
        if (!result) {
            break;
        }

        const std::uint32_t copied_size =
            std::min(requested_size, chunk_bytes_read);
        std::memcpy(dest_bytes, aligned_buffer, copied_size);
        dest_bytes += copied_size;
        current_offset += copied_size;
        remaining -= copied_size;
        total_copied += copied_size;
        if (copied_size < requested_size) {
            break;
        }
    }

    if (out_bytes_read != nullptr) {
        *out_bytes_read = total_copied;
        return result;
    }

    return remaining == 0u;
}

bool IOAsyncFileUnit::Write(const void* source, const std::uint32_t offset_lo,
                            const std::uint32_t offset_hi,
                            const std::uint32_t size) {
    const auto source_address =
        reinterpret_cast<std::uintptr_t>(source);
    if ((sector_mask & source_address) == 0u && (sector_mask & size) == 0u) {
        std::uint32_t requested_size = size;
        return openwow::vfs::IOUnitContainer_WriteFileHandleAtOffset(
            static_cast<int>(file_handle_index),
            source,
            (static_cast<std::uint64_t>(offset_hi) << 32) | offset_lo,
            &requested_size);
    }

    if (aligned_buffer == nullptr) {
        return false;
    }

    bool result = true;
    auto current_offset =
        (static_cast<std::uint64_t>(offset_hi) << 32) | offset_lo;
    auto remaining = size;
    auto* source_bytes = static_cast<const std::uint8_t*>(source);

    while (remaining != 0u) {
        const auto requested_chunk = std::min(remaining, 0x100000u);
        std::memcpy(aligned_buffer, source_bytes, requested_chunk);

        auto dispatched_chunk = requested_chunk;
        if ((sector_mask & requested_chunk) != 0u) {
            dispatched_chunk = (~sector_mask) & (sector_mask + requested_chunk);
        }

        if (!openwow::vfs::IOUnitContainer_WriteFileHandleAtOffset(
                static_cast<int>(file_handle_index),
                aligned_buffer,
                current_offset,
                &dispatched_chunk)) {
            result = false;
            break;
        }

        source_bytes += requested_chunk;
        current_offset += requested_chunk;
        remaining -= requested_chunk;
    }

    if (current_offset > cached_end_position) {
        cached_end_position = current_offset;
    }

    return result;
}

bool IOAsyncFileUnit::Flush() {
    return openwow::vfs::IOUnitContainer_FlushFileHandle(
        static_cast<int>(file_handle_index));
}

void IOAsyncFileUnit::Destroy(bool free_self) {
    this->~IOAsyncFileUnit();
    if (free_self) {
        std::free(this);
    }
}

bool IOAsyncFileUnit::Close() {
    if (raw_buffer != nullptr) {
        SMemFree(raw_buffer, ".\\IOUnitContainer.cpp", 0x40, 0);
        raw_buffer = nullptr;
        aligned_buffer = nullptr;
    }

    if (file_handle_index == 0) {
        return false;
    }

    openwow::vfs::RuntimeSFileHandleMetadata handle_metadata;
    const bool have_metadata = openwow::vfs::QueryRuntimeSFileHandleMetadata(
        static_cast<int>(file_handle_index), &handle_metadata);

    std::array<char, 1024> reopen_path{};
    std::uint64_t physical_size = 0;
    if (have_metadata) {
        physical_size = handle_metadata.size;
        SStrCopy(reopen_path.data(), handle_metadata.native_path.c_str(),
                 reopen_path.size());
    }

    if (!openwow::vfs::IOUnitContainer_CloseFileHandle(
            static_cast<int>(file_handle_index))) {
        return false;
    }

    file_handle_index = 0;
    if (!have_metadata || physical_size <= cached_end_position) {
        return true;
    }

    int truncate_handle = 0;
    if (!OpenRuntimeFileHandleForPath(reopen_path.data(), 0x100E,
                                      &truncate_handle)) {
        return false;
    }

    const bool resized = openwow::vfs::IOUnitContainer_SetFileHandleSize(
        truncate_handle, cached_end_position, 0);
    const bool truncate_close_ok =
        openwow::vfs::IOUnitContainer_CloseFileHandle(truncate_handle);
    return resized && truncate_close_ok;
}

bool IOAsyncFileUnit::GetSize(std::uint32_t* out_lo, std::uint32_t* out_hi) {
    if (out_lo) {
        *out_lo = static_cast<std::uint32_t>(cached_end_position & 0xFFFFFFFFu);
    }
    if (out_hi) {
        *out_hi = static_cast<std::uint32_t>(cached_end_position >> 32);
    }
    return true;
}

bool IOAsyncFileUnit::SetSize(const std::uint32_t size_lo,
                              const std::uint32_t size_hi) {
    const auto requested_size = ComposeOffset64(size_lo, size_hi);
    cached_end_position = requested_size;
    const auto physical_size =
        RoundUpAsyncFileUnitPhysicalSize(requested_size, sector_mask);
    return openwow::vfs::IOUnitContainer_SetFileHandleSize(
        static_cast<int>(file_handle_index), physical_size, 0);
}

void IOMpqSectorReaderUnit::ResetInlineStorage() {
    std::fill(inline_sector_offsets.begin(), inline_sector_offsets.end(), 0u);
    std::fill(inline_aux_entries.begin(), inline_aux_entries.end(), 0u);
    heap_sector_offsets.reset();
    heap_aux_entries.reset();
    sector_offsets = inline_sector_offsets.data();
    aux_entries = inline_aux_entries.data();
    aux_entry_count = 0;
}

void IOMpqSectorReaderUnit::InitSectorTable(
    const std::uint32_t file_size_value,
    const std::uint32_t compressed_size,
    const std::uint32_t sector_count,
    const std::uint32_t sector_size_value,
    const std::uint32_t block_flags_value,
    const std::uint32_t decryption_key_value,
    const std::uint8_t owned_flag,
    IOUnit* const chain_next) {
    ResetInlineStorage();

    next = chain_next;
    owned = owned_flag;
    refcount.store(1);

    sector_offset_entry_count = sector_count + 2u;
    if (sector_offset_entry_count > kInlineSectorOffsetCapacity) {
        heap_sector_offsets =
            std::make_unique<std::uint32_t[]>(sector_offset_entry_count);
        std::fill_n(heap_sector_offsets.get(), sector_offset_entry_count, 0u);
        sector_offsets = heap_sector_offsets.get();
    }

    sector_size = sector_size_value;
    block_flags = block_flags_value;
    decryption_key = decryption_key_value;
    file_size = file_size_value;
    final_sector_size = ComputeLogicalSectorSize(
        file_size, sector_size, sector_count - 1u);

    sector_offsets[0] = 0xFFFFFFFDu;
    sector_offsets[1] = compressed_size;
}

void IOMpqSectorReaderUnit::InitSingleUnit(
    const std::uint32_t file_size_value,
    const std::uint32_t compressed_size,
    const std::uint32_t block_flags_value,
    const std::uint32_t decryption_key_value,
    const std::uint8_t owned_flag,
    IOUnit* const chain_next) {
    ResetInlineStorage();

    next = chain_next;
    owned = owned_flag;
    refcount.store(1);

    sector_offset_entry_count = 3;
    sector_size = file_size_value;
    block_flags = block_flags_value;
    decryption_key = decryption_key_value;
    file_size = file_size_value;
    final_sector_size = ComputeLogicalSectorSize(file_size, sector_size, 0u);

    sector_offsets[0] = 0;
    sector_offsets[1] = compressed_size;
}

bool IOMpqSectorReaderUnit::GetSize(std::uint32_t* out_lo,
                                    std::uint32_t* out_hi) {
    if (out_lo) {
        *out_lo = file_size;
    }
    if (out_hi) {
        *out_hi = 0;
    }
    return true;
}

std::uint32_t IOMpqSectorReaderUnit::GetLogicalSectorSize(
    const std::uint32_t sector_index) const {
    return ComputeLogicalSectorSize(file_size, sector_size, sector_index);
}

std::uint32_t IOMpqSectorReaderUnit::GetMaxReadPlanDescriptorCount(
    const std::uint64_t logical_offset, const std::uint64_t span_size) const {
    return ComputeMaxReadPlanDescriptorCount(
        logical_offset, span_size, sector_size);
}

std::uint32_t IOMpqSectorReaderUnit::CountNonNominalPackedSectorRun(
    const std::uint32_t start_sector_index,
    const std::uint32_t end_sector_index) const {
    return CountPackedSectorRun(start_sector_index, end_sector_index, false);
}

std::uint32_t IOMpqSectorReaderUnit::CountNominalPackedSectorRun(
    const std::uint32_t start_sector_index,
    const std::uint32_t end_sector_index) const {
    return CountPackedSectorRun(start_sector_index, end_sector_index, true);
}

std::uint32_t IOMpqSectorReaderUnit::GetExpectedPackedSectorSpan(
    const std::uint32_t sector_index) const {
    const auto final_sector_index =
        sector_offset_entry_count > 2u ? sector_offset_entry_count - 3u : 0u;
    return sector_index >= final_sector_index ? final_sector_size : sector_size;
}

std::uint32_t IOMpqSectorReaderUnit::CountPackedSectorRun(
    const std::uint32_t start_sector_index,
    const std::uint32_t end_sector_index,
    const bool match_expected_span) const {
    if (start_sector_index >= end_sector_index || sector_offsets == nullptr) {
        return 0;
    }

    std::uint32_t sector_index = start_sector_index;
    std::uint32_t current_offset = sector_offsets[start_sector_index];
    while (sector_index < end_sector_index) {
        const auto next_offset = sector_offsets[sector_index + 1u];
        const auto packed_span = next_offset - current_offset;
        const bool matches_expected_span =
            packed_span == GetExpectedPackedSectorSpan(sector_index);
        if (matches_expected_span != match_expected_span) {
            break;
        }

        ++sector_index;
        current_offset = next_offset;
    }

    return sector_index - start_sector_index;
}

void IOMpqSectorReaderUnit::Destroy(bool free_self) {
    this->~IOMpqSectorReaderUnit();
    if (free_self) {
        std::free(this);
    }
}

bool IOMpqSectorReaderUnit::UsesInlineSectorOffsets() const {
    return sector_offsets == inline_sector_offsets.data();
}

bool IOMpqSectorReaderUnit::UsesInlineAuxEntries() const {
    return aux_entries == inline_aux_entries.data();
}

bool IOUnitContainer_Release(IOUnit* chain) {

    if (!chain) return true;

    bool success = true;
    IOUnit* stop_at = nullptr;
    IOUnit* unit = chain;

    while (unit) {
        int32_t prev = unit->refcount.fetch_sub(1);
        if (prev > 1) {
            stop_at = unit;
            break;
        }
        if (!unit->Close()) {
            success = false;
        }
        unit = unit->next;
    }

    unit = chain;
    while (unit != stop_at) {
        IOUnit* next_unit = unit->next;
        uint8_t was_owned = unit->owned;
        unit->Destroy(false);
        if (was_owned) {
            std::free(unit);
        }
        unit = next_unit;
    }

    return success;
}

IOUnit* IOUnitContainer_CreateStreamUnit(IOUnitAllocator* alloc,
                                         void* source_handle,
                                         const char* archive_path,
                                         const char* file_path,
                                         IOUnit* chain) {
    if (!chain) return nullptr;

    IOStreamUnit* stream = nullptr;
    uint8_t owned = 0;
    const auto stream_size =
        static_cast<std::uint32_t>(sizeof(IOStreamUnit));

    if (alloc) {
        void* mem = alloc->AllocEntry(stream_size, &owned);
        if (!mem) return nullptr;
        stream = new (mem) IOStreamUnit();
    } else {
        owned = 1;
        void* mem = std::calloc(1, stream_size);
        if (!mem) return nullptr;
        stream = new (mem) IOStreamUnit();
    }

    if (stream) {
        stream->Init(source_handle, archive_path, file_path, owned, chain);
    }

    return stream;
}

IOUnit* IOUnitContainer_CreateReadOnlyFile(
    IOUnitAllocator* alloc, void* archive, const char* path,
    const std::uint32_t block_index,
    const BlockTableEntry& cached_block_entry, IOUnit* chain) {
    if (chain == nullptr) {
        return nullptr;
    }

    std::uint8_t owned = 0;
    void* const storage = AllocateFileUnitStorage(
        alloc, static_cast<std::uint32_t>(sizeof(IOFileReadOnlyUnit)), &owned);
    if (storage == nullptr) {
        return nullptr;
    }

    auto* unit = new (storage) IOFileReadOnlyUnit();
    unit->Init(archive, path, block_index, cached_block_entry, owned, chain);
    return unit;
}

IOUnit* IOUnitContainer_CreateWritableFile(
    IOUnitAllocator* alloc, void* archive, const char* path,
    const std::uint32_t block_index,
    const BlockTableEntry& cached_block_entry, IOUnit* chain) {
    if (chain == nullptr) {
        return nullptr;
    }

    std::uint8_t owned = 0;
    void* const storage = AllocateFileUnitStorage(
        alloc, static_cast<std::uint32_t>(sizeof(IOFileWritableUnit)), &owned);
    if (storage == nullptr) {
        return nullptr;
    }

    auto* unit = new (storage) IOFileWritableUnit();
    unit->Init(archive, path, block_index, cached_block_entry, owned, chain);
    return unit;
}

IOUnit* IOUnitContainer_CreateFileUnit(IOUnitAllocator* alloc, const char* path,
                                       int flags) {
    std::uint8_t owned = 0;
    void* const storage = AllocateFileUnitStorage(
        alloc, static_cast<std::uint32_t>(sizeof(IOLooseFileUnit)), &owned);

    int file_handle = 0;
    if (!OpenRuntimeFileHandleForPath(path, flags, &file_handle)) {
        ReleaseFileUnitStorageOnOpenFailure(storage, owned);
        return nullptr;
    }

    if (storage == nullptr) {
        return nullptr;
    }

    auto* unit = new (storage) IOLooseFileUnit();
    unit->Init(static_cast<std::uint32_t>(file_handle), owned);
    return unit;
}

IOUnit* IOUnitContainer_CreateAsyncFileUnit(IOUnitAllocator* alloc,
                                            const char* path, int flags) {
    std::uint8_t owned = 0;
    void* const storage = AllocateFileUnitStorage(
        alloc, static_cast<std::uint32_t>(sizeof(IOAsyncFileUnit)), &owned);

    int file_handle = 0;
    if (!OpenRuntimeFileHandleForPath(path, flags, &file_handle)) {
        ReleaseFileUnitStorageOnOpenFailure(storage, owned);
        return nullptr;
    }

    if (storage == nullptr) {
        return nullptr;
    }

    auto* unit = new (storage) IOAsyncFileUnit();
    unit->Init(static_cast<std::uint32_t>(file_handle), owned);
    return unit;
}

IOUnit* SFileOpenFileEx_CreateSectorTableReader(
    IOUnitAllocator* alloc, const std::uint32_t file_size,
    const std::uint32_t compressed_size, const std::uint32_t sector_size,
    const std::uint32_t block_flags, const std::uint32_t decryption_key,
    IOUnit* chain) {
    if (!chain || file_size == 0) {
        return chain;
    }

    std::uint8_t owned = 0;
    auto* unit = AllocateMpqSectorReaderUnit(alloc, &owned);
    if (!unit) {
        return nullptr;
    }

    const auto sector_count = (file_size + sector_size - 1u) / sector_size;
    unit->InitSectorTable(file_size, compressed_size, sector_count,
                          sector_size, block_flags, decryption_key, owned,
                          chain);
    return unit;
}

IOUnit* SFileOpenFileEx_CreateSingleUnitSectorReader(
    IOUnitAllocator* alloc, const std::uint32_t file_size,
    const std::uint32_t compressed_size, const std::uint32_t block_flags,
    const std::uint32_t decryption_key, IOUnit* chain) {
    if (!chain) {
        return nullptr;
    }
    if (file_size == 0) {
        return chain;
    }

    std::uint8_t owned = 0;
    auto* unit = AllocateMpqSectorReaderUnit(alloc, &owned);
    if (!unit) {
        return nullptr;
    }

    unit->InitSingleUnit(file_size, compressed_size, block_flags,
                         decryption_key, owned, chain);
    return unit;
}

bool IOUnitContainer_DeletePartFile(
    void* io_provider, const IOUnitContainerPartFileInfo* file_info) {

    const char* source_path = file_info ? file_info->path : nullptr;
    if (!source_path || *source_path == '\0') {
        return false;
    }

    std::array<char, 260> part_path{};
    StreamingStorage::Instance().BuildVariantPath(
        part_path.data(), static_cast<int>(part_path.size()), source_path,
        "part");

    const auto callback_table =
        ReadCompatValueAtOffset<void*>(io_provider, 0x04u);
    void* opened_handle = nullptr;
    if (!FileStack_OpenDeletePartHandle(callback_table, part_path.data(), 0x403u,
                                        &opened_handle)) {
        return false;
    }

    (void)openwow::vfs::FileStack_CloseFileHandle(callback_table,
                                                  opened_handle);
    return true;
}

bool IOFileUnit_DirectRead(uint32_t file_offset, const char* filepath,
                            void* dest, uint32_t size, uint32_t* bytes_read) {

    if (filepath == nullptr || dest == nullptr) {
        return false;
    }

    const std::filesystem::path native_path(filepath);
#if defined(_WIN32)
    std::FILE* const file = _wfopen(native_path.c_str(), L"rb");
#else
    std::FILE* const file = std::fopen(native_path.c_str(), "rb");
#endif
    if (file == nullptr) {
        return false;
    }

    bool seek_ok = false;
#if defined(_WIN32)
    seek_ok = _fseeki64(file, static_cast<__int64>(file_offset), SEEK_SET) == 0;
#else
    seek_ok = fseeko(file, static_cast<off_t>(file_offset), SEEK_SET) == 0;
#endif
    if (!seek_ok) {
        std::fclose(file);
        return false;
    }

    std::uint32_t copied_bytes = 0;
    if (size != 0) {
        copied_bytes = static_cast<std::uint32_t>(
            std::fread(dest, 1, static_cast<std::size_t>(size), file));
    }

    std::fclose(file);
    if (bytes_read != nullptr) {
        *bytes_read = copied_bytes;
    }
    return true;
}

IOUnit* SFileOpenFileEx_CreateFileUnit(IOUnitAllocator* alloc,
                                       const char* path, int flags) {

    if (flags & 0x40) {
        return IOUnitContainer_CreateAsyncFileUnit(alloc, path, flags);
    }
    return IOUnitContainer_CreateFileUnit(alloc, path, flags);
}

void SetIOAsyncFileUnitSectorSizeResolverForTests(
    std::function<std::optional<std::uint32_t>(const std::string&)> resolver) {
    MutableAsyncFileUnitSectorSizeResolverForTests() = std::move(resolver);
}

void SetIOFileUnitDirectReadRetryFlagForTests(const bool enabled) {
    *IOFileUnitDirectReadRetryFlagStorage() = enabled ? 1u : 0u;
}

void ResetIOFileUnitDirectReadRetryFlagForTests() {
    SetIOFileUnitDirectReadRetryFlagForTests(false);
}

void SetIOStreamUnitSourceLifetimeResolverForTests(
    std::function<IOStreamUnitSourceLifetimeHooks(void*)> resolver) {
    MutableIOStreamUnitSourceLifetimeResolverForTests() = std::move(resolver);
}

}

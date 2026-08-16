
#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::core {

inline constexpr std::uint32_t kIOAlignUnitTag = 0x616C6967u;
inline constexpr std::uint32_t kIOLooseFileUnitTag = 0x66696C65u;

std::uint32_t IOFileUnit_GetTypeTag();

std::uint32_t IOAlignUnit_GetTypeTag();

class IOUnit {
public:
    virtual ~IOUnit() = default;

    virtual std::uint32_t GetTypeTag() const { return 0; }

    virtual bool Read(void* dest, uint64_t offset_lo, uint64_t offset_hi,
                      uint32_t size, uint32_t* out_bytes_read) {
        (void)dest;
        (void)offset_lo;
        (void)offset_hi;
        (void)size;
        (void)out_bytes_read;
        return false;
    }

    virtual bool Write(const void*, uint32_t, uint32_t, uint32_t) {
        return false;
    }

    virtual bool Flush() { return false; }

    virtual bool GetSize(uint32_t* out_lo, uint32_t* out_hi) {
        if (out_lo) *out_lo = 0;
        if (out_hi) *out_hi = 0;
        return false;
    }

    virtual bool SetSize(uint32_t size_lo, uint32_t size_hi) {
        (void)size_lo;
        (void)size_hi;
        return false;
    }

    virtual void Destroy(bool) {}

    virtual bool Close() { return true; }

    virtual uint32_t GetField5() const { return 0; }

    virtual uint32_t GetField7() const { return 0; }

    virtual uint32_t DispatchField11() { return 0; }

    IOUnit* next = nullptr;
    uint8_t owned = 0;
    std::atomic<int32_t> refcount{1};
};

class IOUnitAllocator {
public:
    IOUnitAllocator() = default;
    ~IOUnitAllocator();

    void Alloc(uint32_t baseSize, const uint32_t* sizes);

    void* AllocEntry(uint32_t size, uint8_t* out_owned);

    void* GetBlock() const { return block_; }

private:
    void* block_ = nullptr;
    uint8_t* cursor_ = nullptr;
    uint8_t* block_end_ = nullptr;
    bool first_alloc_ = true;
};

struct IOAlignSlot {
    uint32_t tag0 = 0xFFFFFFFF;
    uint32_t tag1 = 0xFFFFFFFF;
    uint8_t  flags = 0;
    uint8_t  pad[3] = {};
    void*    buffer = nullptr;

    void Reset(void* buffer_ptr) {
        tag0 = 0xFFFFFFFF;
        tag1 = 0xFFFFFFFF;
        flags = 0;
        buffer = buffer_ptr;
    }

    [[nodiscard]] std::uint64_t Offset() const {
        return (static_cast<std::uint64_t>(tag1) << 32) | tag0;
    }

    void SetOffset(const std::uint64_t offset) {
        tag0 = static_cast<std::uint32_t>(offset);
        tag1 = static_cast<std::uint32_t>(offset >> 32);
    }

    [[nodiscard]] bool IsDirty() const {
        return flags != 0;
    }

    void SetDirty(const bool dirty) {
        flags = dirty ? 1u : 0u;
    }
};

class IOAlignUnit : public IOUnit {
public:
    IOAlignUnit() = default;
    ~IOAlignUnit() override = default;

    std::uint32_t GetTypeTag() const override;

    void Init(std::uint64_t requested_offset, std::uint32_t slot_size,
              std::uint32_t slot_count, bool coalesce_full_span_reads,
              bool preserve_exact_window_end, std::uint32_t alignment,
              std::uint8_t mode_byte, std::uint64_t backing_end_offset,
              std::uint8_t owned_flag, IOUnit* next_unit);

    void AllocBuffer();

    void SortSlotsByOffset();

    bool FlushDirtySlots();

    bool Read(void* dest, std::uint64_t offset_lo, std::uint64_t offset_hi,
              std::uint32_t size, std::uint32_t* out_bytes_read) override;

    bool Write(const void* source, uint32_t offset_lo, uint32_t offset_hi,
               uint32_t size) override;

    bool Flush() override;

    bool GetSize(uint32_t* out_lo, uint32_t* out_hi) override;

    bool SetSize(uint32_t size_lo, uint32_t size_hi) override;
    void Destroy(bool free_self) override;
    bool Close() override;

    std::uint64_t range_start_offset = 0;
    std::uint64_t window_end_offset = 0;
    std::uint64_t backing_end_offset = 0;

    uint32_t alignment = 4096;
    uint32_t slot_size = 0;
    uint32_t slot_count = 0;
    bool coalesce_full_span_reads = false;
    bool preserve_exact_window_end = false;
    std::uint8_t mode_byte = 0;

    std::vector<IOAlignSlot> slots;
    void* raw_buffer = nullptr;
    uint8_t* aligned_base = nullptr;

private:
    void MoveSlotToBack(std::size_t slot_index);
    void* AcquireSlotBuffer(std::uint64_t aligned_offset, std::uint8_t flags);
    bool FlushSlot(std::size_t slot_index);
};

#pragma pack(push, 1)
struct BlockTableEntry {
    std::uint32_t file_offset_low = 0;
    std::uint32_t file_offset_high = 0;
    std::uint32_t compressed_size_lo = 0;
    std::uint32_t file_size_lo = 0;
    std::uint32_t flags = 0;
    std::uint64_t attribute_qword = 0;
    std::uint32_t attribute_dword = 0;
    std::uint8_t  attribute_md5[16] = {};
    std::uint8_t  attribute_lookup_flag = 0;
};
#pragma pack(pop)
static_assert(sizeof(BlockTableEntry) == 49,
              "BlockTableEntry must match the 49-byte GetBlockTableEntry copy");

namespace detail {

struct ReadOnlyArchiveHandleCompat {
    std::array<std::byte, 0x18> reserved_00{};
    std::uint32_t field_18 = 0;
    std::array<std::byte, 0x194> reserved_1C{};
    std::vector<BlockTableEntry> block_table{};
    std::uint32_t next_write_offset_low = 0xFFFFFFFFu;
    std::uint32_t next_write_offset_high = 0xFFFFFFFFu;

    static constexpr std::uint32_t kInvalidNextWriteOffsetPart = 0xFFFFFFFFu;

    bool HasCachedNextWriteOffset() const {
        return (next_write_offset_low & next_write_offset_high) !=
               kInvalidNextWriteOffsetPart;
    }

    void InvalidateNextWriteOffsetCache() {
        next_write_offset_low = kInvalidNextWriteOffsetPart;
        next_write_offset_high = kInvalidNextWriteOffsetPart;
    }

    std::uint64_t GetNextWriteOffset() const {
        return static_cast<std::uint64_t>(next_write_offset_low) |
               (static_cast<std::uint64_t>(next_write_offset_high) << 32);
    }

    void SetNextWriteOffset(const std::uint64_t next_write_offset) {
        next_write_offset_low = static_cast<std::uint32_t>(next_write_offset);
        next_write_offset_high =
            static_cast<std::uint32_t>(next_write_offset >> 32);
    }

    void AdvanceNextWriteOffset(const std::uint32_t committed_bytes) {
        const auto previous_low = next_write_offset_low;
        next_write_offset_low += committed_bytes;
        next_write_offset_high += next_write_offset_low < previous_low ? 1u : 0u;
    }

    std::uint64_t GetOrComputeNextWriteOffset() {
        if (HasCachedNextWriteOffset()) {
            return GetNextWriteOffset();
        }

        std::uint32_t archive_offset_low = 0;
        std::uint32_t archive_offset_high = 0;
        std::memcpy(&archive_offset_low, reserved_1C.data() + 0x3C,
                    sizeof(archive_offset_low));
        std::memcpy(&archive_offset_high, reserved_1C.data() + 0x40,
                    sizeof(archive_offset_high));

        std::uint64_t next_write_offset =
            static_cast<std::uint64_t>(archive_offset_low) |
            (static_cast<std::uint64_t>(archive_offset_high) << 32);
        next_write_offset += 44u;

        for (const auto& entry : block_table) {
            if ((entry.flags & 0x80000000u) == 0) {
                continue;
            }

            const std::uint64_t entry_offset =
                static_cast<std::uint64_t>(entry.file_offset_low) |
                (static_cast<std::uint64_t>(entry.file_offset_high) << 32);
            const std::uint64_t entry_end =
                entry_offset + static_cast<std::uint64_t>(entry.compressed_size_lo);
            if (next_write_offset < entry_end) {
                next_write_offset = entry_end;
            }
        }

        SetNextWriteOffset(next_write_offset);
        return next_write_offset;
    }
};

static_assert(offsetof(ReadOnlyArchiveHandleCompat, field_18) == 0x18,
              "Read-only archive handle field_18 must stay at +0x18");
static_assert(offsetof(ReadOnlyArchiveHandleCompat, block_table) == 0x1B0,
              "Read-only archive handle block table must stay at +0x1B0");

}

class IOFileReadOnlyUnit : public IOUnit {
public:
    std::uint32_t GetTypeTag() const override;
    bool Read(void* dest, std::uint64_t offset_lo, std::uint64_t offset_hi,
              std::uint32_t size, std::uint32_t* out_bytes_read) override;
    void Init(void* archive_handle, const char* path,
              std::uint32_t block_index_value,
              const BlockTableEntry& cached_block_entry,
              std::uint8_t owned_flag, IOUnit* next_unit);
    void Destroy(bool free_self) override;
    bool Close() override;

    std::uint32_t GetField5() const override { return TruncatePointer(archive); }
    std::uint32_t GetField7() const override { return TruncatePointer(path_ptr); }
    std::uint32_t DispatchField11() override { return GetField7(); }

    std::uint32_t GetArchivePointer() const { return GetField5(); }
    std::uint32_t GetPathPointer() const { return GetField7(); }

    bool GetSize(uint32_t* out_lo, uint32_t* out_hi) override {

        if (out_lo) *out_lo = block_entry.file_size_lo;
        if (out_hi) *out_hi = 0;
        return true;
    }

    bool GetCompressedSize(uint32_t* out_lo, uint32_t* out_hi) {
        if (out_lo) *out_lo = block_entry.compressed_size_lo;
        if (out_hi) *out_hi = 0;
        return true;
    }

    std::uint32_t GetArchiveField24() const;

    std::uint64_t GetAttributeQword() const { return block_entry.attribute_qword; }

    std::uint32_t GetAttributeDword() const { return block_entry.attribute_dword; }

    const std::uint8_t* GetAttributeMd5Digest() const { return block_entry.attribute_md5; }

    std::uint8_t GetAttributeLookupFlag() const { return block_entry.attribute_lookup_flag; }

    std::uint32_t GetBlockFlags() const { return block_entry.flags; }

    bool HasHighFlags() const { return (block_entry.flags & 0xF0000) != 0; }

    void ClearAttributeLookupFlag();

    void MarkBlockEntryDirty();

    std::uint32_t reserved_10 = 0;
    void*         archive = nullptr;
    std::uint32_t path_length = 0;
    char*         path_ptr = inline_path;
    char          inline_path[260] = {};
    std::uint32_t block_index = 0;
    BlockTableEntry block_entry = {};

private:
    void InitArchiveFileState(void* archive_handle, const char* path,
                              std::uint32_t block_index_value,
                              const BlockTableEntry& cached_block_entry,
                              std::uint8_t owned_flag, IOUnit* next_unit);
    void DestroyArchiveFileState(bool free_self);

    static std::uint32_t TruncatePointer(const void* value) {
        return static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(value));
    }
};

class IOFileWritableUnit final : public IOFileReadOnlyUnit {
public:
    std::uint32_t GetTypeTag() const override;
    bool Read(void* dest, std::uint64_t offset_lo, std::uint64_t offset_hi,
              std::uint32_t size, std::uint32_t* out_bytes_read) override;
    bool Write(const void* source, std::uint32_t offset_lo,
               std::uint32_t offset_hi, std::uint32_t size) override;
};

class IOInlinePathStorage {
public:
    static constexpr std::size_t kInlineCapacity = 260;

    IOInlinePathStorage() = default;
    IOInlinePathStorage(const IOInlinePathStorage&) = delete;
    IOInlinePathStorage& operator=(const IOInlinePathStorage&) = delete;

    ~IOInlinePathStorage() { Reset(); }

    void Assign(const char* source);
    void Reset();

    std::uint32_t length() const { return length_; }

    char* data() { return heap_buffer_ ? heap_buffer_ : inline_buffer_; }
    const char* data() const {
        return heap_buffer_ ? heap_buffer_ : inline_buffer_;
    }

    bool uses_heap() const { return heap_buffer_ != nullptr; }

private:
    std::uint32_t length_ = 0;
    char*         heap_buffer_ = nullptr;
    char          inline_buffer_[kInlineCapacity] = {};
};

class IOStreamUnit : public IOUnit {
public:
    IOStreamUnit() = default;

    void Init(void* source_handle, const char* archive_path,
              const char* file_path, uint8_t owned_flag, IOUnit* chain_next);

    void Destroy(bool free_self) override;
    bool Close() override;
    std::uint32_t GetField5() const override;
    std::uint32_t GetField7() const override;
    std::uint32_t DispatchField11() override;

    void* source_handle() const { return source_handle_; }
    const char* archive_path() const { return archive_path_.data(); }
    const char* file_path() const { return file_path_.data(); }
    std::uint32_t archive_path_length() const { return archive_path_.length(); }
    std::uint32_t file_path_length() const { return file_path_.length(); }
    bool archive_path_uses_heap() const { return archive_path_.uses_heap(); }
    bool file_path_uses_heap() const { return file_path_.uses_heap(); }

private:
    static std::uint32_t TruncatePointer(const void* value) {
        return static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(value));
    }

    std::uint32_t     reserved_10_ = 0;
    void*             source_handle_ = nullptr;
    IOInlinePathStorage archive_path_;
    IOInlinePathStorage file_path_;
};

static_assert(sizeof(void*) != 4 || sizeof(IOStreamUnit) == 560,
              "IOStreamUnit must match the 560-byte IDA layout on 32-bit builds");

struct IOStreamUnitSourceLifetimeHooks {
    std::function<void(void*)> retain;
    std::function<void(void*)> release;
};

class IOLooseFileUnit : public IOUnit {
public:
    static constexpr std::uint64_t kInvalidCursorOffset = ~std::uint64_t{0};

    void Init(std::uint32_t file_handle_index, std::uint8_t owned_flag);

    std::uint32_t GetTypeTag() const override { return IOFileUnit_GetTypeTag(); }
    bool Read(void* dest, std::uint64_t offset_lo, std::uint64_t offset_hi,
              std::uint32_t size, std::uint32_t* out_bytes_read) override;
    bool Write(const void* source, std::uint32_t offset_lo,
               std::uint32_t offset_hi, std::uint32_t size) override;
    bool Flush() override;
    bool GetSize(std::uint32_t* out_lo, std::uint32_t* out_hi) override;
    bool SetSize(std::uint32_t size_lo, std::uint32_t size_hi) override;
    void Destroy(bool free_self) override;
    bool Close() override;
    bool ReadWithCount(void* dest, std::uint32_t offset_lo,
                       std::uint32_t offset_hi, std::uint32_t size,
                       std::uint32_t* out_bytes_read);

    std::uint32_t file_handle_index = 0;
    std::uint64_t cached_cursor_offset = kInvalidCursorOffset;
};

class IOAsyncFileUnit : public IOUnit {
public:
    IOAsyncFileUnit() = default;
    ~IOAsyncFileUnit() override;

    void Init(std::uint32_t file_handle_index, std::uint8_t owned_flag);
    std::uint32_t GetTypeTag() const override { return IOFileUnit_GetTypeTag(); }
    bool Read(void* dest, std::uint64_t offset_lo, std::uint64_t offset_hi,
              std::uint32_t size, std::uint32_t* out_bytes_read) override;
    bool Write(const void* source, std::uint32_t offset_lo,
               std::uint32_t offset_hi, std::uint32_t size) override;
    bool Flush() override;
    void Destroy(bool free_self) override;

    bool Close() override;
    bool GetSize(std::uint32_t* out_lo, std::uint32_t* out_hi) override;

    bool SetSize(std::uint32_t size_lo, std::uint32_t size_hi) override;
    bool ReadWithCount(void* dest, std::uint32_t offset_lo,
                       std::uint32_t offset_hi, std::uint32_t size,
                       std::uint32_t* out_bytes_read);

    std::uint32_t file_handle_index = 0;
    void*         raw_buffer = nullptr;
    std::uint8_t* aligned_buffer = nullptr;
    std::uint32_t sector_mask = 0xFFFF;
    std::uint64_t cached_end_position = 0;
};

class IOMpqSectorReaderUnit : public IOUnit {
public:
    static constexpr std::size_t kInlineSectorOffsetCapacity = 10;
    static constexpr std::size_t kInlineAuxEntryCapacity = 1;

    void InitSectorTable(std::uint32_t file_size,
                         std::uint32_t compressed_size,
                         std::uint32_t sector_count,
                         std::uint32_t sector_size,
                         std::uint32_t block_flags,
                         std::uint32_t decryption_key,
                         std::uint8_t owned_flag,
                         IOUnit* chain_next);

    void InitSingleUnit(std::uint32_t file_size,
                        std::uint32_t compressed_size,
                        std::uint32_t block_flags,
                        std::uint32_t decryption_key,
                        std::uint8_t owned_flag,
                        IOUnit* chain_next);

    bool GetSize(std::uint32_t* out_lo, std::uint32_t* out_hi) override;
    void Destroy(bool free_self) override;

    [[nodiscard]] std::uint32_t GetLogicalSectorSize(
        std::uint32_t sector_index) const;

    [[nodiscard]] std::uint32_t GetMaxReadPlanDescriptorCount(
        std::uint64_t logical_offset, std::uint64_t span_size) const;

    [[nodiscard]] std::uint32_t CountNonNominalPackedSectorRun(
        std::uint32_t start_sector_index,
        std::uint32_t end_sector_index) const;

    [[nodiscard]] std::uint32_t CountNominalPackedSectorRun(
        std::uint32_t start_sector_index,
        std::uint32_t end_sector_index) const;

    bool UsesInlineSectorOffsets() const;
    bool UsesInlineAuxEntries() const;

    std::uint32_t sector_offset_entry_count = 0;
    std::uint32_t* sector_offsets = nullptr;
    std::uint32_t aux_entry_count = 0;
    std::uint32_t* aux_entries = nullptr;
    std::uint32_t sector_size = 0;
    std::uint32_t final_sector_size = 0;
    std::uint32_t block_flags = 0;
    std::uint32_t decryption_key = 0;
    std::uint32_t file_size = 0;

private:
    void ResetInlineStorage();
    [[nodiscard]] std::uint32_t GetExpectedPackedSectorSpan(
        std::uint32_t sector_index) const;
    [[nodiscard]] std::uint32_t CountPackedSectorRun(
        std::uint32_t start_sector_index,
        std::uint32_t end_sector_index,
        bool match_expected_span) const;

    std::array<std::uint32_t, kInlineSectorOffsetCapacity> inline_sector_offsets{};
    std::array<std::uint32_t, kInlineAuxEntryCapacity> inline_aux_entries{};
    std::unique_ptr<std::uint32_t[]> heap_sector_offsets;
    std::unique_ptr<std::uint32_t[]> heap_aux_entries;
};

bool IOUnitContainer_Release(IOUnit* chain);

IOUnit* IOUnitContainer_CreateAlignUnit(
    IOUnitAllocator* alloc, std::uint64_t requested_offset,
    std::uint32_t slot_size, std::uint32_t slot_count,
    bool coalesce_full_span_reads, bool preserve_exact_window_end,
    std::uint32_t alignment, std::uint8_t mode_byte, IOUnit* chain);

IOUnit* IOUnitContainer_CreateStreamUnit(IOUnitAllocator* alloc,
                                          void* source_handle,
                                          const char* archive_path,
                                          const char* file_path,
                                          IOUnit* chain);

IOUnit* IOUnitContainer_CreateFileUnit(IOUnitAllocator* alloc,
                                       const char* path, int flags);

IOUnit* IOUnitContainer_CreateAsyncFileUnit(IOUnitAllocator* alloc,
                                            const char* path, int flags);

IOUnit* IOUnitContainer_CreateReadOnlyFile(IOUnitAllocator* alloc,
                                           void* archive, const char* path,
                                           std::uint32_t block_index,
                                           const BlockTableEntry& cached_block_entry,
                                           IOUnit* chain);

IOUnit* IOUnitContainer_CreateWritableFile(IOUnitAllocator* alloc,
                                           void* archive, const char* path,
                                           std::uint32_t block_index,
                                           const BlockTableEntry& cached_block_entry,
                                           IOUnit* chain);

IOUnit* SFileOpenFileEx_CreateSectorTableReader(
    IOUnitAllocator* alloc, std::uint32_t file_size,
    std::uint32_t compressed_size, std::uint32_t sector_size,
    std::uint32_t block_flags, std::uint32_t decryption_key, IOUnit* chain);

IOUnit* SFileOpenFileEx_CreateSingleUnitSectorReader(
    IOUnitAllocator* alloc, std::uint32_t file_size,
    std::uint32_t compressed_size, std::uint32_t block_flags,
    std::uint32_t decryption_key, IOUnit* chain);

struct IOUnitContainerPartFileInfo {
    const char* path = nullptr;
};

bool IOUnitContainer_DeletePartFile(
    void* io_provider, const IOUnitContainerPartFileInfo* file_info);

bool IOFileUnit_DirectRead(uint32_t file_offset, const char* filepath,
                           void* dest, uint32_t size, uint32_t* bytes_read);

IOUnit* SFileOpenFileEx_CreateFileUnit(IOUnitAllocator* alloc,
                                       const char* path, int flags);

void SetIOAsyncFileUnitSectorSizeResolverForTests(
    std::function<std::optional<std::uint32_t>(const std::string&)> resolver);

void SetIOFileUnitDirectReadRetryFlagForTests(bool enabled);
void ResetIOFileUnitDirectReadRetryFlagForTests();

void SetIOStreamUnitSourceLifetimeResolverForTests(
    std::function<IOStreamUnitSourceLifetimeHooks(void*)> resolver);

}

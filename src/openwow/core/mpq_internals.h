
#pragma once

#include "io_unit_container.h"
#include "openwow/vfs/mpq_hash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace openwow::core {

struct MPQHashEntry {
    uint32_t hash_a;
    uint32_t hash_b;
    uint16_t locale;
    uint8_t  platform;
    uint8_t  pad;
    uint32_t block_index;
};
static_assert(sizeof(MPQHashEntry) == 16, "MPQHashEntry must be 16 bytes");

inline constexpr std::uint32_t kStdVectorPod16MaxEntries = 0x0FFFFFFFu;

template <typename Entry>
class ReadArchiveTablesPod16Buffer {
public:
    static_assert(sizeof(Entry) == 16,
                  "ReadArchiveTablesPod16Buffer requires 16-byte entries");
    static_assert(std::is_trivially_copyable_v<Entry>,
                  "ReadArchiveTablesPod16Buffer requires trivially-copyable entries");

    void ResizeFill(std::uint32_t entry_count, const Entry& fill_value) {
        if (entry_count > kStdVectorPod16MaxEntries) {
            throw std::length_error("ReadArchiveTablesPod16Buffer exceeds x86 vector limit");
        }

        if (entry_count <= entries_.size()) {
            entries_.resize(entry_count);
            return;
        }

        entries_.resize(entry_count, fill_value);
    }

    [[nodiscard]] std::uint32_t size() const noexcept {
        return static_cast<std::uint32_t>(entries_.size());
    }

    [[nodiscard]] bool empty() const noexcept {
        return entries_.empty();
    }

    [[nodiscard]] Entry* data() noexcept {
        return entries_.empty() ? nullptr : entries_.data();
    }

    [[nodiscard]] const Entry* data() const noexcept {
        return entries_.empty() ? nullptr : entries_.data();
    }

    [[nodiscard]] std::vector<Entry>& entries() noexcept {
        return entries_;
    }

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept {
        return entries_;
    }

private:
    std::vector<Entry> entries_;
};

using ReadArchiveTablesHashEntryBuffer = ReadArchiveTablesPod16Buffer<MPQHashEntry>;

uint32_t MPQ_HashString(const uint8_t* str, uint32_t hash_type);

uint64_t ComputeFileHashPair(const char* path);

struct JenkinsHashLittle2Result {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

[[nodiscard]] JenkinsHashLittle2Result JenkinsHashLittle2(
    std::span<const std::uint8_t> bytes,
    std::uint32_t first_seed,
    std::uint32_t second_seed) noexcept;

bool BuffersDiffer16(const std::uint8_t* left,
                     const std::uint8_t* right);
bool BuffersEqual16(const std::uint8_t* left,
                    const std::uint8_t* right);
bool IsInvalidAttributeMd5Digest(const std::uint8_t* digest16);

bool SFileFreeBlock(void* block);

void* AllocSFileHandle(uint32_t size, uint8_t flags,
                       const char* source_file, int source_line);

char* NormalizePath(const char* input, char* output, uint32_t output_size);

void UpdateBlockTableEntry(std::vector<BlockTableEntry>& table,
                           uint32_t index, const BlockTableEntry& entry);

struct BitPackedBufferView {
    const std::uint8_t* data = nullptr;
};

struct PackedBitValueReader {
    BitPackedBufferView packed_bits{};
    std::uint32_t value_bit_count = 0;
    std::uint32_t entry_stride_bits = 0;
};

struct BETBlockTableReader {
    BitPackedBufferView packed_fields{};
    openwow::vfs::MPQUint32Vector flags_by_index{};

    std::uint32_t entry_stride_bits = 0;

    std::uint32_t file_offset_bit_offset = 0;
    std::uint32_t file_offset_bit_count = 0;

    std::uint32_t file_size_bit_offset = 0;
    std::uint32_t file_size_bit_count = 0;

    std::uint32_t compressed_size_bit_offset = 0;
    std::uint32_t compressed_size_bit_count = 0;

    std::uint32_t flags_index_bit_offset = 0;
    std::uint32_t flags_index_bit_count = 0;

    std::uint32_t lookup_flag_bit_offset = 0;
    std::uint32_t lookup_flag_bit_count = 0;

    std::uint32_t reader_flags = 0;
};

bool BETTable_ReadBlockTableEntry(const BETBlockTableReader& reader,
                                  std::uint32_t entry_index,
                                  BlockTableEntry* out_entry);

struct ArchiveBlockTableSource {
    const BETBlockTableReader* bet_reader = nullptr;
    std::span<const std::uint32_t> attribute_dwords{};
    std::span<const std::array<std::uint8_t, 16>> attribute_md5_digests{};
    std::span<const std::uint64_t> attribute_qwords{};
    std::uint32_t archive_flags = 0;
};

struct SFileHashLookupArchiveView;
using SFileHashLookupEnsureTablesLoadedFn =
    void (*)(SFileHashLookupArchiveView* archive);

struct SFileHashLookupArchiveView {
    openwow::vfs::MPQHashTableVector hash_table{};
    const std::vector<BlockTableEntry>* block_table = nullptr;
    ArchiveBlockTableSource block_table_source{};
    std::uint16_t locale = 0;
    std::uint8_t platform = 0;
    SFileHashLookupEnsureTablesLoadedFn ensure_tables_loaded = nullptr;
};

struct ArchiveAttributeMd5LookupArchiveView;
using ArchiveAttributeMd5LookupEnsureTablesLoadedFn =
    void (*)(ArchiveAttributeMd5LookupArchiveView* archive);

struct ArchiveAttributeMd5LookupArchiveView {
    const std::vector<BlockTableEntry>* block_table = nullptr;
    ArchiveBlockTableSource block_table_source{};
    std::uint32_t block_entry_count = 0;
    ArchiveAttributeMd5LookupEnsureTablesLoadedFn ensure_tables_loaded = nullptr;
};

BlockTableEntry* GetBlockTableEntry(
    const std::vector<BlockTableEntry>& block_table,
    const ArchiveBlockTableSource& source, BlockTableEntry* out_entry,
    std::uint32_t index);

class ReadArchiveTablesExtendedBlockWordBuffer {
public:
    void Reset(std::uint32_t entry_count);

    [[nodiscard]] std::uint32_t entry_count() const noexcept {
        return entry_count_;
    }

    [[nodiscard]] std::uint16_t* data() noexcept {
        return words_.empty() ? nullptr : words_.data();
    }

    [[nodiscard]] const std::uint16_t* data() const noexcept {
        return words_.empty() ? nullptr : words_.data();
    }

private:
    std::uint32_t              entry_count_ = 0;
    std::vector<std::uint16_t> words_;
};

struct ReadArchiveTablesAttributesHeader {
    std::uint32_t version = 0;
    std::uint32_t flags = 0;
};

void ReadArchiveTables_SetAttributeDword(std::vector<BlockTableEntry>& table,
                                         std::uint32_t index,
                                         std::uint32_t value);
void ReadArchiveTables_SetAttributeQword(std::vector<BlockTableEntry>& table,
                                         std::uint32_t index,
                                         std::uint32_t low,
                                         std::uint32_t high);
void ReadArchiveTables_SetAttributeMd5Digest(std::vector<BlockTableEntry>& table,
                                             std::uint32_t index,
                                             const std::uint8_t* digest16);
void ReadArchiveTables_SetAttributeLookupFlag(std::vector<BlockTableEntry>& table,
                                              std::uint32_t index,
                                              std::uint8_t value);
bool ReadArchiveTables_ApplyAttributes(
    const std::uint8_t* bytes, std::size_t size,
    std::vector<BlockTableEntry>& table,
    ReadArchiveTablesAttributesHeader* out_header = nullptr);

class SFileHashTable {
public:
    SFileHashTable() = default;
    ~SFileHashTable();

    struct LookupContext {
        PackedBitValueReader stored_hash_prefixes{};
    };

    void Init(uint32_t requested_entry_count, int rebuild_shift);

    void Destroy();

    int32_t Lookup(uint64_t hash_pair, void* archive_ctx) const;

    void ConfigureArchiveBackedLookup(const std::uint8_t* tags,
                                      std::uint32_t tag_count,
                                      const PackedBitValueReader& packed_block_indices);

    void ClearArchiveBackedLookup();

    bool Insert(const char* filename, uint32_t block_index, void* archive_ctx);

    bool Resize(int current_rebuild_shift);

    uint32_t GetRequestedEntryCount() const { return requested_entry_count_; }
    uint32_t GetSlotCount() const { return slot_count_; }
    uint32_t GetPackedEntryBitCount() const { return packed_entry_bit_count_; }
    int GetRebuildShift() const { return rebuild_shift_; }
    uint64_t GetProbeMask() const { return probe_mask_; }
    uint64_t GetProbeMidpoint() const { return probe_midpoint_; }
    bool HasPackedEntries() const { return packed_entries_initialized_; }
    const std::vector<uint8_t>& GetTags() const { return tags_; }

private:
    void EnsurePackedEntriesAllocated();

    uint64_t ComputeProbeValue(uint64_t hash_pair) const;
    uint32_t ComputeSlotIndex(uint64_t probe_value) const;
    uint8_t ComputeTagByte(uint64_t probe_value) const;
    uint64_t ComputeStoredHashPrefix(uint64_t hash_pair) const;

    uint32_t requested_entry_count_ = 0;
    uint32_t slot_count_ = 0;
    uint32_t packed_entry_bit_count_ = 0;
    int rebuild_shift_ = 0;
    uint32_t reserved_28_ = 0;
    uint64_t probe_mask_ = 0;
    uint64_t probe_midpoint_ = 0;
    std::vector<uint8_t> tags_;
    PackedBitValueReader archive_packed_block_indices_{};
    std::vector<uint32_t> packed_block_indices_;
    std::vector<uint64_t> stored_hash_prefixes_;
    bool packed_entries_initialized_ = false;
};

uint32_t SFileHashLookup(void* archive, const uint8_t* filename,
                         bool fuzzy_match, bool* is_patch_file);

class ArchiveAttributeMd5Lookup {
public:
    [[nodiscard]] int32_t Lookup(ArchiveAttributeMd5LookupArchiveView* archive,
                                 const std::uint8_t* digest16,
                                 bool* is_patch_file);

    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return block_indices_by_digest_.size();
    }

private:
    struct Key {
        std::array<std::uint8_t, 16> bytes{};

        [[nodiscard]] bool operator==(const Key& other) const noexcept {
            return bytes == other.bytes;
        }
    };

    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept;
    };

    void EnsureInitialized(ArchiveAttributeMd5LookupArchiveView* archive);

    std::once_flag init_once_{};
    bool initialized_ = false;
    std::unordered_map<Key, std::uint32_t, KeyHash> block_indices_by_digest_{};
};

using DecompressStageFunction = bool (*)(void* dest, uint32_t* dest_size,
                                         const void* src, uint32_t src_size);

struct SCompCompressionStageSpec {
    std::uint8_t type = 0;
    std::uint8_t quality = 0;
};

class InlineScratchBuffer16K {
public:
    static constexpr std::uint32_t kInlineCapacity = 0x4000;

    InlineScratchBuffer16K() noexcept;
    InlineScratchBuffer16K(const InlineScratchBuffer16K&) = delete;
    InlineScratchBuffer16K& operator=(const InlineScratchBuffer16K&) = delete;

    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] bool uses_inline_storage() const noexcept {
        return data_ == inline_storage_.data();
    }

    void ResizePreservingPrefix(std::uint32_t requested_size);

private:
    std::uint32_t size_ = 0;
    std::array<std::uint8_t, kInlineCapacity> inline_storage_;
    std::uint8_t* data_ = nullptr;
    std::unique_ptr<std::uint8_t[]> heap_storage_;
};

bool SCompCompress(void* dest, uint32_t* dest_size,
                   std::uint8_t* out_needs_checksum,
                   const void* src, uint32_t src_size,
                   uint32_t stage_count,
                   const SCompCompressionStageSpec* stage_specs);

bool SCompDecompressTable(void* dest, uint32_t* dest_size,
                          const void* src, uint32_t src_size);

bool SCompDecompressZlib(void* dest, uint32_t* dest_size,
                         const void* src, uint32_t src_size);

bool DecompressTable(void* dest, uint32_t* dest_size,
                     const void* src, uint32_t src_size,
                     const DecompressStageFunction* decompress_chain);

}

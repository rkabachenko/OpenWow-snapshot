
#pragma once

#include <cstdint>
#include <cstddef>

namespace openwow::vfs {

void MPQ_InitCryptTable();

std::uint32_t MPQ_DecryptBlock(std::uint32_t* words, std::uint32_t byte_count,
                               std::uint32_t key);

std::uint32_t MPQ_EncryptBlock(std::uint32_t* words, std::uint32_t byte_count,
                               std::uint32_t key);

std::uint32_t MPQ_HashString(const char* str, int hash_type);

struct MPQBlockTableVector {
    std::uint8_t* begin{nullptr};
    std::uint8_t* end{nullptr};
};

static constexpr std::size_t kBlockTableEntrySize = 49;

std::uint32_t GetBlockTableCapacityFromVector(const MPQBlockTableVector* vec);

std::uint8_t* GetBlockTableEntryAt(const MPQBlockTableVector* vec,
                                    std::uint32_t index);

struct MPQHashTableEntry {
    std::uint32_t hash_a;
    std::uint32_t hash_b;
    std::uint16_t locale;
    std::uint16_t platform;
    std::uint32_t block_index;
};

static_assert(sizeof(MPQHashTableEntry) == 16);

MPQHashTableEntry* InitHashTableEntryEmpty(MPQHashTableEntry* entry);

struct MPQHashTableVector {
    MPQHashTableEntry* begin{nullptr};
    MPQHashTableEntry* end{nullptr};
};

std::uint32_t GetHashTableCapacity(const MPQHashTableVector* vec);

bool IsHashTableEmpty(const MPQHashTableVector* vec);

MPQHashTableEntry* GetHashTableEntryAt(const MPQHashTableVector* vec,
                                        std::uint32_t index);

struct MPQUint32Vector {
    std::uint32_t* begin{nullptr};
    std::uint32_t* end{nullptr};
};

std::uint32_t GetUint32VectorCount(const MPQUint32Vector* vec);

std::uint32_t* GetUint32VectorEntryAt(const MPQUint32Vector* vec,
                                       std::uint32_t index);

struct HashTableState {
    std::uint8_t  reserved_00[24]{};
    std::int32_t  shift{0};
    std::uint32_t reserved_28{0};
    std::uint64_t mask{0};
    std::uint64_t midpoint{0};
};

static_assert(offsetof(HashTableState, shift) == 24);
static_assert(offsetof(HashTableState, mask) == 32);
static_assert(offsetof(HashTableState, midpoint) == 40);
static_assert(sizeof(HashTableState) == 48);

void HashTable_Rebuild(HashTableState* state, int shift);

}

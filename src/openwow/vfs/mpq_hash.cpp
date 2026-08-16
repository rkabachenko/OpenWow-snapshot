
#include "openwow/vfs/mpq_hash.h"

#include <cstdlib>

namespace openwow::vfs {

namespace {

constexpr std::size_t kMpqCryptTableEntryCount = 5 * 256;
constexpr std::size_t kMpqBlockCryptTableOffset = 4 * 256;

std::uint32_t g_crypt_table[kMpqCryptTableEntryCount];
std::uint8_t  g_upper_table[256];
bool          g_crypt_table_initialized = false;

[[nodiscard]] constexpr std::uint64_t ShiftLeftOneAllshl(std::uint8_t count) noexcept {
    if (count >= 64) {
        return 0;
    }
    return 1ULL << count;
}

[[noreturn]] void AbortInvalidParameter() {

    std::abort();
}

[[nodiscard]] constexpr std::uint32_t AdvanceMpqBlockKey(
    std::uint32_t key) noexcept {
    return ((~key << 21) + 0x11111111u) | (key >> 11);
}

void EnsureMpqCryptTableInitialized() {
    if (!g_crypt_table_initialized) {
        MPQ_InitCryptTable();
    }
}

}

void MPQ_InitCryptTable() {
    std::uint32_t seed = 0x00100001;

    for (int i = 0; i < 256; ++i) {
        int index = i;
        for (int j = 0; j < 5; ++j) {
            seed = (seed * 125 + 3) % 0x2AAAAB;
            std::uint32_t temp1 = (seed & 0xFFFF) << 16;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            std::uint32_t temp2 = seed & 0xFFFF;

            g_crypt_table[index] = temp1 | temp2;
            index += 256;
        }
    }

    for (int i = 0; i < 256; ++i) {
        if (i >= 'a' && i <= 'z') {
            g_upper_table[i] = static_cast<std::uint8_t>(i - 'a' + 'A');
        } else if (i >= '/' && i <= '/') {
            g_upper_table[i] = '\\';
        } else {
            g_upper_table[i] = static_cast<std::uint8_t>(i);
        }
    }

    g_crypt_table_initialized = true;
}

std::uint32_t MPQ_DecryptBlock(std::uint32_t* words, std::uint32_t byte_count,
                               std::uint32_t key) {
    std::uint32_t result = 0xEEEEEEEEu;
    const std::uint32_t word_count = byte_count >> 2;
    if (word_count == 0) {
        return result;
    }

    EnsureMpqCryptTableInitialized();

    std::uint32_t* current = words;
    const std::uint32_t* const end = words + word_count;

    while (current != end) {
        const std::uint32_t rolling =
            g_crypt_table[kMpqBlockCryptTableOffset + (key & 0xFFu)] + result;
        const std::uint32_t plain = *current ^ (rolling + key);
        *current++ = plain;
        key = AdvanceMpqBlockKey(key);
        result = rolling + plain + (rolling << 5) + 3;
    }

    return result;
}

std::uint32_t MPQ_EncryptBlock(std::uint32_t* words, std::uint32_t byte_count,
                               std::uint32_t key) {
    std::uint32_t result = 0xEEEEEEEEu;
    const std::uint32_t word_count = byte_count >> 2;
    if (word_count == 0) {
        return result;
    }

    EnsureMpqCryptTableInitialized();

    std::uint32_t* current = words;
    const std::uint32_t* const end = words + word_count;

    while (current != end) {
        const std::uint32_t rolling =
            g_crypt_table[kMpqBlockCryptTableOffset + (key & 0xFFu)] + result;
        const std::uint32_t plain = *current;
        *current++ = plain ^ (rolling + key);
        key = AdvanceMpqBlockKey(key);
        result = rolling + plain + (rolling << 5) + 3;
    }

    return result;
}

std::uint32_t MPQ_HashString(const char* str, int hash_type) {
    EnsureMpqCryptTableInitialized();

    const auto* p = reinterpret_cast<const std::uint8_t*>(str);
    std::uint32_t seed1 = 0x7FED7ED7;
    std::uint32_t seed2 = 0xEEEEEEEE;

    while (*p) {
        std::uint8_t ch = g_upper_table[*p];
        seed1 = g_crypt_table[256 * hash_type + ch] ^ (seed2 + seed1);
        seed2 = ch + seed1 + seed2 + 32 * seed2 + 3;
        ++p;
    }

    return seed1;
}

std::uint32_t GetBlockTableCapacityFromVector(const MPQBlockTableVector* vec) {
    if (!vec || !vec->begin) return 0;
    return static_cast<std::uint32_t>(
        (vec->end - vec->begin) / kBlockTableEntrySize);
}

std::uint8_t* GetBlockTableEntryAt(const MPQBlockTableVector* vec,
                                    std::uint32_t index) {
    if (vec == nullptr || vec->begin == nullptr) {
        AbortInvalidParameter();
    }
    std::uint32_t cap = GetBlockTableCapacityFromVector(vec);
    if (index >= cap) {
        AbortInvalidParameter();
    }
    return vec->begin + kBlockTableEntrySize * index;
}

MPQHashTableEntry* InitHashTableEntryEmpty(MPQHashTableEntry* entry) {
    entry->hash_a = 0xFFFFFFFFu;
    entry->hash_b = 0xFFFFFFFFu;
    entry->locale = 0xFFFFu;
    entry->platform = 0xFFFFu;
    entry->block_index = 0xFFFFFFFFu;
    return entry;
}

std::uint32_t GetHashTableCapacity(const MPQHashTableVector* vec) {
    if (!vec || !vec->begin) return 0;
    return static_cast<std::uint32_t>(vec->end - vec->begin);
}

bool IsHashTableEmpty(const MPQHashTableVector* vec) {
    if (!vec || !vec->begin) return true;
    return (vec->end - vec->begin) == 0;
}

MPQHashTableEntry* GetHashTableEntryAt(const MPQHashTableVector* vec,
                                        std::uint32_t index) {
    if (vec == nullptr || vec->begin == nullptr) {
        AbortInvalidParameter();
    }
    std::uint32_t cap = GetHashTableCapacity(vec);
    if (index >= cap) {
        AbortInvalidParameter();
    }
    return &vec->begin[index];
}

std::uint32_t GetUint32VectorCount(const MPQUint32Vector* vec) {
    if (vec->begin == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(vec->end - vec->begin);
}

std::uint32_t* GetUint32VectorEntryAt(const MPQUint32Vector* vec,
                                       std::uint32_t index) {
    if (vec->begin == nullptr) {
        AbortInvalidParameter();
    }
    if (index >= GetUint32VectorCount(vec)) {
        AbortInvalidParameter();
    }
    return &vec->begin[index];
}

void HashTable_Rebuild(HashTableState* state, int shift) {
    if (!state) return;

    state->shift = shift;
    const std::uint64_t size = ShiftLeftOneAllshl(static_cast<std::uint8_t>(shift));
    state->mask = size - 1;
    state->midpoint = ShiftLeftOneAllshl(static_cast<std::uint8_t>(shift - 1));
}

}


#pragma once

#include "storm_intrusive_list.h"

#include <cstdint>
#include <cstring>

namespace openwow::core {

inline constexpr int kHuffmanNodeCount      = 515;
inline constexpr int kHuffmanSymbolCount    = 258;
inline constexpr int kHuffmanEOFSymbol      = 256;
inline constexpr int kHuffmanEscapeSymbol   = 257;
inline constexpr int kHuffmanDecodeLUTSize  = 128;

struct THuffmanNode {

    std::uintptr_t link_prev = 0;
    std::uintptr_t link_next = 0;

    std::uint32_t  symbol    = 0;
    std::uint32_t  weight    = 0;
    THuffmanNode*  child0    = nullptr;
    THuffmanNode*  child1    = nullptr;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(THuffmanNode) == 24,
              "THuffmanNode must match the 6-DWORD IDA layout");
#endif

struct THuffmanDecodeLUTEntry {
    std::uint32_t  version   = 0;
    std::uint32_t  bit_count = 0;
    std::uint32_t  symbol    = 0;
};

static_assert(sizeof(THuffmanDecodeLUTEntry) == 12,
              "THuffmanDecodeLUTEntry must be 12 bytes (3 DWORDs)");

struct THuffmanBitBuffer {
    const std::uint16_t* read_ptr  = nullptr;
    std::uint32_t        bit_cache = 0;
    std::uint32_t        bits_left = 0;
};

struct THuffmanOutputBuffer {
    std::uint8_t*  base     = nullptr;
    std::uint32_t  remaining = 0;
    std::uint8_t*  write_ptr = nullptr;
    std::uint32_t  bit_cache = 0;
    std::uint32_t  bits_held = 0;

    void OutputBits(std::uint32_t value, std::uint32_t bit_count);

    void FlushBits();
};

class THuffmanTree {
public:
    void Init();

    void BuildTree(std::uint8_t compression_type);

    void Clear();

    std::uint32_t Decompress(std::uint8_t* output, std::uint32_t output_size,
                             THuffmanBitBuffer* input);

    std::uint32_t Compress(THuffmanOutputBuffer* output,
                           const std::uint8_t* input, std::uint32_t input_size,
                           std::uint8_t compression_type);

private:
    THuffmanNode* AllocNode(int insert_mode);

    int DecodeSymbol(THuffmanBitBuffer* input);

    void SplitNode(int new_symbol);

    void IncrementFreq(THuffmanNode* node);

    void EncodeSymbol(THuffmanOutputBuffer* output, int symbol);

    static void UnlinkNode(THuffmanNode* node);

    std::uint32_t version_       = 0;

    THuffmanNode  nodes_[kHuffmanNodeCount];

    StormIntrusiveListRootWords<std::uintptr_t> sorted_list_{};

    StormIntrusiveListRootWords<std::uintptr_t> free_list_{};

    std::uint32_t next_free_index_ = 0;

    THuffmanNode* symbol_table_[kHuffmanSymbolCount] = {};

    THuffmanDecodeLUTEntry decode_lut_[kHuffmanDecodeLUTSize] = {};
};

}

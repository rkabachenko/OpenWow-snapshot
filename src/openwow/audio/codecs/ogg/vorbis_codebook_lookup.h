#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace openwow::audio {

namespace detail {

constexpr std::uint8_t kVorbisNoCodewordLength = 0xffu;

inline std::int32_t WrapSignedMul32(std::int32_t lhs, std::int32_t rhs) noexcept {
    const auto product = static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(product));
}

inline std::uint32_t ReverseBits32(std::uint32_t value) noexcept {
    value = ((value & 0x55555555u) << 1u) | ((value >> 1u) & 0x55555555u);
    value = ((value & 0x33333333u) << 2u) | ((value >> 2u) & 0x33333333u);
    value = ((value & 0x0f0f0f0fu) << 4u) | ((value >> 4u) & 0x0f0f0f0fu);
    value = ((value & 0x00ff00ffu) << 8u) | ((value >> 8u) & 0x00ff00ffu);
    return (value << 16u) | (value >> 16u);
}

inline std::uint32_t LowerBitMask(const int bit_count) noexcept {
    if (bit_count <= 0) {
        return 0;
    }
    if (bit_count >= 32) {
        return 0xffffffffu;
    }
    return (1u << bit_count) - 1u;
}

inline int ComputeVorbisBitWidth(std::uint32_t value) noexcept {
    int bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1u;
    }
    return bits;
}

inline int ComputeVorbisIlog(const std::int32_t value) noexcept {
    if (value <= 0) {
        return 0;
    }
    return ComputeVorbisBitWidth(static_cast<std::uint32_t>(value));
}

}

inline bool HasVorbisCodewordLength(const std::uint8_t length) noexcept {
    return length != detail::kVorbisNoCodewordLength;
}

inline int CountVorbisCodewordEntries(const std::uint8_t* lengths, const int entry_count) noexcept {
    if (!lengths || entry_count <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < entry_count; ++i) {
        if (HasVorbisCodewordLength(lengths[i])) {
            ++count;
        }
    }
    return count;
}

struct VorbisCodebookDecodeTables {
    bool sparse{false};
    int sorted_entries{0};
    int fast_huffman_bits{0};
    int max_codeword_length{0};
    const std::uint8_t* codeword_lengths{nullptr};
    const std::uint32_t* sorted_codewords{nullptr};
    const int* sorted_values{nullptr};
};

struct VorbisCodebookDecodeState {
    std::uint32_t acc{0};
    int valid_bits{0};
};

inline bool SkipVorbisVectorDecode(const int lookup_type) noexcept {
    return lookup_type <= 0;
}

template <typename FastHuffmanT>
inline int DecodeVorbisCodebookStart(const VorbisCodebookDecodeTables& tables,
                                     const FastHuffmanT* fast_huffman,
                                     VorbisCodebookDecodeState* state) noexcept {
    if (!fast_huffman || !state || !tables.codeword_lengths) {
        return -1;
    }

    const auto consume_bits = [&](const int bit_count) {
        if (bit_count <= 0) {
            return;
        }
        if (bit_count >= 32) {
            state->acc = 0;
        } else {
            state->acc >>= bit_count;
        }
        state->valid_bits -= bit_count;
        if (state->valid_bits < 0) {
            state->valid_bits = 0;
        }
    };

    if (tables.fast_huffman_bits > 0 && state->valid_bits >= tables.fast_huffman_bits) {
        const auto fast_slot = static_cast<int>(
            fast_huffman[state->acc & detail::LowerBitMask(tables.fast_huffman_bits)]);
        if (fast_slot >= 0) {
            const int codeword_length = tables.codeword_lengths[fast_slot];
            if (state->valid_bits >= codeword_length) {
                consume_bits(codeword_length);
                return fast_slot;
            }
        }
    }

    int available_bits = state->valid_bits;
    if (tables.max_codeword_length > 0 && available_bits > tables.max_codeword_length) {
        available_bits = tables.max_codeword_length;
    }
    if (available_bits <= 0) {
        return -1;
    }

    if (tables.sorted_entries <= 0 || !tables.sorted_codewords) {
        consume_bits(available_bits);
        return -1;
    }

    const std::uint32_t reversed_code =
        detail::ReverseBits32(state->acc & detail::LowerBitMask(available_bits));
    int lower = 0;
    int upper = tables.sorted_entries;
    while (upper - lower > 1) {
        const int half = (upper - lower) >> 1;
        const int mid = lower + half;
        if (reversed_code < tables.sorted_codewords[mid]) {
            upper -= half;
        } else {
            lower += half;
        }
    }

    if (tables.sparse) {
        const int codeword_length = tables.codeword_lengths[lower];
        if (codeword_length <= available_bits) {
            consume_bits(codeword_length);
            return lower;
        }
    } else if (tables.sorted_values) {
        const int symbol = tables.sorted_values[lower];
        if (symbol >= 0) {
            const int codeword_length = tables.codeword_lengths[symbol];
            if (codeword_length <= available_bits) {
                consume_bits(codeword_length);
                return symbol;
            }
        }
    }

    consume_bits(available_bits);
    return -1;
}

inline int ComputeVorbisLookup1QuantValues(int dimensions, int entries) {
    std::int32_t quant_values = static_cast<std::int32_t>(
        std::floor(std::pow(static_cast<double>(entries),
                            1.0 / static_cast<double>(dimensions))));

    while (true) {
        std::int32_t upper_bound = 1;

        while (true) {
            std::int32_t lower_bound = 1;
            upper_bound = 1;

            if (dimensions > 0) {
                int remaining_dimensions = dimensions;
                do {
                    lower_bound = detail::WrapSignedMul32(lower_bound, quant_values);
                    upper_bound = detail::WrapSignedMul32(upper_bound, quant_values + 1);
                    --remaining_dimensions;
                } while (remaining_dimensions != 0);
            }

            if (lower_bound <= entries) {
                break;
            }

            --quant_values;
        }

        if (upper_bound > entries) {
            return quant_values;
        }

        ++quant_values;
    }
}

inline int ComputeVorbisExpandedCodebookValueCount(const int dimensions, const int entries,
                                                   const int sorted_entries,
                                                   const bool sparse) noexcept {
    if (dimensions <= 0) {
        return 0;
    }

    const int row_count = sparse ? sorted_entries : entries;
    if (row_count <= 0) {
        return 0;
    }
    if (row_count > std::numeric_limits<int>::max() / dimensions) {
        return 0;
    }

    return row_count * dimensions;
}

inline bool ExpandVorbisCodebookMultiplicands(const int lookup_type, const int dimensions,
                                              const int entries, const int sorted_entries,
                                              const bool sparse, const float minimum_value,
                                              const float delta_value, const bool sequence_p,
                                              const int lookup_values,
                                              const std::uint16_t* quantized_values,
                                              const int* sorted_values,
                                              float* expanded_values) noexcept {
    const int expanded_count =
        ComputeVorbisExpandedCodebookValueCount(dimensions, entries, sorted_entries, sparse);
    if (!quantized_values || !expanded_values || lookup_values <= 0 || expanded_count <= 0) {
        return false;
    }
    if (sparse && !sorted_values) {
        return false;
    }

    const int row_count = sparse ? sorted_entries : entries;
    for (int row = 0; row < row_count; ++row) {
        const int source_entry = sparse ? sorted_values[row] : row;
        if (source_entry < 0 || source_entry >= entries) {
            return false;
        }

        float last = 0.0f;
        const int row_offset = row * dimensions;
        if (lookup_type == 1) {
            int divisor = 1;
            for (int column = 0; column < dimensions; ++column) {
                const int lookup_index = (source_entry / divisor) % lookup_values;
                if (lookup_index < 0 || lookup_index >= lookup_values) {
                    return false;
                }

                const float value = static_cast<float>(quantized_values[lookup_index]) *
                                        delta_value +
                                    minimum_value + last;
                expanded_values[row_offset + column] = value;
                if (sequence_p) {
                    last = value;
                }

                if (column + 1 < dimensions) {
                    if (divisor > std::numeric_limits<int>::max() / lookup_values) {
                        return false;
                    }
                    divisor *= lookup_values;
                }
            }
            continue;
        }

        if (lookup_type != 2) {
            return false;
        }

        if (source_entry > (std::numeric_limits<int>::max() / dimensions)) {
            return false;
        }
        const int source_offset = source_entry * dimensions;
        for (int column = 0; column < dimensions; ++column) {
            const int lookup_index = source_offset + column;
            if (lookup_index < 0 || lookup_index >= lookup_values) {
                return false;
            }

            const float value = static_cast<float>(quantized_values[lookup_index]) * delta_value +
                                minimum_value + last;
            expanded_values[row_offset + column] = value;
            if (sequence_p) {
                last = value;
            }
        }
    }

    return true;
}

}

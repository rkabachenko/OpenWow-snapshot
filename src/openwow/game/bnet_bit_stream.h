#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace openwow::game {

class BNetBitStream {
public:
    static constexpr int kSuccess = 0;
    static constexpr int kInvalidArgument = 1;
    static constexpr int kBufferExhausted = 2;

    [[nodiscard]] static constexpr std::size_t BytesForBits(std::uint32_t bit_count) {
        return static_cast<std::size_t>(bit_count >> 3u) + ((bit_count & 7u) != 0u);
    }

    bool SetBuffer(std::span<std::uint8_t> buffer);
    bool SetBuffer(std::uint8_t* data, std::uint32_t byte_size);

    [[nodiscard]] int ReadBits(std::uint32_t bit_count,
                               std::span<std::uint8_t> output);

    [[nodiscard]] int ReadBits(std::uint32_t bit_count, std::uint8_t* output);

    [[nodiscard]] int ReadAlignedBytes(std::span<std::uint8_t> output);
    [[nodiscard]] int ReadAlignedBytes(std::uint32_t byte_count, std::uint8_t* output);

    [[nodiscard]] int WriteBits(std::uint32_t bit_count,
                                std::span<const std::uint8_t> input);

    [[nodiscard]] int WriteBits(std::uint32_t bit_count, const std::uint8_t* input);

    [[nodiscard]] int WriteBits32(std::uint32_t value);

    [[nodiscard]] int WriteBits64(std::uint64_t value);

    [[nodiscard]] int FlushBits();

    [[nodiscard]] int WriteAlignedBytes(std::span<const std::uint8_t> input);
    [[nodiscard]] int WriteAlignedBytes(std::uint32_t byte_count, const void* input);

    [[nodiscard]] std::uint32_t bit_position() const { return bit_position_; }
    [[nodiscard]] std::uint32_t total_bits() const { return total_bits_; }
    [[nodiscard]] std::uint8_t* buffer() const { return buffer_; }

protected:

    virtual int OnBufferExhausted() { return 0; }

    virtual ~BNetBitStream() = default;

private:
    [[nodiscard]] int WriteScalarBits(std::uint64_t value, std::uint32_t bit_count);

    std::uint8_t* buffer_{nullptr};
    std::uint32_t total_bits_{0};
    std::uint32_t bit_position_{0};
};

struct BNetBitSizeStream {
    std::uint32_t vtable_placeholder{0};
    std::uint32_t bit_count{0};

    void Reset() { bit_count = 0; }

    [[nodiscard]] std::uint32_t BytesRequired() const {
        return (bit_count >> 3u) + ((bit_count & 7u) != 0u);
    }
};
static_assert(sizeof(BNetBitSizeStream) == 8);

[[nodiscard]] int ReadCreepAddress(std::uint8_t* output, BNetBitStream& stream);

[[nodiscard]] int WriteCreepAddress(const std::uint8_t* input, BNetBitStream& stream);

[[nodiscard]] int CalcBitSize_Simple(const void* input, void* context);

[[nodiscard]] int CalcBitSize_FieldArray(const std::uint32_t* fields, void* context);

[[nodiscard]] int CalcBitSize_CreepNode(const std::uint32_t* node, void* context);

[[nodiscard]] int SerializeFieldArray(const std::uint32_t* fields, BNetBitStream& stream);

[[nodiscard]] int SerializeTriple32(const std::uint32_t* fields, BNetBitStream& stream);

[[nodiscard]] int SerializeCreepNode(const std::uint32_t* node, BNetBitStream& stream);

}

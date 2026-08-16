
#include "openwow/game/bnet_bit_stream.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

bool BNetBitStream::SetBuffer(std::span<std::uint8_t> buffer) {
    constexpr auto kMaxBytes =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 8u);
    if (buffer.size() > kMaxBytes) {
        buffer_ = nullptr;
        total_bits_ = 0;
        bit_position_ = 0;
        return false;
    }

    buffer_ = buffer.data();
    total_bits_ = static_cast<std::uint32_t>(buffer.size()) * 8u;
    bit_position_ = 0;
    return true;
}

bool BNetBitStream::SetBuffer(std::uint8_t* data, std::uint32_t byte_size) {
    if (byte_size != 0 && !data) {
        buffer_ = nullptr;
        total_bits_ = 0;
        bit_position_ = 0;
        return false;
    }
    return SetBuffer(std::span<std::uint8_t>{data, byte_size});
}

int BNetBitStream::ReadBits(std::uint32_t bit_count,
                            std::span<std::uint8_t> output) {
    if (output.size() < BytesForBits(bit_count)) {
        return kInvalidArgument;
    }
    return ReadBits(bit_count, output.data());
}

int BNetBitStream::ReadAlignedBytes(std::span<std::uint8_t> output) {
    if (output.size() > std::numeric_limits<std::uint32_t>::max() / 8u) {
        return kInvalidArgument;
    }
    bit_position_ = (bit_position_ + 7u) & ~0x7u;
    return ReadBits(static_cast<std::uint32_t>(output.size()) * 8u, output);
}

int BNetBitStream::ReadAlignedBytes(std::uint32_t byte_count,
                                    std::uint8_t* output) {
    if (byte_count > std::numeric_limits<std::uint32_t>::max() / 8u ||
        (byte_count != 0 && !output)) {
        return kInvalidArgument;
    }
    return ReadAlignedBytes(std::span<std::uint8_t>{output, byte_count});
}

int BNetBitStream::ReadBits(std::uint32_t bit_count, std::uint8_t* output) {
    if (bit_count == 0) return kSuccess;
    if (!output) return kInvalidArgument;

    if ((bit_position_ & 7) == 0 && bit_count >= 8) {
        do {
            if (bit_position_ >= total_bits_) {
                int result = OnBufferExhausted();
                if (result != 0) return result;
                if (bit_position_ >= total_bits_) return 2;
                if ((bit_position_ & 7) != 0) break;
            }

            std::uint32_t available = total_bits_ - bit_position_;
            std::uint32_t aligned_request = bit_count & ~0x7u;
            std::uint32_t chunk = std::min(aligned_request, available);
            std::uint32_t bytes = chunk >> 3;

            std::memcpy(output, buffer_ + (bit_position_ >> 3), bytes);
            bit_position_ += chunk;
            output += bytes;
            bit_count -= chunk;
        } while (bit_count >= 8);
    }

    while (bit_count >= 32) {
        std::uint32_t accum = 0;
        std::uint32_t remaining = 32;

        while (remaining > 0) {
            if (bit_position_ >= total_bits_) {
                int result = OnBufferExhausted();
                if (result != 0) return result;
                if (bit_position_ >= total_bits_) return 2;
            }

            std::uint32_t bit_off = bit_position_ & 7;
            std::uint32_t can_read = 8 - bit_off;
            std::uint32_t to_read = std::min(can_read, remaining);

            std::uint8_t byte_val = buffer_[bit_position_ >> 3] >> bit_off;
            std::uint32_t mask = (1u << to_read) - 1;
            accum |= static_cast<std::uint32_t>(byte_val & mask) << (remaining - to_read);

            bit_position_ += to_read;
            remaining -= to_read;
        }

        output[0] = static_cast<std::uint8_t>(accum >> 24);
        output[1] = static_cast<std::uint8_t>(accum >> 16);
        output[2] = static_cast<std::uint8_t>(accum >> 8);
        output[3] = static_cast<std::uint8_t>(accum);
        output += 4;
        bit_count -= 32;
    }

    if (bit_count == 0) return kSuccess;

    std::uint32_t accum = 0;
    std::uint32_t remaining = bit_count;

    while (remaining > 0) {
        if (bit_position_ >= total_bits_) {
            int result = OnBufferExhausted();
            if (result != 0) return result;
            if (bit_position_ >= total_bits_) return 2;
        }

        std::uint32_t bit_off = bit_position_ & 7;
        std::uint32_t can_read = 8 - bit_off;
        std::uint32_t to_read = std::min(can_read, remaining);

        std::uint8_t byte_val = buffer_[bit_position_ >> 3] >> bit_off;
        std::uint32_t mask = (1u << to_read) - 1;
        accum |= static_cast<std::uint32_t>(byte_val & mask) << (remaining - to_read);

        bit_position_ += to_read;
        remaining -= to_read;
    }

    std::uint32_t bits_left = bit_count;
    std::uint8_t* p = output;
    if (bits_left >= 8) {
        std::uint32_t shift = bits_left - 8;
        std::uint32_t full_bytes = bits_left >> 3;
        do {
            *p++ = static_cast<std::uint8_t>(accum >> shift);
            bits_left -= 8;
            shift -= 8;
        } while (--full_bytes);
    }
    if (bits_left > 0) {
        *p = static_cast<std::uint8_t>(accum & ((1u << bits_left) - 1));
    }

    return kSuccess;
}

int BNetBitStream::WriteBits(std::uint32_t bit_count,
                             std::span<const std::uint8_t> input) {
    if (input.size() < BytesForBits(bit_count)) {
        return kInvalidArgument;
    }
    return WriteBits(bit_count, input.data());
}

int BNetBitStream::WriteBits(std::uint32_t bit_count, const std::uint8_t* input) {
    if (bit_count == 0) return kSuccess;
    if (!input) return kInvalidArgument;

    if ((bit_position_ & 7) == 0 && bit_count >= 8) {
        do {
            if (bit_position_ >= total_bits_) {
                OnBufferExhausted();
                if (bit_position_ >= total_bits_) return 2;
                if ((bit_position_ & 7) != 0) break;
            }

            std::uint32_t available = total_bits_ - bit_position_;
            std::uint32_t aligned_request = bit_count & ~0x7u;
            std::uint32_t chunk = std::min(aligned_request, available);
            std::uint32_t bytes = chunk >> 3;

            std::memcpy(buffer_ + (bit_position_ >> 3), input, bytes);
            bit_position_ += chunk;
            input += bytes;
            bit_count -= chunk;
        } while (bit_count >= 8);
    }

    while (bit_count >= 32) {

        std::uint32_t accum = (static_cast<std::uint32_t>(input[0]) << 24) |
                              (static_cast<std::uint32_t>(input[1]) << 16) |
                              (static_cast<std::uint32_t>(input[2]) << 8) |
                              static_cast<std::uint32_t>(input[3]);
        std::uint32_t remaining = 32;

        while (remaining > 0) {
            if (bit_position_ >= total_bits_) {
                OnBufferExhausted();
                if (bit_position_ >= total_bits_) return 2;
            }

            std::uint32_t bit_off = bit_position_ & 7;
            std::uint32_t can_write = 8 - bit_off;
            std::uint32_t to_write = std::min(can_write, remaining);

            std::uint8_t mask = static_cast<std::uint8_t>((1u << to_write) - 1);
            std::uint8_t bits = static_cast<std::uint8_t>(accum >> (remaining - to_write)) & mask;

            std::uint8_t& target = buffer_[bit_position_ >> 3];
            target = (target & ~(mask << bit_off)) | (bits << bit_off);

            bit_position_ += to_write;
            remaining -= to_write;
        }

        input += 4;
        bit_count -= 32;
    }

    if (bit_count == 0) return kSuccess;

    std::uint32_t accum = 0;
    std::uint32_t bits_to_load = bit_count;
    const std::uint8_t* p = input;
    if (bits_to_load >= 8) {
        std::uint32_t full_bytes = bits_to_load >> 3;
        do {
            accum = (accum << 8) | *p++;
            bits_to_load -= 8;
        } while (--full_bytes);
    }
    if (bits_to_load > 0) {
        accum = (accum << bits_to_load) | (*p & ((1u << bits_to_load) - 1));
    }

    std::uint32_t remaining = bit_count;
    while (remaining > 0) {
        if (bit_position_ >= total_bits_) {
            OnBufferExhausted();
            if (bit_position_ >= total_bits_) return 2;
        }

        std::uint32_t bit_off = bit_position_ & 7;
        std::uint32_t can_write = 8 - bit_off;
        std::uint32_t to_write = std::min(can_write, remaining);

        std::uint8_t mask = static_cast<std::uint8_t>((1u << to_write) - 1);
        std::uint8_t bits = static_cast<std::uint8_t>(accum >> (remaining - to_write)) & mask;

        std::uint8_t& target = buffer_[bit_position_ >> 3];
        target = (target & ~(mask << bit_off)) | (bits << bit_off);

        bit_position_ += to_write;
        remaining -= to_write;
    }

    return kSuccess;
}

int BNetBitStream::WriteBits32(std::uint32_t value) {
    return WriteScalarBits(value, 32u);
}

int BNetBitStream::WriteBits64(std::uint64_t value) {
    return WriteScalarBits(value, 64u);
}

int BNetBitStream::WriteScalarBits(std::uint64_t value, std::uint32_t bit_count) {
    std::uint32_t remaining = bit_count;

    while (remaining > 0) {
        if (bit_position_ >= total_bits_) {
            OnBufferExhausted();
            if (bit_position_ >= total_bits_) return 2;
        }

        std::uint32_t bit_off = bit_position_ & 7;
        std::uint32_t can_write = 8 - bit_off;
        std::uint32_t to_write = std::min(can_write, remaining);

        std::uint8_t mask = static_cast<std::uint8_t>((1u << to_write) - 1);
        std::uint8_t bits =
            static_cast<std::uint8_t>(value >> (remaining - to_write)) & mask;

        std::uint8_t& target = buffer_[bit_position_ >> 3];
        target = (target & ~(mask << bit_off)) | (bits << bit_off);

        bit_position_ += to_write;
        remaining -= to_write;
    }

    return kSuccess;
}

int BNetBitStream::FlushBits() {
    std::uint32_t aligned = (bit_position_ + 7) & ~0x7u;
    if (aligned <= bit_position_) return 0;

    std::uint32_t remaining = aligned - bit_position_;
    if (remaining == 0) return 0;

    while (remaining > 0) {
        if (bit_position_ >= total_bits_) {
            OnBufferExhausted();
            if (bit_position_ >= total_bits_) return 2;
        }

        std::uint32_t bit_off = bit_position_ & 7;
        std::uint32_t can_write = 8 - bit_off;
        std::uint32_t to_write = std::min(can_write, remaining);

        std::uint8_t mask = static_cast<std::uint8_t>((1u << to_write) - 1);
        buffer_[bit_position_ >> 3] &= ~(mask << bit_off);

        bit_position_ += to_write;
        remaining -= to_write;
    }

    return kSuccess;
}

int BNetBitStream::WriteAlignedBytes(std::span<const std::uint8_t> input) {
    if (input.size() > std::numeric_limits<std::uint32_t>::max() / 8u) {
        return kInvalidArgument;
    }
    const int result = FlushBits();
    if (result != kSuccess) return result;
    return WriteBits(static_cast<std::uint32_t>(input.size()) * 8u, input);
}

int BNetBitStream::WriteAlignedBytes(std::uint32_t byte_count, const void* input) {
    if (byte_count > std::numeric_limits<std::uint32_t>::max() / 8u ||
        (byte_count != 0 && !input)) {
        return kInvalidArgument;
    }
    return WriteAlignedBytes(std::span<const std::uint8_t>{
        static_cast<const std::uint8_t*>(input), byte_count});
}

int ReadCreepAddress(std::uint8_t* output, BNetBitStream& stream) {
    if (!output) return BNetBitStream::kInvalidArgument;
    auto address = std::span<std::uint8_t>{output, 6u};
    int result = stream.ReadAlignedBytes(address.first<4>());
    if (result != 0) return result;
    return stream.ReadAlignedBytes(address.subspan<4, 2>());
}

int WriteCreepAddress(const std::uint8_t* input, BNetBitStream& stream) {
    if (!input) return BNetBitStream::kInvalidArgument;
    const auto address = std::span<const std::uint8_t>{input, 6u};
    int result = stream.WriteAlignedBytes(address.first<4>());
    if (result != 0) return result;
    return stream.WriteAlignedBytes(address.subspan<4, 2>());
}

int CalcBitSize_Simple([[maybe_unused]] const void* input, void* context) {
    if (!context) return BNetBitStream::kInvalidArgument;
    auto* ctx = static_cast<std::uint32_t*>(context);
    ctx[1] = (((ctx[1] + 7) & ~0x7u) + 32u & ~0x7u) + 16u;
    return 0;
}

int CalcBitSize_FieldArray(const std::uint32_t* fields, void* context) {
    if (!fields || !context) return BNetBitStream::kInvalidArgument;
    auto* ctx = static_cast<std::uint32_t*>(context);
    std::uint32_t count = fields[0];
    if (count > 0x7F) return 1;

    ctx[1] += 7;

    for (std::uint32_t i = 0; i < count; ++i) {
        ctx[1] = ((ctx[1] + 7) & ~0x7u) + 32;
    }

    std::uint32_t byte_count = fields[128];
    if (byte_count > 0xFF) return 1;

    ctx[1] += 8;
    ctx[1] = ((ctx[1] + 7) & ~0x7u) + byte_count * 8;

    return 0;
}

int CalcBitSize_CreepNode(const std::uint32_t* node, void* context) {
    if (!node || !context) return BNetBitStream::kInvalidArgument;
    auto* ctx = static_cast<std::uint32_t*>(context);
    std::uint32_t count = node[0];
    if (count > 0x3FF) return 1;

    ctx[1] += 10;
    ctx[1] = ((ctx[1] + 7) & ~0x7u) + count * 8 + 128;

    std::uint32_t entry_count = node[261];
    if (entry_count > 0x3F) return 1;

    ctx[1] += 6;

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        ctx[1] += 64;
        ctx[1] += 32;
    }

    return 0;
}

int SerializeFieldArray(const std::uint32_t* fields, BNetBitStream& stream) {
    if (!fields) return BNetBitStream::kInvalidArgument;
    std::uint32_t count = fields[0];
    if (count > 0x7F) return 1;

    std::uint8_t count_byte = static_cast<std::uint8_t>(count);
    int result = stream.WriteBits(7, std::span<const std::uint8_t>{&count_byte, 1u});
    if (result != 0) return result;

    for (std::uint32_t i = 0; i < count; ++i) {
        result = stream.WriteAlignedBytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(&fields[1 + i]), 4u});
        if (result != 0) return result;
    }

    std::uint32_t byte_count = fields[128];
    if (byte_count > 0xFF) return 1;

    std::uint8_t byte_count_val = static_cast<std::uint8_t>(byte_count);
    result = stream.WriteBits(8, std::span<const std::uint8_t>{&byte_count_val, 1u});
    if (result != 0) return result;

    if (byte_count > 0) {
        result = stream.WriteAlignedBytes(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(&fields[129]), byte_count});
        if (result != 0) return result;
    }

    return 0;
}

int SerializeTriple32(const std::uint32_t* fields, BNetBitStream& stream) {
    if (!fields) return BNetBitStream::kInvalidArgument;
    int result = stream.WriteBits32(fields[0]);
    if (result != 0) return result;

    result = stream.WriteBits32(fields[1]);
    if (result != 0) return result;

    return stream.WriteBits32(fields[2]);
}

int SerializeCreepNode(const std::uint32_t* node, BNetBitStream& stream) {
    if (!node) return BNetBitStream::kInvalidArgument;
    std::uint32_t count = node[0];
    if (count > 0x3FF) return 1;

    {
        std::uint8_t buf[2] = {
            static_cast<std::uint8_t>(count >> 2),
            static_cast<std::uint8_t>(count & 0x3)
        };
        int result = stream.WriteBits(10, std::span<const std::uint8_t>{buf});
        if (result != 0) return result;
    }

    int result = stream.WriteAlignedBytes(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(&node[1]), count});
    if (result != 0) return result;

    result = stream.WriteBits32(node[257]);
    if (result != 0) return result;

    result = stream.WriteBits32(node[258]);
    if (result != 0) return result;

    result = stream.WriteBits32(node[259]);
    if (result != 0) return result;

    result = stream.WriteBits32(node[260]);
    if (result != 0) return result;

    std::uint32_t entry_count = node[261];
    if (entry_count > 0x3F) return 1;

    {
        std::uint8_t buf[1] = {static_cast<std::uint8_t>(entry_count)};
        result = stream.WriteBits(6, std::span<const std::uint8_t>{buf});
        if (result != 0) return result;
    }

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        result = SerializeTriple32(&node[262 + i * 3], stream);
        if (result != 0) return result;
    }

    return 0;
}

}

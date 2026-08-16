#include "storm_bitmap.h"
#include "storm_string.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace openwow::core {

namespace {

constexpr char kMemoryStormSource[] = ".\\MemoryStorm.cpp";
constexpr int kBitmapInitLine = 0x40;
constexpr int kBitmapFreeLine = 94;

size_t ComputeBitmapByteCount(uint32_t bitCapacity) {
    return (static_cast<size_t>(bitCapacity) + 7u) >> 3u;
}

}

StormBitmap::~StormBitmap() {
    Destroy();
}

StormBitmap::StormBitmap(StormBitmap&& other) noexcept
    : data_(other.data_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.capacity_ = 0;
}

StormBitmap& StormBitmap::operator=(StormBitmap&& other) noexcept {
    if (this != &other) {
        Destroy();
        data_ = other.data_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.capacity_ = 0;
    }
    return *this;
}

void StormBitmap::Init(uint32_t bitCapacity, uint8_t initialValue) {
    Destroy();

    const size_t byteSize = ComputeBitmapByteCount(bitCapacity);
    data_ = static_cast<uint8_t*>(SMemAlloc(
        byteSize, kMemoryStormSource, kBitmapInitLine, 0));

    if (data_ == nullptr && byteSize != 0) {

        std::abort();
    }

    if (data_ != nullptr && byteSize != 0) {
        std::memset(data_, initialValue, byteSize);
    }

    capacity_ = bitCapacity;
}

void StormBitmap::Destroy() {
    if (data_) {
        SMemFree(data_, kMemoryStormSource, kBitmapFreeLine, 0);
        data_ = nullptr;
    }
    capacity_ = 0;
}

bool StormBitmap::GetBit(uint32_t index) const {
    if (!data_ || index >= capacity_) return false;
    return (data_[index / 8] & (1u << (index % 8))) != 0;
}

void StormBitmap::SetBit(uint32_t index) {
    if (!data_ || index >= capacity_) return;
    data_[index / 8] |= static_cast<uint8_t>(1u << (index % 8));
}

void StormBitmap::ClearBit(uint32_t index) {
    if (!data_ || index >= capacity_) return;
    data_[index / 8] &= static_cast<uint8_t>(~(1u << (index % 8)));
}

StormBitmapResizeState::~StormBitmapResizeState() = default;

StormBitmapResizeState::StormBitmapResizeState(
    StormBitmapResizeState&& other) noexcept = default;

StormBitmapResizeState& StormBitmapResizeState::operator=(
    StormBitmapResizeState&& other) noexcept = default;

void StormBitmapResize(StormBitmapResizeState& state, int rawBitCount) {
    const int requestedBitCount = rawBitCount - 8;
    if (requestedBitCount == static_cast<int>(state.requested_bit_count)) {
        return;
    }

    if (requestedBitCount > 0) {
        const auto positiveBitCount =
            static_cast<std::uint32_t>(requestedBitCount);
        state.effective_bit_count = positiveBitCount;
        state.reserved_zeroed_state = 0;
        state.requested_bit_count = positiveBitCount;
    }

    state.bitmap_.reset();

    auto bitmap = std::unique_ptr<StormBitmap>(
        new (std::nothrow) StormBitmap());
    if (!bitmap) {
        return;
    }

    bitmap->Init(state.effective_bit_count * state.entry_count, 0);
    state.bitmap_ = std::move(bitmap);
}

}

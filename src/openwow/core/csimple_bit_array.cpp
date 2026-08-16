#include "csimple_bit_array.h"
#include "storm_string.h"

#include <cstring>
#include <utility>

namespace openwow::core {

CSimpleBitArray::~CSimpleBitArray() {
    FreeIfOwned();
}

CSimpleBitArray::CSimpleBitArray(CSimpleBitArray&& other) noexcept
    : data_(other.data_),
      capacity_(other.capacity_),
      owned_(other.owned_) {
    other.data_ = nullptr;
    other.capacity_ = 0;
    other.owned_ = false;
}

CSimpleBitArray& CSimpleBitArray::operator=(CSimpleBitArray&& other) noexcept {
    if (this != &other) {
        FreeIfOwned();
        data_ = other.data_;
        capacity_ = other.capacity_;
        owned_ = other.owned_;
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.owned_ = false;
    }
    return *this;
}

bool CSimpleBitArray::TestBit(std::uint32_t bitIndex) const noexcept {

    return (data_[bitIndex >> 3] & (1u << (bitIndex & 7u))) != 0;
}

void CSimpleBitArray::WriteBit(std::uint32_t bitIndex, bool value) noexcept {

    std::uint8_t* target = &data_[bitIndex >> 3];
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bitIndex & 7u));
    if (value) {
        *target |= mask;
    } else {
        *target &= static_cast<std::uint8_t>(~mask);
    }
}

void CSimpleBitArray::Assign(std::uint8_t* newData, std::uint32_t newCapacity,
                             bool newOwned) noexcept {
    FreeIfOwned();
    data_ = newData;
    capacity_ = newCapacity;
    owned_ = newOwned;
}

void CSimpleBitArray::FreeIfOwned() noexcept {
    if (data_ && owned_) {
        SMemFree(data_, "delete[]", -1, 0);
    }
    data_ = nullptr;
    capacity_ = 0;
    owned_ = false;
}

}

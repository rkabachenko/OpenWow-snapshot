
#pragma once

#include <cstdint>

namespace openwow::core {

class CSimpleBitArray {
public:
    CSimpleBitArray() noexcept = default;

    ~CSimpleBitArray();

    CSimpleBitArray(const CSimpleBitArray&) = delete;
    CSimpleBitArray& operator=(const CSimpleBitArray&) = delete;
    CSimpleBitArray(CSimpleBitArray&& other) noexcept;
    CSimpleBitArray& operator=(CSimpleBitArray&& other) noexcept;

    [[nodiscard]] bool TestBit(std::uint32_t bitIndex) const noexcept;

    void WriteBit(std::uint32_t bitIndex, bool value) noexcept;

    void Assign(std::uint8_t* newData, std::uint32_t newCapacity,
                bool newOwned) noexcept;

    [[nodiscard]] std::uint32_t GetCapacity() const noexcept { return capacity_; }
    [[nodiscard]] bool IsOwned() const noexcept { return owned_; }
    [[nodiscard]] const std::uint8_t* GetData() const noexcept { return data_; }
    [[nodiscard]] std::uint8_t* GetData() noexcept { return data_; }
    [[nodiscard]] bool IsInitialized() const noexcept { return data_ != nullptr; }

private:
    void FreeIfOwned() noexcept;

    std::uint8_t* data_ = nullptr;
    std::uint32_t capacity_ = 0;
    bool owned_ = false;
};

}

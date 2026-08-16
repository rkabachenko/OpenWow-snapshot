
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace openwow::core {

class StormBitmap {
public:
    StormBitmap() = default;
    ~StormBitmap();

    StormBitmap(const StormBitmap&) = delete;
    StormBitmap& operator=(const StormBitmap&) = delete;
    StormBitmap(StormBitmap&& other) noexcept;
    StormBitmap& operator=(StormBitmap&& other) noexcept;

    void Init(uint32_t bitCapacity, uint8_t initialValue = 0);

    void Destroy();

    [[nodiscard]] bool GetBit(uint32_t index) const;

    void SetBit(uint32_t index);

    void ClearBit(uint32_t index);

    [[nodiscard]] uint32_t GetCapacity() const { return capacity_; }

    [[nodiscard]] size_t GetByteSize() const { return (capacity_ + 7) / 8; }

    [[nodiscard]] bool IsInitialized() const { return data_ != nullptr; }

    [[nodiscard]] const uint8_t* GetData() const { return data_; }

private:
    uint8_t* data_ = nullptr;
    uint32_t capacity_ = 0;
};

class StormBitmapResizeState {
public:
    StormBitmapResizeState() = default;
    ~StormBitmapResizeState();

    StormBitmapResizeState(const StormBitmapResizeState&) = delete;
    StormBitmapResizeState& operator=(const StormBitmapResizeState&) = delete;
    StormBitmapResizeState(StormBitmapResizeState&& other) noexcept;
    StormBitmapResizeState& operator=(StormBitmapResizeState&& other) noexcept;

    [[nodiscard]] StormBitmap* bitmap() noexcept { return bitmap_.get(); }
    [[nodiscard]] const StormBitmap* bitmap() const noexcept {
        return bitmap_.get();
    }

    uint32_t effective_bit_count = 0;
    uint32_t reserved_zeroed_state = 0;
    uint32_t requested_bit_count = 0;
    uint32_t entry_count = 0;

private:
    std::unique_ptr<StormBitmap> bitmap_;

    friend void StormBitmapResize(StormBitmapResizeState& state,
                                  int rawBitCount);
};

void StormBitmapResize(StormBitmapResizeState& state, int rawBitCount);

}

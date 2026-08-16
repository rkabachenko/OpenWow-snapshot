
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace openwow::core {

[[noreturn]] inline void ThrowDequeTooLongLengthError() {
    throw std::length_error("deque<T> too long");
}

template <uint32_t kAutoGrowthQuantumLock>
[[nodiscard]] constexpr uint32_t ResolveTSGrowableArrayAutoGrowQuantum(
    uint32_t requested_count) noexcept {
    static_assert(kAutoGrowthQuantumLock != 0,
                  "auto-growth lock quantum must be non-zero");

    if (requested_count >= kAutoGrowthQuantumLock) {
        return kAutoGrowthQuantumLock;
    }

    uint32_t quantum = requested_count;
    for (uint32_t value = requested_count & (requested_count - 1); value != 0;
         value &= (value - 1)) {
        quantum = value;
    }

    return quantum == 0 ? 1 : quantum;
}

template <typename T, uint32_t kAutoGrowthQuantumLock = 64>
class TSGrowableArray {
 public:
    struct UniquePushResult {
        uint32_t slot_index = 0;
        uint32_t legacy_return = 0;
        bool inserted = false;
    };

    TSGrowableArray() = default;
    ~TSGrowableArray() { ReleaseStorage(); }

    TSGrowableArray(const TSGrowableArray& other) { CopyStorageFrom(other); }

    TSGrowableArray& operator=(const TSGrowableArray& other) {
        if (this != &other) {
            TSGrowableArray copy(other);
            Swap(copy);
        }
        return *this;
    }

    TSGrowableArray(TSGrowableArray&& other) noexcept { TakeStorage(std::move(other)); }

    TSGrowableArray& operator=(TSGrowableArray&& other) noexcept {
        if (this != &other) {
            ReleaseStorage();
            TakeStorage(std::move(other));
        }
        return *this;
    }

    void SetCapacity(uint32_t new_capacity) {
        if (new_capacity == capacity_) {
            return;
        }

        T* old_data = data_;
        const uint32_t old_capacity = capacity_;
        const uint32_t preserved_count = std::min(count_, new_capacity);

        capacity_ = new_capacity;

        T* rebuilt = AllocateStorage(new_capacity);
        CopyPrefix(old_data, rebuilt, preserved_count);
        DestroyRange(old_data, 0, count_);
        DeallocateStorage(old_data, old_capacity);

        data_ = rebuilt;

        if (count_ > new_capacity) {
            count_ = new_capacity;
        }
    }

    void SetCount(uint32_t new_count) {
        if (new_count > count_) {
            EnsureCapacity(new_count);
            ValueInitializeRange(count_, new_count);
        } else if (new_count < count_) {
            DestroyRange(data_, new_count, count_);
        }
        count_ = new_count;
    }

    void SetCountUninitialized(uint32_t new_count) {
        static_assert(kSupportsUninitializedCount,
                      "SetCountUninitialized requires trivial Storm array elements");

        if (new_count > count_ && new_count > capacity_) {
            uint32_t step = growth_step_;
            if (step == 0) {
                step = ResolveAutoGrowQuantum(new_count);
            }

            uint32_t aligned_capacity = new_count;
            if (aligned_capacity % step != 0) {
                aligned_capacity += step - (aligned_capacity % step);
            }
            SetCapacity(aligned_capacity);
        }

        count_ = new_count;
    }

    void SetCountZeroInit(uint32_t new_count) {
        if (new_count > count_) {
            EnsureCapacity(new_count);
            ValueInitializeRange(count_, new_count);
        } else if (new_count < count_) {
            DestroyRange(data_, new_count, count_);
        }
        count_ = new_count;
    }

    void SetCountExact(uint32_t new_count) {
        static_assert(kSupportsUninitializedCount,
                      "SetCountExact requires trivially copyable elements");

        if (new_count == count_) {
            return;
        }

        if (new_count == 0) {
            ReleaseStorage();
        } else {
            SetCapacity(new_count);
            count_ = new_count;
        }
    }

    void Clear() {
        DestroyRange(data_, 0, count_);
        count_ = 0;
    }

    [[nodiscard]] uint32_t GetCount() const { return count_; }
    [[nodiscard]] uint32_t GetCapacity() const { return capacity_; }
    [[nodiscard]] T* GetData() { return capacity_ == 0 ? nullptr : data_; }
    [[nodiscard]] const T* GetData() const {
        return capacity_ == 0 ? nullptr : data_;
    }
    [[nodiscard]] T& operator[](uint32_t idx) { return data_[idx]; }
    [[nodiscard]] const T& operator[](uint32_t idx) const { return data_[idx]; }
    [[nodiscard]] uint32_t GetGrowthStep() const { return growth_step_; }

    [[nodiscard]] bool GetAt(int32_t index, T* out) const {
        if (index < 0 || static_cast<uint32_t>(index) >= count_) {
            return false;
        }
        if constexpr (kSupportsUninitializedCount) {
            std::memcpy(out, data_ + static_cast<uint32_t>(index), sizeof(T));
        } else {
            *out = data_[static_cast<uint32_t>(index)];
        }
        return true;
    }

    void SetGrowthStep(uint32_t step) { growth_step_ = step; }

    T* PushBack(const T& value) {
        const uint32_t new_count = count_ + 1;
        EnsureCapacity(new_count);

        T* slot = data_ + count_;
        if constexpr (kSupportsUninitializedCount) {
            std::memcpy(slot, &value, sizeof(T));
        } else {
            std::construct_at(slot, value);
        }

        count_ = new_count;
        return slot;
    }

    T* New() {
        static_assert(kSupportsUninitializedCount,
                      "New() requires trivially copyable elements");

        const uint32_t new_count = count_ + 1;
        EnsureCapacity(new_count);

        T* slot = data_ + count_;
        count_ = new_count;
        return slot;
    }

    [[nodiscard]] uint32_t AppendCopiedRange(
        const uint32_t append_count, const uint32_t source_stride_bytes,
        const void* const source) {
        static_assert(kSupportsUninitializedCount,
                      "AppendCopiedRange requires trivial Storm array elements");

        const uint32_t start_index = count_;
        if (append_count == 0) {
            return start_index;
        }

        EnsureCapacity(start_index + append_count);

        auto* dest = reinterpret_cast<std::byte*>(data_ + start_index);
        auto* cursor = static_cast<const std::byte*>(source);
        for (uint32_t index = 0; index < append_count; ++index) {
            std::memcpy(dest, cursor, sizeof(T));
            dest += sizeof(T);
            cursor += source_stride_bytes;
        }

        count_ = start_index + append_count;
        return start_index;
    }

    [[nodiscard]] uint32_t AppendCopiedRange(
        const uint32_t append_count, const T* source) {
        return AppendCopiedRange(append_count, sizeof(T), source);
    }

    [[nodiscard]] UniquePushResult PushBackUnique(const T& value) {
        for (uint32_t index = 0; index < count_; ++index) {
            if (data_[index] == value) {
                return UniquePushResult{
                    .slot_index = index,
                    .legacy_return = index,
                    .inserted = false,
                };
            }
        }

        const uint32_t slot_index = count_;
        const uint32_t new_count = slot_index + 1;
        EnsureCapacity(new_count);

        T* const slot = data_ + slot_index;
        if constexpr (kSupportsUninitializedCount) {
            std::memcpy(slot, &value, sizeof(T));
        } else {
            std::construct_at(slot, value);
        }

        count_ = new_count;
        return UniquePushResult{
            .slot_index = slot_index,
            .legacy_return = new_count,
            .inserted = true,
        };
    }

    void CopyFrom(uint32_t new_count, const T* source) {
        if (new_count != count_) {
            SetCapacity(new_count);
        }

        if constexpr (kSupportsUninitializedCount) {
            if (new_count != 0) {
                std::memcpy(data_, source, sizeof(T) * new_count);
            }
        } else {
            const uint32_t assigned_count = std::min(count_, new_count);
            for (uint32_t i = 0; i < assigned_count; ++i) {
                data_[i] = source[i];
            }
            for (uint32_t i = assigned_count; i < new_count; ++i) {
                std::construct_at(data_ + i, source[i]);
            }
        }
        count_ = new_count;
    }

    void CopyFrom(const std::vector<T>& source) {
        CopyFrom(static_cast<uint32_t>(source.size()), source.data());
    }

 private:
    static constexpr bool kSupportsUninitializedCount =
        std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

    void EnsureCapacity(uint32_t requested_count) {
        if (requested_count <= capacity_) {
            return;
        }

        SetCapacity(AlignGrowth(requested_count));
    }

    uint32_t ResolveAutoGrowQuantum(uint32_t new_count) {
        if (new_count >= kAutoGrowthQuantumLock) {
            growth_step_ = kAutoGrowthQuantumLock;
        }

        return ResolveTSGrowableArrayAutoGrowQuantum<kAutoGrowthQuantumLock>(
            new_count);
    }

    uint32_t AlignGrowth(uint32_t new_count) {
        uint32_t step = growth_step_;
        if (step == 0) {
            step = ResolveAutoGrowQuantum(new_count);
        }
        uint32_t aligned = new_count;
        if (aligned % step != 0) aligned += step - (aligned % step);
        return aligned;
    }

    uint32_t capacity_ = 0;
    uint32_t count_ = 0;
    T* data_ = nullptr;
    uint32_t growth_step_ = 0;

    static T* AllocateStorage(uint32_t new_capacity) {
        if (new_capacity == 0) {
            return nullptr;
        }

        std::allocator<T> allocator;
        return allocator.allocate(new_capacity);
    }

    static void DeallocateStorage(T* data, uint32_t capacity) {
        if (data == nullptr) {
            return;
        }

        std::allocator<T> allocator;
        allocator.deallocate(data, capacity);
    }

    static void DestroyRange(T* data, uint32_t begin, uint32_t end) {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (uint32_t i = begin; i < end; ++i) {
                std::destroy_at(data + i);
            }
        }
    }

    void ValueInitializeRange(uint32_t begin, uint32_t end) {
        for (uint32_t i = begin; i < end; ++i) {
            std::construct_at(data_ + i, T{});
        }
    }

    static void CopyPrefix(const T* source, T* destination, uint32_t preserved_count) {
        if (preserved_count == 0) {
            return;
        }

        if constexpr (kSupportsUninitializedCount) {
            std::memcpy(destination, source, sizeof(T) * preserved_count);
        } else {
            for (uint32_t i = 0; i < preserved_count; ++i) {
                std::construct_at(destination + i, source[i]);
            }
        }
    }

    void ReleaseStorage() {
        DestroyRange(data_, 0, count_);
        DeallocateStorage(data_, capacity_);
        data_ = nullptr;
        capacity_ = 0;
        count_ = 0;
        growth_step_ = 0;
    }

    void CopyStorageFrom(const TSGrowableArray& other) {
        growth_step_ = other.growth_step_;
        if (other.capacity_ == 0) {
            return;
        }

        data_ = AllocateStorage(other.capacity_);
        capacity_ = other.capacity_;
        CopyPrefix(other.data_, data_, other.count_);
        count_ = other.count_;
    }

    void TakeStorage(TSGrowableArray&& other) noexcept {
        data_ = other.data_;
        capacity_ = other.capacity_;
        count_ = other.count_;
        growth_step_ = other.growth_step_;
        other.data_ = nullptr;
        other.capacity_ = 0;
        other.count_ = 0;
        other.growth_step_ = 0;
    }

    void Swap(TSGrowableArray& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(capacity_, other.capacity_);
        std::swap(count_, other.count_);
        std::swap(growth_step_, other.growth_step_);
    }
};

template <typename T>
class TSExplicitList {
 public:
    void Clear() { entries_.clear(); }
    void Add(const T& entry) { entries_.push_back(entry); }
    [[nodiscard]] uint32_t GetCount() const {
        return static_cast<uint32_t>(entries_.size());
    }
    [[nodiscard]] T* GetEntry(uint32_t idx) {
        if (idx >= entries_.size()) return nullptr;
        return &entries_[idx];
    }

    void SetMaxCount(uint32_t newMaxCount) {
        if (newMaxCount < static_cast<uint32_t>(entries_.size())) {
            entries_.resize(newMaxCount);
        }
        entries_.reserve(newMaxCount);
    }

    [[nodiscard]] uint32_t GetMaxCount() const {
        return static_cast<uint32_t>(entries_.capacity());
    }

 private:
    std::vector<T> entries_;
};

template <typename T, std::uint32_t N>
class InlineExternalArray {
 public:
    static constexpr std::uint32_t kExternalSentinel = 0xFFFFFFFF;

    InlineExternalArray() { inline_data_.fill(T{}); }

    ~InlineExternalArray() = default;

    [[nodiscard]] T* GetPtr(std::uint32_t index) {
        if (index >= count_) return nullptr;
        if (external_)
            return &heap_data_[index];
        return &inline_data_[index];
    }

    [[nodiscard]] const T* GetPtr(std::uint32_t index) const {
        if (index >= count_) return nullptr;
        if (external_)
            return &heap_data_[index];
        return &inline_data_[index];
    }

    [[nodiscard]] std::uint32_t GetCount() const { return count_; }
    [[nodiscard]] bool IsExternal() const { return external_; }

    void Push(const T& value) {
        if (!external_ && count_ < N) {
            inline_data_[count_] = value;
            ++count_;
            return;
        }
        if (!external_) {

            heap_data_.assign(inline_data_.begin(),
                              inline_data_.begin() + count_);
            external_ = true;
        }
        heap_data_.push_back(value);
        ++count_;
    }

    bool Set(std::uint32_t index, const T& value) {
        if (index >= count_) return false;
        if (external_)
            heap_data_[index] = value;
        else
            inline_data_[index] = value;
        return true;
    }

    void Clear() {
        heap_data_.clear();
        inline_data_.fill(T{});
        count_ = 0;
        external_ = false;
    }

 private:
    std::array<T, N> inline_data_{};
    std::vector<T> heap_data_;
    std::uint32_t count_{0};
    bool external_{false};
};

}

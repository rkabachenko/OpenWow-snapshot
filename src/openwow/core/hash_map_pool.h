#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace openwow::core {

template <typename Key, typename Value>
class HashMapPool {
public:
    explicit HashMapPool(size_t capacity = 256)
        : buckets_(capacity), capacity_(capacity) {}

    ~HashMapPool() = default;
    HashMapPool(const HashMapPool&) = default;
    HashMapPool& operator=(const HashMapPool&) = default;
    HashMapPool(HashMapPool&&) noexcept = default;
    HashMapPool& operator=(HashMapPool&&) noexcept = default;

    bool Insert(const Key& key, Value value) {
        if (size_ >= capacity_) return false;

        size_t idx = Hash(key);
        for (size_t i = 0; i < capacity_; ++i) {
            size_t probe = (idx + i) % capacity_;
            auto& b = buckets_[probe];
            if (b.state == BucketState::Empty || b.state == BucketState::Deleted) {
                b.key = key;
                b.value = std::move(value);
                b.state = BucketState::Occupied;
                ++size_;
                return true;
            }
            if (b.state == BucketState::Occupied && b.key == key) {
                return false;
            }
        }
        return false;
    }

    bool Remove(const Key& key) {
        size_t idx = Hash(key);
        for (size_t i = 0; i < capacity_; ++i) {
            size_t probe = (idx + i) % capacity_;
            auto& b = buckets_[probe];
            if (b.state == BucketState::Empty) return false;
            if (b.state == BucketState::Occupied && b.key == key) {
                b.state = BucketState::Deleted;
                --size_;
                return true;
            }
        }
        return false;
    }

    Value* Find(const Key& key) {
        size_t idx = Hash(key);
        for (size_t i = 0; i < capacity_; ++i) {
            size_t probe = (idx + i) % capacity_;
            auto& b = buckets_[probe];
            if (b.state == BucketState::Empty) return nullptr;
            if (b.state == BucketState::Occupied && b.key == key) {
                return &b.value;
            }
        }
        return nullptr;
    }

    const Value* FindConst(const Key& key) const {
        size_t idx = Hash(key);
        for (size_t i = 0; i < capacity_; ++i) {
            size_t probe = (idx + i) % capacity_;
            auto& b = buckets_[probe];
            if (b.state == BucketState::Empty) return nullptr;
            if (b.state == BucketState::Occupied && b.key == key) {
                return &b.value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool Contains(const Key& key) const {
        return FindConst(key) != nullptr;
    }

    [[nodiscard]] size_t GetSize() const { return size_; }

    [[nodiscard]] size_t GetCapacity() const { return capacity_; }

    [[nodiscard]] float GetLoadFactor() const {
        return capacity_ > 0 ? static_cast<float>(size_) / static_cast<float>(capacity_) : 0.0f;
    }

    void Clear() {
        for (auto& b : buckets_) {
            b.state = BucketState::Empty;
        }
        size_ = 0;
    }

    void ForEach(std::function<void(const Key&, Value&)> fn) {
        for (auto& b : buckets_) {
            if (b.state == BucketState::Occupied) {
                fn(b.key, b.value);
            }
        }
    }

    [[nodiscard]] std::vector<Key> GetKeys() const {
        std::vector<Key> keys;
        keys.reserve(size_);
        for (auto& b : buckets_) {
            if (b.state == BucketState::Occupied) {
                keys.push_back(b.key);
            }
        }
        return keys;
    }

    [[nodiscard]] bool IsEmpty() const { return size_ == 0; }

private:
    enum class BucketState : uint8_t { Empty, Occupied, Deleted };

    struct Bucket {
        Key key{};
        Value value{};
        BucketState state{BucketState::Empty};
    };

    [[nodiscard]] size_t Hash(const Key& key) const {
        return std::hash<Key>{}(key) % capacity_;
    }

    std::vector<Bucket> buckets_;
    size_t capacity_{0};
    size_t size_{0};
};

}

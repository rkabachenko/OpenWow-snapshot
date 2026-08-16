#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::core {

using InternedStringId = uint32_t;

class StringInterner {
public:
    StringInterner() = default;
    ~StringInterner() = default;

    StringInterner(const StringInterner&) = delete;
    StringInterner& operator=(const StringInterner&) = delete;

    InternedStringId Intern(const std::string& str);

    [[nodiscard]] const std::string& Get(InternedStringId id) const;

    [[nodiscard]] bool Contains(const std::string& str) const;

    [[nodiscard]] std::optional<InternedStringId> GetId(const std::string& str) const;

    [[nodiscard]] size_t GetRefCount(InternedStringId id) const;

    bool RemoveById(InternedStringId id);

    bool RemoveByString(const std::string& str);

    std::vector<InternedStringId> InternAll(const std::vector<std::string>& strings);

    [[nodiscard]] std::vector<InternedStringId> GetAllIds() const;

    [[nodiscard]] size_t GetUniqueCount() const;

    [[nodiscard]] size_t GetTotalReferences() const;

    [[nodiscard]] size_t GetMemoryUsage() const;

    [[nodiscard]] size_t GetSavings() const;

    [[nodiscard]] float GetDeduplicationRatio() const;

    [[nodiscard]] size_t GetAverageStringLength() const;

    [[nodiscard]] size_t GetLongestStringLength() const;

    void Clear();

private:
    struct Entry {
        std::string value;
        size_t refCount{0};
    };

    InternedStringId nextId_{1};
    std::unordered_map<std::string, InternedStringId> lookup_;
    std::unordered_map<InternedStringId, Entry> entries_;
    size_t totalRefs_{0};
};

}

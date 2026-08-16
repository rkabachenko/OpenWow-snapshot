
#include "openwow/core/string_interner.h"

#include <algorithm>

namespace openwow::core {

InternedStringId StringInterner::Intern(const std::string& str) {
    ++totalRefs_;
    auto it = lookup_.find(str);
    if (it != lookup_.end()) {
        entries_[it->second].refCount++;
        return it->second;
    }
    InternedStringId id = nextId_++;
    lookup_[str] = id;
    entries_[id] = {str, 1};
    return id;
}

const std::string& StringInterner::Get(InternedStringId id) const {
    return entries_.at(id).value;
}

bool StringInterner::Contains(const std::string& str) const {
    return lookup_.count(str) > 0;
}

std::optional<InternedStringId> StringInterner::GetId(const std::string& str) const {
    auto it = lookup_.find(str);
    if (it == lookup_.end()) return std::nullopt;
    return it->second;
}

size_t StringInterner::GetRefCount(InternedStringId id) const {
    auto it = entries_.find(id);
    return it != entries_.end() ? it->second.refCount : 0;
}

bool StringInterner::RemoveById(InternedStringId id) {
    auto it = entries_.find(id);
    if (it == entries_.end()) return false;
    lookup_.erase(it->second.value);
    entries_.erase(it);
    return true;
}

bool StringInterner::RemoveByString(const std::string& str) {
    auto it = lookup_.find(str);
    if (it == lookup_.end()) return false;
    InternedStringId id = it->second;
    entries_.erase(id);
    lookup_.erase(it);
    return true;
}

std::vector<InternedStringId> StringInterner::InternAll(
    const std::vector<std::string>& strings) {
    std::vector<InternedStringId> ids;
    ids.reserve(strings.size());
    for (const auto& s : strings) {
        ids.push_back(Intern(s));
    }
    return ids;
}

std::vector<InternedStringId> StringInterner::GetAllIds() const {
    std::vector<InternedStringId> ids;
    ids.reserve(entries_.size());
    for (const auto& [id, entry] : entries_) {
        ids.push_back(id);
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}

size_t StringInterner::GetUniqueCount() const {
    return entries_.size();
}

size_t StringInterner::GetTotalReferences() const {
    return totalRefs_;
}

float StringInterner::GetDeduplicationRatio() const {
    if (totalRefs_ == 0) return 0.0f;
    return 1.0f - static_cast<float>(entries_.size()) / static_cast<float>(totalRefs_);
}

size_t StringInterner::GetMemoryUsage() const {
    size_t usage = 0;
    for (auto& [id, entry] : entries_) {

        usage += entry.value.capacity() + sizeof(Entry) + sizeof(InternedStringId);
    }

    for (auto& [str, id] : lookup_) {
        usage += str.capacity() + sizeof(InternedStringId);
    }
    return usage;
}

size_t StringInterner::GetSavings() const {

    size_t naiveBytes = 0;
    for (auto& [id, entry] : entries_) {
        naiveBytes += entry.value.size() * entry.refCount;
    }
    size_t actualBytes = 0;
    for (auto& [id, entry] : entries_) {
        actualBytes += entry.value.size();
    }
    return naiveBytes > actualBytes ? naiveBytes - actualBytes : 0;
}

size_t StringInterner::GetAverageStringLength() const {
    if (entries_.empty()) return 0;
    size_t total = 0;
    for (const auto& [id, entry] : entries_) {
        total += entry.value.size();
    }
    return total / entries_.size();
}

size_t StringInterner::GetLongestStringLength() const {
    size_t longest = 0;
    for (const auto& [id, entry] : entries_) {
        longest = std::max(longest, entry.value.size());
    }
    return longest;
}

void StringInterner::Clear() {
    lookup_.clear();
    entries_.clear();
    totalRefs_ = 0;
    nextId_ = 1;
}

}

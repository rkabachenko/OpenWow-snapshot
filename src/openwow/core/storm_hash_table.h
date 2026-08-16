#pragma once

#include <cstdint>
#include <vector>

#include "openwow/core/cmap_hashtable.h"
#include "openwow/core/storm_string.h"

namespace openwow::core {

template <typename T>
class TSHashTable {
 public:
  TSHashTable() = default;
  virtual ~TSHashTable() { Clear(); }

  virtual void Clear() {
    entries_.clear();
    map_.Reset();
  }

  void Init() {
    entries_.clear();
    map_.Reset();
    map_.InitWithBuckets();
  }

  T* Insert(std::uint32_t key) {
    map_.InsertHashedKey(key);
    entries_.emplace_back();
    return &entries_.back();
  }

  T* Find(std::uint32_t key) {
    if (!map_.initialized()) {
      return nullptr;
    }
    for (auto& entry : entries_) {
      if (entry.GetKey() == key) {
        return &entry;
      }
    }
    return nullptr;
  }

  bool Delete(std::uint32_t key) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->GetKey() == key) {
        entries_.erase(it);
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] const T* Find(std::uint32_t key) const {
    if (!map_.initialized()) {
      return nullptr;
    }
    for (const auto& entry : entries_) {
      if (entry.GetKey() == key) {
        return &entry;
      }
    }
    return nullptr;
  }

  T* FindByStringKey(const char* name) {
    if (!map_.initialized() || !name) {
      return nullptr;
    }
    for (auto& entry : entries_) {
      const char* entry_name = entry.GetName();
      if (entry_name == name ||
          (entry_name &&
           SStrCmpUTF8NoCase(entry_name, name, 0x7FFFFFFF) == 0)) {
        return &entry;
      }
    }
    return nullptr;
  }

  [[nodiscard]] const T* FindByStringKey(const char* name) const {
    if (!map_.initialized() || !name) {
      return nullptr;
    }
    for (const auto& entry : entries_) {
      const char* entry_name = entry.GetName();
      if (entry_name == name ||
          (entry_name &&
           SStrCmpUTF8NoCase(entry_name, name, 0x7FFFFFFF) == 0)) {
        return &entry;
      }
    }
    return nullptr;
  }

  [[nodiscard]] std::uint32_t GetEntryCount() const {
    return static_cast<std::uint32_t>(entries_.size());
  }

  [[nodiscard]] bool IsInitialized() const { return map_.initialized(); }

  [[nodiscard]] std::uint32_t GetHashMask() const { return map_.bucket_mask(); }

 protected:
  CMapTable map_;
  std::vector<T> entries_;
};

}

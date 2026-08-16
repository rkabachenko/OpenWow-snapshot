#pragma once
#include "openwow/audio/playback/sound_playback_types.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::audio {

class SoundHandleTable {
 public:
  struct Entry {

    std::uint32_t first{kFreeSlotHandleId};
    SoundHandle second{};
  };
  static constexpr std::uint32_t kFreeSlotHandleId = 0u;

  template <bool kConst>
  class Iterator {
   public:
    using TableType = std::conditional_t<kConst, const SoundHandleTable, SoundHandleTable>;
    using EntryType = std::conditional_t<kConst, const Entry, Entry>;
    using iterator_category = std::forward_iterator_tag;
    using value_type = Entry;
    using difference_type = std::ptrdiff_t;
    using pointer = EntryType *;
    using reference = EntryType &;

    Iterator() = default;
    Iterator(TableType *table, std::size_t slot) : table_(table), slot_(slot) { SkipFree(); }

    template <bool kOtherConst, typename = std::enable_if_t<kConst && !kOtherConst>>
    Iterator(const Iterator<kOtherConst> &other) : table_(other.table_), slot_(other.slot_) {}

    reference operator*() const { return table_->slots_[slot_]; }
    pointer operator->() const { return &table_->slots_[slot_]; }
    Iterator &operator++() {
      ++slot_;
      SkipFree();
      return *this;
    }
    Iterator operator++(int) {
      Iterator copy = *this;
      ++*this;
      return copy;
    }
    friend bool operator==(const Iterator &lhs, const Iterator &rhs) {
      return lhs.slot_ == rhs.slot_;
    }
    friend bool operator!=(const Iterator &lhs, const Iterator &rhs) {
      return lhs.slot_ != rhs.slot_;
    }
    [[nodiscard]] std::size_t slot() const { return slot_; }

   private:
    friend class SoundHandleTable;
    template <bool>
    friend class Iterator;
    void SkipFree() {
      const std::size_t slot_count = table_ != nullptr ? table_->slots_.size() : 0u;
      while (slot_ < slot_count && table_->slots_[slot_].first == kFreeSlotHandleId) {
        ++slot_;
      }
    }
    TableType *table_{nullptr};
    std::size_t slot_{0};
  };
  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, slots_.size()); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, slots_.size()); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  [[nodiscard]] std::size_t size() const { return slot_by_handle_id_.size(); }
  [[nodiscard]] bool empty() const { return slot_by_handle_id_.empty(); }

  iterator find(const std::uint32_t handle_id) {
    const auto it = slot_by_handle_id_.find(handle_id);
    return it == slot_by_handle_id_.end() ? end() : iterator(this, it->second);
  }
  const_iterator find(const std::uint32_t handle_id) const {
    const auto it = slot_by_handle_id_.find(handle_id);
    return it == slot_by_handle_id_.end() ? end() : const_iterator(this, it->second);
  }
  [[nodiscard]] bool contains(const std::uint32_t handle_id) const {
    return slot_by_handle_id_.find(handle_id) != slot_by_handle_id_.end();
  }

  std::pair<iterator, bool> try_emplace(const std::uint32_t handle_id) {
    const auto existing = slot_by_handle_id_.find(handle_id);
    if (existing != slot_by_handle_id_.end()) {
      return {iterator(this, existing->second), false};
    }
    std::size_t slot = 0;
    if (!free_slots_.empty()) {
      slot = free_slots_.back();
      free_slots_.pop_back();
      slots_[slot].first = handle_id;
      slots_[slot].second = SoundHandle{};
    } else {
      slot = slots_.size();
      slots_.emplace_back();
      slots_[slot].first = handle_id;
    }
    slot_by_handle_id_.emplace(handle_id, slot);
    return {iterator(this, slot), true};
  }
  SoundHandle &operator[](const std::uint32_t handle_id) {
    return try_emplace(handle_id).first->second;
  }

  iterator erase(iterator position) {
    const std::size_t slot = position.slot_;
    Entry &entry = slots_[slot];
    slot_by_handle_id_.erase(entry.first);
    entry.first = kFreeSlotHandleId;

    entry.second = SoundHandle{};
    free_slots_.push_back(slot);
    return iterator(this, slot + 1);
  }
  std::size_t erase(const std::uint32_t handle_id) {
    const auto it = find(handle_id);
    if (it == end()) {
      return 0;
    }
    erase(it);
    return 1;
  }

  void clear() {
    slots_.clear();
    free_slots_.clear();
    slot_by_handle_id_.clear();
  }

 private:
  std::deque<Entry> slots_;
  std::vector<std::size_t> free_slots_;
  std::unordered_map<std::uint32_t, std::size_t> slot_by_handle_id_;
};

}

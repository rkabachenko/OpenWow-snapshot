#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace openwow::foundation {

[[nodiscard]] constexpr std::uint64_t MixHash64(std::uint64_t value) noexcept {
  value ^= value >> 33u;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33u;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33u;
  return value;
}

template <class Key>
struct FlatHash {
  [[nodiscard]] std::size_t operator()(const Key& key) const {
    return static_cast<std::size_t>(
        MixHash64(static_cast<std::uint64_t>(std::hash<Key>{}(key))));
  }
};

namespace detail {

template <class Key, class T>
struct MapEntry {
  const Key first;
  T second;
};

using ControlByte = std::uint8_t;
inline constexpr ControlByte kControlEmpty = 0x80u;
inline constexpr ControlByte kControlErased = 0x81u;
inline constexpr ControlByte kControlTagMask = 0x7fu;
inline constexpr ControlByte kControlOccupiedLimit = kControlEmpty;

[[nodiscard]] constexpr bool FlatWouldExceedMaxLoad(std::size_t used,
                                                    std::size_t capacity) noexcept {
  return used * 4u > capacity * 3u;
}

inline constexpr std::size_t kFlatMinCapacity = 8u;

}

template <class Key, class T, class Hash = FlatHash<Key>,
          class KeyEqual = std::equal_to<Key>>
class FlatHashMap {
  static_assert(std::is_move_constructible_v<T>,
                "FlatHashMap relocates elements when the table grows");
  static_assert(std::is_default_constructible_v<Hash> &&
                    std::is_default_constructible_v<KeyEqual>,
                "FlatHashMap constructs its functors on demand and so cannot "
                "carry per-instance hasher state");

 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = detail::MapEntry<Key, T>;
  using size_type = std::size_t;
  using hasher = Hash;
  using key_equal = KeyEqual;

 private:
  struct alignas(value_type) SlotStorage {
    std::byte bytes[sizeof(value_type)];
  };

  static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

  template <bool kIsConst>
  class BasicIterator {
    friend class FlatHashMap;
    friend class BasicIterator<!kIsConst>;

    using MapPtr = std::conditional_t<kIsConst, const FlatHashMap*, FlatHashMap*>;

   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = FlatHashMap::value_type;
    using reference = std::conditional_t<kIsConst, const value_type&, value_type&>;
    using pointer = std::conditional_t<kIsConst, const value_type*, value_type*>;

    BasicIterator() = default;

    template <bool kOtherConst,
              class = std::enable_if_t<kIsConst && !kOtherConst>>
    BasicIterator(const BasicIterator<kOtherConst>& other) noexcept
        : map_(other.map_), slot_(other.slot_) {}

    [[nodiscard]] reference operator*() const noexcept {
      return map_->EntryAt(slot_);
    }
    [[nodiscard]] pointer operator->() const noexcept {
      return &map_->EntryAt(slot_);
    }

    BasicIterator& operator++() noexcept {
      ++slot_;
      while (slot_ < map_->capacity_ &&
             map_->control_[slot_] >= detail::kControlOccupiedLimit) {
        ++slot_;
      }
      return *this;
    }

    BasicIterator operator++(int) noexcept {
      BasicIterator copy = *this;
      ++*this;
      return copy;
    }

    [[nodiscard]] bool operator==(const BasicIterator& other) const noexcept {
      return map_ == other.map_ && slot_ == other.slot_;
    }
    [[nodiscard]] bool operator!=(const BasicIterator& other) const noexcept {
      return !(*this == other);
    }

   private:
    BasicIterator(MapPtr map, std::size_t slot) noexcept
        : map_(map), slot_(slot) {}

    MapPtr map_ = nullptr;
    std::size_t slot_ = 0;
  };

 public:
  using iterator = BasicIterator<false>;
  using const_iterator = BasicIterator<true>;

  FlatHashMap() = default;

  explicit FlatHashMap(size_type expected_elements) { Reserve(expected_elements); }

  FlatHashMap(const FlatHashMap& other) { CopyFrom(other); }

  FlatHashMap(FlatHashMap&& other) noexcept { AdoptFrom(other); }

  FlatHashMap& operator=(const FlatHashMap& other) {
    if (this != &other) {
      Release();
      CopyFrom(other);
    }
    return *this;
  }

  FlatHashMap& operator=(FlatHashMap&& other) noexcept {
    if (this != &other) {
      Release();
      AdoptFrom(other);
    }
    return *this;
  }

  ~FlatHashMap() { DestroyAll(); }

  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_type capacity() const noexcept { return capacity_; }

  [[nodiscard]] iterator begin() noexcept { return iterator(this, FirstOccupied()); }
  [[nodiscard]] iterator end() noexcept { return iterator(this, capacity_); }
  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator(this, FirstOccupied());
  }
  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator(this, capacity_);
  }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

  [[nodiscard]] iterator find(const Key& key) noexcept {
    const std::size_t slot = FindSlot(key);
    return slot == kNoSlot ? end() : iterator(this, slot);
  }

  [[nodiscard]] const_iterator find(const Key& key) const noexcept {
    const std::size_t slot = FindSlot(key);
    return slot == kNoSlot ? end() : const_iterator(this, slot);
  }

  [[nodiscard]] bool contains(const Key& key) const noexcept {
    return FindSlot(key) != kNoSlot;
  }

  [[nodiscard]] size_type count(const Key& key) const noexcept {
    return contains(key) ? 1u : 0u;
  }

  [[nodiscard]] T* FindValue(const Key& key) noexcept {
    const std::size_t slot = FindSlot(key);
    return slot == kNoSlot ? nullptr : &EntryAt(slot).second;
  }

  [[nodiscard]] const T* FindValue(const Key& key) const noexcept {
    const std::size_t slot = FindSlot(key);
    return slot == kNoSlot ? nullptr : &EntryAt(slot).second;
  }

  T& operator[](const Key& key) { return try_emplace(key).first->second; }

  template <class... Args>
  std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
    const auto [slot, inserted] = PrepareInsert(key);
    if (inserted) {
      ConstructAt(slot, key, std::forward<Args>(args)...);
    }
    return {iterator(this, slot), inserted};
  }

  template <class V>
  std::pair<iterator, bool> emplace(const Key& key, V&& value) {
    return try_emplace(key, std::forward<V>(value));
  }

  template <class V>
  std::pair<iterator, bool> insert_or_assign(const Key& key, V&& value) {
    const auto [slot, inserted] = PrepareInsert(key);
    if (inserted) {
      ConstructAt(slot, key, std::forward<V>(value));
    } else {
      EntryAt(slot).second = std::forward<V>(value);
    }
    return {iterator(this, slot), inserted};
  }

  size_type erase(const Key& key) {
    const std::size_t slot = FindSlot(key);
    if (slot == kNoSlot) {
      return 0u;
    }
    EraseSlot(slot);
    return 1u;
  }

  void erase(const_iterator position) {
    if (position.map_ == this && position.slot_ < capacity_ &&
        control_[position.slot_] < detail::kControlOccupiedLimit) {
      EraseSlot(position.slot_);
    }
  }

  void Clear() noexcept {
    DestroyAll();
    for (std::size_t i = 0; i < capacity_; ++i) {
      control_[i] = detail::kControlEmpty;
    }
    size_ = 0;
    erased_ = 0;
  }

  void clear() noexcept { Clear(); }

  void Reserve(size_type expected_elements) {
    if (expected_elements == 0) {
      return;
    }

    std::size_t target = detail::kFlatMinCapacity;
    while (detail::FlatWouldExceedMaxLoad(expected_elements, target)) {
      target *= 2u;
    }
    if (target > capacity_) {
      Resize(target);
    }
  }

  void reserve(size_type expected_elements) { Reserve(expected_elements); }

 private:
  [[nodiscard]] value_type& EntryAt(std::size_t slot) noexcept {
    return *std::launder(reinterpret_cast<value_type*>(&slots_[slot]));
  }

  [[nodiscard]] const value_type& EntryAt(std::size_t slot) const noexcept {
    return *std::launder(reinterpret_cast<const value_type*>(&slots_[slot]));
  }

  struct HashParts {
    std::size_t index_bits;
    detail::ControlByte tag;
  };

  [[nodiscard]] static HashParts SplitHash(std::size_t hash) noexcept {

    return HashParts{
        hash >> 7u,
        static_cast<detail::ControlByte>(hash & detail::kControlTagMask)};
  }

  [[nodiscard]] std::size_t FindSlot(const Key& key) const noexcept {
    if (size_ == 0) {
      return kNoSlot;
    }
    const HashParts parts = SplitHash(Hash{}(key));
    const std::size_t mask = capacity_ - 1u;
    std::size_t slot = parts.index_bits & mask;
    for (;;) {
      const detail::ControlByte control = control_[slot];
      if (control == parts.tag && KeyEqual{}(EntryAt(slot).first, key)) {
        return slot;
      }
      if (control == detail::kControlEmpty) {
        return kNoSlot;
      }
      slot = (slot + 1u) & mask;
    }
  }

  std::pair<std::size_t, bool> PrepareInsert(const Key& key) {
    if (capacity_ == 0 ||
        detail::FlatWouldExceedMaxLoad(size_ + erased_ + 1u, capacity_)) {
      GrowForInsert();
    }

    const HashParts parts = SplitHash(Hash{}(key));
    const std::size_t mask = capacity_ - 1u;
    std::size_t slot = parts.index_bits & mask;
    std::size_t first_erased = kNoSlot;
    for (;;) {
      const detail::ControlByte control = control_[slot];
      if (control == parts.tag && KeyEqual{}(EntryAt(slot).first, key)) {
        return {slot, false};
      }
      if (control == detail::kControlErased) {
        if (first_erased == kNoSlot) {
          first_erased = slot;
        }
      } else if (control == detail::kControlEmpty) {
        break;
      }
      slot = (slot + 1u) & mask;
    }

    if (first_erased != kNoSlot) {

      slot = first_erased;
      --erased_;
    }
    control_[slot] = parts.tag;
    ++size_;
    return {slot, true};
  }

  template <class... Args>
  void ConstructAt(std::size_t slot, const Key& key, Args&&... args) {

    ::new (static_cast<void*>(&slots_[slot]))
        value_type{key, T(std::forward<Args>(args)...)};
  }

  void EraseSlot(std::size_t slot) noexcept {
    EntryAt(slot).~value_type();
    control_[slot] = detail::kControlErased;
    --size_;
    ++erased_;
  }

  void GrowForInsert() {
    if (capacity_ == 0) {
      Resize(detail::kFlatMinCapacity);
      return;
    }

    const bool live_elements_fit =
        !detail::FlatWouldExceedMaxLoad(size_ + 1u, capacity_);
    Resize(live_elements_fit ? capacity_ : capacity_ * 2u);
  }

  void Resize(std::size_t new_capacity) {
    auto new_control = std::unique_ptr<detail::ControlByte[]>(
        new detail::ControlByte[new_capacity]);
    for (std::size_t i = 0; i < new_capacity; ++i) {
      new_control[i] = detail::kControlEmpty;
    }

    auto new_slots = std::unique_ptr<SlotStorage[]>(new SlotStorage[new_capacity]);

    auto old_control = std::move(control_);
    auto old_slots = std::move(slots_);
    const std::size_t old_capacity = capacity_;

    control_ = std::move(new_control);
    slots_ = std::move(new_slots);
    capacity_ = new_capacity;
    erased_ = 0;
    size_ = 0;

    const std::size_t mask = capacity_ - 1u;
    for (std::size_t i = 0; i < old_capacity; ++i) {
      if (old_control[i] >= detail::kControlOccupiedLimit) {
        continue;
      }
      auto* old_entry = std::launder(reinterpret_cast<value_type*>(&old_slots[i]));
      const HashParts parts = SplitHash(Hash{}(old_entry->first));
      std::size_t slot = parts.index_bits & mask;
      while (control_[slot] != detail::kControlEmpty) {
        slot = (slot + 1u) & mask;
      }
      control_[slot] = parts.tag;
      ::new (static_cast<void*>(&slots_[slot]))
          value_type{old_entry->first, std::move(old_entry->second)};
      ++size_;
      old_entry->~value_type();
    }
  }

  void DestroyAll() noexcept {
    if constexpr (!std::is_trivially_destructible_v<value_type>) {
      for (std::size_t i = 0; i < capacity_; ++i) {
        if (control_[i] < detail::kControlOccupiedLimit) {
          EntryAt(i).~value_type();
        }
      }
    }
  }

  void Release() noexcept {
    DestroyAll();
    control_.reset();
    slots_.reset();
    capacity_ = 0;
    size_ = 0;
    erased_ = 0;
  }

  void CopyFrom(const FlatHashMap& other) {
    Reserve(other.size_);
    for (const auto& entry : other) {
      try_emplace(entry.first, entry.second);
    }
  }

  void AdoptFrom(FlatHashMap& other) noexcept {
    control_ = std::move(other.control_);
    slots_ = std::move(other.slots_);
    capacity_ = other.capacity_;
    size_ = other.size_;
    erased_ = other.erased_;
    other.capacity_ = 0;
    other.size_ = 0;
    other.erased_ = 0;
  }

  [[nodiscard]] std::size_t FirstOccupied() const noexcept {
    for (std::size_t i = 0; i < capacity_; ++i) {
      if (control_[i] < detail::kControlOccupiedLimit) {
        return i;
      }
    }
    return capacity_;
  }

  std::unique_ptr<detail::ControlByte[]> control_;
  std::unique_ptr<SlotStorage[]> slots_;
  std::size_t capacity_ = 0;
  std::size_t size_ = 0;
  std::size_t erased_ = 0;
};

template <class Key, class T, class Hash = FlatHash<Key>,
          class KeyEqual = std::equal_to<Key>>
class StableFlatHashMap {
 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = detail::MapEntry<Key, T>;
  using size_type = std::size_t;
  using hasher = Hash;
  using key_equal = KeyEqual;

 private:
  using NodeIndex = std::uint32_t;
  using IndexMap = FlatHashMap<Key, NodeIndex, Hash, KeyEqual>;

  struct alignas(value_type) NodeStorage {
    std::byte bytes[sizeof(value_type)];
  };

  static constexpr std::size_t kTargetChunkBytes = 4096u;
  static constexpr std::size_t kNodesPerChunk =
      std::bit_floor(sizeof(value_type) >= kTargetChunkBytes
                         ? std::size_t{1}
                         : kTargetChunkBytes / sizeof(value_type));
  static constexpr std::size_t kChunkShift =
      static_cast<std::size_t>(std::countr_zero(kNodesPerChunk));
  static constexpr std::size_t kChunkMask = kNodesPerChunk - 1u;

  template <bool kIsConst>
  class BasicIterator {
    friend class StableFlatHashMap;
    friend class BasicIterator<!kIsConst>;

    using MapPtr = std::conditional_t<kIsConst, const StableFlatHashMap*,
                                      StableFlatHashMap*>;
    using IndexIterator =
        std::conditional_t<kIsConst, typename IndexMap::const_iterator,
                           typename IndexMap::iterator>;

   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = StableFlatHashMap::value_type;
    using reference = std::conditional_t<kIsConst, const value_type&, value_type&>;
    using pointer = std::conditional_t<kIsConst, const value_type*, value_type*>;

    BasicIterator() = default;

    template <bool kOtherConst,
              class = std::enable_if_t<kIsConst && !kOtherConst>>
    BasicIterator(const BasicIterator<kOtherConst>& other) noexcept
        : map_(other.map_), index_it_(other.index_it_) {}

    [[nodiscard]] reference operator*() const noexcept {
      return map_->NodeAt(index_it_->second);
    }
    [[nodiscard]] pointer operator->() const noexcept {
      return &map_->NodeAt(index_it_->second);
    }

    BasicIterator& operator++() noexcept {
      ++index_it_;
      return *this;
    }
    BasicIterator operator++(int) noexcept {
      BasicIterator copy = *this;
      ++*this;
      return copy;
    }

    [[nodiscard]] bool operator==(const BasicIterator& other) const noexcept {
      return index_it_ == other.index_it_;
    }
    [[nodiscard]] bool operator!=(const BasicIterator& other) const noexcept {
      return !(*this == other);
    }

   private:
    BasicIterator(MapPtr map, IndexIterator index_it) noexcept
        : map_(map), index_it_(index_it) {}

    MapPtr map_ = nullptr;
    IndexIterator index_it_{};
  };

 public:
  using iterator = BasicIterator<false>;
  using const_iterator = BasicIterator<true>;

  StableFlatHashMap() = default;

  StableFlatHashMap(const StableFlatHashMap& other) { CopyFrom(other); }

  StableFlatHashMap(StableFlatHashMap&& other) noexcept
      : index_(std::move(other.index_)),
        chunks_(std::move(other.chunks_)),
        free_nodes_(std::move(other.free_nodes_)),
        node_high_water_(other.node_high_water_) {
    other.node_high_water_ = 0;
  }

  StableFlatHashMap& operator=(const StableFlatHashMap& other) {
    if (this != &other) {
      Clear();
      CopyFrom(other);
    }
    return *this;
  }

  StableFlatHashMap& operator=(StableFlatHashMap&& other) noexcept {
    if (this != &other) {
      DestroyAllNodes();
      index_ = std::move(other.index_);
      chunks_ = std::move(other.chunks_);
      free_nodes_ = std::move(other.free_nodes_);
      node_high_water_ = other.node_high_water_;
      other.node_high_water_ = 0;
    }
    return *this;
  }

  ~StableFlatHashMap() { DestroyAllNodes(); }

  [[nodiscard]] size_type size() const noexcept { return index_.size(); }
  [[nodiscard]] bool empty() const noexcept { return index_.empty(); }

  [[nodiscard]] iterator begin() noexcept { return iterator(this, index_.begin()); }
  [[nodiscard]] iterator end() noexcept { return iterator(this, index_.end()); }
  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator(this, index_.begin());
  }
  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator(this, index_.end());
  }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

  [[nodiscard]] iterator find(const Key& key) noexcept {
    return iterator(this, index_.find(key));
  }
  [[nodiscard]] const_iterator find(const Key& key) const noexcept {
    return const_iterator(this, index_.find(key));
  }

  [[nodiscard]] bool contains(const Key& key) const noexcept {
    return index_.contains(key);
  }
  [[nodiscard]] size_type count(const Key& key) const noexcept {
    return index_.count(key);
  }

  [[nodiscard]] T* FindValue(const Key& key) noexcept {
    const NodeIndex* node = index_.FindValue(key);
    return node == nullptr ? nullptr : &NodeAt(*node).second;
  }

  [[nodiscard]] const T* FindValue(const Key& key) const noexcept {
    const NodeIndex* node = index_.FindValue(key);
    return node == nullptr ? nullptr : &NodeAt(*node).second;
  }

  T& operator[](const Key& key) { return try_emplace(key).first->second; }

  template <class... Args>
  std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
    const auto [index_it, inserted] = index_.try_emplace(key, kUnlinkedNode);
    if (inserted) {
      index_it->second = ConstructNode(key, std::forward<Args>(args)...);
    }
    return {iterator(this, index_it), inserted};
  }

  template <class V>
  std::pair<iterator, bool> emplace(const Key& key, V&& value) {
    return try_emplace(key, std::forward<V>(value));
  }

  template <class V>
  std::pair<iterator, bool> insert_or_assign(const Key& key, V&& value) {
    const auto [index_it, inserted] = index_.try_emplace(key, kUnlinkedNode);
    if (inserted) {
      index_it->second = ConstructNode(key, std::forward<V>(value));
    } else {
      NodeAt(index_it->second).second = std::forward<V>(value);
    }
    return {iterator(this, index_it), inserted};
  }

  size_type erase(const Key& key) {
    const auto index_it = index_.find(key);
    if (index_it == index_.end()) {
      return 0u;
    }
    ReleaseNode(index_it->second);
    index_.erase(index_it);
    return 1u;
  }

  void Clear() noexcept {
    DestroyAllNodes();
    index_.Clear();
    free_nodes_.clear();
    node_high_water_ = 0;

  }

  void clear() noexcept { Clear(); }

  void Reserve(size_type expected_elements) { index_.Reserve(expected_elements); }
  void reserve(size_type expected_elements) { Reserve(expected_elements); }

 private:

  static constexpr NodeIndex kUnlinkedNode = static_cast<NodeIndex>(-1);

  [[nodiscard]] value_type& NodeAt(NodeIndex node) noexcept {
    return *std::launder(reinterpret_cast<value_type*>(
        chunks_[node >> kChunkShift].get() + (node & kChunkMask)));
  }

  [[nodiscard]] const value_type& NodeAt(NodeIndex node) const noexcept {
    return *std::launder(reinterpret_cast<const value_type*>(
        chunks_[node >> kChunkShift].get() + (node & kChunkMask)));
  }

  template <class... Args>
  NodeIndex ConstructNode(const Key& key, Args&&... args) {
    NodeIndex node = 0;
    if (!free_nodes_.empty()) {
      node = free_nodes_.back();
      free_nodes_.pop_back();
    } else {
      node = node_high_water_;
      if ((static_cast<std::size_t>(node) >> kChunkShift) == chunks_.size()) {

        chunks_.emplace_back(new NodeStorage[kNodesPerChunk]);
      }
      ++node_high_water_;
    }
    ::new (static_cast<void*>(
        chunks_[node >> kChunkShift].get() + (node & kChunkMask)))
        value_type{key, T(std::forward<Args>(args)...)};
    return node;
  }

  void ReleaseNode(NodeIndex node) noexcept {
    NodeAt(node).~value_type();
    free_nodes_.push_back(node);
  }

  void DestroyAllNodes() noexcept {
    if constexpr (!std::is_trivially_destructible_v<value_type>) {
      for (const auto& entry : index_) {
        NodeAt(entry.second).~value_type();
      }
    }
  }

  void CopyFrom(const StableFlatHashMap& other) {
    Reserve(other.size());
    for (const auto& entry : other) {
      try_emplace(entry.first, entry.second);
    }
  }

  IndexMap index_;
  std::vector<std::unique_ptr<NodeStorage[]>> chunks_;
  std::vector<NodeIndex> free_nodes_;
  NodeIndex node_high_water_ = 0;
};

}

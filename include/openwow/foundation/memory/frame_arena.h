#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if !defined(OPENWOW_FRAME_ARENA_DEBUG)
#if defined(NDEBUG)
#define OPENWOW_FRAME_ARENA_DEBUG 0
#else
#define OPENWOW_FRAME_ARENA_DEBUG 1
#endif
#endif

namespace openwow::foundation {

struct FrameArenaMarker {
  std::size_t block = 0;
  std::size_t offset = 0;
};

class FrameArena {
 public:

  static constexpr std::size_t kDefaultBlockBytes = 64u * 1024u;

  static constexpr std::byte kResetFillByte = std::byte{0xDD};

  explicit FrameArena(std::size_t block_bytes = kDefaultBlockBytes)
      : block_bytes_(block_bytes < kMinBlockBytes ? kMinBlockBytes : block_bytes) {}

  FrameArena(const FrameArena&) = delete;
  FrameArena& operator=(const FrameArena&) = delete;
  FrameArena(FrameArena&&) = delete;
  FrameArena& operator=(FrameArena&&) = delete;

  [[nodiscard]] void* Allocate(std::size_t bytes, std::size_t alignment) {
    assert(alignment != 0 && (alignment & (alignment - 1u)) == 0u &&
           "FrameArena alignment must be a power of two");
    if (bytes == 0) {

      bytes = 1;
    }

    if (current_block_ < blocks_.size()) {
      if (void* const result = TryAllocateIn(blocks_[current_block_], bytes, alignment)) {
        return result;
      }
    }
    return AllocateFromNextBlock(bytes, alignment);
  }

  template <class T>
  [[nodiscard]] T* AllocateUninitialized(std::size_t count) {
    static_assert(std::is_trivially_destructible_v<T>,
                  "The arena never runs destructors; a type that owns a "
                  "resource must be held by a container that uses "
                  "FrameArenaAllocator, whose own destructor runs inside the "
                  "scope");
    assert(count <= kMaxAllocationBytes / sizeof(T) && "arena array overflow");
    return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
  }

  template <class T, class... Args>
  [[nodiscard]] T* Create(Args&&... args) {
    static_assert(std::is_trivially_destructible_v<T>,
                  "The arena never runs destructors");
    return ::new (Allocate(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
  }

  template <class T>
  [[nodiscard]] T* CreateArray(std::size_t count, const T& value) {
    T* const data = AllocateUninitialized<T>(count);
    for (std::size_t i = 0; i < count; ++i) {
      ::new (static_cast<void*>(data + i)) T(value);
    }
    return data;
  }

  [[nodiscard]] FrameArenaMarker Mark() const noexcept {
    return FrameArenaMarker{
        current_block_,
        current_block_ < blocks_.size() ? blocks_[current_block_].used : std::size_t{0}};
  }

  void RewindTo(const FrameArenaMarker& marker) noexcept {
    assert(marker.block <= current_block_ &&
           "FrameArena markers must be released in reverse order");
#if OPENWOW_FRAME_ARENA_DEBUG
    PoisonFrom(marker.block, marker.offset);
#endif
    for (std::size_t i = marker.block; i <= current_block_ && i < blocks_.size(); ++i) {
      blocks_[i].used = (i == marker.block) ? marker.offset : 0u;
    }
    current_block_ = marker.block;
    bytes_in_use_ = RecomputeBytesInUse();
    ++generation_;
  }

  void Reset() noexcept { RewindTo(FrameArenaMarker{0, 0}); }

  void ReleaseBlocks() noexcept {
    blocks_.clear();
    current_block_ = 0;
    bytes_in_use_ = 0;
    ++generation_;
  }

  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  [[nodiscard]] std::size_t bytes_in_use() const noexcept { return bytes_in_use_; }
  [[nodiscard]] std::size_t high_water_bytes() const noexcept {
    return high_water_bytes_;
  }
  [[nodiscard]] std::size_t bytes_reserved() const noexcept {
    std::size_t total = 0;
    for (const Block& block : blocks_) {
      total += block.bytes;
    }
    return total;
  }
  [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }

 private:

  static constexpr std::size_t kMinBlockBytes = 1024u;
  static constexpr std::size_t kMaxAllocationBytes =
      static_cast<std::size_t>(-1) / 2u;

  struct Block {
    std::unique_ptr<std::byte[]> data;
    std::size_t bytes = 0;
    std::size_t used = 0;
  };

  [[nodiscard]] static std::uintptr_t AlignUp(std::uintptr_t value,
                                              std::size_t alignment) noexcept {
    const auto mask = static_cast<std::uintptr_t>(alignment) - 1u;
    return (value + mask) & ~mask;
  }

  [[nodiscard]] void* TryAllocateIn(Block& block, std::size_t bytes,
                                    std::size_t alignment) noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(block.data.get());
    const std::uintptr_t aligned = AlignUp(base + block.used, alignment);
    const auto offset = static_cast<std::size_t>(aligned - base);
    if (offset > block.bytes || bytes > block.bytes - offset) {
      return nullptr;
    }
    bytes_in_use_ += (offset + bytes) - block.used;
    block.used = offset + bytes;
    if (bytes_in_use_ > high_water_bytes_) {
      high_water_bytes_ = bytes_in_use_;
    }
    return block.data.get() + offset;
  }

  void* AllocateFromNextBlock(std::size_t bytes, std::size_t alignment) {

    while (current_block_ + 1u < blocks_.size()) {
      ++current_block_;
      if (void* const result = TryAllocateIn(blocks_[current_block_], bytes, alignment)) {
        return result;
      }
    }

    const std::size_t wanted =
        bytes + alignment > block_bytes_ ? bytes + alignment : block_bytes_;

    blocks_.push_back(Block{std::unique_ptr<std::byte[]>(new std::byte[wanted]),
                            wanted, 0u});
    current_block_ = blocks_.size() - 1u;
    void* const result = TryAllocateIn(blocks_[current_block_], bytes, alignment);
    assert(result != nullptr && "a freshly sized arena block must fit its request");
    return result;
  }

  [[nodiscard]] std::size_t RecomputeBytesInUse() const noexcept {
    std::size_t total = 0;
    for (std::size_t i = 0; i < blocks_.size() && i <= current_block_; ++i) {
      total += blocks_[i].used;
    }
    return total;
  }

#if OPENWOW_FRAME_ARENA_DEBUG
  void PoisonFrom(std::size_t block_index, std::size_t offset) noexcept {
    for (std::size_t i = block_index; i <= current_block_ && i < blocks_.size(); ++i) {
      Block& block = blocks_[i];
      for (std::size_t b = (i == block_index) ? offset : 0u; b < block.used; ++b) {
        block.data[b] = kResetFillByte;
      }
    }
  }
#endif

  std::vector<Block> blocks_;
  std::size_t current_block_ = 0;
  std::size_t block_bytes_;
  std::size_t bytes_in_use_ = 0;
  std::size_t high_water_bytes_ = 0;
  std::uint64_t generation_ = 1;
};

[[nodiscard]] inline FrameArena& ThreadFrameArena() {
  static thread_local FrameArena arena;
  return arena;
}

class FrameArenaScope {
 public:
  explicit FrameArenaScope(FrameArena& arena)
      : arena_(arena), marker_(arena.Mark()) {}

  FrameArenaScope(const FrameArenaScope&) = delete;
  FrameArenaScope& operator=(const FrameArenaScope&) = delete;
  FrameArenaScope(FrameArenaScope&&) = delete;
  FrameArenaScope& operator=(FrameArenaScope&&) = delete;

  ~FrameArenaScope() { arena_.RewindTo(marker_); }

  [[nodiscard]] FrameArena& arena() const noexcept { return arena_; }

 private:
  FrameArena& arena_;
  FrameArenaMarker marker_;
};

template <class T>
class ArenaPtr {
 public:
  ArenaPtr() = default;

  ArenaPtr([[maybe_unused]] const FrameArena& arena, T* pointer) noexcept
      : pointer_(pointer)
#if OPENWOW_FRAME_ARENA_DEBUG
        ,
        arena_(&arena),
        generation_(arena.generation())
#endif
  {
  }

  [[nodiscard]] T* get() const noexcept {
    AssertLive();
    return pointer_;
  }
  [[nodiscard]] T& operator*() const noexcept {
    AssertLive();
    return *pointer_;
  }
  [[nodiscard]] T* operator->() const noexcept {
    AssertLive();
    return pointer_;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return pointer_ != nullptr; }

 private:
  void AssertLive() const noexcept {
#if OPENWOW_FRAME_ARENA_DEBUG
    assert((pointer_ == nullptr ||
            (arena_ != nullptr && arena_->generation() == generation_)) &&
           "arena pointer used after its scope was rewound");
#endif
  }

  T* pointer_ = nullptr;
#if OPENWOW_FRAME_ARENA_DEBUG
  const FrameArena* arena_ = nullptr;
  std::uint64_t generation_ = 0;
#endif
};

template <class T>
class ArenaSpan {
 public:
  ArenaSpan() = default;

  ArenaSpan([[maybe_unused]] const FrameArena& arena, T* data,
            std::size_t size) noexcept
      : data_(data),
        size_(size)
#if OPENWOW_FRAME_ARENA_DEBUG
        ,
        arena_(&arena),
        generation_(arena.generation())
#endif
  {
  }

  [[nodiscard]] T* data() const noexcept {
    AssertLive();
    return data_;
  }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] T* begin() const noexcept { return data(); }
  [[nodiscard]] T* end() const noexcept { return data() + size_; }
  [[nodiscard]] T& operator[](std::size_t index) const noexcept {
    assert(index < size_ && "arena span index out of range");
    return data()[index];
  }

 private:
  void AssertLive() const noexcept {
#if OPENWOW_FRAME_ARENA_DEBUG
    assert((data_ == nullptr ||
            (arena_ != nullptr && arena_->generation() == generation_)) &&
           "arena span used after its scope was rewound");
#endif
  }

  T* data_ = nullptr;
  std::size_t size_ = 0;
#if OPENWOW_FRAME_ARENA_DEBUG
  const FrameArena* arena_ = nullptr;
  std::uint64_t generation_ = 0;
#endif
};

template <class T>
class FrameArenaAllocator {
 public:
  using value_type = T;

  FrameArenaAllocator(FrameArena& arena) noexcept
      : arena_(&arena), generation_(arena.generation()) {}

  template <class U>
  FrameArenaAllocator(const FrameArenaAllocator<U>& other) noexcept
      : arena_(other.arena_), generation_(other.generation_) {}

  [[nodiscard]] T* allocate(std::size_t count) {
    assert(arena_->generation() == generation_ &&
           "a container backed by a FrameArena outlived its arena scope");
    return static_cast<T*>(arena_->Allocate(count * sizeof(T), alignof(T)));
  }

  void deallocate(T*, std::size_t) noexcept {}

  [[nodiscard]] bool operator==(const FrameArenaAllocator& other) const noexcept {
    return arena_ == other.arena_;
  }
  [[nodiscard]] bool operator!=(const FrameArenaAllocator& other) const noexcept {
    return !(*this == other);
  }

 private:
  template <class U>
  friend class FrameArenaAllocator;

  FrameArena* arena_;
  std::uint64_t generation_;
};

template <class T>
using ArenaVector = std::vector<T, FrameArenaAllocator<T>>;

using ArenaString =
    std::basic_string<char, std::char_traits<char>, FrameArenaAllocator<char>>;

}

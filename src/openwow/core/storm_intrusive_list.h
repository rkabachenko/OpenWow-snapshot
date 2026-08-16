#pragma once

#include <cstdint>
#include <type_traits>

namespace openwow::core {

template <typename Word>
struct StormIntrusiveLinkWords {
  static_assert(std::is_integral_v<Word>,
                "Storm intrusive list words must use an integral token type");

  Word previous_link = 0;
  Word next_node = 0;
};

template <typename Word>
struct StormIntrusiveListRootWords {
  static_assert(std::is_integral_v<Word>,
                "Storm intrusive list roots must use an integral token type");

  std::int32_t node_link_offset = 0;
  Word tail_link = 0;
  Word head_node = 0;
};

template <typename Word>
inline constexpr Word kStormIntrusiveSentinelBit = static_cast<Word>(1);

template <typename Word>
Word* ResolveStormIntrusiveNativeWords(Word raw_pointer);

template <typename Word>
StormIntrusiveLinkWords<Word>* UnlinkStormIntrusiveNativeLink(
    Word raw_link_pointer);

struct StormDualLinkCleanupState {
  StormIntrusiveLinkWords<std::uintptr_t> primary_link{};
  StormIntrusiveLinkWords<std::uintptr_t> secondary_link{};
  void* aux_buffer = nullptr;
};

template <typename Word>
StormIntrusiveLinkWords<Word>* GetStormIntrusiveRootLinkWords(
    StormIntrusiveListRootWords<Word>& list) {
  return reinterpret_cast<StormIntrusiveLinkWords<Word>*>(&list.tail_link);
}

template <typename Word>
const StormIntrusiveLinkWords<Word>* GetStormIntrusiveRootLinkWords(
    const StormIntrusiveListRootWords<Word>& list) {
  return reinterpret_cast<const StormIntrusiveLinkWords<Word>*>(&list.tail_link);
}

template <typename Word>
StormIntrusiveLinkWords<Word>* GetStormIntrusiveNodeLinkWords(
    StormIntrusiveListRootWords<Word>& list, void* node_base) {
  if (node_base == nullptr) {
    return GetStormIntrusiveRootLinkWords(list);
  }

  const auto node_link_offset =
      list.node_link_offset >= 0 ? static_cast<std::uintptr_t>(list.node_link_offset)
                                 : 0u;
  return reinterpret_cast<StormIntrusiveLinkWords<Word>*>(
      reinterpret_cast<std::uintptr_t>(node_base) + node_link_offset);
}

template <typename Word>
const StormIntrusiveLinkWords<Word>* GetStormIntrusiveNodeLinkWords(
    const StormIntrusiveListRootWords<Word>& list, const void* node_base) {
  if (node_base == nullptr) {
    return GetStormIntrusiveRootLinkWords(list);
  }

  const auto node_link_offset =
      list.node_link_offset >= 0 ? static_cast<std::uintptr_t>(list.node_link_offset)
                                 : 0u;
  return reinterpret_cast<const StormIntrusiveLinkWords<Word>*>(
      reinterpret_cast<std::uintptr_t>(node_base) + node_link_offset);
}

template <typename Word>
[[nodiscard]] bool StormIntrusiveRootIsLinked(
    const StormIntrusiveListRootWords<Word>& list) {
  return list.head_node != 0;
}

template <typename Word>
[[nodiscard]] bool StormIntrusiveNodeIsLinked(
    const StormIntrusiveListRootWords<Word>& list, const void* node_base) {
  if (node_base == nullptr) {
    return StormIntrusiveRootIsLinked(list);
  }

  return GetStormIntrusiveNodeLinkWords(list, node_base)->next_node != 0;
}

template <typename Word>
[[nodiscard]] bool IsStormIntrusiveLinkNodeNull(
    const StormIntrusiveLinkWords<Word>& link) {
  const Word next = link.next_node;
  return (next & kStormIntrusiveSentinelBit<Word>) != 0 || next == 0;
}

template <typename Word>
[[nodiscard]] Word GetStormIntrusiveFirstNodeToken(
    const StormIntrusiveListRootWords<Word>& list) {
  const Word head_node = list.head_node;
  if ((head_node & kStormIntrusiveSentinelBit<Word>) != 0 || head_node == 0) {
    return 0;
  }

  return head_node;
}

template <typename Word>
[[nodiscard]] void* GetStormIntrusiveFirstNativeNode(
    const StormIntrusiveListRootWords<Word>& list) {
  const Word first_node = GetStormIntrusiveFirstNodeToken(list);
  if (first_node == 0) {
    return nullptr;
  }

  return reinterpret_cast<void*>(static_cast<std::uintptr_t>(first_node));
}

template <typename Word>
[[nodiscard]] void* GetStormIntrusiveNextNativeNode(
    const StormIntrusiveListRootWords<Word>& list, const void* current_node) {
  if (current_node == nullptr) {
    return GetStormIntrusiveFirstNativeNode(list);
  }

  const auto* link_words = GetStormIntrusiveNodeLinkWords(list, current_node);
  const Word next_token = link_words->next_node;
  if ((next_token & kStormIntrusiveSentinelBit<Word>) != 0 ||
      next_token == 0) {
    return nullptr;
  }

  return reinterpret_cast<void*>(static_cast<std::uintptr_t>(next_token));
}

template <typename Word>

[[nodiscard]] Word* ResolveStormIntrusiveNativeHeadPreviousLinkWord(
    StormIntrusiveListRootWords<Word>& list) {
  auto* const root_link_words = GetStormIntrusiveRootLinkWords(list);
  const Word head_node = root_link_words->next_node;
  if ((head_node & kStormIntrusiveSentinelBit<Word>) != 0 || head_node == 0) {
    return reinterpret_cast<Word*>(
        static_cast<std::uintptr_t>(head_node &
                                    ~kStormIntrusiveSentinelBit<Word>));
  }

  if (list.node_link_offset >= 0) {
    return reinterpret_cast<Word*>(
        reinterpret_cast<std::uintptr_t>(head_node) +
        static_cast<std::uintptr_t>(list.node_link_offset));
  }

  const auto* const tail_link_words =
      reinterpret_cast<const StormIntrusiveLinkWords<Word>*>(
          static_cast<std::uintptr_t>(root_link_words->previous_link));
  const auto root_link_address =
      static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(
          root_link_words));
  const auto head_node_address = static_cast<std::intptr_t>(head_node);
  const auto tail_successor =
      static_cast<std::intptr_t>(tail_link_words->next_node);
  return reinterpret_cast<Word*>(static_cast<std::uintptr_t>(
      root_link_address - tail_successor + head_node_address));
}

template <typename Word>
std::uintptr_t GetStormIntrusiveNodeLinkOffset(
    const StormIntrusiveListRootWords<Word>& list,
    const StormIntrusiveLinkWords<Word>& anchor_link) {
  if (list.node_link_offset >= 0) {
    return static_cast<std::uintptr_t>(list.node_link_offset);
  }

  auto* const previous_link_words =
      reinterpret_cast<const StormIntrusiveLinkWords<Word>*>(
          static_cast<std::uintptr_t>(anchor_link.previous_link));
  return reinterpret_cast<std::uintptr_t>(&anchor_link) -
         previous_link_words->next_node;
}

template <typename Word>
void InitializeStormIntrusiveListRoot(StormIntrusiveListRootWords<Word>& list,
                                      std::int32_t node_link_offset) {
  list.node_link_offset = node_link_offset;
  auto* const root_link = GetStormIntrusiveRootLinkWords(list);
  const auto root_token =
      static_cast<Word>(reinterpret_cast<std::uintptr_t>(root_link));
  list.tail_link = root_token;
  list.head_node = root_token | kStormIntrusiveSentinelBit<Word>;
}

template <typename Word>
void InsertStormIntrusiveNodeBefore(StormIntrusiveLinkWords<Word>& anchor_link,
                                    void* node_base,
                                    StormIntrusiveLinkWords<Word>& node_link) {
  auto* const previous_link_words =
      reinterpret_cast<StormIntrusiveLinkWords<Word>*>(
          static_cast<std::uintptr_t>(anchor_link.previous_link));
  node_link.previous_link = anchor_link.previous_link;
  node_link.next_node = previous_link_words->next_node;
  previous_link_words->next_node =
      static_cast<Word>(reinterpret_cast<std::uintptr_t>(node_base));
  anchor_link.previous_link =
      static_cast<Word>(reinterpret_cast<std::uintptr_t>(&node_link));
}

template <typename Word>
void InsertStormIntrusiveNodeAfter(StormIntrusiveListRootWords<Word>& list,
                                   StormIntrusiveLinkWords<Word>& anchor_link,
                                   void* node_base,
                                   StormIntrusiveLinkWords<Word>& node_link) {
  node_link.previous_link =
      static_cast<Word>(reinterpret_cast<std::uintptr_t>(&anchor_link));
  node_link.next_node = anchor_link.next_node;

  const Word next_node = anchor_link.next_node;
  if ((next_node & kStormIntrusiveSentinelBit<Word>) == 0 && next_node != 0) {
    const auto node_link_offset =
        GetStormIntrusiveNodeLinkOffset(list, anchor_link);
    auto* const next_link_words = reinterpret_cast<StormIntrusiveLinkWords<Word>*>(
        static_cast<std::uintptr_t>(next_node) + node_link_offset);
    next_link_words->previous_link =
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(&node_link));
  } else {
    auto* const root_link_words = reinterpret_cast<StormIntrusiveLinkWords<Word>*>(
        static_cast<std::uintptr_t>(
            next_node & ~kStormIntrusiveSentinelBit<Word>));
    root_link_words->previous_link =
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(&node_link));
  }

  anchor_link.next_node =
      static_cast<Word>(reinterpret_cast<std::uintptr_t>(node_base));
}

template <typename Word>

void RelinkStormIntrusiveNode(StormIntrusiveListRootWords<Word>& list,
                              void* node_base,
                              int mode,
                              void* anchor_base = nullptr) {
  auto* const node_link_words = GetStormIntrusiveNodeLinkWords(list, node_base);
  if (node_link_words->previous_link != 0) {
    UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(node_link_words)));
  }

  auto* const anchor_link_words =
      GetStormIntrusiveNodeLinkWords(list, anchor_base);
  if (mode == 1) {
    InsertStormIntrusiveNodeAfter(list, *anchor_link_words, node_base,
                                  *node_link_words);
    return;
  }

  InsertStormIntrusiveNodeBefore(*anchor_link_words, node_base,
                                 *node_link_words);
}

template <typename Word, typename ResolveWordPointer>
StormIntrusiveLinkWords<Word>* UnlinkStormIntrusiveLink(
    const Word raw_link_pointer, ResolveWordPointer&& resolve_word_pointer) {
  auto&& resolve = resolve_word_pointer;
  auto* const link_words = reinterpret_cast<StormIntrusiveLinkWords<Word>*>(
      resolve(raw_link_pointer));
  if (link_words == nullptr || link_words->previous_link == 0) {
    return link_words;
  }

  const Word next_node = link_words->next_node;
  Word* next_link_owner = nullptr;
  if ((next_node & kStormIntrusiveSentinelBit<Word>) == 0 && next_node != 0) {
    auto* const previous_link_words =
        reinterpret_cast<StormIntrusiveLinkWords<Word>*>(
            resolve(link_words->previous_link));
    if (previous_link_words == nullptr) {
      return link_words;
    }

    const Word current_link_offset = previous_link_words->next_node;
    next_link_owner =
        resolve(static_cast<Word>(raw_link_pointer + next_node -
                                  current_link_offset));
  } else {
    next_link_owner =
        resolve(static_cast<Word>(next_node &
                                  ~kStormIntrusiveSentinelBit<Word>));
  }

  if (next_link_owner == nullptr) {
    return link_words;
  }

  *next_link_owner = link_words->previous_link;

  auto* const previous_next_field = resolve(link_words->previous_link);
  if (previous_next_field == nullptr) {
    return link_words;
  }

  previous_next_field[1] = next_node;
  link_words->previous_link = 0;
  link_words->next_node = 0;
  return link_words;
}

template <typename Word>
Word* ResolveStormIntrusiveNativeWords(const Word raw_pointer) {
  if (raw_pointer == 0) {
    return nullptr;
  }

  return reinterpret_cast<Word*>(static_cast<std::uintptr_t>(raw_pointer));
}

template <typename Word>
StormIntrusiveLinkWords<Word>* UnlinkStormIntrusiveNativeLink(
    const Word raw_link_pointer) {
  return UnlinkStormIntrusiveLink<Word>(raw_link_pointer,
                                        &ResolveStormIntrusiveNativeWords<Word>);
}

template <typename Word>
void UnlinkAllStormIntrusiveNativeNodes(StormIntrusiveListRootWords<Word>& list) {
  for (void* node = GetStormIntrusiveFirstNativeNode(list); node != nullptr;
       node = GetStormIntrusiveFirstNativeNode(list)) {
    auto* const link_words = GetStormIntrusiveNodeLinkWords(list, node);
    UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(link_words)));
  }
}

template <typename Word>
void DetachDualStormIntrusiveLinks(
    StormIntrusiveLinkWords<Word>& primary_link,
    StormIntrusiveLinkWords<Word>& secondary_link) {
  if (primary_link.next_node == 0) {
    return;
  }

  if (primary_link.previous_link != 0) {
    UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(&primary_link)));
  }

  if (secondary_link.previous_link != 0) {
    UnlinkStormIntrusiveNativeLink<Word>(
        static_cast<Word>(reinterpret_cast<std::uintptr_t>(&secondary_link)));
  }
}

template <typename FreeBlockFn>
StormIntrusiveLinkWords<std::uintptr_t>* FreeAuxBufferAndDetachStormLinks(
    StormIntrusiveLinkWords<std::uintptr_t>& primary_link,
    StormIntrusiveLinkWords<std::uintptr_t>& secondary_link,
    void*& aux_buffer,
    FreeBlockFn&& free_block) {
  auto&& release_block = free_block;
  if (aux_buffer != nullptr) {
    release_block(aux_buffer);
  }

  if (secondary_link.previous_link != 0) {
    UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(&secondary_link));
  }

  if (primary_link.previous_link != 0) {
    UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(&primary_link));
  }

  return &primary_link;
}

template <typename FreeBlockFn>
StormIntrusiveLinkWords<std::uintptr_t>* FreeAuxBufferAndDetachStormLinks(
    StormDualLinkCleanupState& cleanup, FreeBlockFn&& free_block) {
  return FreeAuxBufferAndDetachStormLinks(cleanup.primary_link,
                                          cleanup.secondary_link,
                                          cleanup.aux_buffer, free_block);
}

}

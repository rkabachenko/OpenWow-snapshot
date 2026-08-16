#pragma once

#include "openwow/game/cmirror_slot_bucket.h"
#include "openwow/core/storm_intrusive_list.h"

#include <cstddef>
#include <cstdint>

namespace openwow::game {

struct MirrorCallbackNode {
  core::StormIntrusiveLinkWords<std::uintptr_t> slot_link{};
  core::StormIntrusiveLinkWords<std::uintptr_t> active_link{};

  std::uintptr_t callback_fn{0};
  std::uintptr_t callback_context{0};

  std::uint32_t words_remaining{0};

  std::uint32_t absolute_offset_bytes{0};
  std::uint32_t compact_slot_offset{0};
  std::uint32_t size_bytes{0};
  std::uint32_t activation_mode{0};

  bool dispatching{false};
  bool deferred_delete{false};
  bool force_notify{false};
};

static_assert(offsetof(MirrorCallbackNode, slot_link) == 0,
              "slot_link must be at offset 0 for node_link_offset == 0");

inline constexpr std::int32_t kMirrorSlotLinkOffset = 0;

MirrorCallbackNode* CMirrorHandler_RegisterSlotCallback(
    MirrorSlotBucket& bucket,
    std::uint32_t absolute_offset_bytes,
    std::uint32_t compact_slot_offset,
    std::uint32_t size_bytes,
    std::uintptr_t callback_fn,
    std::uintptr_t callback_context,
    std::uint32_t activation_mode,
    bool force_notify);

bool CMirrorHandler_UnregisterSlotCallback(
    MirrorSlotBucket& bucket,
    std::uintptr_t callback_fn,
    std::uintptr_t callback_context);

void CMirrorHandler_UnlinkNode(MirrorCallbackNode& node);

MirrorCallbackNode* CMirrorHandler_DestroyNode(
    MirrorSlotBucket& bucket,
    MirrorCallbackNode* node);

}

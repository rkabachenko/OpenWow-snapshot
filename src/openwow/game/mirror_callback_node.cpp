
#include "openwow/game/mirror_callback_node.h"

#include <new>

namespace openwow::game {

void CMirrorHandler_UnlinkNode(MirrorCallbackNode& node) {

  if (node.active_link.previous_link != 0) {
    core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(&node.active_link));
  }

  if (node.slot_link.previous_link != 0) {
    core::UnlinkStormIntrusiveNativeLink<std::uintptr_t>(
        reinterpret_cast<std::uintptr_t>(&node.slot_link));
  }
}

MirrorCallbackNode* CMirrorHandler_DestroyNode(
    MirrorSlotBucket& bucket,
    MirrorCallbackNode* node) {
  if (node == nullptr) {
    return nullptr;
  }

  auto* const next = static_cast<MirrorCallbackNode*>(
      core::GetStormIntrusiveNextNativeNode(bucket, node));

  CMirrorHandler_UnlinkNode(*node);
  delete node;
  return next;
}

MirrorCallbackNode* CMirrorHandler_RegisterSlotCallback(
    MirrorSlotBucket& bucket,
    const std::uint32_t absolute_offset_bytes,
    const std::uint32_t compact_slot_offset,
    const std::uint32_t size_bytes,
    const std::uintptr_t callback_fn,
    const std::uintptr_t callback_context,
    const std::uint32_t activation_mode,
    const bool force_notify) {
  auto* node = new (std::nothrow) MirrorCallbackNode{};
  if (node == nullptr) {
    return nullptr;
  }

  auto* const root_link =
      core::GetStormIntrusiveRootLinkWords(bucket);
  core::InsertStormIntrusiveNodeBefore(
      *root_link, node, node->slot_link);

  node->absolute_offset_bytes = absolute_offset_bytes;
  node->compact_slot_offset = compact_slot_offset;
  node->size_bytes = size_bytes;
  node->callback_fn = callback_fn;
  node->callback_context = callback_context;
  node->activation_mode = activation_mode;
  node->dispatching = false;
  node->deferred_delete = false;
  node->force_notify = force_notify;

  return node;
}

bool CMirrorHandler_UnregisterSlotCallback(
    MirrorSlotBucket& bucket,
    const std::uintptr_t callback_fn,
    const std::uintptr_t callback_context) {
  for (auto* node = static_cast<MirrorCallbackNode*>(
           core::GetStormIntrusiveFirstNativeNode(bucket));
       node != nullptr;
       node = static_cast<MirrorCallbackNode*>(
           core::GetStormIntrusiveNextNativeNode(bucket, node))) {
    if (node->callback_fn != callback_fn ||
        node->callback_context != callback_context) {
      continue;
    }

    if (!node->dispatching) {
      CMirrorHandler_DestroyNode(bucket, node);
    } else {
      node->deferred_delete = true;
    }
    return true;
  }

  return false;
}

}

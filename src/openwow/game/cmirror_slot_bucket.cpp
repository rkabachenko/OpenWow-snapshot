
#include "openwow/game/cmirror_slot_bucket.h"

#include "openwow/game/mirror_callback_node.h"

namespace openwow::game {

void CMirrorHandler_ClearSlotBucketArray(
    const std::uint32_t count,
    MirrorSlotBucket* buckets) {
  if (!buckets || count == 0) {
    return;
  }

  for (std::uint32_t i = 0; i < count; ++i) {
    auto& list_root = buckets[i];

    for (auto* node = static_cast<MirrorCallbackNode*>(
             core::GetStormIntrusiveFirstNativeNode(list_root));
         node != nullptr;
         node = static_cast<MirrorCallbackNode*>(
             core::GetStormIntrusiveFirstNativeNode(list_root))) {
      CMirrorHandler_UnlinkNode(*node);
      delete node;
    }
  }
}

}


#include "openwow/core/blizzard_hash_map.h"

namespace openwow::core {

void BlizzardHashMapBucketArray::DestroyBucketArray() {
    if (allocator && allocator->free) {

        for (std::uint32_t i = 0; i < capacity; ++i) {
            if (data[i]) {
                allocator->free(data[i]);
            }
        }

        if (data) {
            allocator->free(data);
        }
    }

    data = nullptr;
    capacity = 0;
    used_count = 0;
    load_thresh = 0;
}

}

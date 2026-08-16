
#include "openwow/core/blizzard_hash_map_key.h"

namespace openwow::core {

BlizzardHashMapFreeFn g_blizzardHashMapFree = nullptr;

void BlizzardHashMapKey::Clear() {
    hash = 0;

    if (refcounted_buffer) {

        if (--(*refcounted_buffer) == 0) {
            if (g_blizzardHashMapFree) {
                g_blizzardHashMapFree(refcounted_buffer);
            }
        }
        refcounted_buffer = nullptr;
    }

    string_ptr = nullptr;
}

}


#include "openwow/game/player_name_desc.h"

#include <atomic>
#include <cstring>
#include <list>
#include <new>

namespace openwow::game {

namespace {

std::atomic<std::uint32_t> g_player_name_display_flags{0};
std::atomic<std::uint32_t> g_player_name_display_flags_revision{0};

std::list<PlayerNameDesc*> g_active_player_name_descriptors;

}

PlayerNameDesc* PlayerNameDesc_Allocate(void* ,
                                        int   ,
                                        std::uint32_t generation) {

    auto* desc = new (std::nothrow) PlayerNameDesc;
    if (!desc) {
        return nullptr;
    }

    desc->listLinkNext   = 0;
    desc->listLinkPrev   = 0;
    desc->gxString       = 0;
    desc->textColor      = 0;
    desc->guidLow        = 0;
    desc->guidHigh       = 0;
    desc->flags          = kPlayerNameDescDefaultFlags;
    desc->generation     = generation;
    desc->attachments[0] = 0;
    desc->attachments[1] = 0;
    desc->attachments[2] = 0;
    desc->attachments[3] = 0;
    desc->textWidth      = 0.0f;

    return desc;
}

PlayerNameDesc* PlayerNameDesc_CreateForGuid(const std::uint64_t guid_raw,
                                             const std::uint32_t generation) {
    if (guid_raw == 0u) {
        return nullptr;
    }

    auto* const desc = PlayerNameDesc_Allocate(nullptr, 0, generation);
    if (desc == nullptr) {
        return nullptr;
    }

    desc->guidLow = static_cast<std::uint32_t>(guid_raw & 0xFFFFFFFFu);
    desc->guidHigh = static_cast<std::uint32_t>(guid_raw >> 32u);
    desc->flags |= PNDF_FORCE_VISIBLE;
    g_active_player_name_descriptors.push_front(desc);
    return desc;
}

void PlayerNameDesc_Destroy(PlayerNameDesc* const desc) {
    if (desc == nullptr) {
        return;
    }

    PlayerNameDesc_RemoveAttachmentsByType(*desc, kWorldTextStringType_All);
    desc->gxString = 0;
    g_active_player_name_descriptors.remove(desc);
    delete desc;
}

void PlayerName_IncrementGeneration(PlayerNameSubsystem& sys) {
    ++sys.generation;
}

std::uint32_t PlayerName_GetDisplayFlags() {
    return g_player_name_display_flags.load(std::memory_order_acquire);
}

std::uint32_t PlayerName_GetDisplayFlagsRevision() {
    return g_player_name_display_flags_revision.load(std::memory_order_acquire);
}

bool PlayerName_SetDisplayFlag(const std::uint32_t flag, const bool enabled) {
    auto previous = g_player_name_display_flags.load(std::memory_order_relaxed);
    for (;;) {
        const auto desired = enabled ? (previous | flag) : (previous & ~flag);
        if (desired == previous) {
            return false;
        }
        if (g_player_name_display_flags.compare_exchange_weak(
                previous, desired, std::memory_order_release,
                std::memory_order_relaxed)) {
            g_player_name_display_flags_revision.fetch_add(
                1, std::memory_order_release);
            return true;
        }
    }
}

void WorldTextString_DestroyAndFree(void* entry) {

    delete static_cast<std::uint8_t*>(entry);
}

void PlayerNameDesc_RemoveAttachmentsByType(PlayerNameDesc& desc,
                                            std::uint32_t type) {
    for (int i = 3; i >= 0; --i) {
        if (desc.attachments[i] == 0) {
            continue;
        }

        if (type == kWorldTextStringType_All) {
            WorldTextString_DestroyAndFree(
                reinterpret_cast<void*>(desc.attachments[i]));
            desc.attachments[i] = 0;
        } else {
            auto* wts = reinterpret_cast<std::uint32_t*>(desc.attachments[i]);
            std::uint32_t entryType = wts[2];
            if (type == entryType) {
                WorldTextString_DestroyAndFree(wts);
                desc.attachments[i] = 0;
            }
        }
    }
}

void PlayerNameDesc_RemoveAttachmentsByType_Safe(PlayerNameDesc* desc,
                                                 std::uint32_t type) {
    if (desc) {
        PlayerNameDesc_RemoveAttachmentsByType(*desc, type);
    }
}

bool WorldTextString_UpdateScreenPosition_Safe(void* wts,
                                               std::uint32_t ) {
    if (!wts) {
        return false;
    }

    return true;
}

bool PlayerName_UpdateAttachments(PlayerNameDesc& desc,
                                  std::uint32_t elapsed_ms) {

    bool any_survived = false;

    for (int i = 3; i >= 0; --i) {
        if (desc.attachments[i] == 0) {
            continue;
        }

        auto* wts = reinterpret_cast<void*>(desc.attachments[i]);
        if (!WorldTextString_UpdateScreenPosition_Safe(wts, elapsed_ms)) {

            WorldTextString_DestroyAndFree(wts);
            desc.attachments[i] = 0;
        } else {
            any_survived = true;
        }
    }

    return any_survived;
}

}

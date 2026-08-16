#pragma once

#include <cstdint>

namespace openwow::game {

enum PlayerNameDescFlags : std::uint32_t {
    PNDF_DIRTY_TEXT            = 0x01,
    PNDF_DIRTY_COLOR           = 0x02,
    PNDF_FORCE_VISIBLE         = 0x04,
    PNDF_HAS_RENDER_CALLBACK   = 0x08,
};

static constexpr std::uint32_t kPlayerNameDescDefaultFlags =
    PNDF_DIRTY_TEXT | PNDF_DIRTY_COLOR;

struct PlayerNameDesc {

    std::uint32_t listLinkNext;
    std::uint32_t listLinkPrev;

    std::uint32_t gxString;

    std::uint32_t textColor;

    std::uint32_t guidLow;
    std::uint32_t guidHigh;

    std::uint32_t flags;

    std::uint32_t generation;

    std::uint32_t attachments[4];

    float         textWidth;
};

static_assert(sizeof(PlayerNameDesc) == 0x34,
              "PlayerNameDesc must be exactly 0x34 (52) bytes");

struct PlayerNameSubsystem {
    std::uint32_t generation;
    std::uint32_t displayFlags;

    void*         allocator;
    void*         nameFont;
    void*         stringBatch;
    void*         pvpRankTexture;
    std::uint32_t listOffset;
    std::uint32_t listHead;
};

PlayerNameDesc* PlayerNameDesc_Allocate(void* allocator,
                                        int clear,
                                        std::uint32_t generation);

PlayerNameDesc* PlayerNameDesc_CreateForGuid(std::uint64_t guid_raw,
                                             std::uint32_t generation);

void PlayerNameDesc_Destroy(PlayerNameDesc* desc);

void PlayerName_IncrementGeneration(PlayerNameSubsystem& sys);

[[nodiscard]] std::uint32_t PlayerName_GetDisplayFlags();
[[nodiscard]] std::uint32_t PlayerName_GetDisplayFlagsRevision();
bool PlayerName_SetDisplayFlag(std::uint32_t flag, bool enabled);

static constexpr std::uint32_t kWorldTextStringType_All = 11;

void WorldTextString_DestroyAndFree(void* entry);

void PlayerNameDesc_RemoveAttachmentsByType(PlayerNameDesc& desc,
                                            std::uint32_t type);

void PlayerNameDesc_RemoveAttachmentsByType_Safe(PlayerNameDesc* desc,
                                                 std::uint32_t type);

bool WorldTextString_UpdateScreenPosition_Safe(void* wts,
                                               std::uint32_t elapsed_ms);

bool PlayerName_UpdateAttachments(PlayerNameDesc& desc,
                                  std::uint32_t elapsed_ms);

}


#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

class WorldSession;

int PetitionFrame_ValidateRename(const char* newName);

void PetitionFrame_BuyGuildCharter(WorldSession& session, const char* guildName);

void PetitionFrame_BuyPetition(WorldSession& session, std::uint32_t selection_index,
                               const char* petitionName);

void PetitionFrame_ClickPetitionButton(WorldSession& session);

bool PetitionFrame_TurnInGuildCharter(WorldSession& session);

void PetitionFrame_TurnInSelectedPetition(WorldSession& session);

bool PetitionFrame_HasFilledArenaPetition(WorldSession& session);

bool PetitionFrame_TurnInArenaPetition(
    WorldSession& session, std::uint32_t team_size,
    const std::array<std::uint32_t, 5>& extra_fields);

[[nodiscard]] bool PetitionFrame_RequestTabardVendorActivate(
    WorldSession& session, std::uint64_t vendor_guid);

void PetitionFrame_RequestTabardInfo(WorldSession& session);

static constexpr uint32_t kPetitionMsg_AlreadyInGuild = 92;
static constexpr uint32_t kPetitionMsg_NotEnoughMoney = 40;
static constexpr uint32_t kPetitionMsg_AlreadyInArenaTeam = 522;
static constexpr uint32_t kPetitionMsg_ArenaRequiresLevel = 544;

struct AuctionContainerState {
    uint64_t itemGuid;
    uint32_t containerId;
    uint32_t slotId;
    uint8_t  flags;
};

void SetAuctionContainerItem(uint64_t itemGuid, uint32_t containerId,
                              uint32_t slotId, uint8_t flags);

AuctionContainerState GetAuctionContainerItem();

}

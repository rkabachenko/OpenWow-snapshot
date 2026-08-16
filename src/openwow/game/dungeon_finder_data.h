
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DungeonCategory : uint8_t {
    Normal  = 0,
    Heroic  = 1,
    Raid    = 2,
    Event   = 3,
};

struct DungeonListEntry {
    uint32_t       dungeonId   = 0;
    std::string    name;
    uint32_t       minLevel    = 0;
    uint32_t       maxLevel    = 0;
    DungeonCategory category   = DungeonCategory::Normal;
    bool           isAvailable = true;
    bool           isCompleted = false;
    bool           isRandom    = false;
    uint32_t       mapId       = 0;
    uint32_t       difficulty  = 0;
};

struct LFGProposalPlayer {
    ObjectGuid guid;
    uint32_t   role     = 0;
    bool       accepted = false;
    bool       isMe     = false;
};

struct LFGProposalEntry {
    uint32_t                      proposalId = 0;
    uint32_t                      dungeonId  = 0;
    uint8_t                       state      = 0;
    std::vector<LFGProposalPlayer> players;
};

class DungeonFinderData {
 public:
    void SetDungeonList(const std::vector<DungeonListEntry>& list);
    [[nodiscard]] std::vector<DungeonListEntry> GetDungeonList() const;
    [[nodiscard]] std::optional<DungeonListEntry> GetDungeon(uint32_t dungeonId) const;
    [[nodiscard]] std::vector<DungeonListEntry> GetDungeonsByCategory(DungeonCategory cat) const;
    [[nodiscard]] std::vector<DungeonListEntry> GetAvailableDungeons() const;
    [[nodiscard]] std::vector<DungeonListEntry> GetEligibleDungeons(uint32_t playerLevel) const;
    [[nodiscard]] std::optional<DungeonListEntry> GetRandomDungeonEntry() const;

    void SetProposal(const LFGProposalEntry& proposal);
    [[nodiscard]] std::optional<LFGProposalEntry> GetProposal() const;
    [[nodiscard]] bool HasProposal() const;
    void AcceptProposal();
    void DeclineProposal();
    void ClearProposal();
    void SetProposalAccepted(ObjectGuid guid, bool accepted);
    [[nodiscard]] uint32_t GetAcceptedCount() const;
    [[nodiscard]] uint32_t GetDeclinedCount() const;
    [[nodiscard]] bool AllAccepted() const;

    void Reset();

 private:
    std::vector<DungeonListEntry>     dungeons_;
    std::optional<LFGProposalEntry>   proposal_;
    mutable std::mutex                mutex_;
};

}

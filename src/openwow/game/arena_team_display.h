#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class ArenaTeamSize : uint8_t {
    Team2v2 = 2,
    Team3v3 = 3,
    Team5v5 = 5,
};

struct ArenaTeamMemberInfo {
    ObjectGuid guid{};
    std::string name;
    uint8_t classId = 0;
    uint8_t level = 0;
    uint32_t personalRating = 0;
    uint32_t weeklyGames = 0;
    uint32_t weeklyWins = 0;
    uint32_t seasonGames = 0;
    uint32_t seasonWins = 0;
    bool isOnline = false;
    bool isCaptain = false;
};

struct ArenaTeamDisplayInfo {
    uint32_t teamId = 0;
    std::string teamName;
    ArenaTeamSize size = ArenaTeamSize::Team2v2;
    uint32_t rating = 0;
    uint32_t weeklyGames = 0;
    uint32_t weeklyWins = 0;
    uint32_t seasonGames = 0;
    uint32_t seasonWins = 0;
    ObjectGuid captainGuid{};
    std::vector<ArenaTeamMemberInfo> members;
};

class ArenaTeamDisplay {
 public:
    ArenaTeamDisplay() = default;

    void SetTeam(ArenaTeamSize size, const ArenaTeamDisplayInfo& info);
    [[nodiscard]] std::optional<ArenaTeamDisplayInfo> GetTeam(
        ArenaTeamSize size) const;
    [[nodiscard]] bool HasTeam(ArenaTeamSize size) const;
    [[nodiscard]] size_t GetTeamCount() const;
    [[nodiscard]] size_t GetMemberCount(ArenaTeamSize size) const;
    [[nodiscard]] float GetWinRate(ArenaTeamSize size) const;
    [[nodiscard]] std::vector<ArenaTeamSize> GetTeamSizes() const;

    void Reset();

 private:
    std::map<ArenaTeamSize, ArenaTeamDisplayInfo> teams_;
};

}

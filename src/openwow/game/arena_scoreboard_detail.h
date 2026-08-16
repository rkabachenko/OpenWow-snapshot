
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class ArenaScoreTeamSide : uint8_t {
    Green = 0,
    Gold  = 1,
};

enum class ArenaMatchResult : uint8_t {
    Win  = 0,
    Loss = 1,
    Draw = 2,
};

struct ArenaScorePlayerEntry {
    uint64_t          guid         = 0;
    std::string       name;
    uint32_t          killingBlows = 0;
    uint32_t          deaths       = 0;
    uint32_t          honorGained  = 0;
    uint32_t          damageDone   = 0;
    uint32_t          healingDone  = 0;
    int32_t           ratingChange = 0;
    ArenaScoreTeamSide teamSide    = ArenaScoreTeamSide::Green;
    uint32_t          specIcon     = 0;
    uint8_t           classId      = 0;
    bool              isAlive      = true;
};

struct ArenaScoreTeamInfo {
    std::string name;
    uint32_t    oldRating    = 0;
    uint32_t    newRating    = 0;
    uint32_t    playerCount  = 0;
};

class ArenaScoreboardDetail {
 public:
    void SetMatchInfo(uint32_t mapId, uint32_t durationMs,
                      uint8_t bracketType);

    void AddPlayer(const ArenaScorePlayerEntry& entry);

    void SetTeamInfo(ArenaScoreTeamSide side, const ArenaScoreTeamInfo& info);

    void SetResult(ArenaMatchResult result);

    [[nodiscard]] std::vector<ArenaScorePlayerEntry> GetPlayers(
        ArenaScoreTeamSide side) const;

    [[nodiscard]] ArenaScoreTeamInfo GetTeamInfo(
        ArenaScoreTeamSide side) const;

    [[nodiscard]] uint32_t GetMatchDuration() const;
    [[nodiscard]] uint8_t  GetBracketType() const;
    [[nodiscard]] ArenaMatchResult GetResult() const;
    [[nodiscard]] uint32_t GetPlayerCount() const;

    [[nodiscard]] std::optional<ArenaScorePlayerEntry> GetMVP() const;

    void Reset();

 private:
    std::vector<ArenaScorePlayerEntry> players_;
    ArenaScoreTeamInfo                 greenTeam_;
    ArenaScoreTeamInfo                 goldTeam_;
    uint32_t                           mapId_       = 0;
    uint32_t                           durationMs_  = 0;
    uint8_t                            bracketType_ = 2;
    ArenaMatchResult                   result_      = ArenaMatchResult::Draw;
};

}

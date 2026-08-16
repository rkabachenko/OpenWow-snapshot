
#include "openwow/game/arena_scoreboard_detail.h"

namespace openwow::game {

void ArenaScoreboardDetail::SetMatchInfo(uint32_t mapId, uint32_t durationMs,
                                         uint8_t bracketType) {
    mapId_       = mapId;
    durationMs_  = durationMs;
    bracketType_ = bracketType;
}

void ArenaScoreboardDetail::AddPlayer(const ArenaScorePlayerEntry& entry) {
    players_.push_back(entry);
}

void ArenaScoreboardDetail::SetTeamInfo(ArenaScoreTeamSide side,
                                        const ArenaScoreTeamInfo& info) {
    if (side == ArenaScoreTeamSide::Green) {
        greenTeam_ = info;
    } else {
        goldTeam_ = info;
    }
}

void ArenaScoreboardDetail::SetResult(ArenaMatchResult result) {
    result_ = result;
}

std::vector<ArenaScorePlayerEntry> ArenaScoreboardDetail::GetPlayers(
    ArenaScoreTeamSide side) const {
    std::vector<ArenaScorePlayerEntry> result;
    for (const auto& p : players_) {
        if (p.teamSide == side) {
            result.push_back(p);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const ArenaScorePlayerEntry& a,
                 const ArenaScorePlayerEntry& b) {
                  return a.damageDone > b.damageDone;
              });
    return result;
}

ArenaScoreTeamInfo ArenaScoreboardDetail::GetTeamInfo(
    ArenaScoreTeamSide side) const {
    return (side == ArenaScoreTeamSide::Green) ? greenTeam_ : goldTeam_;
}

uint32_t ArenaScoreboardDetail::GetMatchDuration() const {
    return durationMs_;
}

uint8_t ArenaScoreboardDetail::GetBracketType() const {
    return bracketType_;
}

ArenaMatchResult ArenaScoreboardDetail::GetResult() const {
    return result_;
}

uint32_t ArenaScoreboardDetail::GetPlayerCount() const {
    return static_cast<uint32_t>(players_.size());
}

std::optional<ArenaScorePlayerEntry> ArenaScoreboardDetail::GetMVP() const {
    if (players_.empty()) return std::nullopt;

    const ArenaScorePlayerEntry* best = &players_[0];
    uint64_t bestContrib = static_cast<uint64_t>(best->damageDone) +
                           static_cast<uint64_t>(best->healingDone);

    for (size_t i = 1; i < players_.size(); ++i) {
        uint64_t contrib = static_cast<uint64_t>(players_[i].damageDone) +
                           static_cast<uint64_t>(players_[i].healingDone);
        if (contrib > bestContrib) {
            best       = &players_[i];
            bestContrib = contrib;
        }
    }
    return *best;
}

void ArenaScoreboardDetail::Reset() {
    players_.clear();
    greenTeam_ = {};
    goldTeam_  = {};
    mapId_       = 0;
    durationMs_  = 0;
    bracketType_ = 2;
    result_      = ArenaMatchResult::Draw;
}

}

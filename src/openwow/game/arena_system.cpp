
#include "openwow/game/arena_system.h"

#include <algorithm>

namespace openwow::game {

ArenaSystem& ArenaSystem::Get() {
    static ArenaSystem instance;
    return instance;
}

void ArenaSystem::SetTeam(uint8_t slot, const ArenaSystemTeam& team) {
    std::lock_guard lock(mutex_);
    if (slot < teams_.size()) {
        teams_[slot] = team;
    }
}

const ArenaSystemTeam* ArenaSystem::GetTeam(uint8_t slot) const {
    std::lock_guard lock(mutex_);
    if (slot < teams_.size() && teams_[slot].team_id != 0) {
        return &teams_[slot];
    }
    return nullptr;
}

bool ArenaSystem::HasTeam(uint8_t slot) const {
    std::lock_guard lock(mutex_);
    return slot < teams_.size() && teams_[slot].team_id != 0;
}

uint32_t ArenaSystem::GetTeamId(uint8_t slot) const {
    std::lock_guard lock(mutex_);
    return slot < teams_.size() ? teams_[slot].team_id : 0;
}

int ArenaSystem::FindTeamSlotById(uint32_t team_id) const {
    std::lock_guard lock(mutex_);
    for (std::size_t slot = 0; slot < teams_.size(); ++slot) {
        if (teams_[slot].team_id == team_id) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

void ArenaSystem::UpdateLocalPlayerTeam(uint8_t slot, uint32_t team_id,
                                        uint32_t local_week_games,
                                        uint32_t local_week_wins,
                                        uint32_t local_personal_rating,
                                        bool local_player_is_captain) {
    std::lock_guard lock(mutex_);
    if (slot >= teams_.size()) {
        return;
    }

    auto& team = teams_[slot];
    team.team_id = team_id;
    team.local_week_games = local_week_games;
    team.local_week_wins = local_week_wins;
    team.local_personal_rating = local_personal_rating;
    team.local_player_is_captain = local_player_is_captain;
}

bool ArenaSystem::UpdateTeamStatsById(uint32_t team_id, uint32_t rating,
                                      uint32_t week_games, uint32_t week_wins,
                                      uint32_t season_games, uint32_t season_wins,
                                      uint32_t rank) {
    std::lock_guard lock(mutex_);
    for (auto& team : teams_) {
        if (team.team_id != team_id) {
            continue;
        }

        team.rating = rating;
        team.played_week = week_games;
        team.won_week = week_wins;
        team.played_season = season_games;
        team.won_season = season_wins;
        team.rank = rank;
        return true;
    }
    return false;
}

void ArenaSystem::UpdateTeamRosterById(uint32_t team_id, uint8_t team_type,
                                       const std::vector<ArenaSystemMember>& members) {
    std::lock_guard lock(mutex_);
    for (auto& team : teams_) {
        if (team.team_id != team_id) {
            continue;
        }

        team.team_type = team_type;
        team.members = members;
        team.captain_guid = 0;
        for (const auto& member : team.members) {
            if (member.guid != 0 && member.rank == 0) {
                team.captain_guid = member.guid;
                break;
            }
        }
        break;
    }
}

void ArenaSystem::SetArenaPoints(uint32_t points) {
    std::lock_guard lock(mutex_);
    arena_points_ = points;
}

uint32_t ArenaSystem::GetArenaPoints() const {
    std::lock_guard lock(mutex_);
    return arena_points_;
}

void ArenaSystem::SetInArena(bool in, uint8_t teamSize) {
    std::lock_guard lock(mutex_);
    in_arena_ = in;
    arena_team_size_ = teamSize;
}

bool ArenaSystem::IsInArena() const {
    std::lock_guard lock(mutex_);
    return in_arena_;
}

uint8_t ArenaSystem::GetArenaTeamSize() const {
    std::lock_guard lock(mutex_);
    return arena_team_size_;
}

void ArenaSystem::StartQueue(uint8_t team_size, bool rated, bool skirmish) {
    std::lock_guard lock(mutex_);
    match_info_ = ArenaMatchInfo{};
    match_info_.phase = ArenaMatchPhase::Queued;
    match_info_.team_size = team_size;
    match_info_.is_rated = rated;
    match_info_.is_skirmish = skirmish;
    match_info_.queue_wait_time = 0.0f;
}

void ArenaSystem::SetMatchPhase(ArenaMatchPhase phase) {
    std::lock_guard lock(mutex_);
    match_info_.phase = phase;
    if (phase == ArenaMatchPhase::GatesOpen ||
        phase == ArenaMatchPhase::InProgress) {
        in_arena_ = true;
        arena_team_size_ = match_info_.team_size;
    }
    if (phase == ArenaMatchPhase::None) {
        in_arena_ = false;
    }
}

ArenaMatchPhase ArenaSystem::GetMatchPhase() const {
    std::lock_guard lock(mutex_);
    return match_info_.phase;
}

const ArenaMatchInfo& ArenaSystem::GetMatchInfo() const {
    std::lock_guard lock(mutex_);
    return match_info_;
}

void ArenaSystem::SetConfirmTimer(float seconds) {
    std::lock_guard lock(mutex_);
    match_info_.confirm_timer = std::max(0.0f, seconds);
}

void ArenaSystem::SetPrepTimer(float seconds) {
    std::lock_guard lock(mutex_);
    match_info_.prep_timer = std::max(0.0f, seconds);
}

void ArenaSystem::SetMatchMapId(uint32_t map_id) {
    std::lock_guard lock(mutex_);
    match_info_.map_id = map_id;
}

void ArenaSystem::SetMatchTeamNames(const std::string& green,
                                     const std::string& gold) {
    std::lock_guard lock(mutex_);
    match_info_.green_team_name = green;
    match_info_.gold_team_name = gold;
}

void ArenaSystem::SetMatchResult(bool won, int32_t rating_change,
                                  uint32_t new_rating, uint32_t mmr_change) {
    std::lock_guard lock(mutex_);
    match_info_.phase = ArenaMatchPhase::Finished;
    match_info_.won = won;
    match_info_.rating_change = rating_change;
    match_info_.new_rating = new_rating;
    match_info_.mmr_change = mmr_change;
    in_arena_ = false;
}

void ArenaSystem::UpdateTimers(float dt) {
    std::lock_guard lock(mutex_);
    switch (match_info_.phase) {
        case ArenaMatchPhase::Queued:
            match_info_.queue_wait_time += dt;
            break;
        case ArenaMatchPhase::WaitJoin:
            match_info_.confirm_timer = std::max(0.0f, match_info_.confirm_timer - dt);
            break;
        case ArenaMatchPhase::Preparation:
            match_info_.prep_timer = std::max(0.0f, match_info_.prep_timer - dt);
            if (match_info_.prep_timer <= 0.0f) {
                match_info_.phase = ArenaMatchPhase::GatesOpen;
                in_arena_ = true;
                arena_team_size_ = match_info_.team_size;
            }
            break;
        case ArenaMatchPhase::GatesOpen:
        case ArenaMatchPhase::InProgress:
            match_info_.elapsed_time += dt;
            break;
        default:
            break;
    }
}

void ArenaSystem::AcceptArenaQueue() {
    std::lock_guard lock(mutex_);
    if (match_info_.phase == ArenaMatchPhase::WaitJoin) {
        match_info_.phase = ArenaMatchPhase::Preparation;
        match_info_.prep_timer = 60.0f;
    }
}

void ArenaSystem::DeclineArenaQueue() {
    std::lock_guard lock(mutex_);
    if (match_info_.phase == ArenaMatchPhase::WaitJoin) {
        match_info_ = ArenaMatchInfo{};
    }
}

void ArenaSystem::LeaveArena() {
    std::lock_guard lock(mutex_);
    match_info_ = ArenaMatchInfo{};
    in_arena_ = false;
    arena_team_size_ = 0;
}

bool ArenaSystem::IsSkirmish() const {
    std::lock_guard lock(mutex_);
    return match_info_.is_skirmish;
}

void ArenaSystem::SetSeasonInfo(const ArenaSeasonInfo& info) {
    std::lock_guard lock(mutex_);
    season_info_ = info;
}

const ArenaSeasonInfo& ArenaSystem::GetSeasonInfo() const {
    std::lock_guard lock(mutex_);
    return season_info_;
}

void ArenaSystem::SetHighestPersonalRating(uint32_t rating) {
    std::lock_guard lock(mutex_);
    season_info_.highest_personal_rating = rating;
}

void ArenaSystem::SetHighestTeamRating(uint32_t rating) {
    std::lock_guard lock(mutex_);
    season_info_.highest_team_rating = rating;
}

void ArenaSystem::Reset() {
    std::lock_guard lock(mutex_);
    teams_ = {};
    arena_points_ = 0;
    in_arena_ = false;
    arena_team_size_ = 0;
    match_info_ = ArenaMatchInfo{};
    season_info_ = ArenaSeasonInfo{};
}

}

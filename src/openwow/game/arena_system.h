
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

struct ArenaSystemMember {
    uint64_t guid = 0;
    std::string name;
    uint32_t rank = 0;
    uint32_t played_week = 0;
    uint32_t won_week = 0;
    uint32_t played_season = 0;
    uint32_t won_season = 0;
    uint32_t personal_rating = 0;
    uint32_t class_id = 0;
    bool is_online = false;
};

struct ArenaSystemTeam {
    uint32_t team_id = 0;
    std::string name;
    uint8_t team_type = 0;
    uint32_t rating = 0;
    uint32_t played_week = 0;
    uint32_t won_week = 0;
    uint32_t played_season = 0;
    uint32_t won_season = 0;

    uint32_t rank = 0;
    uint32_t local_week_games = 0;
    uint32_t local_week_wins = 0;
    uint32_t local_personal_rating = 0;
    bool local_player_is_captain = false;
    uint64_t captain_guid = 0;
    std::vector<ArenaSystemMember> members;
};

enum class ArenaMatchPhase : uint8_t {
    None         = 0,
    Queued       = 1,
    WaitJoin     = 2,
    Preparation  = 3,
    GatesOpen    = 4,
    InProgress   = 5,
    Finished     = 6,
};

struct ArenaMatchInfo {
    ArenaMatchPhase phase = ArenaMatchPhase::None;
    uint8_t team_size = 0;
    bool is_rated = false;
    bool is_skirmish = false;
    uint32_t map_id = 0;

    float queue_wait_time = 0.0f;
    float confirm_timer = 0.0f;

    float prep_timer = 0.0f;

    float elapsed_time = 0.0f;

    bool won = false;
    int32_t rating_change = 0;
    uint32_t new_rating = 0;
    uint32_t mmr_change = 0;

    std::string green_team_name;
    std::string gold_team_name;
};

struct ArenaSeasonInfo {
    uint32_t season_id = 0;
    bool is_active = false;
    uint32_t season_start_time = 0;
    uint32_t highest_personal_rating = 0;
    uint32_t highest_team_rating = 0;
};

class ArenaSystem {
 public:
    static ArenaSystem& Get();

    void SetTeam(uint8_t slot, const ArenaSystemTeam& team);
    [[nodiscard]] const ArenaSystemTeam* GetTeam(uint8_t slot) const;
    [[nodiscard]] bool HasTeam(uint8_t slot) const;
    [[nodiscard]] uint32_t GetTeamId(uint8_t slot) const;
    [[nodiscard]] int FindTeamSlotById(uint32_t team_id) const;
    void UpdateLocalPlayerTeam(uint8_t slot, uint32_t team_id,
                               uint32_t local_week_games,
                               uint32_t local_week_wins,
                               uint32_t local_personal_rating,
                               bool local_player_is_captain);
    [[nodiscard]] bool UpdateTeamStatsById(uint32_t team_id, uint32_t rating,
                                           uint32_t week_games, uint32_t week_wins,
                                           uint32_t season_games, uint32_t season_wins,
                                           uint32_t rank);
    void UpdateTeamRosterById(uint32_t team_id, uint8_t team_type,
                              const std::vector<ArenaSystemMember>& members);

    void SetArenaPoints(uint32_t points);
    [[nodiscard]] uint32_t GetArenaPoints() const;

    void SetInArena(bool in, uint8_t teamSize = 0);
    [[nodiscard]] bool IsInArena() const;
    [[nodiscard]] uint8_t GetArenaTeamSize() const;

    void StartQueue(uint8_t team_size, bool rated, bool skirmish);
    void SetMatchPhase(ArenaMatchPhase phase);
    [[nodiscard]] ArenaMatchPhase GetMatchPhase() const;
    [[nodiscard]] const ArenaMatchInfo& GetMatchInfo() const;

    void SetConfirmTimer(float seconds);
    void SetPrepTimer(float seconds);
    void SetMatchMapId(uint32_t map_id);
    void SetMatchTeamNames(const std::string& green, const std::string& gold);
    void SetMatchResult(bool won, int32_t rating_change,
                        uint32_t new_rating, uint32_t mmr_change);

    void UpdateTimers(float dt);

    void AcceptArenaQueue();

    void DeclineArenaQueue();

    void LeaveArena();

    [[nodiscard]] bool IsSkirmish() const;

    void SetSeasonInfo(const ArenaSeasonInfo& info);
    [[nodiscard]] const ArenaSeasonInfo& GetSeasonInfo() const;
    void SetHighestPersonalRating(uint32_t rating);
    void SetHighestTeamRating(uint32_t rating);

    void Reset();

 private:
    ArenaSystem() = default;

    std::array<ArenaSystemTeam, 3> teams_{};
    uint32_t arena_points_ = 0;
    bool in_arena_ = false;
    uint8_t arena_team_size_ = 0;
    ArenaMatchInfo match_info_{};
    ArenaSeasonInfo season_info_{};
    mutable std::mutex mutex_;
};

}


#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openwow::game {

enum class ArenaBracket : uint8_t {
    Bracket2v2 = 0,
    Bracket3v3 = 1,
    Bracket5v5 = 2,
};

inline constexpr uint8_t kArenaBracketCount = 3;

struct ArenaPersonalInfo {
    std::string team_name;
    uint32_t team_rating      = 0;
    uint32_t personal_rating  = 0;
    uint32_t games_played     = 0;
    uint32_t games_won        = 0;
    uint32_t season_games     = 0;
    uint32_t season_wins      = 0;
};

struct PvPSummary {
    uint32_t honor_points         = 0;
    uint32_t arena_points         = 0;
    uint32_t hk_today             = 0;
    uint32_t hk_yesterday         = 0;
    uint32_t hk_lifetime          = 0;
    uint32_t dk_lifetime          = 0;
    uint32_t honor_today          = 0;
    uint32_t honor_yesterday      = 0;
    uint32_t honor_lifetime       = 0;
    uint32_t highest_pvp_rank     = 0;
};

class HonorSystem {
 public:
    static HonorSystem& Get();

    void SetHonorPoints(uint32_t points);
    [[nodiscard]] uint32_t GetHonorPoints() const;

    void SetHonorToday(uint32_t h);
    void SetHonorYesterday(uint32_t h);
    void SetHonorLifetime(uint32_t h);
    [[nodiscard]] uint32_t GetHonorToday() const;
    [[nodiscard]] uint32_t GetHonorYesterday() const;
    [[nodiscard]] uint32_t GetHonorLifetime() const;

    void SetArenaPoints(uint32_t points);
    [[nodiscard]] uint32_t GetArenaPoints() const;

    void SetHKToday(uint32_t kills);
    void SetHKYesterday(uint32_t kills);
    void SetHKLifetime(uint32_t kills);
    void SetDKLifetime(uint32_t kills);

    [[nodiscard]] uint32_t GetHKToday() const;
    [[nodiscard]] uint32_t GetHKYesterday() const;
    [[nodiscard]] uint32_t GetHKLifetime() const;
    [[nodiscard]] uint32_t GetDKLifetime() const;

    void SetHighestPvPRank(uint32_t rank);
    [[nodiscard]] uint32_t GetHighestPvPRank() const;

    [[nodiscard]] static uint32_t ComputeHonorForKill(uint32_t base_honor,
                                                       uint32_t kill_count_today);

    void RecordKill(uint32_t victim_id);

    [[nodiscard]] uint32_t GetKillCountOnVictim(uint32_t victim_id) const;

    void ResetDailyKills();

    void SetArenaInfo(ArenaBracket bracket, const ArenaPersonalInfo& info);
    [[nodiscard]] const ArenaPersonalInfo& GetArenaInfo(ArenaBracket bracket) const;
    [[nodiscard]] bool HasArenaTeam(ArenaBracket bracket) const;

    [[nodiscard]] static uint32_t EstimateWeeklyArenaPoints(uint32_t team_rating);

    [[nodiscard]] PvPSummary GetPvPSummary() const;

    void Reset();

 private:
    HonorSystem() = default;

    uint32_t honor_points_   = 0;
    uint32_t arena_points_   = 0;
    uint32_t hk_today_       = 0;
    uint32_t hk_yesterday_   = 0;
    uint32_t hk_lifetime_    = 0;
    uint32_t dk_lifetime_    = 0;
    uint32_t honor_today_    = 0;
    uint32_t honor_yesterday_= 0;
    uint32_t honor_lifetime_ = 0;
    uint32_t highest_rank_   = 0;

    ArenaPersonalInfo arena_info_[kArenaBracketCount] = {};

    std::unordered_map<uint32_t, uint32_t> daily_kills_;

    mutable std::mutex mutex_;
};

}

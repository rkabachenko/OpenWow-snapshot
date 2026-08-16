
#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <string>

namespace openwow::game {

namespace detail {

inline constexpr const char* kAllianceRankNames[] = {
    "No Rank",
    "Private",
    "Corporal",
    "Sergeant",
    "Master Sergeant",
    "Sergeant Major",
    "Knight",
    "Knight-Lieutenant",
    "Knight-Captain",
    "Knight-Champion",
    "Lieutenant Commander",
    "Commander",
    "Marshal",
    "Field Marshal",
    "Grand Marshal",
};

inline constexpr const char* kHordeRankNames[] = {
    "No Rank",
    "Scout",
    "Grunt",
    "Sergeant",
    "Senior Sergeant",
    "First Sergeant",
    "Stone Guard",
    "Blood Guard",
    "Legionnaire",
    "Centurion",
    "Champion",
    "Lieutenant General",
    "General",
    "Warlord",
    "High Warlord",
};

inline constexpr uint32_t kMaxRankIndex = 14;

}

class InspectHonorData {
 public:
    InspectHonorData() = default;

    void SetPlayerGuid(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetPlayerGuid() const;

    void SetLifetimeHKs(uint32_t v);
    [[nodiscard]] uint32_t GetLifetimeHKs() const;

    void SetTodayHKs(uint32_t v);
    [[nodiscard]] uint32_t GetTodayHKs() const;

    void SetYesterdayHKs(uint32_t v);
    [[nodiscard]] uint32_t GetYesterdayHKs() const;

    void SetThisWeekHKs(uint32_t v);
    [[nodiscard]] uint32_t GetThisWeekHKs() const;

    void SetLastWeekHKs(uint32_t v);
    [[nodiscard]] uint32_t GetLastWeekHKs() const;

    void SetHonorPoints(uint32_t v);
    [[nodiscard]] uint32_t GetHonorPoints() const;

    void SetArenaPoints(uint32_t v);
    [[nodiscard]] uint32_t GetArenaPoints() const;

    void SetHighestRank(uint32_t v);
    [[nodiscard]] uint32_t GetHighestRank() const;

    [[nodiscard]] std::string GetHighestRankName(bool isAlliance) const;

    [[nodiscard]] bool IsActive() const;
    void SetActive(bool v);

    [[nodiscard]] bool HasData() const;

    void Reset();

 private:
    ObjectGuid guid_{};
    uint32_t lifetime_hks_  = 0;
    uint32_t today_hks_     = 0;
    uint32_t yesterday_hks_ = 0;
    uint32_t this_week_hks_ = 0;
    uint32_t last_week_hks_ = 0;
    uint32_t honor_points_  = 0;
    uint32_t arena_points_  = 0;
    uint32_t highest_rank_  = 0;
    bool active_            = false;
    bool set_any_           = false;
};

}

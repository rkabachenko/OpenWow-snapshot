
#include "openwow/game/honor_system.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

HonorSystem& HonorSystem::Get() {
    static HonorSystem instance;
    return instance;
}

void HonorSystem::SetHonorPoints(uint32_t points) {
    std::lock_guard lock(mutex_);
    honor_points_ = points;
}

uint32_t HonorSystem::GetHonorPoints() const {
    std::lock_guard lock(mutex_);
    return honor_points_;
}

void HonorSystem::SetHonorToday(uint32_t h) {
    std::lock_guard lock(mutex_);
    honor_today_ = h;
}

void HonorSystem::SetHonorYesterday(uint32_t h) {
    std::lock_guard lock(mutex_);
    honor_yesterday_ = h;
}

void HonorSystem::SetHonorLifetime(uint32_t h) {
    std::lock_guard lock(mutex_);
    honor_lifetime_ = h;
}

uint32_t HonorSystem::GetHonorToday() const {
    std::lock_guard lock(mutex_);
    return honor_today_;
}

uint32_t HonorSystem::GetHonorYesterday() const {
    std::lock_guard lock(mutex_);
    return honor_yesterday_;
}

uint32_t HonorSystem::GetHonorLifetime() const {
    std::lock_guard lock(mutex_);
    return honor_lifetime_;
}

void HonorSystem::SetArenaPoints(uint32_t points) {
    std::lock_guard lock(mutex_);
    arena_points_ = points;
}

uint32_t HonorSystem::GetArenaPoints() const {
    std::lock_guard lock(mutex_);
    return arena_points_;
}

void HonorSystem::SetHKToday(uint32_t kills) {
    std::lock_guard lock(mutex_);
    hk_today_ = kills;
}

void HonorSystem::SetHKYesterday(uint32_t kills) {
    std::lock_guard lock(mutex_);
    hk_yesterday_ = kills;
}

void HonorSystem::SetHKLifetime(uint32_t kills) {
    std::lock_guard lock(mutex_);
    hk_lifetime_ = kills;
}

void HonorSystem::SetDKLifetime(uint32_t kills) {
    std::lock_guard lock(mutex_);
    dk_lifetime_ = kills;
}

uint32_t HonorSystem::GetHKToday() const {
    std::lock_guard lock(mutex_);
    return hk_today_;
}

uint32_t HonorSystem::GetHKYesterday() const {
    std::lock_guard lock(mutex_);
    return hk_yesterday_;
}

uint32_t HonorSystem::GetHKLifetime() const {
    std::lock_guard lock(mutex_);
    return hk_lifetime_;
}

uint32_t HonorSystem::GetDKLifetime() const {
    std::lock_guard lock(mutex_);
    return dk_lifetime_;
}

void HonorSystem::SetHighestPvPRank(uint32_t rank) {
    std::lock_guard lock(mutex_);
    highest_rank_ = rank;
}

uint32_t HonorSystem::GetHighestPvPRank() const {
    std::lock_guard lock(mutex_);
    return highest_rank_;
}

uint32_t HonorSystem::ComputeHonorForKill(uint32_t base_honor,
                                           uint32_t kill_count_today) {

    static constexpr uint32_t kDRPercents[] = {100, 75, 50, 25, 0};

    uint32_t idx = (kill_count_today == 0) ? 0 : kill_count_today - 1;
    if (idx >= 5) return 0;

    uint32_t pct = kDRPercents[idx];
    return (base_honor * pct) / 100;
}

void HonorSystem::RecordKill(uint32_t victim_id) {
    std::lock_guard lock(mutex_);
    daily_kills_[victim_id]++;
}

uint32_t HonorSystem::GetKillCountOnVictim(uint32_t victim_id) const {
    std::lock_guard lock(mutex_);
    auto it = daily_kills_.find(victim_id);
    return it != daily_kills_.end() ? it->second : 0;
}

void HonorSystem::ResetDailyKills() {
    std::lock_guard lock(mutex_);
    daily_kills_.clear();
}

void HonorSystem::SetArenaInfo(ArenaBracket bracket,
                               const ArenaPersonalInfo& info) {
    std::lock_guard lock(mutex_);
    uint8_t idx = static_cast<uint8_t>(bracket);
    if (idx < kArenaBracketCount) {
        arena_info_[idx] = info;
    }
}

const ArenaPersonalInfo& HonorSystem::GetArenaInfo(ArenaBracket bracket) const {
    std::lock_guard lock(mutex_);
    uint8_t idx = static_cast<uint8_t>(bracket);
    static const ArenaPersonalInfo kEmpty{};
    return (idx < kArenaBracketCount) ? arena_info_[idx] : kEmpty;
}

bool HonorSystem::HasArenaTeam(ArenaBracket bracket) const {
    std::lock_guard lock(mutex_);
    uint8_t idx = static_cast<uint8_t>(bracket);
    if (idx >= kArenaBracketCount) return false;
    return !arena_info_[idx].team_name.empty();
}

uint32_t HonorSystem::EstimateWeeklyArenaPoints(uint32_t team_rating) {

    if (team_rating == 0) return 0;

    if (team_rating < 1500) {
        return static_cast<uint32_t>(team_rating * 0.22);
    }

    double points = 1511.26 / (1.0 + 1639.28 * std::exp(-0.00412 * team_rating));
    points = std::min(points, 5000.0);
    return static_cast<uint32_t>(points);
}

PvPSummary HonorSystem::GetPvPSummary() const {
    std::lock_guard lock(mutex_);
    PvPSummary s;
    s.honor_points    = honor_points_;
    s.arena_points    = arena_points_;
    s.hk_today        = hk_today_;
    s.hk_yesterday    = hk_yesterday_;
    s.hk_lifetime     = hk_lifetime_;
    s.dk_lifetime     = dk_lifetime_;
    s.honor_today     = honor_today_;
    s.honor_yesterday = honor_yesterday_;
    s.honor_lifetime  = honor_lifetime_;
    s.highest_pvp_rank = highest_rank_;
    return s;
}

void HonorSystem::Reset() {
    std::lock_guard lock(mutex_);
    honor_points_ = 0;
    arena_points_ = 0;
    hk_today_ = 0;
    hk_yesterday_ = 0;
    hk_lifetime_ = 0;
    dk_lifetime_ = 0;
    honor_today_ = 0;
    honor_yesterday_ = 0;
    honor_lifetime_ = 0;
    highest_rank_ = 0;
    daily_kills_.clear();
    for (auto& ai : arena_info_) ai = ArenaPersonalInfo{};
}

}

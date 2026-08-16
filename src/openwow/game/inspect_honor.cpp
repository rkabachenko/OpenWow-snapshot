
#include "openwow/game/inspect_honor.h"

#include <algorithm>

namespace openwow::game {

void InspectHonorData::SetPlayerGuid(ObjectGuid guid) {
    guid_ = guid;
}

ObjectGuid InspectHonorData::GetPlayerGuid() const {
    return guid_;
}

void InspectHonorData::SetLifetimeHKs(uint32_t v) {
    lifetime_hks_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetLifetimeHKs() const {
    return lifetime_hks_;
}

void InspectHonorData::SetTodayHKs(uint32_t v) {
    today_hks_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetTodayHKs() const {
    return today_hks_;
}

void InspectHonorData::SetYesterdayHKs(uint32_t v) {
    yesterday_hks_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetYesterdayHKs() const {
    return yesterday_hks_;
}

void InspectHonorData::SetThisWeekHKs(uint32_t v) {
    this_week_hks_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetThisWeekHKs() const {
    return this_week_hks_;
}

void InspectHonorData::SetLastWeekHKs(uint32_t v) {
    last_week_hks_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetLastWeekHKs() const {
    return last_week_hks_;
}

void InspectHonorData::SetHonorPoints(uint32_t v) {
    honor_points_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetHonorPoints() const {
    return honor_points_;
}

void InspectHonorData::SetArenaPoints(uint32_t v) {
    arena_points_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetArenaPoints() const {
    return arena_points_;
}

void InspectHonorData::SetHighestRank(uint32_t v) {
    highest_rank_ = v;
    set_any_ = true;
}

uint32_t InspectHonorData::GetHighestRank() const {
    return highest_rank_;
}

std::string InspectHonorData::GetHighestRankName(bool isAlliance) const {
    uint32_t idx = highest_rank_;
    if (idx > detail::kMaxRankIndex) idx = detail::kMaxRankIndex;
    return isAlliance ? detail::kAllianceRankNames[idx]
                      : detail::kHordeRankNames[idx];
}

bool InspectHonorData::IsActive() const {
    return active_;
}

void InspectHonorData::SetActive(bool v) {
    active_ = v;
}

bool InspectHonorData::HasData() const {
    return set_any_;
}

void InspectHonorData::Reset() {
    guid_          = ObjectGuid{};
    lifetime_hks_  = 0;
    today_hks_     = 0;
    yesterday_hks_ = 0;
    this_week_hks_ = 0;
    last_week_hks_ = 0;
    honor_points_  = 0;
    arena_points_  = 0;
    highest_rank_  = 0;
    active_        = false;
    set_any_       = false;
}

}

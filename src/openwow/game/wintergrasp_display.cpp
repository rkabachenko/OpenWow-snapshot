
#include "openwow/game/wintergrasp_display.h"

#include <cstdio>

namespace openwow::game {

void WintergraspDisplay::SetBattleInfo(const WGBattleInfo& info) {
    battleInfo_ = info;
}

WGBattleInfo WintergraspDisplay::GetBattleInfo() const {
    return battleInfo_;
}

void WintergraspDisplay::AddWorkshop(const WGWorkshopInfo& workshop) {
    if (workshops_.size() < kMaxWorkshops) {
        workshops_.push_back(workshop);
    }
}

std::vector<WGWorkshopInfo> WintergraspDisplay::GetWorkshops() const {
    return workshops_;
}

void WintergraspDisplay::SetWorkshopControl(uint32_t workshopId,
                                            WGFaction faction) {
    for (auto& ws : workshops_) {
        if (ws.workshopId == workshopId) {
            ws.controllingFaction = faction;
            return;
        }
    }
}

void WintergraspDisplay::AddTower(const WGTowerInfo& tower) {
    if (towers_.size() < kMaxTowers) {
        towers_.push_back(tower);
    }
}

std::vector<WGTowerInfo> WintergraspDisplay::GetTowers() const {
    return towers_;
}

void WintergraspDisplay::SetTowerHealth(uint32_t towerId, float health,
                                        bool isDestroyed) {
    for (auto& t : towers_) {
        if (t.towerId == towerId) {
            t.health      = health;
            t.isDestroyed = isDestroyed;
            return;
        }
    }
}

bool WintergraspDisplay::IsInBattle() const {
    return battleInfo_.state == WGBattleState::InProgress;
}

std::string WintergraspDisplay::GetTimeRemaining() const {
    uint32_t totalSec = battleInfo_.timeRemainingMs / 1000;
    uint32_t minutes  = totalSec / 60;
    uint32_t seconds  = totalSec % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
    return std::string(buf);
}

WGFaction WintergraspDisplay::GetDefenderFaction() const {
    return battleInfo_.controllingFaction;
}

WGFaction WintergraspDisplay::GetAttackerFaction() const {
    return battleInfo_.attackingFaction;
}

uint16_t WintergraspDisplay::GetAllianceCount() const {
    return battleInfo_.numAlliance;
}

uint16_t WintergraspDisplay::GetHordeCount() const {
    return battleInfo_.numHorde;
}

void WintergraspDisplay::Reset() {
    battleInfo_ = {};
    workshops_.clear();
    towers_.clear();
}

}

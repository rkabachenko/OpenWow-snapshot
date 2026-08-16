
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class WGBattleState : uint8_t {
    Idle       = 0,
    Warmup     = 1,
    InProgress = 2,
    Cooldown   = 3,
};

enum class WGFaction : uint8_t {
    Alliance = 0,
    Horde    = 1,
    Neutral  = 2,
};

struct WGWorkshopInfo {
    uint32_t  workshopId         = 0;
    std::string name;
    WGFaction controllingFaction = WGFaction::Neutral;
};

struct WGTowerInfo {
    uint32_t  towerId            = 0;
    std::string name;
    WGFaction controllingFaction = WGFaction::Neutral;
    float     health             = 1.0f;
    bool      isDestroyed        = false;
};

struct WGBattleInfo {
    WGBattleState state               = WGBattleState::Idle;
    WGFaction     controllingFaction   = WGFaction::Neutral;
    WGFaction     attackingFaction     = WGFaction::Neutral;
    uint32_t      timeRemainingMs      = 0;
    uint16_t      numAlliance          = 0;
    uint16_t      numHorde             = 0;
};

class WintergraspDisplay {
 public:
    static constexpr uint32_t kMaxTowers    = 7;
    static constexpr uint32_t kMaxWorkshops = 6;

    void SetBattleInfo(const WGBattleInfo& info);
    [[nodiscard]] WGBattleInfo GetBattleInfo() const;

    void AddWorkshop(const WGWorkshopInfo& workshop);
    [[nodiscard]] std::vector<WGWorkshopInfo> GetWorkshops() const;
    void SetWorkshopControl(uint32_t workshopId, WGFaction faction);

    void AddTower(const WGTowerInfo& tower);
    [[nodiscard]] std::vector<WGTowerInfo> GetTowers() const;
    void SetTowerHealth(uint32_t towerId, float health, bool isDestroyed);

    [[nodiscard]] bool IsInBattle() const;
    [[nodiscard]] std::string GetTimeRemaining() const;
    [[nodiscard]] WGFaction GetDefenderFaction() const;
    [[nodiscard]] WGFaction GetAttackerFaction() const;
    [[nodiscard]] uint16_t GetAllianceCount() const;
    [[nodiscard]] uint16_t GetHordeCount() const;

    void Reset();

 private:
    WGBattleInfo                 battleInfo_;
    std::vector<WGWorkshopInfo>  workshops_;
    std::vector<WGTowerInfo>     towers_;
};

}

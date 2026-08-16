#pragma once

#include <cstdint>

namespace openwow::game {

struct AggroRangeInfo {
    float baseRange = 0.0f;
    float detectionRange = 0.0f;
    float alertRange = 0.0f;
    bool isAggressive = false;
    bool isElite = false;
    std::uint8_t creatureLevel = 0;
    std::uint8_t playerLevel = 0;
};

class AggroRangeDisplay {
 public:

    [[nodiscard]] static float CalculateAggroRange(
        std::uint8_t playerLevel, std::uint8_t creatureLevel,
        bool isElite = false);

    [[nodiscard]] static float CalculateDetectionRange(
        float baseRange, bool stealth = false, float stealthLevel = 0.0f);

    void SetPlayerLevel(std::uint8_t level);
    [[nodiscard]] std::uint8_t GetPlayerLevel() const;

    [[nodiscard]] bool IsInAggroRange(float distance,
                                      std::uint8_t creatureLevel,
                                      bool isElite) const;

    [[nodiscard]] AggroRangeInfo GetAggroInfo(
        std::uint8_t creatureLevel, bool isElite = false,
        bool stealth = false, float stealthLevel = 0.0f) const;

    void SetShowAggroRadius(bool show);
    [[nodiscard]] bool IsShowingAggroRadius() const;

    void Reset();

 private:
    std::uint8_t playerLevel_ = 1;
    bool showAggroRadius_ = false;
};

}

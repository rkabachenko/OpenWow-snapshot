#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DRCategory : std::uint8_t {
    Stun,
    Fear,
    Root,
    Incapacitate,
    Silence,
    Disarm,
    Horror,
    Cyclone,
    Banish,
    Disorient,
    FrostShock,
};

enum class DRLevel : std::uint8_t {
    Full    = 0,
    Half    = 1,
    Quarter = 2,
    Immune  = 3,
};

struct DREntry {
    DRCategory category{};
    DRLevel level = DRLevel::Full;
    float resetTimer = 0.0f;
    static constexpr float kResetTime = 18.0f;
};

class DiminishingReturnsTracker {
 public:

    DRLevel ApplyDR(ObjectGuid target, DRCategory cat);

    [[nodiscard]] DRLevel GetDRLevel(ObjectGuid target,
                                     DRCategory cat) const;

    [[nodiscard]] float GetTimeUntilReset(ObjectGuid target,
                                          DRCategory cat) const;

    [[nodiscard]] static float GetDurationMultiplier(DRLevel level);

    [[nodiscard]] std::vector<DREntry> GetAllDR(ObjectGuid target) const;

    [[nodiscard]] bool IsImmune(ObjectGuid target, DRCategory cat) const;

    void Update(float dt);

    [[nodiscard]] std::size_t GetTrackedTargetCount() const;

    [[nodiscard]] static std::string GetCategoryName(DRCategory cat);

    void Clear();

 private:
    struct TargetKey {
        std::uint64_t guid;
        DRCategory category;

        bool operator==(const TargetKey& o) const {
            return guid == o.guid && category == o.category;
        }
    };

    struct TargetKeyHash {
        std::size_t operator()(const TargetKey& k) const {

            auto h = std::hash<std::uint64_t>{}(k.guid);
            h ^= std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(
                     k.category)) +
                 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<TargetKey, DREntry, TargetKeyHash> entries_;
};

}

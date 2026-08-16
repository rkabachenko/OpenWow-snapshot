#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class DebuffDisplayType : uint8_t {
    Magic    = 0,
    Curse    = 1,
    Disease  = 2,
    Poison   = 3,
    Physical = 4,
    None     = 5,
};

struct DebuffDisplayEntry {
    uint32_t         auraIndex  = 0;
    uint32_t         spellId    = 0;
    std::string      name;
    std::string      iconPath;
    uint32_t         count      = 0;
    float            duration   = 0.0f;
    float            remaining  = 0.0f;
    DebuffDisplayType debuffType = DebuffDisplayType::None;
    bool             isMine     = false;
    ObjectGuid       casterGuid;
};

class DebuffDisplayData {
public:

    void SetDebuffs(std::vector<DebuffDisplayEntry> debuffs);

    [[nodiscard]] std::vector<DebuffDisplayEntry> GetDebuffs() const;

    [[nodiscard]] std::optional<DebuffDisplayEntry> GetDebuff(uint32_t auraIndex) const;

    [[nodiscard]] uint32_t GetDebuffCount() const;

    [[nodiscard]] bool HasDebuff(uint32_t spellId) const;

    [[nodiscard]] std::vector<DebuffDisplayEntry> GetDebuffsByType(DebuffDisplayType type) const;

    [[nodiscard]] std::vector<DebuffDisplayEntry> GetDispellableDebuffs() const;

    [[nodiscard]] std::vector<DebuffDisplayEntry> GetMyDebuffs() const;

    [[nodiscard]] static uint32_t GetBorderColor(DebuffDisplayType type);

    void Update(float dt);

    void SortByDuration();

    void SortByTimeRemaining();

    [[nodiscard]] std::vector<DebuffDisplayEntry> GetExpiringSoon(float thresholdSeconds) const;

    void RemoveExpired();

    [[nodiscard]] std::optional<DebuffDisplayEntry> GetLongestDebuff() const;

    [[nodiscard]] uint32_t GetHighestStackCount() const;

    [[nodiscard]] uint32_t GetDebuffCountByType(DebuffDisplayType type) const;

    [[nodiscard]] float GetAverageRemainingTime() const;

    [[nodiscard]] static std::string FormatRemainingTime(float seconds);

    void RemoveDebuff(uint32_t auraIndex);

    void AddDebuff(const DebuffDisplayEntry& entry);

    [[nodiscard]] bool HasDispellableDebuffs() const;

    void Clear();

private:
    std::vector<DebuffDisplayEntry> debuffs_;
};

}

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class BuffDisplayType : uint8_t {
    Buff         = 0,
    Debuff       = 1,
    WeaponEnchant = 2,
};

struct BuffFrameIcon {
    uint32_t        auraIndex      = 0;
    uint32_t        spellId        = 0;
    uint32_t        iconId         = 0;
    std::string     name;
    float           timeRemaining  = -1.0f;
    float           totalDuration  = 0.0f;
    uint8_t         stacks         = 0;
    BuffDisplayType type           = BuffDisplayType::Buff;
    bool            isStealable    = false;
    std::string     casterName;
    std::string     tooltip;
    double          expirationTime = 0.0;
};

class BuffFrameDisplay {
public:
    void SetBuffs(std::vector<BuffFrameIcon> buffs);
    void SetDebuffs(std::vector<BuffFrameIcon> debuffs);

    [[nodiscard]] std::vector<BuffFrameIcon> GetBuffs() const;
    [[nodiscard]] std::vector<BuffFrameIcon> GetDebuffs() const;
    [[nodiscard]] uint32_t GetBuffCount() const;
    [[nodiscard]] uint32_t GetDebuffCount() const;

    [[nodiscard]] std::optional<BuffFrameIcon> GetBuff(uint32_t auraIndex) const;
    [[nodiscard]] std::optional<BuffFrameIcon> GetDebuff(uint32_t auraIndex) const;
    [[nodiscard]] bool HasBuff(uint32_t spellId) const;
    [[nodiscard]] bool HasDebuff(uint32_t spellId) const;

    void UpdateTimers(float deltaTime);
    [[nodiscard]] std::vector<BuffFrameIcon> GetExpiredBuffs() const;

    void RemoveBuff(uint32_t auraIndex);
    void RemoveDebuff(uint32_t auraIndex);
    void ClearAll();

    void SetMaxVisible(uint32_t maxBuffs, uint32_t maxDebuffs);
    [[nodiscard]] std::vector<BuffFrameIcon> GetVisibleBuffs() const;
    [[nodiscard]] std::vector<BuffFrameIcon> GetVisibleDebuffs() const;

    void SortByTimeRemaining();

private:
    std::vector<BuffFrameIcon> buffs_;
    std::vector<BuffFrameIcon> debuffs_;
    uint32_t maxVisibleBuffs_   = 32;
    uint32_t maxVisibleDebuffs_ = 16;
};

}

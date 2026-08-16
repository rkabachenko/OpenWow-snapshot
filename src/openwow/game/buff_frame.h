#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct BuffDisplayEntry {
    uint32_t    auraIndex   = 0;
    uint32_t    spellId     = 0;
    std::string name;
    std::string iconPath;
    uint32_t    count       = 0;
    float       duration    = 0.0f;
    float       remaining   = 0.0f;
    ObjectGuid  casterGuid;
    bool        isMine      = false;
    bool        isStealable = false;
};

class BuffFrameData {
public:

    void SetBuffs(std::vector<BuffDisplayEntry> buffs);

    void AddBuff(const BuffDisplayEntry& buff);

    bool RemoveBuff(uint32_t auraIndex);

    bool RemoveBuffBySpellId(uint32_t spellId);

    uint32_t RemoveExpiredBuffs();

    [[nodiscard]] std::vector<BuffDisplayEntry> GetBuffs() const;

    [[nodiscard]] std::optional<BuffDisplayEntry> GetBuff(uint32_t auraIndex) const;

    [[nodiscard]] uint32_t GetBuffCount() const;

    [[nodiscard]] bool HasBuff(uint32_t spellId) const;

    [[nodiscard]] std::optional<BuffDisplayEntry> GetBuffBySpellId(uint32_t spellId) const;

    [[nodiscard]] std::vector<BuffDisplayEntry> GetMyBuffs() const;

    [[nodiscard]] std::vector<BuffDisplayEntry> GetStealableBuffs() const;

    [[nodiscard]] std::vector<BuffDisplayEntry> GetExpiringBuffs(float threshold) const;

    void Update(float dt);

    void SortByTimeRemaining();

    void SortByName();

    [[nodiscard]] std::vector<BuffDisplayEntry> GetConsolidatedBuffs(
        float minDuration = 300.0f) const;

    [[nodiscard]] float GetTotalDuration() const;

    [[nodiscard]] uint32_t GetMaxAuraIndex() const;

    void Clear();

private:
    std::vector<BuffDisplayEntry> buffs_;
};

}

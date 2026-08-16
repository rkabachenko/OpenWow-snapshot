#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class DebuffDispelType : uint8_t {
    None    = 0,
    Magic   = 1,
    Curse   = 2,
    Disease = 3,
    Poison  = 4,
    Enrage  = 5,
};

struct DebuffBorderColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct DebuffTypeInfo {
    uint32_t         spellId     = 0;
    DebuffDispelType dispelType  = DebuffDispelType::None;
    std::string      name;
    bool             isBossDebuff = false;
    uint8_t          priority     = 0;
};

class DebuffTypeDisplay {
public:
    void SetDebuffs(std::vector<DebuffTypeInfo> debuffs);

    [[nodiscard]] std::vector<DebuffTypeInfo> GetDebuffs() const;

    [[nodiscard]] std::vector<DebuffTypeInfo> GetDebuffsByType(DebuffDispelType type) const;
    [[nodiscard]] static DebuffBorderColor    GetBorderColor(DebuffDispelType type);
    [[nodiscard]] std::vector<DebuffTypeInfo> GetBossDebuffs() const;
    [[nodiscard]] bool                        HasDispelType(DebuffDispelType type) const;
    [[nodiscard]] uint32_t                    GetCountByType(DebuffDispelType type) const;
    [[nodiscard]] uint32_t                    GetTotalCount() const;

    void RemoveDebuff(uint32_t spellId);
    void ClearAll();

    [[nodiscard]] static bool        CanDispel(DebuffDispelType type,
                                               std::string const& playerClass);
    [[nodiscard]] static std::string GetDispelTypeName(DebuffDispelType type);

private:
    std::vector<DebuffTypeInfo> debuffs_;
};

}

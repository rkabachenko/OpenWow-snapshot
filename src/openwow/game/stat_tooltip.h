
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct StatTooltipColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct StatTooltipLine {
    std::string     text;
    StatTooltipColor color;
    bool            isHeader = false;
    uint8_t         indent   = 0;
};

class StatTooltipBuilder {
public:
    StatTooltipBuilder() = default;

    void AddHeader(const std::string& text);

    void AddLine(const std::string& text,
                 float r = 1.0f, float g = 1.0f, float b = 1.0f);

    void AddEquipEffect(const std::string& text);

    void AddUseEffect(const std::string& text);

    void AddSetBonus(const std::string& text, bool isActive);

    void AddStat(const std::string& statName, int32_t value);

    void AddDamage(float minDmg, float maxDmg, float speed);

    void AddDPS(float dps);

    void AddRequirement(const std::string& text, bool isMet);

    void AddBlankLine();

    [[nodiscard]] std::vector<StatTooltipLine> GetLines() const;
    [[nodiscard]] size_t GetLineCount() const;

    [[nodiscard]] std::string GetFormattedText() const;

    void Clear();

private:
    std::vector<StatTooltipLine> lines_;
};

}

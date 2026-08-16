
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class TooltipAlignment : uint8_t {
    Left   = 0,
    Center = 1,
    Right  = 2,
};

struct TooltipLine {
    std::string text;
    uint32_t color       = 0xFFFFFFFF;
    TooltipAlignment alignment = TooltipAlignment::Left;
    bool isBold          = false;
    bool isWrap          = false;

    std::string rightText;
    uint32_t rightColor  = 0xFFFFFFFF;
};

enum class TooltipSection : uint8_t {
    Header,
    Body,
    Equip,
    Set,
    Socket,
    Durability,
    RequiredLevel,
    Sell,
    Description,
    Flavor,
};

struct TooltipStatEntry {
    std::string name;
    int32_t value = 0;
};

struct TooltipSocketInfo {
    uint8_t socketCount  = 0;
    std::string socketBonus;
};

struct TooltipSetInfo {
    std::string setName;
    uint32_t piecesOwned = 0;
    uint32_t piecesTotal = 0;
    std::vector<std::string> bonuses;
};

class TooltipBuilder {
public:
    TooltipBuilder() = default;

    void AddLine(const std::string& text,
                 uint32_t color = 0xFFFFFFFF,
                 TooltipAlignment alignment = TooltipAlignment::Left);

    void AddDoubleLine(const std::string& left, const std::string& right,
                       uint32_t leftColor = 0xFFFFFFFF,
                       uint32_t rightColor = 0xFFFFFFFF);

    void AddBlankLine();

    void AddHeader(const std::string& text, uint32_t qualityColor);

    void AddSection(TooltipSection section,
                    const std::vector<TooltipLine>& lines);

    void SetMoneyLine(uint32_t copper);

    [[nodiscard]] std::vector<TooltipLine> GetLines() const;
    [[nodiscard]] uint32_t GetLineCount() const;

    void Clear();

    [[nodiscard]] std::vector<TooltipLine> Build() const;

    static TooltipBuilder BuildItemTooltip(
        uint32_t itemId,
        const std::string& name,
        uint32_t quality,
        uint32_t itemLevel,
        uint32_t requiredLevel,
        const std::string& slot,
        const std::string& armorType,
        float damage,
        float speed,
        const std::vector<TooltipStatEntry>& stats,
        const TooltipSocketInfo& socketInfo,
        const TooltipSetInfo& setInfo,
        const std::string& flavorText);

    static TooltipBuilder BuildSpellTooltip(
        const std::string& name,
        const std::string& rank,
        const std::string& cost,
        const std::string& castTime,
        const std::string& range,
        const std::string& cooldown,
        const std::string& description);

    static uint32_t QualityColor(uint32_t quality);

private:
    std::vector<TooltipLine> lines_;
};

}

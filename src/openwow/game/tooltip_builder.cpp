
#include "openwow/game/tooltip_builder.h"

#include <cstdio>

namespace openwow::game {

void TooltipBuilder::AddLine(const std::string& text,
                             uint32_t color,
                             TooltipAlignment alignment) {
    TooltipLine line;
    line.text      = text;
    line.color     = color;
    line.alignment = alignment;
    lines_.push_back(std::move(line));
}

void TooltipBuilder::AddDoubleLine(const std::string& left,
                                   const std::string& right,
                                   uint32_t leftColor,
                                   uint32_t rightColor) {
    TooltipLine line;
    line.text       = left;
    line.color      = leftColor;
    line.alignment  = TooltipAlignment::Left;
    line.rightText  = right;
    line.rightColor = rightColor;
    lines_.push_back(std::move(line));
}

void TooltipBuilder::AddBlankLine() {
    TooltipLine line;
    line.text = "";
    lines_.push_back(std::move(line));
}

void TooltipBuilder::AddHeader(const std::string& text, uint32_t qualityColor) {
    TooltipLine line;
    line.text      = text;
    line.color     = qualityColor;
    line.alignment = TooltipAlignment::Left;
    line.isBold    = true;
    lines_.push_back(std::move(line));
}

void TooltipBuilder::AddSection(TooltipSection ,
                                const std::vector<TooltipLine>& sectionLines) {
    for (const auto& l : sectionLines) {
        lines_.push_back(l);
    }
}

void TooltipBuilder::SetMoneyLine(uint32_t copper) {
    uint32_t gold   = copper / 10000;
    uint32_t silver = (copper / 100) % 100;
    uint32_t cop    = copper % 100;

    char buf[128];
    if (gold > 0) {
        std::snprintf(buf, sizeof(buf), "Sell Price: %u Gold %u Silver %u Copper",
                      gold, silver, cop);
    } else if (silver > 0) {
        std::snprintf(buf, sizeof(buf), "Sell Price: %u Silver %u Copper",
                      silver, cop);
    } else {
        std::snprintf(buf, sizeof(buf), "Sell Price: %u Copper", cop);
    }
    AddLine(buf);
}

std::vector<TooltipLine> TooltipBuilder::GetLines() const {
    return lines_;
}

uint32_t TooltipBuilder::GetLineCount() const {
    return static_cast<uint32_t>(lines_.size());
}

void TooltipBuilder::Clear() {
    lines_.clear();
}

std::vector<TooltipLine> TooltipBuilder::Build() const {
    return lines_;
}

uint32_t TooltipBuilder::QualityColor(uint32_t quality) {
    switch (quality) {
        case 0: return 0xFF9D9D9D;
        case 1: return 0xFFFFFFFF;
        case 2: return 0xFF1EFF00;
        case 3: return 0xFF0070DD;
        case 4: return 0xFFA335EE;
        case 5: return 0xFFFF8000;
        case 6: return 0xFFE6CC80;
        default: return 0xFFFFFFFF;
    }
}

TooltipBuilder TooltipBuilder::BuildItemTooltip(
    uint32_t ,
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
    const std::string& flavorText) {

    TooltipBuilder tb;

    tb.AddHeader(name, QualityColor(quality));

    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Item Level %u", itemLevel);
        tb.AddLine(buf, 0xFFFFD100);
    }

    if (!slot.empty() || !armorType.empty()) {
        tb.AddDoubleLine(slot, armorType);
    }

    if (damage > 0.0f || speed > 0.0f) {
        char dmgBuf[64];
        char spdBuf[32];
        std::snprintf(dmgBuf, sizeof(dmgBuf), "%.0f Damage", damage);
        std::snprintf(spdBuf, sizeof(spdBuf), "Speed %.2f", speed);
        tb.AddDoubleLine(dmgBuf, spdBuf);
    }

    for (const auto& st : stats) {
        char buf[128];
        if (st.value >= 0) {
            std::snprintf(buf, sizeof(buf), "+%d %s", st.value, st.name.c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "%d %s", st.value, st.name.c_str());
        }
        tb.AddLine(buf);
    }

    if (socketInfo.socketCount > 0) {
        for (uint8_t i = 0; i < socketInfo.socketCount; ++i) {
            tb.AddLine("[Socket]", 0xFF808080);
        }
        if (!socketInfo.socketBonus.empty()) {
            tb.AddLine("Socket Bonus: " + socketInfo.socketBonus, 0xFF808080);
        }
    }

    if (!setInfo.setName.empty()) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s (%u/%u)",
                      setInfo.setName.c_str(), setInfo.piecesOwned, setInfo.piecesTotal);
        tb.AddLine(buf, 0xFFFFD100);
        for (const auto& bonus : setInfo.bonuses) {
            tb.AddLine("  " + bonus, 0xFF808080);
        }
    }

    if (requiredLevel > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Requires Level %u", requiredLevel);
        tb.AddLine(buf);
    }

    if (!flavorText.empty()) {
        tb.AddLine("\"" + flavorText + "\"", 0xFFFFD100);
    }

    return tb;
}

TooltipBuilder TooltipBuilder::BuildSpellTooltip(
    const std::string& name,
    const std::string& rank,
    const std::string& cost,
    const std::string& castTime,
    const std::string& range,
    const std::string& cooldown,
    const std::string& description) {

    TooltipBuilder tb;

    if (!rank.empty()) {
        tb.AddDoubleLine(name, rank);
    } else {
        tb.AddLine(name);
    }

    if (!cost.empty() || !range.empty()) {
        tb.AddDoubleLine(cost, range);
    }

    if (!castTime.empty() || !cooldown.empty()) {
        tb.AddDoubleLine(castTime, cooldown);
    }

    if (!description.empty()) {
        tb.AddLine(description, 0xFFFFD100);
    }

    return tb;
}

}


#include "openwow/game/stat_tooltip.h"

#include <cstdio>

namespace openwow::game {

namespace {

StatTooltipLine MakeLine(const std::string& text,
                         float r, float g, float b,
                         bool header = false, uint8_t indent = 0) {
    StatTooltipLine l;
    l.text     = text;
    l.color    = {r, g, b};
    l.isHeader = header;
    l.indent   = indent;
    return l;
}

}

void StatTooltipBuilder::AddHeader(const std::string& text) {
    lines_.push_back(MakeLine(text, 1.0f, 1.0f, 1.0f, true));
}

void StatTooltipBuilder::AddLine(const std::string& text,
                                 float r, float g, float b) {
    lines_.push_back(MakeLine(text, r, g, b));
}

void StatTooltipBuilder::AddEquipEffect(const std::string& text) {

    lines_.push_back(MakeLine("Equip: " + text, 0.0f, 1.0f, 0.0f));
}

void StatTooltipBuilder::AddUseEffect(const std::string& text) {

    lines_.push_back(MakeLine("Use: " + text, 0.0f, 1.0f, 0.0f));
}

void StatTooltipBuilder::AddSetBonus(const std::string& text, bool isActive) {
    if (isActive) {
        lines_.push_back(MakeLine(text, 0.0f, 1.0f, 0.0f));
    } else {
        lines_.push_back(MakeLine(text, 0.5f, 0.5f, 0.5f));
    }
}

void StatTooltipBuilder::AddStat(const std::string& statName, int32_t value) {
    char buf[128];
    if (value >= 0) {
        std::snprintf(buf, sizeof(buf), "+%d %s", value, statName.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%d %s", value, statName.c_str());
    }
    lines_.push_back(MakeLine(buf, 1.0f, 1.0f, 1.0f));
}

void StatTooltipBuilder::AddDamage(float minDmg, float maxDmg, float speed) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.0f - %.0f Damage            Speed %.2f",
                  minDmg, maxDmg, speed);
    lines_.push_back(MakeLine(buf, 1.0f, 1.0f, 1.0f));
}

void StatTooltipBuilder::AddDPS(float dps) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%.1f damage per second)", dps);
    lines_.push_back(MakeLine(buf, 1.0f, 1.0f, 1.0f));
}

void StatTooltipBuilder::AddRequirement(const std::string& text, bool isMet) {
    if (isMet) {
        lines_.push_back(MakeLine(text, 1.0f, 1.0f, 1.0f));
    } else {
        lines_.push_back(MakeLine(text, 1.0f, 0.0f, 0.0f));
    }
}

void StatTooltipBuilder::AddBlankLine() {
    lines_.push_back(MakeLine("", 1.0f, 1.0f, 1.0f));
}

std::vector<StatTooltipLine> StatTooltipBuilder::GetLines() const {
    return lines_;
}

size_t StatTooltipBuilder::GetLineCount() const {
    return lines_.size();
}

std::string StatTooltipBuilder::GetFormattedText() const {
    std::string result;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) result += '\n';
        result += lines_[i].text;
    }
    return result;
}

void StatTooltipBuilder::Clear() {
    lines_.clear();
}

}

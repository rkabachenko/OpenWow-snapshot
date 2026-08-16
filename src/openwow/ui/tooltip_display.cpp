
#include "openwow/ui/tooltip_display.h"

namespace openwow::ui {

void TooltipDisplay::ClearLines() {
    lines_.clear();
}

void TooltipDisplay::AddLine(const std::string& text, float r, float g, float b) {
    TooltipDisplayLine line;
    line.text = text;
    line.r = r;
    line.g = g;
    line.b = b;
    lines_.push_back(std::move(line));
}

void TooltipDisplay::AddDoubleLine(const std::string& leftText,
                                   const std::string& rightText,
                                   float lr, float lg, float lb,
                                   float rr, float rg, float rb) {
    TooltipDisplayLine line;
    line.text = leftText;
    line.r = lr;
    line.g = lg;
    line.b = lb;
    line.isDoubleWidth = true;
    line.rightText = rightText;
    line.rightR = rr;
    line.rightG = rg;
    line.rightB = rb;
    lines_.push_back(std::move(line));
}

const std::vector<TooltipDisplayLine>& TooltipDisplay::GetLines() const {
    return lines_;
}

std::size_t TooltipDisplay::GetLineCount() const {
    return lines_.size();
}

void TooltipDisplay::SetOwner(const std::string& frameName,
                              TooltipAnchorMode mode) {
    ownerFrame_ = frameName;
    anchorMode_ = mode;
}

const std::string& TooltipDisplay::GetOwner() const {
    return ownerFrame_;
}

TooltipAnchorMode TooltipDisplay::GetAnchorMode() const {
    return anchorMode_;
}

bool TooltipDisplay::IsShown() const { return shown_; }
void TooltipDisplay::Show()  { shown_ = true;  fadingOut_ = false; }
void TooltipDisplay::Hide()  { shown_ = false; fadingIn_  = false; }

void TooltipDisplay::SetItemTooltip(std::uint32_t itemId) {
    typeTag_ = TypeTag::Item;
    typeId_  = itemId;
    unitGuid_ = openwow::game::ObjectGuid{};
}

void TooltipDisplay::SetSpellTooltip(std::uint32_t spellId) {
    typeTag_ = TypeTag::Spell;
    typeId_  = spellId;
    unitGuid_ = openwow::game::ObjectGuid{};
}

void TooltipDisplay::SetUnitTooltip(openwow::game::ObjectGuid guid) {
    typeTag_  = TypeTag::Unit;
    typeId_   = static_cast<std::uint32_t>(guid.GetRawValue() & 0xFFFFFFFF);
    unitGuid_ = guid;
}

std::string TooltipDisplay::GetTooltipType() const {
    switch (typeTag_) {
        case TypeTag::Item:  return "item";
        case TypeTag::Spell: return "spell";
        case TypeTag::Unit:  return "unit";
        default:             return "";
    }
}

std::uint32_t TooltipDisplay::GetTooltipId() const {
    return typeId_;
}

void TooltipDisplay::FadeIn() {
    fadingIn_  = true;
    fadingOut_ = false;
    shown_     = true;
}

void TooltipDisplay::FadeOut() {
    fadingOut_ = true;
    fadingIn_  = false;
}

bool TooltipDisplay::IsFadingIn()  const { return fadingIn_;  }
bool TooltipDisplay::IsFadingOut() const { return fadingOut_; }

void TooltipDisplay::Reset() {
    lines_.clear();
    ownerFrame_.clear();
    anchorMode_ = TooltipAnchorMode::Default;
    shown_     = false;
    typeTag_   = TypeTag::None;
    typeId_    = 0;
    unitGuid_  = openwow::game::ObjectGuid{};
    fadingIn_  = false;
    fadingOut_ = false;
}

}
